#include "pocsag_internal.h"
#include "libpocsag/encoder.h"
#include <stdlib.h>

void pocsag_encoder_init(pocsag_encoder_t *enc)
{
	memset(enc, 0, sizeof(*enc));
}

void pocsag_encoder_reset(pocsag_encoder_t *enc)
{
	enc->count = 0;
}

pocsag_err_t pocsag_encoder_add(pocsag_encoder_t *enc,
                                uint32_t address,
                                pocsag_func_t function,
                                pocsag_msg_type_t type,
                                const char *text)
{
	if (!enc)
		return POCSAG_ERR_PARAM;
	if (enc->count >= POCSAG_TX_MAX_MESSAGES)
		return POCSAG_ERR_OVERFLOW;
	if (address > 0x1FFFFFu)
		return POCSAG_ERR_PARAM;

	pocsag_msg_t *m = &enc->messages[enc->count];
	m->address = address;
	m->function = function;
	m->type = type;

	if (text && type != POCSAG_MSG_TONE_ONLY) {
		size_t len = strlen(text);
		if (len >= POCSAG_MSG_MAX)
			len = POCSAG_MSG_MAX - 1;
		memcpy(m->text, text, len);
		m->text[len] = '\0';
		m->text_len = len;
	} else {
		m->text[0] = '\0';
		m->text_len = 0;
	}

	enc->count++;
	return POCSAG_OK;
}

/* internal per-message codeword list */
typedef struct {
	uint32_t cws[80];
	int      count;
	int      sent;
	int      frame;
} msg_cws_t;

static int cmp_by_frame(const void *a, const void *b)
{
	const msg_cws_t *ma = (const msg_cws_t *)a;
	const msg_cws_t *mb = (const msg_cws_t *)b;
	return ma->frame - mb->frame;
}

pocsag_err_t pocsag_encode(const pocsag_encoder_t *enc,
                           uint8_t *out, size_t out_cap,
                           size_t *out_len, size_t *out_bits)
{
	if (!enc || !out || !out_len || !out_bits)
		return POCSAG_ERR_PARAM;
	if (enc->count == 0)
		return POCSAG_ERR_PARAM;

	/* build codeword lists for each message */
	msg_cws_t msgs[POCSAG_TX_MAX_MESSAGES];
	int nmsg = 0;

	for (size_t i = 0; i < enc->count; i++) {
		const pocsag_msg_t *m = &enc->messages[i];
		msg_cws_t *mc = &msgs[nmsg];
		mc->count = 0;
		mc->sent = 0;
		mc->frame = m->address & 7;

		/* address codeword */
		mc->cws[mc->count++] = pocsag_cw_address(m->address, m->function);

		/* message data codewords */
		if (m->type != POCSAG_MSG_TONE_ONLY && m->text_len > 0) {
			uint32_t chunks[40];
			int nchunks;

			if (m->type == POCSAG_MSG_ALPHA) {
				nchunks = pocsag_alpha_encode(m->text, m->text_len,
				                              chunks, 40);
			} else {
				nchunks = pocsag_numeric_encode(m->text, m->text_len,
				                                chunks, 40);
			}

			if (nchunks < 0)
				return POCSAG_ERR_BADCHAR;

			for (int c = 0; c < nchunks && mc->count < 80; c++)
				mc->cws[mc->count++] = pocsag_cw_message(chunks[c]);
		}

		nmsg++;
	}

	/* sort by frame */
	qsort(msgs, (size_t)nmsg, sizeof(msg_cws_t), cmp_by_frame);

	/* write bitstream */
	pocsag_bs_t bs;
	bs_init(&bs, out, out_cap);

	/* preamble: 576 bits of alternating 1/0 */
	for (int i = 0; i < POCSAG_PREAMBLE_BITS; i++) {
		if (bs_write_bit(&bs, (i & 1) ? 0 : 1) < 0)
			return POCSAG_ERR_OVERFLOW;
	}

	/* generate batches until all messages are sent */
	int done = 0;
	while (!done) {
		uint32_t batch[POCSAG_CODEWORDS_PER_BATCH];
		for (int i = 0; i < POCSAG_CODEWORDS_PER_BATCH; i++)
			batch[i] = POCSAG_IDLE_CODEWORD;

		int cursor = 0;

		/* pass 1: continuation messages (start from slot 0) */
		for (int m = 0; m < nmsg; m++) {
			if (msgs[m].sent == 0 || msgs[m].sent >= msgs[m].count)
				continue;
			while (msgs[m].sent < msgs[m].count &&
			       cursor < POCSAG_CODEWORDS_PER_BATCH) {
				batch[cursor++] = msgs[m].cws[msgs[m].sent++];
			}
		}

		/* pass 2: new messages at their frame positions */
		for (int m = 0; m < nmsg; m++) {
			if (msgs[m].sent != 0)
				continue;

			int target = msgs[m].frame * 2;

			if (target < cursor)
				continue; /* defer to next batch */

			/* find next new message's frame to limit placement */
			int limit = POCSAG_CODEWORDS_PER_BATCH;
			for (int n = m + 1; n < nmsg; n++) {
				if (msgs[n].sent == 0) {
					limit = msgs[n].frame * 2;
					break;
				}
			}

			cursor = target;

			while (msgs[m].sent < msgs[m].count && cursor < limit) {
				batch[cursor++] = msgs[m].cws[msgs[m].sent++];
			}
		}

		/* write sync + batch */
		if (bs_write_codeword(&bs, POCSAG_SYNC_CODEWORD) < 0)
			return POCSAG_ERR_OVERFLOW;
		for (int i = 0; i < POCSAG_CODEWORDS_PER_BATCH; i++) {
			if (bs_write_codeword(&bs, batch[i]) < 0)
				return POCSAG_ERR_OVERFLOW;
		}

		/* check if all messages are complete */
		done = 1;
		for (int m = 0; m < nmsg; m++) {
			if (msgs[m].sent < msgs[m].count) {
				done = 0;
				break;
			}
		}
	}

	*out_bits = bs.total_bits;
	*out_len = bs.byte_pos + (bs.bit_pos < 7 ? 1 : 0);
	return POCSAG_OK;
}

pocsag_err_t pocsag_encode_single(uint32_t address,
                                  pocsag_func_t function,
                                  pocsag_msg_type_t type,
                                  const char *text,
                                  uint8_t *out, size_t out_cap,
                                  size_t *out_len, size_t *out_bits)
{
	pocsag_encoder_t enc;
	pocsag_encoder_init(&enc);

	pocsag_err_t err = pocsag_encoder_add(&enc, address, function, type, text);
	if (err != POCSAG_OK)
		return err;

	return pocsag_encode(&enc, out, out_cap, out_len, out_bits);
}
