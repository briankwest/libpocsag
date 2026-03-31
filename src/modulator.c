#include "pocsag_internal.h"
#include "libpocsag/modulator.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int pocsag_srate_valid(uint32_t sr)
{
	return sr == POCSAG_SRATE_8000  || sr == POCSAG_SRATE_16000
	    || sr == POCSAG_SRATE_32000 || sr == POCSAG_SRATE_48000;
}

int pocsag_baud_valid(uint32_t br)
{
	return br == POCSAG_BAUD_512 || br == POCSAG_BAUD_1200
	    || br == POCSAG_BAUD_2400;
}

void pocsag_mod_init(pocsag_mod_t *mod,
                     uint32_t sample_rate, uint32_t baud_rate)
{
	pocsag_mod_init_custom(mod, sample_rate, baud_rate,
	                       POCSAG_FSK_MARK_HZ(baud_rate),
	                       POCSAG_FSK_SPACE_HZ(baud_rate));
}

static int freq_valid(float f, uint32_t sample_rate)
{
	return f > 0.0f && isfinite(f)
	       && f < (float)sample_rate / 2.0f;
}

void pocsag_mod_init_custom(pocsag_mod_t *mod,
                            uint32_t sample_rate, uint32_t baud_rate,
                            float mark_freq, float space_freq)
{
	mod->sample_rate = sample_rate;
	mod->baud_rate   = baud_rate;
	mod->mark_freq   = freq_valid(mark_freq, sample_rate)
	                   ? mark_freq : 0.0f;
	mod->space_freq  = freq_valid(space_freq, sample_rate)
	                   ? space_freq : 0.0f;
	mod->phase       = 0.0;
}

void pocsag_mod_reset(pocsag_mod_t *mod)
{
	mod->phase = 0.0;
}

size_t pocsag_mod_samples_needed(const pocsag_mod_t *mod, size_t nbits)
{
	if (!mod || mod->baud_rate == 0)
		return 0;
	/* ceil(nbits * sample_rate / baud_rate) */
	return (size_t)(((uint64_t)nbits * mod->sample_rate
	                 + mod->baud_rate - 1) / mod->baud_rate);
}

pocsag_err_t pocsag_modulate(pocsag_mod_t *mod,
                             const uint8_t *bits, size_t nbits,
                             float *out, size_t out_cap,
                             size_t *out_len)
{
	if (!mod || !bits || !out || !out_len)
		return POCSAG_ERR_PARAM;
	if (!pocsag_srate_valid(mod->sample_rate))
		return POCSAG_ERR_PARAM;
	if (!pocsag_baud_valid(mod->baud_rate))
		return POCSAG_ERR_PARAM;
	if (mod->sample_rate / mod->baud_rate < 5)
		return POCSAG_ERR_PARAM;
	if (mod->mark_freq <= 0.0f || mod->space_freq <= 0.0f)
		return POCSAG_ERR_PARAM;

	size_t needed = pocsag_mod_samples_needed(mod, nbits);
	if (needed > out_cap)
		return POCSAG_ERR_OVERFLOW;

	double spb = (double)mod->sample_rate / (double)mod->baud_rate;
	double omega_mark  = 2.0 * M_PI * (double)mod->mark_freq
	                     / (double)mod->sample_rate;
	double omega_space = 2.0 * M_PI * (double)mod->space_freq
	                     / (double)mod->sample_rate;
	double phase = mod->phase;
	size_t si = 0;

	for (size_t bi = 0; bi < nbits; bi++) {
		int bit = (bits[bi / 8] >> (7 - (bi & 7))) & 1;
		double omega = bit ? omega_mark : omega_space;

		/* bit boundary computed in floating-point to handle
		 * non-integer samples-per-bit correctly */
		size_t next = (size_t)((double)(bi + 1) * spb + 0.5);
		if (next > needed)
			next = needed;

		while (si < next) {
			out[si++] = (float)sin(phase);
			phase += omega;
		}

		/* wrap phase per-bit to maintain precision on long
		 * transmissions (avoids large-argument sin()) */
		if (phase > 2.0 * M_PI)
			phase -= 2.0 * M_PI * (int)(phase / (2.0 * M_PI));
	}

	mod->phase = phase;

	*out_len = si;
	return POCSAG_OK;
}

pocsag_err_t pocsag_baseband(const uint8_t *bits, size_t nbits,
                             uint32_t sample_rate, uint32_t baud_rate,
                             float *out, size_t out_cap,
                             size_t *out_len)
{
	return pocsag_baseband_ex(bits, nbits, sample_rate, baud_rate,
	                          0, out, out_cap, out_len);
}

pocsag_err_t pocsag_baseband_ex(const uint8_t *bits, size_t nbits,
                                uint32_t sample_rate, uint32_t baud_rate,
                                int flags,
                                float *out, size_t out_cap,
                                size_t *out_len)
{
	if (!bits || !out || !out_len)
		return POCSAG_ERR_PARAM;
	if (!pocsag_srate_valid(sample_rate))
		return POCSAG_ERR_PARAM;
	if (!pocsag_baud_valid(baud_rate))
		return POCSAG_ERR_PARAM;
	if (sample_rate / baud_rate < 5)
		return POCSAG_ERR_PARAM;

	double spb = (double)sample_rate / (double)baud_rate;
	size_t needed = (size_t)(((uint64_t)nbits * sample_rate
	                          + baud_rate - 1) / baud_rate);
	if (needed > out_cap)
		return POCSAG_ERR_OVERFLOW;

	size_t si = 0;
	for (size_t bi = 0; bi < nbits; bi++) {
		int bit = (bits[bi / 8] >> (7 - (bi & 7))) & 1;
		float level = bit ? -0.73f : 0.73f;

		size_t next = (size_t)((double)(bi + 1) * spb + 0.5);
		if (next > needed)
			next = needed;

		while (si < next)
			out[si++] = level;
	}

	/* Apply 75µs de-emphasis (first-order IIR lowpass) to pre-cancel
	 * the radio's TX pre-emphasis.  After the radio boosts highs,
	 * the receiver gets a flat NRZ signal. */
	if ((flags & POCSAG_BASEBAND_DEEMPH) && si > 0) {
		float alpha = 1.0f / (1.0f + (float)sample_rate * 75e-6f);
		float prev = out[0];
		for (size_t i = 1; i < si; i++) {
			prev = prev + alpha * (out[i] - prev);
			out[i] = prev;
		}
	}

	*out_len = si;
	return POCSAG_OK;
}
