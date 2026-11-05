#ifndef STSV_PS3_AUDIO_OUTPUT_H
#define STSV_PS3_AUDIO_OUTPUT_H

#define AUDIO_OUTPUT_RATE 48000.0

/* Called on the mixer thread for every block: add interleaved stereo float
 * samples into block (already zeroed), frames frames long. */
typedef void (*AudioMixFunction)(float *block, int frames);

/* Opens the 48 kHz stereo port and starts the mixer and feeder threads.
 * Returns 0 on success. */
int audio_output_start(AudioMixFunction mix);

#endif
