/*
 * modem_demo - Demonstrate the full POCSAG modem pipeline:
 *   encode → modulate → demodulate → decode
 *
 * Usage: ./modem_demo [baud_rate]
 *   baud_rate: 512, 1200 (default), or 2400
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpocsag/pocsag.h>

#define SAMPLE_RATE 48000
#define MAX_AUDIO   (256 * 1024)

static float audio_buf[MAX_AUDIO];

/* ---- demodulator → decoder bridge ------------------------------- */

typedef struct {
	pocsag_decoder_t decoder;
	int              msg_count;
} rx_state_t;

static void on_message(const pocsag_msg_t *msg, void *user)
{
	rx_state_t *rx = (rx_state_t *)user;
	rx->msg_count++;

	const char *type_str = "unknown";
	switch (msg->type) {
	case POCSAG_MSG_NUMERIC:   type_str = "numeric";   break;
	case POCSAG_MSG_ALPHA:     type_str = "alpha";     break;
	case POCSAG_MSG_TONE_ONLY: type_str = "tone-only"; break;
	}

	printf("  [RX] addr=%-7u func=%d type=%-9s msg=\"%s\"\n",
	       (unsigned)msg->address, msg->function, type_str, msg->text);
}

static void on_bit(int bit, void *user)
{
	rx_state_t *rx = (rx_state_t *)user;
	uint8_t b = (uint8_t)(bit & 1);
	pocsag_decoder_feed_bits(&rx->decoder, &b, 1);
}

/* ----------------------------------------------------------------- */

int main(int argc, char **argv)
{
	uint32_t baud = POCSAG_BAUD_1200;
	if (argc > 1)
		baud = (uint32_t)atoi(argv[1]);

	printf("POCSAG modem demo  (sample_rate=%u  baud=%u)\n\n",
	       SAMPLE_RATE, (unsigned)baud);

	/* --- encode -------------------------------------------------- */
	pocsag_encoder_t enc;
	pocsag_encoder_init(&enc);

	pocsag_encoder_add(&enc, 1234, POCSAG_FUNC_NUMERIC,
	                   POCSAG_MSG_NUMERIC, "5551234");
	pocsag_encoder_add(&enc, 56789, POCSAG_FUNC_ALPHA,
	                   POCSAG_MSG_ALPHA, "Hello from libpocsag!");
	pocsag_encoder_add(&enc, 100, POCSAG_FUNC_TONE1,
	                   POCSAG_MSG_TONE_ONLY, NULL);

	uint8_t bitstream[POCSAG_BITSTREAM_MAX];
	size_t  bs_len = 0, bs_bits = 0;

	pocsag_err_t err = pocsag_encode(&enc, bitstream, sizeof(bitstream),
	                                 &bs_len, &bs_bits);
	if (err != POCSAG_OK) {
		fprintf(stderr, "encode: %s\n", pocsag_strerror(err));
		return 1;
	}
	printf("[TX] encoded %zu bits (%zu bytes)\n", bs_bits, bs_len);

	/* --- modulate ------------------------------------------------ */
	pocsag_mod_t mod;
	pocsag_mod_init(&mod, SAMPLE_RATE, baud);

	size_t alen = 0;
	err = pocsag_modulate(&mod, bitstream, bs_bits,
	                      audio_buf, MAX_AUDIO, &alen);
	if (err != POCSAG_OK) {
		fprintf(stderr, "modulate: %s\n", pocsag_strerror(err));
		return 1;
	}
	printf("[TX] modulated → %zu audio samples (%.3f seconds)\n\n",
	       alen, (double)alen / SAMPLE_RATE);

	/* --- demodulate + decode ------------------------------------- */
	rx_state_t rx;
	memset(&rx, 0, sizeof(rx));
	pocsag_decoder_init(&rx.decoder, on_message, &rx);

	pocsag_demod_t dem;
	pocsag_demod_init(&dem, SAMPLE_RATE, baud, on_bit, &rx);

	err = pocsag_demodulate(&dem, audio_buf, alen);
	if (err != POCSAG_OK) {
		fprintf(stderr, "demodulate: %s\n", pocsag_strerror(err));
		return 1;
	}
	pocsag_decoder_flush(&rx.decoder);

	printf("\n[RX] %u bits recovered, %u transitions\n",
	       (unsigned)dem.stat_bits, (unsigned)dem.stat_transitions);
	printf("[RX] %u messages decoded, %u BCH corrections, %u errors\n",
	       (unsigned)rx.decoder.stat_messages,
	       (unsigned)rx.decoder.stat_corrected,
	       (unsigned)rx.decoder.stat_errors);

	return (rx.msg_count == 3) ? 0 : 1;
}
