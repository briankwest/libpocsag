#include "pocsag_internal.h"
#include "libpocsag/decoder.h"

static void deliver_message(pocsag_decoder_t *dec)
{
	if (!dec->msg_active)
		return;

	pocsag_msg_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.address = dec->msg_address;
	msg.function = dec->msg_function;

	if (dec->msg_data_count == 0) {
		msg.type = POCSAG_MSG_TONE_ONLY;
	} else if (dec->msg_function == POCSAG_FUNC_ALPHA) {
		msg.type = POCSAG_MSG_ALPHA;
		int len = pocsag_alpha_decode(dec->msg_data, dec->msg_data_count,
		                              msg.text, sizeof(msg.text));
		msg.text_len = len > 0 ? (size_t)len : 0;
	} else {
		msg.type = POCSAG_MSG_NUMERIC;
		int len = pocsag_numeric_decode(dec->msg_data, dec->msg_data_count,
		                                msg.text, sizeof(msg.text));
		msg.text_len = len > 0 ? (size_t)len : 0;
	}

	if (dec->callback)
		dec->callback(&msg, dec->user);

	dec->msg_active = 0;
	dec->stat_messages++;
}

static void process_codeword(pocsag_decoder_t *dec, uint32_t cw)
{
	dec->stat_codewords++;

	/* BCH error correction */
	uint32_t syn = pocsag_bch_syndrome(cw);
	if (syn != 0) {
		if (pocsag_bch_correct(&cw) == 0) {
			dec->stat_corrected++;
		} else {
			dec->stat_errors++;
			return;
		}
	} else if (!pocsag_parity_check(cw)) {
		/* syndrome OK but parity bad: flip parity bit */
		cw ^= 1u;
		dec->stat_corrected++;
	}

	/* idle codeword */
	if (cw == POCSAG_IDLE_CODEWORD) {
		if (dec->msg_active)
			deliver_message(dec);
		return;
	}

	if (!(cw & 0x80000000u)) {
		/* address codeword */
		if (dec->msg_active)
			deliver_message(dec);

		uint32_t addr_upper = (cw >> 13) & 0x3FFFFu;
		uint32_t func = (cw >> 11) & 0x3u;
		int frame = dec->cw_count / 2;

		dec->msg_active = 1;
		dec->msg_address = (addr_upper << 3) | (uint32_t)frame;
		dec->msg_function = (pocsag_func_t)func;
		dec->msg_data_count = 0;
	} else {
		/* message codeword */
		if (dec->msg_active &&
		    dec->msg_data_count < POCSAG_DEC_MAX_DATA) {
			uint32_t data = (cw >> 11) & 0xFFFFFu;
			dec->msg_data[dec->msg_data_count++] = data;
		}
	}
}

static void process_bit(pocsag_decoder_t *dec, int bit)
{
	switch (dec->state) {
	case POCSAG_DEC_HUNTING:
		dec->shift_reg = (dec->shift_reg << 1) | (uint32_t)(bit & 1);
		if (dec->shift_reg == POCSAG_SYNC_CODEWORD) {
			dec->state = POCSAG_DEC_BATCH;
			dec->cw_bits = 0;
			dec->cw_count = 0;
			dec->cw_accum = 0;
		}
		break;

	case POCSAG_DEC_BATCH:
		dec->cw_accum = (dec->cw_accum << 1) | (uint32_t)(bit & 1);
		dec->cw_bits++;
		if (dec->cw_bits == 32) {
			process_codeword(dec, dec->cw_accum);
			dec->cw_count++;
			dec->cw_bits = 0;
			dec->cw_accum = 0;
			if (dec->cw_count >= POCSAG_CODEWORDS_PER_BATCH) {
				dec->state = POCSAG_DEC_HUNTING;
				dec->shift_reg = 0;
			}
		}
		break;
	}
}

void pocsag_decoder_init(pocsag_decoder_t *dec,
                         pocsag_msg_cb_t callback, void *user)
{
	memset(dec, 0, sizeof(*dec));
	dec->callback = callback;
	dec->user = user;
}

void pocsag_decoder_reset(pocsag_decoder_t *dec)
{
	pocsag_msg_cb_t cb = dec->callback;
	void *user = dec->user;
	memset(dec, 0, sizeof(*dec));
	dec->callback = cb;
	dec->user = user;
}

pocsag_err_t pocsag_decoder_feed_bits(pocsag_decoder_t *dec,
                                      const uint8_t *bits, size_t count)
{
	if (!dec || !bits)
		return POCSAG_ERR_PARAM;

	for (size_t i = 0; i < count; i++)
		process_bit(dec, bits[i] & 1);

	return POCSAG_OK;
}

pocsag_err_t pocsag_decoder_feed_bytes(pocsag_decoder_t *dec,
                                       const uint8_t *data, size_t nbytes)
{
	if (!dec || !data)
		return POCSAG_ERR_PARAM;

	for (size_t i = 0; i < nbytes; i++) {
		uint8_t byte = data[i];
		for (int b = 7; b >= 0; b--)
			process_bit(dec, (byte >> b) & 1);
	}

	return POCSAG_OK;
}

void pocsag_decoder_flush(pocsag_decoder_t *dec)
{
	if (dec && dec->msg_active)
		deliver_message(dec);
}
