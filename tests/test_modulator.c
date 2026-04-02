#include "test.h"
#include "libpocsag/modulator.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
static void test_mod_init(void)
{
	pocsag_mod_t m;
	pocsag_mod_init(&m, 48000, 1200);
	ASSERT_EQ_INT((int)m.sample_rate, 48000);
	ASSERT_EQ_INT((int)m.baud_rate, 1200);
	ASSERT(m.mark_freq  == POCSAG_FSK_MARK_HZ(1200));
	ASSERT(m.space_freq == POCSAG_FSK_SPACE_HZ(1200));
	ASSERT(m.phase == 0.0);
}

/* ------------------------------------------------------------------ */
static void test_mod_init_custom(void)
{
	pocsag_mod_t m;
	pocsag_mod_init_custom(&m, 16000, 512, 1500.0f, 2500.0f);
	ASSERT_EQ_INT((int)m.sample_rate, 16000);
	ASSERT_EQ_INT((int)m.baud_rate, 512);
	ASSERT(m.mark_freq  == 1500.0f);
	ASSERT(m.space_freq == 2500.0f);
}

/* ------------------------------------------------------------------ */
static void test_mod_samples_needed(void)
{
	pocsag_mod_t m;
	size_t leadin_48k = 48000 * POCSAG_LEADIN_MS / 1000;

	/* 48000 / 1200 = 40 samples per bit + lead-in */
	pocsag_mod_init(&m, 48000, 1200);
	ASSERT(pocsag_mod_samples_needed(&m, 1)   == 40 + leadin_48k);
	ASSERT(pocsag_mod_samples_needed(&m, 10)  == 400 + leadin_48k);
	ASSERT(pocsag_mod_samples_needed(&m, 100) == 4000 + leadin_48k);

	/* 48000 / 512 = 93.75 spb — ceiling applies */
	pocsag_mod_init(&m, 48000, 512);
	ASSERT(pocsag_mod_samples_needed(&m, 1) == 94 + leadin_48k);
	ASSERT(pocsag_mod_samples_needed(&m, 4) == 375 + leadin_48k);
}

/* ------------------------------------------------------------------ */
static void test_mod_output_count(void)
{
	pocsag_mod_t m;
	pocsag_mod_init(&m, 48000, 1200);

	uint8_t bits[2] = { 0xA5, 0x00 };
	size_t leadin = 48000 * POCSAG_LEADIN_MS / 1000;
	static float buf[25000];
	size_t  len = 0;

	/* modulate 8 bits → 320 + lead-in samples */
	ASSERT(pocsag_modulate(&m, bits, 8, buf, sizeof(buf)/sizeof(buf[0]), &len) == POCSAG_OK);
	ASSERT(len == 320 + leadin);

	/* modulate 16 bits → 640 + lead-in samples */
	pocsag_mod_reset(&m);
	ASSERT(pocsag_modulate(&m, bits, 16, buf, sizeof(buf)/sizeof(buf[0]), &len) == POCSAG_OK);
	ASSERT(len == 640 + leadin);
}

/* ------------------------------------------------------------------ */
static void test_mod_amplitude(void)
{
	pocsag_mod_t m;
	pocsag_mod_init(&m, 48000, 1200);

	/* preamble byte: 10101010 */
	uint8_t bits[1] = { 0xAA };
	static float buf[25000];
	size_t  len = 0;

	ASSERT(pocsag_modulate(&m, bits, 8, buf, sizeof(buf)/sizeof(buf[0]), &len) == POCSAG_OK);
	for (size_t i = 0; i < len; i++) {
		ASSERT(buf[i] >= -1.0f && buf[i] <= 1.0f);
	}
}

/* ------------------------------------------------------------------ */
static void test_mod_phase_continuity(void)
{
	pocsag_mod_t m;
	pocsag_mod_init(&m, 48000, 1200);

	/* Modulate alternating 10101010 and check that no two adjacent
	 * samples differ by more than what one sample step of the higher
	 * frequency could produce.  max_delta = 2*sin(pi*f/fs). */
	uint8_t bits[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
	static float buf[26000];
	size_t  len = 0;

	ASSERT(pocsag_modulate(&m, bits, 32, buf, sizeof(buf)/sizeof(buf[0]), &len) == POCSAG_OK);

	double max_omega = 2.0 * M_PI * (double)POCSAG_FSK_SPACE_HZ(1200)
	                   / 48000.0;
	float  max_delta = (float)(2.0 * sin(max_omega / 2.0)) + 0.01f;

	for (size_t i = 1; i < len; i++) {
		float d = buf[i] - buf[i - 1];
		if (d < 0) d = -d;
		ASSERT(d <= max_delta);
	}
}

/* ------------------------------------------------------------------ */
static void test_mod_errors(void)
{
	pocsag_mod_t m;
	pocsag_mod_init(&m, 48000, 1200);

	uint8_t bits[1] = { 0xFF };
	float   buf[40];
	size_t  len = 0;

	/* NULL pointers */
	ASSERT(pocsag_modulate(NULL, bits, 8, buf, 40, &len) == POCSAG_ERR_PARAM);
	ASSERT(pocsag_modulate(&m, NULL, 8, buf, 40, &len) == POCSAG_ERR_PARAM);
	ASSERT(pocsag_modulate(&m, bits, 8, NULL, 40, &len) == POCSAG_ERR_PARAM);
	ASSERT(pocsag_modulate(&m, bits, 8, buf, 40, NULL) == POCSAG_ERR_PARAM);

	/* buffer too small */
	ASSERT(pocsag_modulate(&m, bits, 8, buf, 10, &len) == POCSAG_ERR_OVERFLOW);

	/* invalid sample rate */
	pocsag_mod_init(&m, 44100, 1200);
	ASSERT(pocsag_modulate(&m, bits, 8, buf, 40, &len) == POCSAG_ERR_PARAM);

	/* invalid baud rate */
	pocsag_mod_init(&m, 48000, 9600);
	ASSERT(pocsag_modulate(&m, bits, 8, buf, 40, &len) == POCSAG_ERR_PARAM);
}

/* ------------------------------------------------------------------ */
void test_modulator(void)
{
	printf("modulator\n");
	RUN_TEST(test_mod_init);
	RUN_TEST(test_mod_init_custom);
	RUN_TEST(test_mod_samples_needed);
	RUN_TEST(test_mod_output_count);
	RUN_TEST(test_mod_amplitude);
	RUN_TEST(test_mod_phase_continuity);
	RUN_TEST(test_mod_errors);
}
