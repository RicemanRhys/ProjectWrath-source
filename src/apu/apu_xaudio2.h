/**
 * XAudio2 Audio Output Backend
 */
#ifndef APU_XAUDIO2_H
#define APU_XAUDIO2_H

#include <stdint.h>

/* Initialize XAudio2. Returns 1 on success, 0 on failure. */
int xa2_init(void);

/* Shut down XAudio2 and release all resources. */
void xa2_shutdown(void);

/* Returns 1 if XAudio2 is active. */
int xa2_is_active(void);

/* Submit interleaved 16-bit samples, xa2_channels() per frame (stereo, or
 * FL FR C LFE BL BR). Returns 1 if accepted. */
int xa2_submit_samples(const int16_t *samples, int num_samples);

/* The channels the output takes: 2, or 6 when RECOMP_SPEAKERS=5.1 asked for
 * 5.1 and the default device has those speakers; 0 when not initialized. */
int xa2_channels(void);
/* The default device's channel count, read when 5.1 was asked for (0: not). */
int xa2_device_channels(void);

/* Get the preferred buffer size in samples. */
int xa2_get_buffer_size(void);

/* Buffers submitted and not yet played, and how many the voice can hold. */
int xa2_buffers_queued(void);
int xa2_max_buffers(void);

#endif /* APU_XAUDIO2_H */
