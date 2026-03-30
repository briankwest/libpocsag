#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpocsag/pocsag.h>

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s <address> <n|a|t> [message]\n", prog);
	fprintf(stderr, "  address: 0-2097151\n");
	fprintf(stderr, "  n = numeric, a = alphanumeric, t = tone-only\n");
	fprintf(stderr, "Output: raw POCSAG bitstream on stdout\n");
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	uint32_t address = (uint32_t)strtoul(argv[1], NULL, 10);
	char type_ch = argv[2][0];

	pocsag_func_t func;
	pocsag_msg_type_t type;
	const char *text = argc > 3 ? argv[3] : NULL;

	switch (type_ch) {
	case 'n':
		func = POCSAG_FUNC_NUMERIC;
		type = POCSAG_MSG_NUMERIC;
		break;
	case 'a':
		func = POCSAG_FUNC_ALPHA;
		type = POCSAG_MSG_ALPHA;
		break;
	case 't':
		func = POCSAG_FUNC_TONE1;
		type = POCSAG_MSG_TONE_ONLY;
		text = NULL;
		break;
	default:
		usage(argv[0]);
		return 1;
	}

	uint8_t buf[POCSAG_BITSTREAM_MAX];
	size_t len, bits;

	pocsag_err_t err = pocsag_encode_single(address, func, type, text,
	                                        buf, sizeof(buf), &len, &bits);
	if (err != POCSAG_OK) {
		fprintf(stderr, "encode error: %s\n", pocsag_strerror(err));
		return 1;
	}

	fwrite(buf, 1, len, stdout);
	fprintf(stderr, "encoded %zu bits (%zu bytes)\n", bits, len);

	return 0;
}
