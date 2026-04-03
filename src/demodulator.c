#include "pocsag_internal.h"
#include "libpocsag/demodulator.h"
#include "libpocsag/modulator.h"   /* POCSAG_FSK_{MARK,SPACE}_HZ, POCSAG_SRATE_* */
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void demod_precompute(pocsag_demod_t *d)
{
	d->omega_mark  = 2.0 * M_PI * (double)d->mark_freq
	                 / (double)d->sample_rate;
	d->omega_space = 2.0 * M_PI * (double)d->space_freq
	                 / (double)d->sample_rate;
	d->phase_inc   = (double)d->baud_rate / (double)d->sample_rate;
}

void pocsag_demod_init(pocsag_demod_t *demod,
                       uint32_t sample_rate, uint32_t baud_rate,
                       pocsag_bit_cb_t callback, void *user)
{
	pocsag_demod_init_custom(demod, sample_rate, baud_rate,
	                         POCSAG_FSK_MARK_HZ(baud_rate),
	                         POCSAG_FSK_SPACE_HZ(baud_rate),
	                         callback, user);
}

void pocsag_demod_init_custom(pocsag_demod_t *demod,
                              uint32_t sample_rate, uint32_t baud_rate,
                              float mark_freq, float space_freq,
                              pocsag_bit_cb_t callback, void *user)
{
	memset(demod, 0, sizeof(*demod));
	demod->sample_rate = sample_rate;
	demod->baud_rate   = baud_rate;
	demod->mark_freq   = (mark_freq > 0.0f && isfinite(mark_freq)
	                       && mark_freq < (float)sample_rate / 2.0f)
	                      ? mark_freq : 0.0f;
	demod->space_freq  = (space_freq > 0.0f && isfinite(space_freq)
	                       && space_freq < (float)sample_rate / 2.0f)
	                      ? space_freq : 0.0f;
	demod->callback    = callback;
	demod->user        = user;
	demod->last_bit    = -1;
	demod_precompute(demod);
}

void pocsag_demod_reset(pocsag_demod_t *demod)
{
	pocsag_bit_cb_t cb   = demod->callback;
	void           *user = demod->user;
	uint32_t sr = demod->sample_rate;
	uint32_t br = demod->baud_rate;
	float    mf = demod->mark_freq;
	float    sf = demod->space_freq;

	memset(demod, 0, sizeof(*demod));
	demod->sample_rate = sr;
	demod->baud_rate   = br;
	demod->mark_freq   = mf;
	demod->space_freq  = sf;
	demod->callback    = cb;
	demod->user        = user;
	demod->last_bit    = -1;
	demod_precompute(demod);
}

pocsag_err_t pocsag_demodulate(pocsag_demod_t *d,
                               const float *samples, size_t nsamples)
{
	if (!d || !samples)
		return POCSAG_ERR_PARAM;
	if (!pocsag_srate_valid(d->sample_rate))
		return POCSAG_ERR_PARAM;
	if (!pocsag_baud_valid(d->baud_rate))
		return POCSAG_ERR_PARAM;
	if (d->sample_rate / d->baud_rate < 5)
		return POCSAG_ERR_PARAM;
	if (d->mark_freq <= 0.0f || d->space_freq <= 0.0f)
		return POCSAG_ERR_PARAM;

	for (size_t i = 0; i < nsamples; i++) {
		double s = (double)samples[i];
		double t = (double)d->sample_idx;

		/* Accumulate quadrature correlators for mark and space
		 * frequencies.  This is a non-coherent matched-filter
		 * detector: energy = I^2 + Q^2 is independent of the
		 * incoming carrier phase. */
		d->mark_i  += s * cos(d->omega_mark  * t);
		d->mark_q  += s * sin(d->omega_mark  * t);
		d->space_i += s * cos(d->omega_space * t);
		d->space_q += s * sin(d->omega_space * t);
		d->sample_idx++;

		/* advance fractional bit clock */
		d->bit_phase += d->phase_inc;

		if (d->bit_phase >= 1.0) {
			/* decision: compare mark energy vs space energy */
			double me = d->mark_i  * d->mark_i
			          + d->mark_q  * d->mark_q;
			double se = d->space_i * d->space_i
			          + d->space_q * d->space_q;
			double energy = me - se;
			int    bit = (energy > 0.0) ? 1 : 0;

			/* track transitions (useful for diagnostics) */
			if (d->last_bit >= 0 && bit != d->last_bit)
				d->stat_transitions++;

			d->last_bit    = bit;
			d->last_energy = energy;
			d->bit_phase  -= 1.0;

			/* deliver the recovered bit */
			if (d->callback)
				d->callback(bit, d->user);
			d->stat_bits++;

			/* reset correlators for the next bit period */
			d->mark_i  = 0.0;
			d->mark_q  = 0.0;
			d->space_i = 0.0;
			d->space_q = 0.0;
			d->sample_idx = 0;
		}
	}

	return POCSAG_OK;
}

pocsag_err_t pocsag_demod_baseband(pocsag_demod_t *d,
                                   const float *samples, size_t nsamples)
{
	if (!d || !samples)
		return POCSAG_ERR_PARAM;
	if (!pocsag_baud_valid(d->baud_rate) || d->sample_rate == 0)
		return POCSAG_ERR_PARAM;

	/* Two-stage cascaded IIR low-pass at 1.5x baud rate to reject
	 * wideband FM discriminator noise.  Two stages give -12 dB/oct
	 * rolloff (vs -6 for one stage), which is critical because the
	 * FM noise floor is often louder than the POCSAG data signal.
	 *
	 * mark_q  = stage 1 state
	 * space_q = stage 2 state */
	double alpha_lp = 1.0 / (1.0 + (double)d->sample_rate
	                  / (2.0 * M_PI * 1.5 * (double)d->baud_rate));

	for (size_t i = 0; i < nsamples; i++) {
		/* two-stage IIR low-pass, then accumulate */
		d->mark_q  += alpha_lp * ((double)samples[i] - d->mark_q);
		d->space_q += alpha_lp * (d->mark_q - d->space_q);
		d->mark_i  += d->space_q;
		d->sample_idx++;

		d->bit_phase += d->phase_inc;

		if (d->bit_phase >= 1.0) {
			/* decision: negative sum → bit 1, positive → bit 0
			 * (matches pocsag_baseband() polarity) */
			int bit = (d->mark_i < 0.0) ? 1 : 0;

			if (d->last_bit >= 0 && bit != d->last_bit)
				d->stat_transitions++;

			d->last_bit   = bit;
			d->last_energy = d->mark_i;
			d->bit_phase -= 1.0;

			if (d->callback)
				d->callback(bit, d->user);
			d->stat_bits++;

			d->mark_i     = 0.0;
			d->sample_idx = 0;
			/* note: mark_q (LPF state) is NOT reset — it
			 * carries the filter memory across bit boundaries
			 * for smooth continuity */
		}
	}

	return POCSAG_OK;
}

/* ================================================================
 * Multi-phase FSK receiver
 * ================================================================ */

static void rx_on_bit(int bit, void *user)
{
	struct pocsag_rx_phase_ctx *ctx = (struct pocsag_rx_phase_ctx *)user;
	uint8_t b = (uint8_t)(bit & 1);
	pocsag_decoder_feed_bits(&ctx->rx->decoder[ctx->phase], &b, 1);
}

void pocsag_rx_init(pocsag_rx_t *rx, uint32_t sample_rate,
                    uint32_t baud_rate,
                    pocsag_msg_cb_t cb, void *user)
{
	memset(rx, 0, sizeof(*rx));
	rx->active = -1;
	double phase_max = (double)sample_rate / (double)baud_rate;
	for (int p = 0; p < POCSAG_RX_NPHASE; p++) {
		rx->ctx[p].rx    = rx;
		rx->ctx[p].phase = p;
		pocsag_demod_init(&rx->demod[p], sample_rate, baud_rate,
		                  rx_on_bit, &rx->ctx[p]);
		/* offset each demod's bit clock evenly */
		rx->demod[p].bit_phase = (double)p / (double)POCSAG_RX_NPHASE;
		/* advance sample_idx to match so correlators stay coherent */
		rx->demod[p].sample_idx = (uint32_t)((double)p * phase_max
		                           / (double)POCSAG_RX_NPHASE);
		pocsag_decoder_init(&rx->decoder[p], cb, user);
	}
}

void pocsag_rx_reset(pocsag_rx_t *rx)
{
	uint32_t sr = rx->demod[0].sample_rate;
	uint32_t br = rx->demod[0].baud_rate;
	pocsag_msg_cb_t cb = rx->decoder[0].callback;
	void *user = rx->decoder[0].user;
	pocsag_rx_init(rx, sr, br, cb, user);
}

void pocsag_rx_flush(pocsag_rx_t *rx)
{
	for (int p = 0; p < POCSAG_RX_NPHASE; p++)
		pocsag_decoder_flush(&rx->decoder[p]);
	rx->active = -1;
}

pocsag_err_t pocsag_rx_feed(pocsag_rx_t *rx,
                            const float *samples, size_t nsamples)
{
	if (!rx || !samples)
		return POCSAG_ERR_PARAM;

	for (int p = 0; p < POCSAG_RX_NPHASE; p++) {
		if (rx->active >= 0 && p != rx->active)
			continue;

		pocsag_dec_state_t prev = rx->decoder[p].state;
		pocsag_demodulate(&rx->demod[p], samples, nsamples);

		/* lock to first phase that finds sync */
		if (rx->active < 0 &&
		    rx->decoder[p].state != POCSAG_DEC_HUNTING)
			rx->active = p;

		/* batch done — clear correlators, unlock for next frame */
		if (prev != POCSAG_DEC_HUNTING &&
		    rx->decoder[p].state == POCSAG_DEC_HUNTING) {
			rx->demod[p].mark_i = rx->demod[p].mark_q = 0.0;
			rx->demod[p].space_i = rx->demod[p].space_q = 0.0;
			rx->demod[p].sample_idx = 0;
			if (p == rx->active)
				rx->active = -1;
		}
	}
	return POCSAG_OK;
}
