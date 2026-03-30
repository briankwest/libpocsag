#include "test.h"
#include <libpocsag/bch.h>
#include <libpocsag/types.h>

/* these are defined in codeword.c */
extern uint32_t pocsag_cw_address(uint32_t address, unsigned int func);
extern uint32_t pocsag_cw_message(uint32_t data20);

static void test_cw_address_valid_bch(void)
{
	uint32_t cw = pocsag_cw_address(1234567, 0);
	ASSERT_EQ_U32(pocsag_bch_syndrome(cw), 0);
	ASSERT(pocsag_parity_check(cw));
}

static void test_cw_address_type_bit(void)
{
	uint32_t cw = pocsag_cw_address(100, 0);
	/* bit 31 should be 0 for address */
	ASSERT(!(cw & 0x80000000u));
}

static void test_cw_address_fields(void)
{
	uint32_t addr = 1000; /* frame = 1000 % 8 = 0 */
	uint32_t func = 3;
	uint32_t cw = pocsag_cw_address(addr, func);

	/* extract address upper 18 bits */
	uint32_t addr_upper = (cw >> 13) & 0x3FFFFu;
	ASSERT_EQ_U32(addr_upper, addr >> 3);

	/* extract function */
	uint32_t f = (cw >> 11) & 0x3u;
	ASSERT_EQ_U32(f, func);
}

static void test_cw_message_type_bit(void)
{
	uint32_t cw = pocsag_cw_message(0x12345);
	/* bit 31 should be 1 for message */
	ASSERT(cw & 0x80000000u);
}

static void test_cw_message_valid_bch(void)
{
	uint32_t cw = pocsag_cw_message(0xABCDE);
	ASSERT_EQ_U32(pocsag_bch_syndrome(cw), 0);
	ASSERT(pocsag_parity_check(cw));
}

static void test_cw_message_data(void)
{
	uint32_t data = 0xFFFFF;
	uint32_t cw = pocsag_cw_message(data);
	uint32_t extracted = (cw >> 11) & 0xFFFFFu;
	ASSERT_EQ_U32(extracted, data);
}

static void test_cw_frame_assignment(void)
{
	/* address 13: frame should be 13 % 8 = 5 */
	/* the address codeword's position in the batch determines the frame,
	   but the upper 18 bits store address >> 3 */
	uint32_t cw = pocsag_cw_address(13, 0);
	uint32_t addr_upper = (cw >> 13) & 0x3FFFFu;
	ASSERT_EQ_U32(addr_upper, 13 >> 3);
	/* frame 5 = 13 % 8, address is reconstructed by decoder as
	   (addr_upper << 3) | frame */
}

void test_codeword(void)
{
	printf("Codeword:\n");
	RUN_TEST(test_cw_address_valid_bch);
	RUN_TEST(test_cw_address_type_bit);
	RUN_TEST(test_cw_address_fields);
	RUN_TEST(test_cw_message_type_bit);
	RUN_TEST(test_cw_message_valid_bch);
	RUN_TEST(test_cw_message_data);
	RUN_TEST(test_cw_frame_assignment);
	printf("\n");
}
