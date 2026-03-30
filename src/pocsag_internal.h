#ifndef POCSAG_INTERNAL_H
#define POCSAG_INTERNAL_H

#include "libpocsag/types.h"
#include "libpocsag/bch.h"
#include "libpocsag/error.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Sample-rate / baud-rate validation (modulator.c) */
int pocsag_srate_valid(uint32_t sr);
int pocsag_baud_valid(uint32_t br);

/* Codeword construction */
uint32_t pocsag_cw_address(uint32_t address, pocsag_func_t func);
uint32_t pocsag_cw_message(uint32_t data20);

/* Numeric BCD encode/decode.
 * Returns number of 20-bit chunks produced (encode) or chars decoded (decode),
 * or -1 on error. */
int pocsag_numeric_encode(const char *text, size_t len,
                          uint32_t *chunks, int max_chunks);
int pocsag_numeric_decode(const uint32_t *chunks, int nchunks,
                          char *text, size_t text_cap);

/* 7-bit alpha encode/decode.
 * Returns number of 20-bit chunks produced (encode) or chars decoded (decode),
 * or -1 on error. */
int pocsag_alpha_encode(const char *text, size_t len,
                        uint32_t *chunks, int max_chunks);
int pocsag_alpha_decode(const uint32_t *chunks, int nchunks,
                        char *text, size_t text_cap);

/* Bitstream writer */
typedef struct {
	uint8_t *data;
	size_t   cap;
	size_t   byte_pos;
	int      bit_pos;
	size_t   total_bits;
} pocsag_bs_t;

static inline void bs_init(pocsag_bs_t *bs, uint8_t *buf, size_t cap)
{
	bs->data = buf;
	bs->cap = cap;
	bs->byte_pos = 0;
	bs->bit_pos = 7;
	bs->total_bits = 0;
	if (cap > 0)
		memset(buf, 0, cap);
}

static inline int bs_write_bit(pocsag_bs_t *bs, int bit)
{
	if (bs->byte_pos >= bs->cap)
		return -1;
	if (bit)
		bs->data[bs->byte_pos] |= (1u << bs->bit_pos);
	bs->bit_pos--;
	if (bs->bit_pos < 0) {
		bs->bit_pos = 7;
		bs->byte_pos++;
	}
	bs->total_bits++;
	return 0;
}

static inline int bs_write_codeword(pocsag_bs_t *bs, uint32_t cw)
{
	for (int i = 31; i >= 0; i--) {
		if (bs_write_bit(bs, (cw >> i) & 1) < 0)
			return -1;
	}
	return 0;
}

#endif
