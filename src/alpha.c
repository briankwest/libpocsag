#include "pocsag_internal.h"

int pocsag_alpha_encode(const char *text, size_t len,
                        uint32_t *chunks, int max_chunks)
{
	if (!text || !chunks)
		return -1;

	int nchunks = 0;
	uint32_t chunk = 0;
	int chunk_bit = 19; /* fill from MSB (bit 19) downward */

	for (size_t i = 0; i <= len; i++) {
		uint8_t ch = (i < len) ? (uint8_t)text[i] : 0x04; /* EOT */

		for (int bit = 0; bit < 7; bit++) {
			if ((ch >> bit) & 1)
				chunk |= (1u << chunk_bit);

			chunk_bit--;
			if (chunk_bit < 0) {
				if (nchunks >= max_chunks)
					return -1;
				chunks[nchunks++] = chunk;
				chunk = 0;
				chunk_bit = 19;
			}
		}
	}

	/* flush partial chunk */
	if (chunk_bit < 19) {
		if (nchunks >= max_chunks)
			return -1;
		chunks[nchunks++] = chunk;
	}

	return nchunks;
}

int pocsag_alpha_decode(const uint32_t *chunks, int nchunks,
                        char *text, size_t text_cap)
{
	if (!chunks || !text || text_cap == 0)
		return -1;

	int total_bits = nchunks * 20;
	size_t pos = 0;
	int bit_pos = 0;

	while (bit_pos + 7 <= total_bits) {
		uint8_t ch = 0;

		for (int bit = 0; bit < 7; bit++) {
			int abs_bit = bit_pos + bit;
			int ci = abs_bit / 20;
			int cb = 19 - (abs_bit % 20);

			if ((chunks[ci] >> cb) & 1)
				ch |= (1u << bit);
		}

		bit_pos += 7;

		if (ch == 0x00 || ch == 0x04)
			break;

		if (pos + 1 >= text_cap)
			break;

		text[pos++] = (char)ch;
	}

	text[pos] = '\0';
	return (int)pos;
}
