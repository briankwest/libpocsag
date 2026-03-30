#include "libpocsag/bch.h"
#include "libpocsag/types.h"

#define BCH_GEN POCSAG_BCH_POLY

uint32_t pocsag_bch_encode(uint32_t data21)
{
	uint32_t reg = data21 << 10;

	for (int i = 30; i >= 10; i--) {
		if (reg & (1u << i))
			reg ^= (BCH_GEN << (i - 10));
	}

	return reg & 0x3FFu;
}

uint32_t pocsag_codeword_build(uint32_t data21)
{
	uint32_t bch = pocsag_bch_encode(data21);
	uint32_t cw = (data21 << 11) | (bch << 1);

	/* even parity over bits 31..1 */
	uint32_t p = cw >> 1;
	p ^= p >> 16;
	p ^= p >> 8;
	p ^= p >> 4;
	p ^= p >> 2;
	p ^= p >> 1;
	cw |= (p & 1);

	return cw;
}

uint32_t pocsag_bch_syndrome(uint32_t codeword)
{
	uint32_t reg = codeword >> 1;

	for (int i = 30; i >= 10; i--) {
		if (reg & (1u << i))
			reg ^= (BCH_GEN << (i - 10));
	}

	return reg & 0x3FFu;
}

int pocsag_parity_check(uint32_t codeword)
{
	uint32_t p = codeword;
	p ^= p >> 16;
	p ^= p >> 8;
	p ^= p >> 4;
	p ^= p >> 2;
	p ^= p >> 1;
	return !(p & 1);
}

int pocsag_bch_correct(uint32_t *codeword)
{
	uint32_t cw = *codeword;
	uint32_t syndrome = pocsag_bch_syndrome(cw);

	if (syndrome == 0) {
		if (pocsag_parity_check(cw))
			return 0;
		/* parity error only: flip bit 0 */
		*codeword = cw ^ 1u;
		return 0;
	}

	/* try flipping each BCH-protected bit (31..1) */
	for (int i = 1; i <= 31; i++) {
		uint32_t trial = cw ^ (1u << i);
		if (pocsag_bch_syndrome(trial) == 0) {
			if (pocsag_parity_check(trial)) {
				*codeword = trial;
				return 0;
			}
			return -1; /* multi-bit error */
		}
	}

	return -1;
}
