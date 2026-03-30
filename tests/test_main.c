#include <stdio.h>

int test_pass = 0;
int test_fail = 0;

extern void test_bch(void);
extern void test_codeword(void);
extern void test_numeric(void);
extern void test_alpha(void);
extern void test_encoder(void);
extern void test_decoder(void);
extern void test_roundtrip(void);

int main(void)
{
	printf("libpocsag test suite\n\n");

	test_bch();
	test_codeword();
	test_numeric();
	test_alpha();
	test_encoder();
	test_decoder();
	test_roundtrip();

	printf("\n%d passed, %d failed\n", test_pass, test_fail);
	return test_fail > 0 ? 1 : 0;
}
