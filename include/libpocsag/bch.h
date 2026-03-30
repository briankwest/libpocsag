#ifndef LIBPOCSAG_BCH_H
#define LIBPOCSAG_BCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t pocsag_bch_encode(uint32_t data21);
uint32_t pocsag_codeword_build(uint32_t data21);
uint32_t pocsag_bch_syndrome(uint32_t codeword);
int      pocsag_bch_correct(uint32_t *codeword);
int      pocsag_parity_check(uint32_t codeword);

#ifdef __cplusplus
}
#endif

#endif
