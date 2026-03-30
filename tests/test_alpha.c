#include "test.h"
#include <libpocsag/types.h>

extern int pocsag_alpha_encode(const char *text, size_t len,
                               uint32_t *chunks, int max_chunks);
extern int pocsag_alpha_decode(const uint32_t *chunks, int nchunks,
                               char *text, size_t text_cap);

static void test_alpha_roundtrip_short(void)
{
	uint32_t chunks[8];
	char buf[64];

	int nc = pocsag_alpha_encode("Hi", 2, chunks, 8);
	ASSERT(nc > 0);

	int len = pocsag_alpha_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, "Hi");
}

static void test_alpha_roundtrip_long(void)
{
	uint32_t chunks[16];
	char buf[128];
	const char *msg = "Hello, World! This is POCSAG.";
	size_t mlen = strlen(msg);

	int nc = pocsag_alpha_encode(msg, mlen, chunks, 16);
	ASSERT(nc > 0);

	int len = pocsag_alpha_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, msg);
}

static void test_alpha_special(void)
{
	uint32_t chunks[8];
	char buf[64];
	const char *msg = "Test@#$%";
	size_t mlen = strlen(msg);

	int nc = pocsag_alpha_encode(msg, mlen, chunks, 8);
	ASSERT(nc > 0);

	int len = pocsag_alpha_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, msg);
}

static void test_alpha_single_char(void)
{
	uint32_t chunks[4];
	char buf[16];

	int nc = pocsag_alpha_encode("A", 1, chunks, 4);
	ASSERT(nc > 0);

	int len = pocsag_alpha_decode(chunks, nc, buf, sizeof(buf));
	ASSERT(len > 0);
	ASSERT_STR_EQ(buf, "A");
}

static void test_alpha_chunk_count(void)
{
	uint32_t chunks[16];

	/* "Hi" = 2 chars + EOT = 3 chars * 7 bits = 21 bits -> 2 chunks */
	int nc = pocsag_alpha_encode("Hi", 2, chunks, 16);
	ASSERT_EQ_INT(nc, 2);
}

void test_alpha(void)
{
	printf("Alpha:\n");
	RUN_TEST(test_alpha_roundtrip_short);
	RUN_TEST(test_alpha_roundtrip_long);
	RUN_TEST(test_alpha_special);
	RUN_TEST(test_alpha_single_char);
	RUN_TEST(test_alpha_chunk_count);
	printf("\n");
}
