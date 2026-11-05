/* Explicitly unavailable for the menu milestone. Never report fake playback. */
#include <stdio.h>
#include "stsvita_glue.h"

void stsv_audio_init(void)
{
    fprintf(stderr, "[ps3 audio] unavailable in menu milestone; all audio loads fail (muted)\n");
}

int32_t stsv_sound_load(const void *data, int32_t len) { (void)data; (void)len; return 0; }
void stsv_sound_free(int32_t buffer) { (void)buffer; }
int32_t stsv_sound_play(int32_t buffer, float volume, float pitch, float pan, int32_t loop)
{ (void)buffer; (void)volume; (void)pitch; (void)pan; (void)loop; return -1; }
void stsv_sound_stop(int32_t source) { (void)source; }
void stsv_sound_pause(int32_t source) { (void)source; }
void stsv_sound_resume(int32_t source) { (void)source; }
void stsv_sound_set_volume(int32_t source, float volume) { (void)source; (void)volume; }
void stsv_sound_set_pitch(int32_t source, float pitch) { (void)source; (void)pitch; }
void stsv_sound_set_looping(int32_t source, int32_t loop) { (void)source; (void)loop; }
int64_t stsv_music_open(const void *data, int32_t len) { (void)data; (void)len; return 0; }
int64_t stsv_music_open_path(const void *path, int32_t len) { (void)path; (void)len; return 0; }
void stsv_music_free(int64_t handle) { (void)handle; }
void stsv_music_play(int64_t handle) { (void)handle; }
void stsv_music_pause(int64_t handle) { (void)handle; }
void stsv_music_stop(int64_t handle) { (void)handle; }
void stsv_music_update(int64_t handle) { (void)handle; }
int32_t stsv_music_is_playing(int64_t handle) { (void)handle; return 0; }
void stsv_music_set_volume(int64_t handle, float volume) { (void)handle; (void)volume; }
void stsv_music_set_looping(int64_t handle, int32_t loop) { (void)handle; (void)loop; }
