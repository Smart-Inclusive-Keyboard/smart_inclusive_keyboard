#pragma once

/*
 * I2S WAV audio output.
 *
 * Per project requirements, sound output is always I2S -- there
 * is no PWM / internal-DAC backend. On boards without a speaker
 * (CONFIG_BOARD_HAS_SPEAKER unset) the calls below compile to a
 * no-op stub so callers in narrator/ don't need to #ifdef every
 * use site.
 *
 * Audio model: a single-track preempting mixer. audio_play_wav()
 * cancels whatever is currently playing and starts the new clip
 * from its first sample. This matches the narrator's behaviour
 * (rapid navigation should not stack overlapping letter sounds).
 *
 * The clip data is expected to be a standard RIFF/WAVE blob
 * (PCM, 16-bit, mono or stereo). Embedded WAVs from
 * components/narrator/wav/ are pulled in via EMBED_FILES at
 * build time.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the I2S TX channel using the pins / port from
 * board_get()->i2s. Idempotent. Returns 0 on success. */
int audio_init(void);

/* Start playing a WAV clip from a flat memory blob.
 * Any currently playing clip is preempted. Returns 0 on
 * success (or whether the audio backend is a stub).
 *
 * `wav_data` must remain valid until the clip finishes (or
 * until the next audio_play_wav() call). For EMBED_FILES data
 * this is guaranteed because the symbol lives in flash. */
int audio_play_wav(const void *wav_data, size_t wav_len);

/* Stop any currently playing clip. */
void audio_stop(void);

/* Set the digital volume in percent [0..100]. */
void audio_set_volume(int percent);

/* Get the digital volume in percent [0..100]. */
int audio_get_volume(void);


/* True if a clip is currently playing. */
bool audio_is_playing(void);

/*
 * Play a short procedurally-generated arpeggio (~0.3 s) intended
 * as a "speaker is alive" chime at boot. No external WAV asset
 * required. Audible only when CONFIG_BOARD_HAS_PSRAM &&
 * CONFIG_BOARD_HAS_SPEAKER; otherwise a no-op.
 */
void audio_play_startup_tune(void);

#ifdef __cplusplus
}
#endif
