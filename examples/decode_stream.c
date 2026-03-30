#include <stdio.h>
#include <libpocsag/pocsag.h>

static void on_message(const pocsag_msg_t *msg, void *user)
{
	(void)user;

	const char *type;
	switch (msg->type) {
	case POCSAG_MSG_NUMERIC:   type = "NUM";  break;
	case POCSAG_MSG_ALPHA:     type = "ALPHA"; break;
	case POCSAG_MSG_TONE_ONLY: type = "TONE"; break;
	default:                   type = "???";  break;
	}

	printf("[%s] addr=%u func=%d", type, (unsigned)msg->address,
	       (int)msg->function);

	if (msg->text_len > 0)
		printf(" msg=\"%s\"", msg->text);

	printf("\n");
}

int main(void)
{
	pocsag_decoder_t dec;
	pocsag_decoder_init(&dec, on_message, NULL);

	uint8_t buf[4096];
	size_t n;

	while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0)
		pocsag_decoder_feed_bytes(&dec, buf, n);

	pocsag_decoder_flush(&dec);

	fprintf(stderr, "codewords=%u corrected=%u errors=%u messages=%u\n",
	        (unsigned)dec.stat_codewords,
	        (unsigned)dec.stat_corrected,
	        (unsigned)dec.stat_errors,
	        (unsigned)dec.stat_messages);

	return 0;
}
