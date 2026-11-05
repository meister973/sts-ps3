/* PS3 audio for VitaAudio: sound effects, music, and a software mixer.
 * Decoding every sound effect up front would need ~200 MB, so a sound keeps
 * only its path; short ones are decoded on first play into a capped cache and
 * long ones (ambience loops, music) stream from disk on the mixer thread.
 * One mutex guards voices, music and the cache. The game thread decodes and
 * opens files without it; the mixer refills 8 KB stream buffers while holding
 * it, which can briefly delay a game-thread call but never the output, since
 * the mixer runs ~85 ms ahead of the port. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mutex.h>

#include "audio_output.h"
#include "audio_source.h"
#include "stsvita_glue.h"

#define MAX_SOUNDS 1024
#define MAX_VOICES 32
#define MAX_MUSIC 8
#define CACHE_BYTES (8L * 1024 * 1024)
#define STREAM_OVER_BYTES (1024L * 1024)

enum { SLOT_FREE, SLOT_OPENING, SLOT_LIVE };

typedef struct {
    char *path;
    short *pcm;
    long frames, bytes;
    int channels, streamed, failed, playing;
    double rate;
    unsigned last_used;
} Sound;

typedef struct {
    int state;
    unsigned generation, started;
    int sound;
    AudioSource source;
    float volume, pitch, pan;
    int paused;
} Voice;

typedef struct {
    int state;
    AudioSource source;
    float volume;
    int playing;
} Music;

static Sound sounds[MAX_SOUNDS];
static Voice voices[MAX_VOICES];
static Music musics[MAX_MUSIC];
static sys_mutex_t lock;
static int ready;
static long cache_bytes;
static unsigned use_clock;

static void lock_audio(void) { sysMutexLock(lock, 0); }
static void unlock_audio(void) { sysMutexUnlock(lock); }

/* Caller holds the lock. */
static void release_voice(Voice *voice)
{
    audio_source_close(&voice->source);
    if (voice->state == SLOT_LIVE)
        sounds[voice->sound].playing--;
    voice->state = SLOT_FREE;
}

static void mix_voices(float *block, int frames)
{
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice *voice = &voices[i];
        if (voice->state != SLOT_LIVE || voice->paused)
            continue;
        float left = voice->volume * (voice->pan > 0 ? 1 - voice->pan : 1);
        float right = voice->volume * (voice->pan < 0 ? 1 + voice->pan : 1);
        double step = voice->source.rate / AUDIO_OUTPUT_RATE * voice->pitch;
        if (!audio_source_mix(&voice->source, block, frames, step, left, right))
            release_voice(voice);
    }
}

static void mix_music(float *block, int frames)
{
    for (int i = 0; i < MAX_MUSIC; i++) {
        Music *music = &musics[i];
        if (music->state != SLOT_LIVE || !music->playing)
            continue;
        double step = music->source.rate / AUDIO_OUTPUT_RATE;
        if (!audio_source_mix(&music->source, block, frames, step, music->volume, music->volume))
            music->playing = 0;
    }
}

static void mix(float *block, int frames)
{
    lock_audio();
    mix_voices(block, frames);
    mix_music(block, frames);
    unlock_audio();
}

void stsv_audio_init(void)
{
    sys_mutex_attr_t attributes;
    if (ready)
        return;
    sysMutexAttrInitialize(attributes);
    if (sysMutexCreate(&lock, &attributes) != 0) {
        fprintf(stderr, "[ps3 audio] mutex creation failed; muted\n");
        return;
    }
    int result = audio_output_start(mix);
    if (result != 0) {
        fprintf(stderr, "[ps3 audio] output start failed (%d); muted\n", result);
        return;
    }
    ready = 1;
    fprintf(stderr, "[ps3 audio] 48 kHz mixer ready (%d voices, %ld KiB effect cache)\n", MAX_VOICES, CACHE_BYTES >> 10);
}

static char *copy_path(const void *utf8, int32_t length)
{
    char *path = malloc((size_t)length + 1);
    if (!path)
        return NULL;
    memcpy(path, utf8, (size_t)length);
    path[length] = 0;
    return path;
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

int32_t stsv_sound_open_path(const void *path_utf8, int32_t length)
{
    if (!ready || !path_utf8 || length <= 0)
        return 0;
    char *path = copy_path(path_utf8, length);
    if (!path || !file_exists(path)) {
        free(path);
        return 0;
    }
    lock_audio();
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (sounds[i].path)
            continue;
        memset(&sounds[i], 0, sizeof(sounds[i]));
        sounds[i].path = path;
        unlock_audio();
        return i + 1;
    }
    unlock_audio();
    free(path);
    fprintf(stderr, "[ps3 audio] sound table full\n");
    return 0;
}

/* The game no longer hands sounds over as bytes; see stsv_sound_open_path. */
int32_t stsv_sound_load(const void *data, int32_t length) { (void)data; (void)length; return 0; }

static Sound *sound_from_handle(int32_t handle)
{
    if (handle < 1 || handle > MAX_SOUNDS || !sounds[handle - 1].path)
        return NULL;
    return &sounds[handle - 1];
}

/* Caller holds the lock. Frees idle cached effects, least recently used first. */
static void make_cache_room(long bytes)
{
    while (cache_bytes + bytes > CACHE_BYTES) {
        Sound *oldest = NULL;
        for (int i = 0; i < MAX_SOUNDS; i++) {
            Sound *sound = &sounds[i];
            if (sound->pcm && !sound->playing && (!oldest || sound->last_used < oldest->last_used))
                oldest = sound;
        }
        if (!oldest)
            return;
        free(oldest->pcm);
        oldest->pcm = NULL;
        cache_bytes -= oldest->bytes;
    }
}

/* Decodes outside the lock; only publishing the result takes it. */
static void ensure_decoded(Sound *sound)
{
    short *pcm = NULL;
    long frames = 0;
    int channels = 0;
    double rate = 0;
    if (sound->pcm || sound->streamed || sound->failed)
        return;
    int result = audio_decode_file(sound->path, STREAM_OVER_BYTES, &pcm, &frames, &channels, &rate);
    if (result < 0) {
        sound->streamed = 1;
        return;
    }
    if (result == 0) {
        sound->failed = 1;
        fprintf(stderr, "[ps3 audio] cannot decode %s\n", sound->path);
        return;
    }
    long bytes = frames * channels * 2;
    lock_audio();
    make_cache_room(bytes);
    sound->pcm = pcm;
    sound->frames = frames;
    sound->channels = channels;
    sound->rate = rate;
    sound->bytes = bytes;
    cache_bytes += bytes;
    unlock_audio();
}

/* Caller holds the lock. Prefers a free voice, otherwise the oldest one. */
static int claim_voice(void)
{
    int chosen = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].state == SLOT_FREE)
            return i;
        if (voices[i].state == SLOT_LIVE && (chosen < 0 || voices[i].started < voices[chosen].started))
            chosen = i;
    }
    if (chosen >= 0)
        release_voice(&voices[chosen]);
    return chosen;
}

int32_t stsv_sound_play(int32_t handle, float volume, float pitch, float pan, int32_t loop)
{
    Sound *sound = sound_from_handle(handle);
    if (!ready || !sound)
        return -1;
    ensure_decoded(sound);
    if (sound->failed)
        return -1;
    lock_audio();
    int slot = claim_voice();
    if (slot < 0) {
        unlock_audio();
        return -1;
    }
    Voice *voice = &voices[slot];
    voice->state = SLOT_OPENING;
    voice->sound = handle - 1;
    sound->playing++; /* pins the cached PCM before the lock is dropped */
    int streamed = sound->streamed;
    if (!streamed)
        audio_source_use_pcm(&voice->source, sound->pcm, sound->frames, sound->channels, sound->rate);
    unlock_audio();
    if (streamed && !audio_source_open_stream(&voice->source, sound->path)) {
        lock_audio();
        voice->state = SLOT_FREE;
        sound->playing--;
        unlock_audio();
        return -1;
    }
    lock_audio();
    voice->source.loop = loop;
    voice->volume = volume;
    voice->pitch = pitch > 0 ? pitch : 1;
    voice->pan = pan;
    voice->paused = 0;
    voice->started = ++use_clock;
    voice->generation = (voice->generation + 1) & 0x00ffffff;
    sound->last_used = use_clock;
    voice->state = SLOT_LIVE;
    int32_t id = (int32_t)(voice->generation << 6 | (unsigned)(slot + 1));
    unlock_audio();
    return id;
}

/* Caller holds the lock. Ids carry a generation so a stale id is ignored. */
static Voice *voice_from_id(int32_t id)
{
    int slot = (id & 63) - 1;
    if (id <= 0 || slot < 0 || slot >= MAX_VOICES)
        return NULL;
    Voice *voice = &voices[slot];
    if (voice->state != SLOT_LIVE || voice->generation != ((unsigned)id >> 6))
        return NULL;
    return voice;
}

void stsv_sound_free(int32_t handle)
{
    Sound *sound = sound_from_handle(handle);
    if (!sound)
        return;
    lock_audio();
    for (int i = 0; i < MAX_VOICES; i++)
        if (voices[i].state == SLOT_LIVE && voices[i].sound == handle - 1)
            release_voice(&voices[i]);
    if (sound->pcm)
        cache_bytes -= sound->bytes;
    free(sound->pcm);
    free(sound->path);
    memset(sound, 0, sizeof(*sound));
    unlock_audio();
}

void stsv_sound_stop(int32_t id)
{
    lock_audio();
    Voice *voice = voice_from_id(id);
    if (voice)
        release_voice(voice);
    unlock_audio();
}

static void set_paused(int32_t id, int paused)
{
    lock_audio();
    Voice *voice = voice_from_id(id);
    if (voice)
        voice->paused = paused;
    unlock_audio();
}

void stsv_sound_pause(int32_t id) { set_paused(id, 1); }
void stsv_sound_resume(int32_t id) { set_paused(id, 0); }

void stsv_sound_set_volume(int32_t id, float volume)
{
    lock_audio();
    Voice *voice = voice_from_id(id);
    if (voice)
        voice->volume = volume;
    unlock_audio();
}

void stsv_sound_set_pitch(int32_t id, float pitch)
{
    lock_audio();
    Voice *voice = voice_from_id(id);
    if (voice && pitch > 0)
        voice->pitch = pitch;
    unlock_audio();
}

void stsv_sound_set_looping(int32_t id, int32_t loop)
{
    lock_audio();
    Voice *voice = voice_from_id(id);
    if (voice)
        voice->source.loop = loop;
    unlock_audio();
}

int64_t stsv_music_open_path(const void *path_utf8, int32_t length)
{
    if (!ready || !path_utf8 || length <= 0)
        return 0;
    char *path = copy_path(path_utf8, length);
    if (!path)
        return 0;
    int slot = -1;
    lock_audio();
    for (int i = 0; i < MAX_MUSIC && slot < 0; i++)
        if (musics[i].state == SLOT_FREE)
            slot = i;
    if (slot >= 0)
        musics[slot].state = SLOT_OPENING;
    unlock_audio();
    if (slot < 0) {
        free(path);
        return 0;
    }
    Music *music = &musics[slot];
    int opened = audio_source_open_stream(&music->source, path);
    free(path);
    lock_audio();
    music->state = opened ? SLOT_LIVE : SLOT_FREE;
    music->volume = 1;
    music->playing = 0;
    unlock_audio();
    return opened ? slot + 1 : 0;
}

/* Music always streams from its path; see stsv_music_open_path. */
int64_t stsv_music_open(const void *data, int32_t length) { (void)data; (void)length; return 0; }

/* Caller holds the lock. */
static Music *music_from_handle(int64_t handle)
{
    if (handle < 1 || handle > MAX_MUSIC || musics[handle - 1].state != SLOT_LIVE)
        return NULL;
    return &musics[handle - 1];
}

void stsv_music_play(int64_t handle)
{
    lock_audio();
    Music *music = music_from_handle(handle);
    if (music) {
        if (music->source.ended)
            audio_source_rewind(&music->source);
        music->playing = 1;
    }
    unlock_audio();
}

void stsv_music_pause(int64_t handle)
{
    lock_audio();
    Music *music = music_from_handle(handle);
    if (music)
        music->playing = 0;
    unlock_audio();
}

void stsv_music_stop(int64_t handle)
{
    lock_audio();
    Music *music = music_from_handle(handle);
    if (music) {
        music->playing = 0;
        audio_source_rewind(&music->source);
    }
    unlock_audio();
}

/* The mixer thread streams music, so there is nothing to pump per frame. */
void stsv_music_update(int64_t handle) { (void)handle; }

int32_t stsv_music_is_playing(int64_t handle)
{
    lock_audio();
    Music *music = music_from_handle(handle);
    int playing = music && music->playing;
    unlock_audio();
    return playing;
}

void stsv_music_set_volume(int64_t handle, float volume)
{
    lock_audio();
    Music *music = music_from_handle(handle);
    if (music)
        music->volume = volume;
    unlock_audio();
}

void stsv_music_set_looping(int64_t handle, int32_t loop)
{
    lock_audio();
    Music *music = music_from_handle(handle);
    if (music)
        music->source.loop = loop;
    unlock_audio();
}

void stsv_music_free(int64_t handle)
{
    lock_audio();
    Music *music = music_from_handle(handle);
    if (music)
        music->state = SLOT_FREE;
    unlock_audio();
    if (music)
        audio_source_close(&music->source);
}
