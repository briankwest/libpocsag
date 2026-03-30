#ifndef LIBPOCSAG_DEMODULATOR_H
#define LIBPOCSAG_DEMODULATOR_H

#include <stdint.h>
#include <stddef.h>
#include "error.h"

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

#ifdef __cplusplus
}
#endif

#endif
