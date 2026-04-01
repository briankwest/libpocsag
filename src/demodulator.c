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

	for (size_t i = 0; i < nsamples; i++) {
		/* accumulate sample level over the bit period */
		d->mark_i += (double)samples[i];
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
		}
	}

	return POCSAG_OK;
}
