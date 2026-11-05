/* Decoding and resampled reading for sounds and music. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_source.h"

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define HOST_BIG_ENDIAN 1
#else
#define HOST_BIG_ENDIAN 0
#endif

static int refill_stream(AudioSource *source)
{
    int bitstream;
    int capacity = (int)sizeof(source->buffer);
    for (int attempt = 0; attempt < 2; attempt++) {
        long bytes = ov_read(&source->file, (char *)source->buffer, capacity, HOST_BIG_ENDIAN, 2, 1, &bitstream);
        if (bytes > 0) {
            source->buffer_frames = (int)(bytes / (2 * source->channels));
            source->buffer_position = 0;
            return source->buffer_frames > 0;
        }
        if (bytes < 0 || !source->loop || ov_pcm_seek(&source->file, 0) != 0)
            return 0;
    }
    return 0;
}

static int read_pcm_frame(AudioSource *source, float frame[2])
{
    if (source->pcm_position >= source->pcm_frames) {
        if (!source->loop || source->pcm_frames == 0)
            return 0;
        source->pcm_position = 0;
    }
    const short *sample = source->pcm + source->pcm_position * source->channels;
    frame[0] = sample[0] / 32768.0f;
    frame[1] = sample[source->channels - 1] / 32768.0f;
    source->pcm_position++;
    return 1;
}

static int read_stream_frame(AudioSource *source, float frame[2])
{
    if (source->buffer_position >= source->buffer_frames && !refill_stream(source))
        return 0;
    const short *sample = source->buffer + source->buffer_position * source->channels;
    frame[0] = sample[0] / 32768.0f;
    frame[1] = sample[source->channels - 1] / 32768.0f;
    source->buffer_position++;
    return 1;
}

static int read_frame(AudioSource *source, float frame[2])
{
    if (source->pcm)
        return read_pcm_frame(source, frame);
    return read_stream_frame(source, frame);
}

/* Loads the first two frames so interpolation has both ends. */
static void prime(AudioSource *source)
{
    source->ended = 0;
    source->fraction = 0;
    if (!read_frame(source, source->previous)) {
        source->ended = 1;
        return;
    }
    if (!read_frame(source, source->current))
        memcpy(source->current, source->previous, sizeof(source->current));
}

int audio_source_open_stream(AudioSource *source, const char *path)
{
    memset(source, 0, sizeof(*source));
    if (ov_fopen(path, &source->file) != 0)
        return 0;
    vorbis_info *info = ov_info(&source->file, -1);
    if (!info || info->channels < 1 || info->channels > 2) {
        ov_clear(&source->file);
        return 0;
    }
    source->file_open = 1;
    source->channels = info->channels;
    source->rate = info->rate;
    prime(source);
    return 1;
}

void audio_source_use_pcm(AudioSource *source, const short *pcm, long frames, int channels, double rate)
{
    memset(source, 0, sizeof(*source));
    source->pcm = pcm;
    source->pcm_frames = frames;
    source->channels = channels;
    source->rate = rate;
    prime(source);
}

void audio_source_close(AudioSource *source)
{
    if (source->file_open)
        ov_clear(&source->file);
    source->file_open = 0;
    source->pcm = NULL;
    source->ended = 1;
}

void audio_source_rewind(AudioSource *source)
{
    source->pcm_position = 0;
    source->buffer_frames = source->buffer_position = 0;
    if (source->file_open)
        ov_pcm_seek(&source->file, 0);
    prime(source);
}

int audio_source_mix(AudioSource *source, float *out, int frames, double step, float gain_left, float gain_right)
{
    for (int i = 0; i < frames && !source->ended; i++) {
        float t = (float)source->fraction;
        out[i * 2] += (source->previous[0] + (source->current[0] - source->previous[0]) * t) * gain_left;
        out[i * 2 + 1] += (source->previous[1] + (source->current[1] - source->previous[1]) * t) * gain_right;
        source->fraction += step;
        while (source->fraction >= 1.0 && !source->ended) {
            source->fraction -= 1.0;
            memcpy(source->previous, source->current, sizeof(source->previous));
            if (!read_frame(source, source->current))
                source->ended = 1;
        }
    }
    return !source->ended;
}

static int decode_wav(const char *path, long max_bytes, short **pcm, long *frames, int *channels, double *rate)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char *data = size > 44 ? malloc(size) : NULL;
    int ok = data && fread(data, 1, size, file) == (size_t)size;
    fclose(file);
    if (!ok || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) {
        free(data);
        return 0;
    }
    int format = 0, bits = 0;
    unsigned sample_rate = 0;
    const unsigned char *samples = NULL;
    long sample_bytes = 0;
    for (long position = 12; position + 8 <= size;) {
        unsigned chunk = data[position + 4] | data[position + 5] << 8 | data[position + 6] << 16 | (unsigned)data[position + 7] << 24;
        if (!memcmp(data + position, "fmt ", 4) && position + 24 <= size) {
            format = data[position + 8] | data[position + 9] << 8;
            *channels = data[position + 10] | data[position + 11] << 8;
            sample_rate = data[position + 12] | data[position + 13] << 8 | data[position + 14] << 16 | (unsigned)data[position + 15] << 24;
            bits = data[position + 22] | data[position + 23] << 8;
        }
        if (!memcmp(data + position, "data", 4)) {
            samples = data + position + 8;
            sample_bytes = chunk;
            if (samples + sample_bytes > data + size)
                sample_bytes = (long)(data + size - samples);
        }
        position += 8 + ((chunk + 1u) & ~1u);
    }
    int result = 0;
    if (format == 1 && bits == 16 && *channels >= 1 && *channels <= 2 && samples && sample_rate) {
        result = -1;
        if (sample_bytes <= max_bytes && (*pcm = malloc(sample_bytes)) != NULL) {
            /* WAV samples are little-endian; convert to host order. */
            for (long i = 0; i < sample_bytes / 2; i++)
                (*pcm)[i] = (short)(samples[i * 2] | samples[i * 2 + 1] << 8);
            *frames = sample_bytes / (2 * *channels);
            *rate = sample_rate;
            result = 1;
        }
    }
    free(data);
    return result;
}

static int decode_ogg(const char *path, long max_bytes, short **pcm, long *frames, int *channels, double *rate)
{
    OggVorbis_File file;
    int bitstream;
    if (ov_fopen(path, &file) != 0)
        return 0;
    vorbis_info *info = ov_info(&file, -1);
    ogg_int64_t total = ov_pcm_total(&file, -1);
    if (!info || info->channels < 1 || info->channels > 2 || total <= 0) {
        ov_clear(&file);
        return 0;
    }
    long bytes = (long)total * info->channels * 2;
    if (bytes > max_bytes) {
        ov_clear(&file);
        return -1;
    }
    char *buffer = malloc(bytes);
    if (!buffer) {
        ov_clear(&file);
        return -1;
    }
    long filled = 0;
    while (filled < bytes) {
        long got = ov_read(&file, buffer + filled, (int)(bytes - filled), HOST_BIG_ENDIAN, 2, 1, &bitstream);
        if (got <= 0)
            break;
        filled += got;
    }
    *channels = info->channels;
    *rate = info->rate;
    *frames = filled / (2 * info->channels);
    *pcm = (short *)buffer;
    ov_clear(&file);
    return 1;
}

int audio_decode_file(const char *path, long max_bytes, short **pcm, long *frames, int *channels, double *rate)
{
    size_t length = strlen(path);
    /* WAVs cannot stream (vorbisfile only reads OGG); the game's are all small. */
    if (length > 4 && !strcmp(path + length - 4, ".wav"))
        return decode_wav(path, max_bytes, pcm, frames, channels, rate) == 1;
    return decode_ogg(path, max_bytes, pcm, frames, channels, rate);
}
