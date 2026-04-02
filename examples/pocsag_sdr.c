/*
 * pocsag_sdr — Receive and decode POCSAG from an RTL-SDR dongle.
 *
 * Uses async IQ reading with a ring buffer for continuous sample
 * capture (no gaps from rtlsdr_read_sync blocking).
 *
 * Pipeline:
 *   RTL-SDR IQ @ 240 kHz (async reader thread → ring buffer)
 *   → FM discriminator → DC block → optional de-emphasis
 *   → decimate to 48 kHz → squelch → FSK/baseband demod → decoder
 *
 * Usage:
 *   pocsag_sdr -f 462.550 -b 1200 -g 40 -v -S 12000:14000 -E
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <pthread.h>
#include <rtl-sdr.h>
#include <libpocsag/pocsag.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEFAULT_SDR_RATE   240000
#define DEFAULT_AUDIO_RATE 48000
#define IQ_CHUNK           65536
#define RING_SIZE           (IQ_CHUNK * 32)

/* squelch defaults (FM-inverted: low RMS = signal) */
#define DEFAULT_SQ_OPEN    15000
#define DEFAULT_SQ_CLOSE   17000
#define SQ_DEBOUNCE_OPEN   3
#define SQ_DEBOUNCE_CLOSE  5

/* ── ring buffer for async IQ reader ── */

static unsigned char   g_ring[RING_SIZE];
static volatile size_t g_ring_head = 0;
static volatile size_t g_ring_tail = 0;
static pthread_mutex_t g_ring_mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_ring_cond = PTHREAD_COND_INITIALIZER;

static volatile int    g_running = 1;
static rtlsdr_dev_t   *g_dev;

static void sighandler(int sig)
{
	(void)sig;
	g_running = 0;
	rtlsdr_cancel_async(g_dev);
}

static void rtlsdr_cb(unsigned char *buf, uint32_t len, void *ctx)
{
	(void)ctx;
	if (!g_running) { rtlsdr_cancel_async(g_dev); return; }
	pthread_mutex_lock(&g_ring_mtx);
	for (uint32_t i = 0; i < len; i++) {
		g_ring[g_ring_head % RING_SIZE] = buf[i];
		g_ring_head++;
	}
	pthread_cond_signal(&g_ring_cond);
	pthread_mutex_unlock(&g_ring_mtx);
}

static void *reader_thread(void *arg)
{
	(void)arg;
	rtlsdr_read_async(g_dev, rtlsdr_cb, NULL, 0, IQ_CHUNK);
	return NULL;
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

/* ── demod → decoder bridge ── */

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

/* ── WAV helpers ── */

static FILE *wav_open(const char *path, uint32_t sr)
{
	FILE *f = fopen(path, "wb");
	if (!f) return NULL;
	uint8_t hdr[44] = {0};
	memcpy(hdr, "RIFF", 4); memcpy(hdr + 8, "WAVEfmt ", 8);
	uint32_t v; uint16_t s;
	v = 16;    memcpy(hdr + 16, &v, 4);
	s = 1;     memcpy(hdr + 20, &s, 2);
	s = 1;     memcpy(hdr + 22, &s, 2);
	v = sr;    memcpy(hdr + 24, &v, 4);
	v = sr*2;  memcpy(hdr + 28, &v, 4);
	s = 2;     memcpy(hdr + 32, &s, 2);
	s = 16;    memcpy(hdr + 34, &s, 2);
	memcpy(hdr + 36, "data", 4);
	fwrite(hdr, 1, 44, f);
	return f;
}

static void wav_close(FILE *f, uint32_t samples)
{
	if (!f) return;
	uint32_t data = samples * 2, file = 36 + data;
	fseek(f, 4, SEEK_SET);  fwrite(&file, 4, 1, f);
	fseek(f, 40, SEEK_SET); fwrite(&data, 4, 1, f);
	fclose(f);
}

/* ── usage ── */

static void usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s -f <freq_mhz> [options]\n\n"
	    "  -f freq       Frequency in MHz (required)\n"
	    "  -b baud       POCSAG baud rate: 512, 1200, 2400 (default 1200)\n"
	    "  -d index      RTL-SDR device index (default 0)\n"
	    "  -g gain       Tuner gain in dB*10 (default auto)\n"
	    "  -s rate       SDR sample rate (default %d)\n"
	    "  -r rate       Audio output rate (default %d)\n"
	    "  -S open:close Squelch thresholds (default %d:%d)\n"
	    "  -m mode       Demod mode: fsk (default) or baseband\n"
	    "  -w file       Dump audio to WAV\n"
	    "  -E            Disable de-emphasis\n"
	    "  -i            Invert polarity\n"
	    "  -v            Verbose stats\n",
	    prog, DEFAULT_SDR_RATE, DEFAULT_AUDIO_RATE,
	    DEFAULT_SQ_OPEN, DEFAULT_SQ_CLOSE);
}

int main(int argc, char **argv)
{
	double    freq_mhz    = 0.0;
	uint32_t  baud        = 1200;
	int       dev_idx     = 0;
	int       gain        = -1;
	uint32_t  sdr_rate    = DEFAULT_SDR_RATE;
	uint32_t  audio_rate  = DEFAULT_AUDIO_RATE;
	int       sq_open_th  = DEFAULT_SQ_OPEN;
	int       sq_close_th = DEFAULT_SQ_CLOSE;
	const char *wav_path  = NULL;
	int       use_fsk     = 1;
	int       no_deemph   = 0;
	int       invert      = 0;
	int       verbose     = 0;

	int opt;
	while ((opt = getopt(argc, argv, "f:b:d:g:s:r:S:m:w:Eivh")) != -1) {
		switch (opt) {
		case 'f': freq_mhz    = atof(optarg); break;
		case 'b': baud        = (uint32_t)atoi(optarg); break;
		case 'd': dev_idx     = atoi(optarg); break;
		case 'g': gain        = atoi(optarg); break;
		case 's': sdr_rate    = (uint32_t)atoi(optarg); break;
		case 'r': audio_rate  = (uint32_t)atoi(optarg); break;
		case 'S': sscanf(optarg, "%d:%d", &sq_open_th, &sq_close_th); break;
		case 'm': use_fsk     = (strcmp(optarg, "baseband") != 0); break;
		case 'w': wav_path    = optarg; break;
		case 'E': no_deemph   = 1; break;
		case 'i': invert      = 1; break;
		case 'v': verbose     = 1; break;
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

	/* set up decoder chain */
	rx_state_t rx;
	memset(&rx, 0, sizeof(rx));
	rx.verbose = verbose;
	pocsag_decoder_init(&rx.decoder, on_message, NULL);
	pocsag_demod_init(&rx.demod, audio_rate, baud, on_bit, &rx);

	/* open SDR */
	int count = (int)rtlsdr_get_device_count();
	if (count == 0) { fprintf(stderr, "No RTL-SDR devices found\n"); return 1; }
	fprintf(stderr, "Found %d RTL-SDR device(s)\n", count);

	int rc = rtlsdr_open(&g_dev, (uint32_t)dev_idx);
	if (rc < 0) { fprintf(stderr, "Failed to open device %d\n", dev_idx); return 1; }

	rtlsdr_set_center_freq(g_dev, freq_hz);
	rtlsdr_set_sample_rate(g_dev, sdr_rate);
	if (gain < 0) {
		rtlsdr_set_tuner_gain_mode(g_dev, 0);
		fprintf(stderr, "Gain: auto\n");
	} else {
		rtlsdr_set_tuner_gain_mode(g_dev, 1);
		rtlsdr_set_tuner_gain(g_dev, gain);
		fprintf(stderr, "Gain: %.1f dB\n", gain / 10.0);
	}
	rtlsdr_reset_buffer(g_dev);

	fprintf(stderr, "%.4f MHz | SDR %u Hz | Audio %u Hz | POCSAG %u baud | %s%s%s\n",
	        freq_hz / 1e6, sdr_rate, audio_rate, baud,
	        use_fsk ? "FSK" : "baseband",
	        no_deemph ? " | no de-emph" : "",
	        invert ? " | inverted" : "");
	fprintf(stderr, "Squelch: open=%d close=%d\n", sq_open_th, sq_close_th);
	fprintf(stderr, "Listening... (Ctrl-C to stop)\n\n");

	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	FILE *wav_fp = wav_path ? wav_open(wav_path, audio_rate) : NULL;
	uint32_t wav_samples = 0;

	/* start async reader */
	pthread_t reader;
	pthread_create(&reader, NULL, reader_thread, NULL);

	/* FM demod state */
	float prev_i = 0, prev_q = 0;
	float dc_x_prev = 0, dc_y_prev = 0;
	const float alpha_dc = 0.998f;
	float deemph = 0;
	float alpha_de = 1.0f / (1.0f + (float)sdr_rate * 75e-6f);
	double dec_step = (double)audio_rate / (double)sdr_rate;
	double dec_acc = 0;
	float dec_sum = 0;
	int dec_count = 0;
	float pol = invert ? -1.0f : 1.0f;

	float audio_buf[16384];
	unsigned char iq_chunk[IQ_CHUNK];

	/* squelch state */
	int sq_open = 0, sq_open_cnt = 0, sq_close_cnt = 0;

	while (g_running) {
		/* pull IQ from ring */
		pthread_mutex_lock(&g_ring_mtx);
		while (g_ring_head - g_ring_tail < IQ_CHUNK && g_running)
			pthread_cond_wait(&g_ring_cond, &g_ring_mtx);
		if (!g_running) { pthread_mutex_unlock(&g_ring_mtx); break; }
		for (int i = 0; i < IQ_CHUNK; i++)
			iq_chunk[i] = g_ring[(g_ring_tail + i) % RING_SIZE];
		g_ring_tail += IQ_CHUNK;
		pthread_mutex_unlock(&g_ring_mtx);

		/* FM demod → decimate */
		int niq = IQ_CHUNK / 2;
		int audio_pos = 0;

		for (int i = 0; i < niq; i++) {
			float si = ((float)iq_chunk[i * 2]     - 127.5f) / 127.5f;
			float sq = ((float)iq_chunk[i * 2 + 1] - 127.5f) / 127.5f;
			float dot   = si * prev_i + sq * prev_q;
			float cross = sq * prev_i - si * prev_q;
			float fm    = atan2f(cross, dot) * pol;
			prev_i = si; prev_q = sq;

			float dc_out = fm - dc_x_prev + alpha_dc * dc_y_prev;
			dc_x_prev = fm; dc_y_prev = dc_out;

			if (!no_deemph)
				deemph += alpha_de * (dc_out - deemph);

			dec_sum += no_deemph ? dc_out : deemph;
			dec_count++;
			dec_acc += dec_step;
			if (dec_acc >= 1.0) {
				dec_acc -= 1.0;
				audio_buf[audio_pos++] = dec_sum / (float)dec_count;
				dec_sum = 0; dec_count = 0;
			}
		}
		if (audio_pos == 0) continue;

		/* WAV dump */
		if (wav_fp) {
			float wav_scale = no_deemph ? 5000.0f : 16000.0f;
			for (int j = 0; j < audio_pos; j++) {
				float s = audio_buf[j] * wav_scale;
				if (s > 32767) s = 32767; if (s < -32768) s = -32768;
				int16_t pcm = (int16_t)s;
				fwrite(&pcm, 2, 1, wav_fp);
				wav_samples++;
			}
		}

		/* squelch */
		int64_t rms_sum = 0;
		float sq_scale = no_deemph ? 5000.0f : 16000.0f;
		for (int j = 0; j < audio_pos; j++) {
			int32_t v = (int32_t)(audio_buf[j] * sq_scale);
			rms_sum += (int64_t)v * v;
		}
		int32_t rms = (int32_t)sqrtf((float)(rms_sum / audio_pos));

		if (!sq_open) {
			if (rms < sq_open_th) {
				sq_open_cnt++;
				if (sq_open_cnt >= SQ_DEBOUNCE_OPEN) {
					sq_open = 1; sq_close_cnt = 0;
					if (verbose)
						fprintf(stderr, "[squelch] OPEN  rms=%d\n", rms);
					pocsag_decoder_reset(&rx.decoder);
					pocsag_demod_reset(&rx.demod);
				}
			} else { sq_open_cnt = 0; }
		} else {
			if (rms > sq_close_th) {
				sq_close_cnt++;
				if (sq_close_cnt >= SQ_DEBOUNCE_CLOSE) {
					sq_open = 0; sq_open_cnt = 0;
					if (verbose)
						fprintf(stderr, "[squelch] CLOSE rms=%d\n", rms);
					pocsag_decoder_flush(&rx.decoder);
				}
			} else { sq_close_cnt = 0; }
		}

		/* feed demod when squelch open */
		if (sq_open) {
			pocsag_dec_state_t prev = rx.decoder.state;
			if (use_fsk)
				pocsag_demodulate(&rx.demod, audio_buf, (size_t)audio_pos);
			else
				pocsag_demod_baseband(&rx.demod, audio_buf, (size_t)audio_pos);

			if (prev == POCSAG_DEC_BATCH && rx.decoder.state == POCSAG_DEC_HUNTING)
				pocsag_demod_reset(&rx.demod);
		}

		/* stats */
		if (verbose && rx.demod.stat_bits > 0 &&
		    rx.demod.stat_bits - rx.last_report >= 5000) {
			fprintf(stderr, "[stats] bits=%u trans=%u msgs=%u bch_ok=%u bch_err=%u rms=%d sq=%s\n",
			        rx.demod.stat_bits, rx.demod.stat_transitions,
			        rx.decoder.stat_messages, rx.decoder.stat_corrected,
			        rx.decoder.stat_errors, rms, sq_open ? "OPEN" : "closed");
			rx.last_report = rx.demod.stat_bits;
		}
	}

	rtlsdr_cancel_async(g_dev);
	pthread_join(reader, NULL);
	pocsag_decoder_flush(&rx.decoder);

	wav_close(wav_fp, wav_samples);
	if (wav_path) fprintf(stderr, "Wrote %u samples to %s\n", wav_samples, wav_path);

	fprintf(stderr, "\nStopped. bits=%u transitions=%u messages=%u\n",
	        rx.demod.stat_bits, rx.demod.stat_transitions,
	        rx.decoder.stat_messages);

	rtlsdr_close(g_dev);
	return 0;
}
