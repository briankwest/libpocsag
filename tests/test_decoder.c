#include "test.h"
#include <stdlib.h>
#include <libpocsag/decoder.h>
#include <libpocsag/encoder.h>
#include <libpocsag/types.h>

static pocsag_msg_t last_msg;
static int msg_count;

static void cb(const pocsag_msg_t *msg, void *user)
{
	(void)user;
	memcpy(&last_msg, msg, sizeof(last_msg));
	msg_count++;
}

static void test_dec_basic_numeric(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(1000, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
	                     "12345", buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, cb, NULL);
	msg_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(msg_count, 1);
	ASSERT_EQ_U32(last_msg.address, 1000);
	ASSERT_EQ_INT(last_msg.function, POCSAG_FUNC_NUMERIC);
	ASSERT_STR_EQ(last_msg.text, "12345");
}

static void test_dec_basic_alpha(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(2000, POCSAG_FUNC_ALPHA, POCSAG_MSG_ALPHA,
	                     "Hello", buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, cb, NULL);
	msg_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(msg_count, 1);
	ASSERT_EQ_U32(last_msg.address, 2000);
	ASSERT_EQ_INT(last_msg.function, POCSAG_FUNC_ALPHA);
	ASSERT_STR_EQ(last_msg.text, "Hello");
}

static void test_dec_tone_only(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(500, POCSAG_FUNC_TONE1, POCSAG_MSG_TONE_ONLY,
	                     NULL, buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, cb, NULL);
	msg_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(msg_count, 1);
	ASSERT_EQ_U32(last_msg.address, 500);
	ASSERT_EQ_INT(last_msg.type, POCSAG_MSG_TONE_ONLY);
}

static void test_dec_feed_bits(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(100, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
	                     "999", buf, sizeof(buf), &len, &bits);

	/* convert to bit array */
	uint8_t *bit_buf = (uint8_t *)malloc(bits);
	ASSERT(bit_buf != NULL);

	for (size_t i = 0; i < bits; i++) {
		size_t byte_idx = i / 8;
		int bit_idx = 7 - (int)(i % 8);
		bit_buf[i] = (buf[byte_idx] >> bit_idx) & 1;
	}

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, cb, NULL);
	msg_count = 0;

	pocsag_decoder_feed_bits(&dec, bit_buf, bits);
	pocsag_decoder_flush(&dec);

	free(bit_buf);

	ASSERT_EQ_INT(msg_count, 1);
	ASSERT_EQ_U32(last_msg.address, 100);
	ASSERT_STR_EQ(last_msg.text, "999");
}

static void test_dec_stats(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(0, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
	                     "0", buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, cb, NULL);
	msg_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT(dec.stat_codewords == 16);
	ASSERT(dec.stat_errors == 0);
}

static void test_dec_reset(void)
{
	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, cb, NULL);

	dec.shift_reg = 0xDEADBEEF;
	dec.state = POCSAG_DEC_BATCH;

	pocsag_decoder_reset(&dec);

	ASSERT_EQ_INT(dec.state, POCSAG_DEC_HUNTING);
	ASSERT_EQ_U32(dec.shift_reg, 0);
	ASSERT(dec.callback == cb);
}

void test_decoder(void)
{
	printf("Decoder:\n");
	RUN_TEST(test_dec_basic_numeric);
	RUN_TEST(test_dec_basic_alpha);
	RUN_TEST(test_dec_tone_only);
	RUN_TEST(test_dec_feed_bits);
	RUN_TEST(test_dec_stats);
	RUN_TEST(test_dec_reset);
	printf("\n");
}
