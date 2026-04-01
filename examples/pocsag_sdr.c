/*
 * pocsag_sdr — Receive and decode POCSAG from an RTL-SDR dongle.
 *
 * Pipeline:
 *   RTL-SDR IQ @ 240 kHz → FM discriminator → DC block → de-emphasis
 *   → decimate to 48 kHz → baseband slicer → POCSAG decoder
 *
 * Usage:
 *   pocsag_sdr -f 462.550 -b 1200
 *   pocsag_sdr -f 462.550 -b 1200 -g 40 -i -v
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <rtl-sdr.h>
#include <libpocsag/pocsag.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEFAULT_SDR_RATE   240000
#define DEFAULT_AUDIO_RATE 48000
#define IQ_BUFSZ           (16384 * 4)   /* 65536 bytes = 32768 IQ samples */

static volatile int g_running = 1;

static void sighandler(int sig)
{
	(void)sig;
	g_running = 0;
}

/* ── decoder callback ── */

static void on_message(const pocsag_msg_t *msg, void *user)
{
	(void)user;
	const char *type_str = "unknown";
	switch (msg->type) {
	case POCSAG_MSG_NUMERIC:   type_str = "numeric";   break;
	case POCSAG_MSG_ALPHA:     type_str = "alpha";     break;
	case POCSAG_MSG_TONE_ONLY: type_str = "tone-only"; break;
	}
	printf("[POCSAG] addr=%-7u func=%d type=%-9s msg=\"%s\"\n",
	       (unsigned)msg->address, msg->function, type_str, msg->text);
	fflush(stdout);
}

/* ── demodulator → decoder bridge ── */

typedef struct {
	pocsag_decoder_t decoder;
	pocsag_demod_t   demod;
	int              verbose;
	uint32_t         last_report;
} rx_state_t;

static void on_bit(int bit, void *user)
{
	rx_state_t *rx = (rx_state_t *)user;
	uint8_t b = (uint8_t)(bit & 1);
	pocsag_decoder_feed_bits(&rx->decoder, &b, 1);
}

/* ── usage ── */

static void usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s -f <freq_mhz> [options]\n"
	    "\n"
	    "  -f freq   Frequency in MHz (required)\n"
	    "  -b baud   POCSAG baud rate: 512, 1200, 2400 (default 1200)\n"
	    "  -d index  RTL-SDR device index (default 0)\n"
	    "  -g gain   Tuner gain in dB*10 (default auto)\n"
	    "  -s rate   SDR sample rate (default %d)\n"
	    "  -r rate   Audio output rate (default %d)\n"
	    "  -i        Invert polarity\n"
	    "  -v        Verbose (print stats periodically)\n",
	    prog, DEFAULT_SDR_RATE, DEFAULT_AUDIO_RATE);
}

int main(int argc, char **argv)
{
	double    freq_mhz  = 0.0;
	uint32_t  baud      = 1200;
	int       dev_idx   = 0;
	int       gain      = -1;         /* -1 = auto */
	uint32_t  sdr_rate  = DEFAULT_SDR_RATE;
	uint32_t  audio_rate = DEFAULT_AUDIO_RATE;
	int       invert    = 0;
	int       verbose   = 0;

	int opt;
	while ((opt = getopt(argc, argv, "f:b:d:g:s:r:ivh")) != -1) {
		switch (opt) {
		case 'f': freq_mhz   = atof(optarg); break;
		case 'b': baud       = (uint32_t)atoi(optarg); break;
		case 'd': dev_idx    = atoi(optarg); break;
		case 'g': gain       = atoi(optarg); break;
		case 's': sdr_rate   = (uint32_t)atoi(optarg); break;
		case 'r': audio_rate = (uint32_t)atoi(optarg); break;
		case 'i': invert     = 1; break;
		case 'v': verbose    = 1; break;
		default:  usage(argv[0]); return 1;
		}
	}

	if (freq_mhz <= 0.0) {
		fprintf(stderr, "Error: frequency required (-f)\n");
		usage(argv[0]);
		return 1;
	}

	if (!pocsag_baud_valid(baud)) {
		fprintf(stderr, "Error: invalid baud rate %u\n", baud);
		return 1;
	}

	uint32_t freq_hz = (uint32_t)(freq_mhz * 1e6 + 0.5);

	/* ── set up decoder chain ── */

	rx_state_t rx;
	memset(&rx, 0, sizeof(rx));
	rx.verbose = verbose;

	pocsag_decoder_init(&rx.decoder, on_message, NULL);
	pocsag_demod_init(&rx.demod, audio_rate, baud, on_bit, &rx);

	/* ── open RTL-SDR ── */

	int count = (int)rtlsdr_get_device_count();
	if (count == 0) {
		fprintf(stderr, "No RTL-SDR devices found\n");
		return 1;
	}
	fprintf(stderr, "Found %d RTL-SDR device(s)\n", count);

	rtlsdr_dev_t *dev = NULL;
	int rc = rtlsdr_open(&dev, (uint32_t)dev_idx);
	if (rc < 0) {
		fprintf(stderr, "Failed to open device %d (rc=%d)\n", dev_idx, rc);
		return 1;
	}

	rtlsdr_set_center_freq(dev, freq_hz);
	rtlsdr_set_sample_rate(dev, sdr_rate);

	if (gain < 0) {
		rtlsdr_set_tuner_gain_mode(dev, 0);
		fprintf(stderr, "Gain: auto\n");
	} else {
		rtlsdr_set_tuner_gain_mode(dev, 1);
		rtlsdr_set_tuner_gain(dev, gain);
		fprintf(stderr, "Gain: %.1f dB\n", gain / 10.0);
	}

	rtlsdr_reset_buffer(dev);

	fprintf(stderr, "%.4f MHz | SDR %u Hz | Audio %u Hz | POCSAG %u baud%s\n",
	        freq_hz / 1e6, sdr_rate, audio_rate, baud,
	        invert ? " | INVERTED" : "");
	fprintf(stderr, "Listening... (Ctrl-C to stop)\n\n");

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	/* ── allocate buffers ── */

	unsigned char *iq_buf = (unsigned char *)malloc(IQ_BUFSZ);
	/* max audio samples per IQ chunk after decimation */
	int max_audio = (int)((IQ_BUFSZ / 2) *
	                      ((double)audio_rate / sdr_rate)) + 16;
	float *audio_buf = (float *)malloc((size_t)max_audio * sizeof(float));

	/* ── FM demod state ── */

	float prev_i = 0.0f, prev_q = 0.0f;

	/* DC blocking filter: H(z) = (1 - z^-1) / (1 - a*z^-1) */
	float dc_x_prev = 0.0f, dc_y_prev = 0.0f;
	const float alpha_dc = 0.998f;

	/* De-emphasis: 75 us time constant */
	float deemph = 0.0f;
	float alpha_de = 1.0f / (1.0f + (float)sdr_rate * 75e-6f);

	/* Fractional decimation: sdr_rate → audio_rate */
	double dec_step = (double)audio_rate / (double)sdr_rate;
	double dec_acc = 0.0;
	float  dec_sum = 0.0f;
	int    dec_count = 0;

	/* polarity: invert flips the sign of the FM discriminator output */
	float pol = invert ? -1.0f : 1.0f;

	/* ── main loop ── */

	while (g_running) {
		int n_read = 0;
		rc = rtlsdr_read_sync(dev, iq_buf, IQ_BUFSZ, &n_read);
		if (rc < 0) {
			fprintf(stderr, "rtlsdr_read_sync failed (rc=%d)\n", rc);
			break;
		}
		if (n_read == 0) continue;

		int niq = n_read / 2;
		int audio_pos = 0;

		for (int i = 0; i < niq; i++) {
			float si = ((float)iq_buf[i * 2]     - 127.5f) / 127.5f;
			float sq = ((float)iq_buf[i * 2 + 1] - 127.5f) / 127.5f;

			/* FM discriminator (atan2 of cross/dot product) */
			float dot   = si * prev_i + sq * prev_q;
			float cross = sq * prev_i - si * prev_q;
			float fm    = atan2f(cross, dot) * pol;
			prev_i = si;
			prev_q = sq;

			/* DC blocker */
			float dc_out = fm - dc_x_prev + alpha_dc * dc_y_prev;
			dc_x_prev = fm;
			dc_y_prev = dc_out;

			/* De-emphasis (75 us) */
			deemph += alpha_de * (dc_out - deemph);

			/* Averaging decimation to audio rate */
			dec_sum += deemph;
			dec_count++;
			dec_acc += dec_step;

			if (dec_acc >= 1.0) {
				dec_acc -= 1.0;
				float avg = dec_sum / (float)dec_count;
				if (audio_pos < max_audio)
					audio_buf[audio_pos++] = avg;
				dec_sum = 0.0f;
				dec_count = 0;
			}
		}

		if (audio_pos == 0) continue;

		/* feed baseband audio into POCSAG demodulator */
		pocsag_demod_baseband(&rx.demod, audio_buf, (size_t)audio_pos);

		/* periodic stats */
		if (verbose && rx.demod.stat_bits - rx.last_report >= 10000) {
			fprintf(stderr, "[stats] bits=%u transitions=%u msgs=%u "
			        "bch_ok=%u bch_err=%u\n",
			        rx.demod.stat_bits, rx.demod.stat_transitions,
			        rx.decoder.stat_messages,
			        rx.decoder.stat_corrected,
			        rx.decoder.stat_errors);
			rx.last_report = rx.demod.stat_bits;
		}
	}

	/* flush any partial message */
	pocsag_decoder_flush(&rx.decoder);

	fprintf(stderr, "\nStopped. bits=%u transitions=%u messages=%u\n",
	        rx.demod.stat_bits, rx.demod.stat_transitions,
	        rx.decoder.stat_messages);

	free(audio_buf);
	free(iq_buf);
	rtlsdr_close(dev);
	return 0;
}
