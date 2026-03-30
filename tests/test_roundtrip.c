#include "test.h"
#include <libpocsag/encoder.h>
#include <libpocsag/decoder.h>
#include <libpocsag/types.h>
#include <stdlib.h>

static pocsag_msg_t rt_msgs[16];
static int rt_count;

static void rt_cb(const pocsag_msg_t *msg, void *user)
{
	(void)user;
	if (rt_count < 16)
		memcpy(&rt_msgs[rt_count], msg, sizeof(pocsag_msg_t));
	rt_count++;
}

static void test_rt_numeric(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(12345, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
	                     "9876543210", buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, rt_cb, NULL);
	rt_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(rt_count, 1);
	ASSERT_EQ_U32(rt_msgs[0].address, 12345);
	ASSERT_EQ_INT(rt_msgs[0].function, POCSAG_FUNC_NUMERIC);
	ASSERT_STR_EQ(rt_msgs[0].text, "9876543210");
}

static void test_rt_alpha(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;
	const char *msg = "The quick brown fox jumps over the lazy dog";

	pocsag_encode_single(2097151, POCSAG_FUNC_ALPHA, POCSAG_MSG_ALPHA,
	                     msg, buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, rt_cb, NULL);
	rt_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(rt_count, 1);
	ASSERT_EQ_U32(rt_msgs[0].address, 2097151);
	ASSERT_STR_EQ(rt_msgs[0].text, msg);
}

static void test_rt_tone(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(42, POCSAG_FUNC_TONE2, POCSAG_MSG_TONE_ONLY,
	                     NULL, buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, rt_cb, NULL);
	rt_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(rt_count, 1);
	ASSERT_EQ_U32(rt_msgs[0].address, 42);
	ASSERT_EQ_INT(rt_msgs[0].type, POCSAG_MSG_TONE_ONLY);
}

static void test_rt_multi_message(void)
{
	pocsag_encoder_t enc;
	pocsag_encoder_init(&enc);

	/* two messages in different frames */
	pocsag_encoder_add(&enc, 100, POCSAG_FUNC_NUMERIC,
	                   POCSAG_MSG_NUMERIC, "111");
	pocsag_encoder_add(&enc, 205, POCSAG_FUNC_ALPHA,
	                   POCSAG_MSG_ALPHA, "Hello");

	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_err_t err = pocsag_encode(&enc, buf, sizeof(buf), &len, &bits);
	ASSERT_EQ_INT(err, POCSAG_OK);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, rt_cb, NULL);
	rt_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(rt_count, 2);

	/* find each message (order may vary) */
	int found100 = 0, found205 = 0;
	for (int i = 0; i < 2; i++) {
		if (rt_msgs[i].address == 100) {
			ASSERT_STR_EQ(rt_msgs[i].text, "111");
			found100 = 1;
		} else if (rt_msgs[i].address == 205) {
			ASSERT_STR_EQ(rt_msgs[i].text, "Hello");
			found205 = 1;
		}
	}
	ASSERT(found100 && found205);
}

static void test_rt_max_address(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(0x1FFFFF, POCSAG_FUNC_NUMERIC,
	                     POCSAG_MSG_NUMERIC, "42",
	                     buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, rt_cb, NULL);
	rt_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(rt_count, 1);
	ASSERT_EQ_U32(rt_msgs[0].address, 0x1FFFFF);
	ASSERT_STR_EQ(rt_msgs[0].text, "42");
}

static void test_rt_address_zero(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(0, POCSAG_FUNC_ALPHA, POCSAG_MSG_ALPHA,
	                     "zero", buf, sizeof(buf), &len, &bits);

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, rt_cb, NULL);
	rt_count = 0;

	pocsag_decoder_feed_bytes(&dec, buf, len);
	pocsag_decoder_flush(&dec);

	ASSERT_EQ_INT(rt_count, 1);
	ASSERT_EQ_U32(rt_msgs[0].address, 0);
	ASSERT_STR_EQ(rt_msgs[0].text, "zero");
}

void test_roundtrip(void)
{
	printf("Roundtrip:\n");
	RUN_TEST(test_rt_numeric);
	RUN_TEST(test_rt_alpha);
	RUN_TEST(test_rt_tone);
	RUN_TEST(test_rt_multi_message);
	RUN_TEST(test_rt_max_address);
	RUN_TEST(test_rt_address_zero);
	printf("\n");
}
