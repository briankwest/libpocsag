#ifndef LIBPOCSAG_DEMODULATOR_H
#define LIBPOCSAG_DEMODULATOR_H

#include <stdint.h>
#include <stddef.h>
#include "error.h"
#include "decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Thread safety: each pocsag_demod_t instance may be used by one
 * thread at a time.  Distinct instances may be used concurrently
 * without synchronisation.  The library contains no global mutable
 * state.  Callbacks must not modify the demodulator instance. */

/* Callback invoked for each recovered bit (0 or 1).
 * Feed these into pocsag_decoder_feed_bits() to decode messages. */
typedef void (*pocsag_bit_cb_t)(int bit, void *user);

typedef struct {
	uint32_t sample_rate;   /* audio sample rate in Hz */
	uint32_t baud_rate;     /* expected bit rate */
	float    mark_freq;     /* tone frequency for binary 1 (Hz) */
	float    space_freq;    /* tone frequency for binary 0 (Hz) */

	pocsag_bit_cb_t callback;
	void           *user;

	/* precomputed angular frequencies */
	double   omega_mark;    /* 2*PI*mark_freq/sample_rate */
	double   omega_space;   /* 2*PI*space_freq/sample_rate */
	double   phase_inc;     /* baud_rate / sample_rate */

	/* correlator accumulators (non-coherent matched filter) */
	double   mark_i, mark_q;
	double   space_i, space_q;
	double   bit_phase;     /* fractional position within current bit */
	uint32_t sample_idx;    /* sample index within current bit period */

	/* state */
	int      last_bit;      /* previous bit decision (-1 = none) */
	double   last_energy;   /* previous decision energy */

	/* statistics */
	uint32_t stat_bits;         /* total bits recovered */
	uint32_t stat_transitions;  /* bit transitions detected */
} pocsag_demod_t;

/* Initialize demodulator with default AFSK frequencies */
void         pocsag_demod_init(pocsag_demod_t *demod,
                               uint32_t sample_rate,
                               uint32_t baud_rate,
                               pocsag_bit_cb_t callback,
                               void *user);

/* Initialize demodulator with custom tone frequencies */
void         pocsag_demod_init_custom(pocsag_demod_t *demod,
                                      uint32_t sample_rate,
                                      uint32_t baud_rate,
                                      float mark_freq,
                                      float space_freq,
                                      pocsag_bit_cb_t callback,
                                      void *user);

/* Reset demodulator state (preserves configuration and callback) */
void         pocsag_demod_reset(pocsag_demod_t *demod);

/* Feed audio samples into the demodulator.  Recovered bits are
 * delivered one at a time through the callback.
 *
 * samples:  float audio in the range [-1, +1]
 * nsamples: number of samples to process
 */
pocsag_err_t pocsag_demodulate(pocsag_demod_t *demod,
                               const float *samples,
                               size_t nsamples);

/* Baseband NRZ demodulator.  Recovers bits from FM discriminator
 * output (flat-level-per-bit) by summing sample values over each
 * bit period and thresholding at zero.
 *
 * Polarity (matches pocsag_baseband() output / standard FM discrim):
 *   negative level → bit 1
 *   positive level → bit 0
 *
 * Reuses the same pocsag_demod_t struct and bit callback.
 * The mark_freq / space_freq fields are unused in this mode.
 */
pocsag_err_t pocsag_demod_baseband(pocsag_demod_t *demod,
                                   const float *samples,
                                   size_t nsamples);

/* ---- Multi-phase FSK receiver ----
 *
 * Runs POCSAG_RX_NPHASE demod+decoder pairs at evenly-spaced
 * bit-clock offsets.  Whichever decoder finds sync first is used
 * for the remainder of that batch.  Proven approach for reliable
 * decode when the symbol clock phase is unknown (SDR / audio).
 *
 *   pocsag_rx_t rx;
 *   pocsag_rx_init(&rx, 48000, 1200, my_msg_cb, ctx);
 *   // audio loop:
 *   pocsag_rx_feed(&rx, audio, n);
 *   // squelch close:
 *   pocsag_rx_flush(&rx);
 *   // squelch open:
 *   pocsag_rx_reset(&rx);
 */

#define POCSAG_RX_NPHASE 5

typedef struct pocsag_rx pocsag_rx_t;  /* forward — defined in .c */

struct pocsag_rx_phase_ctx {
	pocsag_rx_t *rx;
	int          phase;
};

struct pocsag_rx {
	pocsag_demod_t   demod[POCSAG_RX_NPHASE];
	pocsag_decoder_t decoder[POCSAG_RX_NPHASE];
	struct pocsag_rx_phase_ctx ctx[POCSAG_RX_NPHASE];
	int              active;   /* locked phase, or -1 */
};

void         pocsag_rx_init(pocsag_rx_t *rx, uint32_t sample_rate,
                            uint32_t baud_rate,
                            pocsag_msg_cb_t cb, void *user);
void         pocsag_rx_reset(pocsag_rx_t *rx);
void         pocsag_rx_flush(pocsag_rx_t *rx);
pocsag_err_t pocsag_rx_feed(pocsag_rx_t *rx,
                            const float *samples, size_t nsamples);

#ifdef __cplusplus
}
#endif

#endif
