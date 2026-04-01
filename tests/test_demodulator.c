#include "test.h"
#include "libpocsag/modulator.h"
#include "libpocsag/demodulator.h"
#include "libpocsag/encoder.h"
#include "libpocsag/decoder.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- helpers ---------------------------------------------------- */

#define BIT_BUF_MAX 65536

typedef struct {
	uint8_t bits[BIT_BUF_MAX];
	size_t  count;
} bit_buf_t;

static void collect_bit(int bit, void *user)
{
	bit_buf_t *b = (bit_buf_t *)user;
	if (b->count < BIT_BUF_MAX)
		b->bits[b->count++] = (uint8_t)(bit & 1);
}

/* generate a pure tone at freq Hz for nbits worth of samples */
static size_t gen_tone(float *buf, size_t cap,
                       double freq, uint32_t sr, uint32_t baud,
                       size_t nbits)
{
	double spb = (double)sr / (double)baud;
	size_t n   = (size_t)(nbits * spb + 0.5);
	double omega = 2.0 * M_PI * freq / (double)sr;
	if (n > cap) n = cap;
	for (size_t i = 0; i < n; i++)
		buf[i] = (float)sin(omega * (double)i);
	return n;
}

/* ------------------------------------------------------------------ */
static void test_demod_init(void)
{
	pocsag_demod_t d;
	pocsag_demod_init(&d, 48000, 1200, collect_bit, NULL);
	ASSERT_EQ_INT((int)d.sample_rate, 48000);
	ASSERT_EQ_INT((int)d.baud_rate, 1200);
	ASSERT(d.callback == collect_bit);
	ASSERT(d.last_bit == -1);
	ASSERT_EQ_INT((int)d.stat_bits, 0);
}

/* ------------------------------------------------------------------ */
static void test_demod_constant_mark(void)
{
	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t d;
	pocsag_demod_init(&d, 48000, 1200, collect_bit, &bb);

	/* 20 bit-periods of pure mark tone → all 1s */
	float buf[1000];
	size_t n = gen_tone(buf, 1000,
	                    (double)POCSAG_FSK_MARK_HZ(1200), 48000, 1200, 20);

	ASSERT(pocsag_demodulate(&d, buf, n) == POCSAG_OK);
	ASSERT(bb.count == 20);

	for (size_t i = 0; i < bb.count; i++)
		ASSERT_EQ_INT(bb.bits[i], 1);
}

/* ------------------------------------------------------------------ */
static void test_demod_constant_space(void)
{
	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t d;
	pocsag_demod_init(&d, 48000, 1200, collect_bit, &bb);

	float buf[1000];
	size_t n = gen_tone(buf, 1000,
	                    (double)POCSAG_FSK_SPACE_HZ(1200), 48000, 1200, 20);

	ASSERT(pocsag_demodulate(&d, buf, n) == POCSAG_OK);
	ASSERT(bb.count == 20);

	for (size_t i = 0; i < bb.count; i++)
		ASSERT_EQ_INT(bb.bits[i], 0);
}

/* ------------------------------------------------------------------ */
static void test_demod_roundtrip_bits(void)
{
	/* Modulate a known bit pattern, demodulate, verify */
	uint8_t pattern[4] = { 0xA5, 0x3C, 0xF0, 0x0F };
	size_t nbits = 32;

	pocsag_mod_t mod;
	pocsag_mod_init(&mod, 48000, 1200);

	float audio[1500];
	size_t alen = 0;
	ASSERT(pocsag_modulate(&mod, pattern, nbits,
	                       audio, 1500, &alen) == POCSAG_OK);

	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t dem;
	pocsag_demod_init(&dem, 48000, 1200, collect_bit, &bb);
	ASSERT(pocsag_demodulate(&dem, audio, alen) == POCSAG_OK);

	ASSERT(bb.count == nbits);
	for (size_t i = 0; i < nbits; i++) {
		int expected = (pattern[i / 8] >> (7 - (i & 7))) & 1;
		ASSERT_EQ_INT(bb.bits[i], expected);
	}
}

/* ------------------------------------------------------------------ */

typedef struct {
	pocsag_msg_t msg;
	int          got;
} rx_ctx_t;

static void on_msg(const pocsag_msg_t *msg, void *user)
{
	rx_ctx_t *rx = (rx_ctx_t *)user;
	rx->msg = *msg;
	rx->got = 1;
}

static void test_demod_roundtrip_message(void)
{
	/* full pipeline: encode → modulate → demodulate → decode */
	uint8_t bitstream[POCSAG_BITSTREAM_MAX];
	size_t  bs_len = 0, bs_bits = 0;

	ASSERT(pocsag_encode_single(1234, POCSAG_FUNC_NUMERIC,
	                            POCSAG_MSG_NUMERIC, "5551234",
	                            bitstream, sizeof(bitstream),
	                            &bs_len, &bs_bits) == POCSAG_OK);

	/* modulate */
	pocsag_mod_t mod;
	pocsag_mod_init(&mod, 48000, 1200);

	size_t  need = pocsag_mod_samples_needed(&mod, bs_bits);
	/* use static buffer — max ~45k samples for a single message */
	static float audio[65536];
	size_t  alen = 0;

	ASSERT(need <= 65536);
	ASSERT(pocsag_modulate(&mod, bitstream, bs_bits,
	                       audio, 65536, &alen) == POCSAG_OK);

	/* demodulate → collect bits */
	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t dem;
	pocsag_demod_init(&dem, 48000, 1200, collect_bit, &bb);
	ASSERT(pocsag_demodulate(&dem, audio, alen) == POCSAG_OK);

	/* decode the recovered bits */
	rx_ctx_t rx;
	memset(&rx, 0, sizeof(rx));

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, on_msg, &rx);
	pocsag_decoder_feed_bits(&dec, bb.bits, bb.count);
	pocsag_decoder_flush(&dec);

	ASSERT(rx.got == 1);
	ASSERT_EQ_INT((int)rx.msg.address, 1234);
	ASSERT_EQ_INT((int)rx.msg.type, POCSAG_MSG_NUMERIC);
	ASSERT_STR_EQ(rx.msg.text, "5551234");
}

/* ------------------------------------------------------------------ */
static void test_demod_roundtrip_alpha(void)
{
	uint8_t bitstream[POCSAG_BITSTREAM_MAX];
	size_t  bs_len = 0, bs_bits = 0;

	ASSERT(pocsag_encode_single(2000, POCSAG_FUNC_ALPHA,
	                            POCSAG_MSG_ALPHA, "Hello POCSAG",
	                            bitstream, sizeof(bitstream),
	                            &bs_len, &bs_bits) == POCSAG_OK);

	pocsag_mod_t mod;
	pocsag_mod_init(&mod, 48000, 1200);

	static float audio[65536];
	size_t  alen = 0;

	ASSERT(pocsag_modulate(&mod, bitstream, bs_bits,
	                       audio, 65536, &alen) == POCSAG_OK);

	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t dem;
	pocsag_demod_init(&dem, 48000, 1200, collect_bit, &bb);
	ASSERT(pocsag_demodulate(&dem, audio, alen) == POCSAG_OK);

	rx_ctx_t rx;
	memset(&rx, 0, sizeof(rx));

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, on_msg, &rx);
	pocsag_decoder_feed_bits(&dec, bb.bits, bb.count);
	pocsag_decoder_flush(&dec);

	ASSERT(rx.got == 1);
	ASSERT_EQ_INT((int)rx.msg.address, 2000);
	ASSERT_EQ_INT((int)rx.msg.type, POCSAG_MSG_ALPHA);
	ASSERT_STR_EQ(rx.msg.text, "Hello POCSAG");
}

/* ------------------------------------------------------------------ */
static void test_demod_errors(void)
{
	float buf[40] = {0};
	ASSERT(pocsag_demodulate(NULL, buf, 40) == POCSAG_ERR_PARAM);

	pocsag_demod_t d;
	pocsag_demod_init(&d, 48000, 1200, collect_bit, NULL);
	ASSERT(pocsag_demodulate(&d, NULL, 40) == POCSAG_ERR_PARAM);

	/* invalid sample rate */
	pocsag_demod_init(&d, 44100, 1200, collect_bit, NULL);
	ASSERT(pocsag_demodulate(&d, buf, 40) == POCSAG_ERR_PARAM);

	/* invalid baud rate */
	pocsag_demod_init(&d, 48000, 300, collect_bit, NULL);
	ASSERT(pocsag_demodulate(&d, buf, 40) == POCSAG_ERR_PARAM);
}

/* ------------------------------------------------------------------ */
static void test_demod_reset(void)
{
	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t d;
	pocsag_demod_init(&d, 48000, 1200, collect_bit, &bb);

	float buf[400];
	size_t n = gen_tone(buf, 400,
	                    (double)POCSAG_FSK_MARK_HZ(1200), 48000, 1200, 10);
	pocsag_demodulate(&d, buf, n);
	ASSERT(d.stat_bits > 0);

	pocsag_demod_reset(&d);
	ASSERT_EQ_INT((int)d.stat_bits, 0);
	ASSERT_EQ_INT(d.last_bit, -1);
	ASSERT(d.callback == collect_bit);
}

/* ------------------------------------------------------------------ */
/* helper: full roundtrip at a given sample rate and baud rate */
static int roundtrip_at(uint32_t sr, uint32_t baud)
{
	uint8_t bitstream[POCSAG_BITSTREAM_MAX];
	size_t  bs_len = 0, bs_bits = 0;

	if (pocsag_encode_single(1234, POCSAG_FUNC_NUMERIC,
	                         POCSAG_MSG_NUMERIC, "5551234",
	                         bitstream, sizeof(bitstream),
	                         &bs_len, &bs_bits) != POCSAG_OK)
		return 0;

	pocsag_mod_t mod;
	pocsag_mod_init(&mod, sr, baud);

	size_t need = pocsag_mod_samples_needed(&mod, bs_bits);
	static float audio[524288];
	size_t alen = 0;
	if (need > sizeof(audio) / sizeof(audio[0]))
		return 0;
	if (pocsag_modulate(&mod, bitstream, bs_bits,
	                    audio, sizeof(audio) / sizeof(audio[0]),
	                    &alen) != POCSAG_OK)
		return 0;

	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t dem;
	pocsag_demod_init(&dem, sr, baud, collect_bit, &bb);
	if (pocsag_demodulate(&dem, audio, alen) != POCSAG_OK)
		return 0;

	rx_ctx_t rx;
	memset(&rx, 0, sizeof(rx));

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, on_msg, &rx);
	pocsag_decoder_feed_bits(&dec, bb.bits, bb.count);
	pocsag_decoder_flush(&dec);

	if (!rx.got) return 0;
	if (rx.msg.address != 1234) return 0;
	if (strcmp(rx.msg.text, "5551234") != 0) return 0;
	return 1;
}

static void test_demod_all_rates(void)
{
	static const uint32_t srates[] = { 8000, 16000, 32000, 48000 };
	static const uint32_t bauds[]  = { 512, 1200, 2400 };

	for (int s = 0; s < 4; s++) {
		for (int b = 0; b < 3; b++) {
			/* skip 8000/2400: only 3.3 samples per bit,
			 * below the minimum of 5 required by the
			 * correlator */
			if (srates[s] / bauds[b] < 5)
				continue;

			if (!roundtrip_at(srates[s], bauds[b])) {
				printf(" FAIL\n    sr=%u baud=%u\n",
				       (unsigned)srates[s], (unsigned)bauds[b]);
				test_fail++;
				return;
			}
		}
	}
}

static void test_demod_reject_low_spb(void)
{
	/* 8000/2400 = 3.33 spb, below the 5 spb minimum */
	float buf[40] = {0};
	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t d;
	pocsag_demod_init(&d, 8000, 2400, collect_bit, &bb);
	ASSERT(pocsag_demodulate(&d, buf, 40) == POCSAG_ERR_PARAM);

	/* 8000/1200 = 6.67 spb, should be accepted */
	pocsag_demod_init(&d, 8000, 1200, collect_bit, &bb);
	ASSERT(pocsag_demodulate(&d, buf, 40) == POCSAG_OK);
}

/* ------------------------------------------------------------------ */
static void test_demod_baseband_roundtrip(void)
{
	/* encode → pocsag_baseband → pocsag_demod_baseband → decode */
	uint8_t bitstream[POCSAG_BITSTREAM_MAX];
	size_t  bs_len = 0, bs_bits = 0;

	ASSERT(pocsag_encode_single(1234, POCSAG_FUNC_NUMERIC,
	                            POCSAG_MSG_NUMERIC, "5551234",
	                            bitstream, sizeof(bitstream),
	                            &bs_len, &bs_bits) == POCSAG_OK);

	/* generate baseband NRZ */
	static float audio[65536];
	size_t alen = 0;
	ASSERT(pocsag_baseband(bitstream, bs_bits, 48000, 1200,
	                       audio, 65536, &alen) == POCSAG_OK);

	/* demodulate baseband → collect bits */
	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t dem;
	pocsag_demod_init(&dem, 48000, 1200, collect_bit, &bb);
	ASSERT(pocsag_demod_baseband(&dem, audio, alen) == POCSAG_OK);

	/* decode */
	rx_ctx_t rx;
	memset(&rx, 0, sizeof(rx));

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, on_msg, &rx);
	pocsag_decoder_feed_bits(&dec, bb.bits, bb.count);
	pocsag_decoder_flush(&dec);

	ASSERT(rx.got == 1);
	ASSERT_EQ_INT((int)rx.msg.address, 1234);
	ASSERT_EQ_INT((int)rx.msg.type, POCSAG_MSG_NUMERIC);
	ASSERT_STR_EQ(rx.msg.text, "5551234");
}

/* ------------------------------------------------------------------ */
static void test_demod_baseband_alpha(void)
{
	uint8_t bitstream[POCSAG_BITSTREAM_MAX];
	size_t  bs_len = 0, bs_bits = 0;

	ASSERT(pocsag_encode_single(2000, POCSAG_FUNC_ALPHA,
	                            POCSAG_MSG_ALPHA, "Hello POCSAG",
	                            bitstream, sizeof(bitstream),
	                            &bs_len, &bs_bits) == POCSAG_OK);

	static float audio[65536];
	size_t alen = 0;
	ASSERT(pocsag_baseband(bitstream, bs_bits, 48000, 1200,
	                       audio, 65536, &alen) == POCSAG_OK);

	bit_buf_t bb;
	memset(&bb, 0, sizeof(bb));

	pocsag_demod_t dem;
	pocsag_demod_init(&dem, 48000, 1200, collect_bit, &bb);
	ASSERT(pocsag_demod_baseband(&dem, audio, alen) == POCSAG_OK);

	rx_ctx_t rx;
	memset(&rx, 0, sizeof(rx));

	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, on_msg, &rx);
	pocsag_decoder_feed_bits(&dec, bb.bits, bb.count);
	pocsag_decoder_flush(&dec);

	ASSERT(rx.got == 1);
	ASSERT_EQ_INT((int)rx.msg.address, 2000);
	ASSERT_STR_EQ(rx.msg.text, "Hello POCSAG");
}

/* ------------------------------------------------------------------ */
void test_demodulator(void)
{
	printf("demodulator\n");
	RUN_TEST(test_demod_init);
	RUN_TEST(test_demod_constant_mark);
	RUN_TEST(test_demod_constant_space);
	RUN_TEST(test_demod_roundtrip_bits);
	RUN_TEST(test_demod_roundtrip_message);
	RUN_TEST(test_demod_roundtrip_alpha);
	RUN_TEST(test_demod_errors);
	RUN_TEST(test_demod_reset);
	RUN_TEST(test_demod_all_rates);
	RUN_TEST(test_demod_reject_low_spb);
	RUN_TEST(test_demod_baseband_roundtrip);
	RUN_TEST(test_demod_baseband_alpha);
}
