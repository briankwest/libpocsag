#include "test.h"
#include <libpocsag/types.h>

/* internal functions from numeric.c */
extern int pocsag_numeric_encode(const char *text, size_t len,
                                 uint32_t *chunks, int max_chunks);
extern int pocsag_numeric_decode(const uint32_t *chunks, int nchunks,
                                 char *text, size_t text_cap);

static void test_numeric_roundtrip_short(void)
{
	uint32_t chunks[8];
	char buf[64];

	int nc = pocsag_numeric_encode("12345", 5, chunks, 8);
	ASSERT_EQ_INT(nc, 1);

	int len = pocsag_numeric_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, "12345");
}

static void test_numeric_roundtrip_long(void)
{
	uint32_t chunks[8];
	char buf[64];

	int nc = pocsag_numeric_encode("1234567890", 10, chunks, 8);
	ASSERT_EQ_INT(nc, 2);

	int len = pocsag_numeric_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, "1234567890");
}

static void test_numeric_special_chars(void)
{
	uint32_t chunks[8];
	char buf[64];

	int nc = pocsag_numeric_encode("*U -[]", 6, chunks, 8);
	ASSERT(nc > 0);

	int len = pocsag_numeric_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, "*U -[]");
}

static void test_numeric_padding(void)
{
	uint32_t chunks[8];
	char buf[64];

	/* 3 digits: should pad with spaces to fill 5-digit chunk */
	int nc = pocsag_numeric_encode("123", 3, chunks, 8);
	ASSERT_EQ_INT(nc, 1);

	int len = pocsag_numeric_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	/* trailing spaces should be trimmed */
	ASSERT_STR_EQ(buf, "123");
}

static void test_numeric_bad_char(void)
{
	uint32_t chunks[8];
	int nc = pocsag_numeric_encode("12A34", 5, chunks, 8);
	ASSERT_EQ_INT(nc, -1);
}

static void test_numeric_empty(void)
{
	uint32_t chunks[8];
	int nc = pocsag_numeric_encode("", 0, chunks, 8);
	ASSERT_EQ_INT(nc, 0);
}

void test_numeric(void)
{
	printf("Numeric:\n");
	RUN_TEST(test_numeric_roundtrip_short);
	RUN_TEST(test_numeric_roundtrip_long);
	RUN_TEST(test_numeric_special_chars);
	RUN_TEST(test_numeric_padding);
	RUN_TEST(test_numeric_bad_char);
	RUN_TEST(test_numeric_empty);
	printf("\n");
}
