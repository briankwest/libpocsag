#include "test.h"
#include <libpocsag/bch.h>
#include <libpocsag/types.h>

static void test_bch_sync_valid(void)
{
	ASSERT_EQ_U32(pocsag_bch_syndrome(POCSAG_SYNC_CODEWORD), 0);
	ASSERT(pocsag_parity_check(POCSAG_SYNC_CODEWORD));
}

static void test_bch_idle_valid(void)
{
	ASSERT_EQ_U32(pocsag_bch_syndrome(POCSAG_IDLE_CODEWORD), 0);
	ASSERT(pocsag_parity_check(POCSAG_IDLE_CODEWORD));
}

static void test_bch_build_verify(void)
{
	/* build a codeword and verify it passes checks */
	uint32_t data21 = 0x0A5A5A;
	uint32_t cw = pocsag_codeword_build(data21);
	ASSERT_EQ_U32(pocsag_bch_syndrome(cw), 0);
	ASSERT(pocsag_parity_check(cw));
}

static void test_bch_correct_single(void)
{
	uint32_t cw = pocsag_codeword_build(0x123456 & 0x1FFFFF);
	/* flip one bit */
	uint32_t bad = cw ^ (1u << 15);
	ASSERT(pocsag_bch_syndrome(bad) != 0);
	ASSERT_EQ_INT(pocsag_bch_correct(&bad), 0);
	ASSERT_EQ_U32(bad, cw);
}

static void test_bch_correct_parity_bit(void)
{
	uint32_t cw = pocsag_codeword_build(0x0ABCDE & 0x1FFFFF);
	/* flip parity bit (bit 0) */
	uint32_t bad = cw ^ 1u;
	ASSERT_EQ_INT(pocsag_bch_correct(&bad), 0);
	ASSERT_EQ_U32(bad, cw);
}

static void test_bch_detect_double(void)
{
	uint32_t cw = pocsag_codeword_build(0x055555);
	/* flip two bits */
	uint32_t bad = cw ^ (1u << 20) ^ (1u << 10);
	ASSERT_EQ_INT(pocsag_bch_correct(&bad), -1);
}

static void test_bch_encode_zero(void)
{
	/* data21=0 should produce BCH=0 */
	ASSERT_EQ_U32(pocsag_bch_encode(0), 0);
}

static void test_bch_all_bits_correctable(void)
{
	uint32_t cw = pocsag_codeword_build(0x1FFFFF);
	for (int i = 0; i < 32; i++) {
		uint32_t bad = cw ^ (1u << i);
		ASSERT_EQ_INT(pocsag_bch_correct(&bad), 0);
		ASSERT_EQ_U32(bad, cw);
	}
}

void test_bch(void)
{
	printf("BCH:\n");
	RUN_TEST(test_bch_sync_valid);
	RUN_TEST(test_bch_idle_valid);
	RUN_TEST(test_bch_build_verify);
	RUN_TEST(test_bch_correct_single);
	RUN_TEST(test_bch_correct_parity_bit);
	RUN_TEST(test_bch_detect_double);
	RUN_TEST(test_bch_encode_zero);
	RUN_TEST(test_bch_all_bits_correctable);
	printf("\n");
}
