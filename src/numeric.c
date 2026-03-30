#include "pocsag_internal.h"

/*
 * POCSAG BCD uses bit-reversed nibbles: each 4-bit digit has its
 * bits reversed before transmission (LSB transmitted first).
 *
 * Standard BCD:  0=0000, 1=0001, 2=0010, ..., 9=1001
 * Reversed:      0=0000, 1=1000, 2=0100, ..., 9=1001
 *
 * The bcd_table maps each 4-bit REVERSED value back to the display
 * character.  char_to_bcd maps a display character to its REVERSED
 * 4-bit value for encoding.
 */
static const char bcd_table[] = "084 2*6]195-3U7[";

static int char_to_bcd(char c)
{
	switch (c) {
	case '0':           return 0;
	case '1':           return 8;
	case '2':           return 4;
	case '3':           return 12;
	case '4':           return 2;
	case '5':           return 10;
	case '6':           return 6;
	case '7':           return 14;
	case '8':           return 1;
	case '9':           return 9;
	case '*': case '.': return 5;
	case 'U': case 'u': return 13;
	case ' ':           return 3;
	case '-':           return 11;
	case '[': case '(': return 15;
	case ']': case ')': return 7;
	}
	return -1;
}

int pocsag_numeric_encode(const char *text, size_t len,
                          uint32_t *chunks, int max_chunks)
{
	if (!text || !chunks)
		return -1;

	int nchunks = 0;
	uint32_t chunk = 0;
	int digits = 0;

	for (size_t i = 0; i < len; i++) {
		int bcd = char_to_bcd(text[i]);
		if (bcd < 0)
			return -1;

		chunk |= ((uint32_t)bcd << (16 - digits * 4));
		digits++;

		if (digits == 5) {
			if (nchunks >= max_chunks)
				return -1;
			chunks[nchunks++] = chunk;
			chunk = 0;
			digits = 0;
		}
	}

	/* flush partial chunk (pad with space=3, bit-reversed 0xC) */
	if (digits > 0) {
		while (digits < 5) {
			chunk |= (3u << (16 - digits * 4));
			digits++;
		}
		if (nchunks >= max_chunks)
			return -1;
		chunks[nchunks++] = chunk;
	}

	return nchunks;
}

int pocsag_numeric_decode(const uint32_t *chunks, int nchunks,
                          char *text, size_t text_cap)
{
	if (!chunks || !text || text_cap == 0)
		return -1;

	size_t pos = 0;

	for (int c = 0; c < nchunks; c++) {
		for (int d = 0; d < 5; d++) {
			int bcd = (chunks[c] >> (16 - d * 4)) & 0xF;
			if (pos + 1 >= text_cap)
				goto done;
			text[pos++] = bcd_table[bcd];
		}
	}

done:
	/* trim trailing spaces */
	while (pos > 0 && text[pos - 1] == ' ')
		pos--;

	text[pos] = '\0';
	return (int)pos;
}
