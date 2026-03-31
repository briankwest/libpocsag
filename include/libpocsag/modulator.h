#ifndef LIBPOCSAG_MODULATOR_H
#define LIBPOCSAG_MODULATOR_H

#include <stdint.h>
#include <stddef.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Thread safety: each pocsag_mod_t instance may be used by one thread
 * at a time.  Distinct instances may be used concurrently without
 * synchronisation.  The library contains no global mutable state. */

/* Default FSK tone formula: mark = baud_rate, space = 2 * baud_rate.
 * This yields exactly 1 and 2 tone cycles per bit period, which is
 * optimal for the correlator at all supported sample-rate / baud-rate
 * combinations.  Use pocsag_mod_init_custom() to override. */
#define POCSAG_FSK_MARK_HZ(baud)   ((float)(baud))
#define POCSAG_FSK_SPACE_HZ(baud)  ((float)((baud) * 2))

/* Supported audio sample rates (Hz) */
#define POCSAG_SRATE_8000   8000
#define POCSAG_SRATE_16000  16000
#define POCSAG_SRATE_32000  32000
#define POCSAG_SRATE_48000  48000

/* Standard POCSAG baud rates */
#define POCSAG_BAUD_512   512
#define POCSAG_BAUD_1200  1200
#define POCSAG_BAUD_2400  2400

/* Validation helpers */
int pocsag_srate_valid(uint32_t sr);
int pocsag_baud_valid(uint32_t br);

typedef struct {
	uint32_t sample_rate;   /* audio sample rate in Hz (e.g. 48000) */
	uint32_t baud_rate;     /* bit rate: 512, 1200, or 2400 */
	float    mark_freq;     /* tone frequency for binary 1 (Hz) */
	float    space_freq;    /* tone frequency for binary 0 (Hz) */
	double   phase;         /* oscillator phase for continuity */
} pocsag_mod_t;

/* Initialize modulator with default AFSK frequencies */
void         pocsag_mod_init(pocsag_mod_t *mod,
                             uint32_t sample_rate,
                             uint32_t baud_rate);

/* Initialize modulator with custom tone frequencies */
void         pocsag_mod_init_custom(pocsag_mod_t *mod,
                                    uint32_t sample_rate,
                                    uint32_t baud_rate,
                                    float mark_freq,
                                    float space_freq);

/* Reset oscillator phase (call between independent transmissions) */
void         pocsag_mod_reset(pocsag_mod_t *mod);

/* Calculate the number of output samples needed for nbits input bits */
size_t       pocsag_mod_samples_needed(const pocsag_mod_t *mod,
                                       size_t nbits);

/* Modulate a packed bitstream (MSB-first, as output by pocsag_encode)
 * into continuous-phase FSK audio samples in the range [-1, +1].
 *
 * bits:    packed byte array (MSB-first)
 * nbits:   number of valid bits to modulate
 * out:     output float sample buffer
 * out_cap: capacity of out in samples
 * out_len: receives the actual number of samples written
 */
pocsag_err_t pocsag_modulate(pocsag_mod_t *mod,
                             const uint8_t *bits, size_t nbits,
                             float *out, size_t out_cap,
                             size_t *out_len);

/* Generate baseband NRZ samples from a packed bitstream.
 *
 * Produces flat-level-per-bit output suitable for feeding an FM
 * transmitter via line/mic input, or for direct decoding by
 * multimon-ng (post-FM-discriminator format).
 *
 * Polarity (matches multimon-ng / standard FM discriminator):
 *   bit 1 → -1.0  (negative level)
 *   bit 0 → +1.0  (positive level)
 *
 * bits:        packed byte array (MSB-first, from pocsag_encode)
 * nbits:       number of valid bits
 * sample_rate: output sample rate in Hz (8000, 16000, 32000, 48000)
 * baud_rate:   POCSAG baud rate (512, 1200, 2400)
 * out:         output float sample buffer
 * out_cap:     capacity of out in samples
 * out_len:     receives the actual number of samples written
 */
pocsag_err_t pocsag_baseband(const uint8_t *bits, size_t nbits,
                             uint32_t sample_rate, uint32_t baud_rate,
                             float *out, size_t out_cap,
                             size_t *out_len);

/* Baseband flags for pocsag_baseband_ex() */
#define POCSAG_BASEBAND_DEEMPH  0x01  /* apply 75µs de-emphasis to cancel
                                       * radio TX pre-emphasis */

/* Extended baseband with flags.
 * Same as pocsag_baseband() but accepts flags for signal conditioning.
 * POCSAG_BASEBAND_DEEMPH: apply a 75µs first-order IIR lowpass so that
 * after the radio's pre-emphasis the receiver gets a flat NRZ signal. */
pocsag_err_t pocsag_baseband_ex(const uint8_t *bits, size_t nbits,
                                uint32_t sample_rate, uint32_t baud_rate,
                                int flags,
                                float *out, size_t out_cap,
                                size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
