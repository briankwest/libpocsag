#include "test.h"
#include <libpocsag/encoder.h>
#include <libpocsag/bch.h>
#include <libpocsag/types.h>

static void test_enc_single_numeric(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_err_t err = pocsag_encode_single(
		1234000, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
		"5551234", buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, POCSAG_OK);
	ASSERT(bits > POCSAG_PREAMBLE_BITS);
	ASSERT(len > 0);
}

static void test_enc_single_alpha(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_err_t err = pocsag_encode_single(
		100, POCSAG_FUNC_ALPHA, POCSAG_MSG_ALPHA,
		"Test message", buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, POCSAG_OK);
	ASSERT(bits > 0);
}

static void test_enc_tone_only(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_err_t err = pocsag_encode_single(
		200, POCSAG_FUNC_TONE1, POCSAG_MSG_TONE_ONLY,
		NULL, buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, POCSAG_OK);
	/* preamble(576) + sync(32) + 16 codewords(512) = 1120 bits */
	ASSERT_EQ_INT((int)bits, 1120);
}

static void test_enc_preamble(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(500, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
	                     "123", buf, sizeof(buf), &len, &bits);

	/* check first 72 bytes are 0xAA (preamble) */
	for (int i = 0; i < 72; i++)
		ASSERT_EQ_U32(buf[i], 0xAA);
}

static void test_enc_sync_present(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(0, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
	                     "0", buf, sizeof(buf), &len, &bits);

	/* sync codeword starts at bit 576 (byte 72) */
	uint32_t sync = ((uint32_t)buf[72] << 24) |
	                ((uint32_t)buf[73] << 16) |
	                ((uint32_t)buf[74] << 8)  |
	                ((uint32_t)buf[75]);
	ASSERT_EQ_U32(sync, POCSAG_SYNC_CODEWORD);
}

static void test_enc_all_codewords_valid(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_encode_single(8, POCSAG_FUNC_ALPHA, POCSAG_MSG_ALPHA,
	                     "Hello", buf, sizeof(buf), &len, &bits);

	/* skip preamble (72 bytes), skip sync (4 bytes) */
	int offset = 76; /* byte offset of first data codeword */
	for (int i = 0; i < POCSAG_CODEWORDS_PER_BATCH; i++) {
		int pos = offset + i * 4;
		uint32_t cw = ((uint32_t)buf[pos] << 24) |
		              ((uint32_t)buf[pos+1] << 16) |
		              ((uint32_t)buf[pos+2] << 8)  |
		              ((uint32_t)buf[pos+3]);
		ASSERT_EQ_U32(pocsag_bch_syndrome(cw), 0);
		ASSERT(pocsag_parity_check(cw));
	}
}

static void test_enc_multi_message(void)
{
	pocsag_encoder_t enc;
	pocsag_encoder_init(&enc);

	pocsag_encoder_add(&enc, 100, POCSAG_FUNC_NUMERIC,
	                   POCSAG_MSG_NUMERIC, "111");
	pocsag_encoder_add(&enc, 200, POCSAG_FUNC_NUMERIC,
	                   POCSAG_MSG_NUMERIC, "222");

	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_err_t err = pocsag_encode(&enc, buf, sizeof(buf), &len, &bits);
	ASSERT_EQ_INT(err, POCSAG_OK);
	ASSERT(bits > 0);
}

static void test_enc_bad_address(void)
{
	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_err_t err = pocsag_encode_single(
		0x200000, POCSAG_FUNC_NUMERIC, POCSAG_MSG_NUMERIC,
		"123", buf, sizeof(buf), &len, &bits);

	ASSERT_EQ_INT(err, POCSAG_ERR_PARAM);
}

void test_encoder(void)
{
	printf("Encoder:\n");
	RUN_TEST(test_enc_single_numeric);
	RUN_TEST(test_enc_single_alpha);
	RUN_TEST(test_enc_tone_only);
	RUN_TEST(test_enc_preamble);
	RUN_TEST(test_enc_sync_present);
	RUN_TEST(test_enc_all_codewords_valid);
	RUN_TEST(test_enc_multi_message);
	RUN_TEST(test_enc_bad_address);
	printf("\n");
}
