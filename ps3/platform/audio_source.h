#ifndef STSV_PS3_AUDIO_SOURCE_H
#define STSV_PS3_AUDIO_SOURCE_H

#include <vorbis/vorbisfile.h>

#define AUDIO_STREAM_FRAMES 2048

/* One playing sound: either decoded 16-bit PCM in memory (shared, not owned)
 * or an OGG file streamed from disk. Reads resample with linear interpolation. */
typedef struct {
    const short *pcm;
    long pcm_frames, pcm_position;
    OggVorbis_File file;
    int file_open;
    short buffer[AUDIO_STREAM_FRAMES * 2];
    int buffer_frames, buffer_position;
    int channels;
    double rate;
    int loop, ended;
    float previous[2], current[2];
    double fraction;
} AudioSource;

/* Returns 1 on success. */
int audio_source_open_stream(AudioSource *source, const char *path);
void audio_source_use_pcm(AudioSource *source, const short *pcm, long frames, int channels, double rate);
void audio_source_close(AudioSource *source);
void audio_source_rewind(AudioSource *source);

/* Adds frames of stereo output into out at step source frames per output
 * frame. Returns 0 once a non-looping source has ended. */
int audio_source_mix(AudioSource *source, float *out, int frames, double step, float gain_left, float gain_right);

/* Fully decodes a .ogg or 16-bit PCM .wav into native-endian shorts.
 * Returns 1 on success, -1 if the decoded size exceeds max_bytes (stream it
 * instead), 0 on failure. */
int audio_decode_file(const char *path, long max_bytes, short **pcm, long *frames, int *channels, double *rate);

#endif
