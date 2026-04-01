/*
 * pocsag_sdr — Receive and decode POCSAG from an RTL-SDR dongle.
 *
 * Pipeline:
 *   RTL-SDR IQ @ 240 kHz → FM discriminator → DC block → de-emphasis
 *   → decimate to 48 kHz → squelch gate → baseband slicer → POCSAG decoder
 *
 * Usage:
 *   pocsag_sdr -f 462.550 -b 1200
 *   pocsag_sdr -f 462.550 -b 1200 -g 40 -i -v
 *   pocsag_sdr -f 462.550 -b 1200 -S 15000:17000
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

/* Squelch defaults (FM-inverted: low RMS = signal present).
 * These match sdr_record.c / kerchunk proven values. */
#define DEFAULT_SQ_OPEN    15000
#define DEFAULT_SQ_CLOSE   17000
#define SQ_DEBOUNCE_OPEN   3
#define SQ_DEBOUNCE_CLOSE  5

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
	    "  -f freq       Frequency in MHz (required)\n"
	    "  -b baud       POCSAG baud rate: 512, 1200, 2400 (default 1200)\n"
	    "  -d index      RTL-SDR device index (default 0)\n"
	    "  -g gain       Tuner gain in dB*10 (default auto)\n"
	    "  -s rate       SDR sample rate (default %d)\n"
	    "  -r rate       Audio output rate (default %d)\n"
	    "  -S open:close Squelch thresholds (default %d:%d)\n"
	    "  -m mode       Demod mode: fsk (default) or baseband\n"
	    "  -w file       Dump raw audio to 16-bit WAV for analysis\n"
	    "  -i            Invert polarity\n"
	    "  -v            Verbose (print stats periodically)\n",
	    prog, DEFAULT_SDR_RATE, DEFAULT_AUDIO_RATE,
	    DEFAULT_SQ_OPEN, DEFAULT_SQ_CLOSE);
}

int main(int argc, char **argv)
{
	double    freq_mhz    = 0.0;
	uint32_t  baud        = 1200;
	int       dev_idx     = 0;
	int       gain        = -1;         /* -1 = auto */
	uint32_t  sdr_rate    = DEFAULT_SDR_RATE;
	uint32_t  audio_rate  = DEFAULT_AUDIO_RATE;
	int       sq_open_th  = DEFAULT_SQ_OPEN;
	int       sq_close_th = DEFAULT_SQ_CLOSE;
	const char *wav_path  = NULL;
	int       use_fsk     = 1;        /* default: FSK correlator */
	int       invert      = 0;
	int       verbose     = 0;

	int opt;
	while ((opt = getopt(argc, argv, "f:b:d:g:s:r:S:m:w:ivh")) != -1) {
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

	fprintf(stderr, "%.4f MHz | SDR %u Hz | Audio %u Hz | POCSAG %u baud | %s%s\n",
	        freq_hz / 1e6, sdr_rate, audio_rate, baud,
	        use_fsk ? "FSK" : "baseband",
	        invert ? " | INVERTED" : "");
	fprintf(stderr, "Squelch: open=%d close=%d (FM-inverted RMS)\n",
	        sq_open_th, sq_close_th);
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

	/* ── WAV dump (for signal analysis) ── */

	FILE *wav_fp = NULL;
	uint32_t wav_samples = 0;

	if (wav_path) {
		wav_fp = fopen(wav_path, "wb");
		if (!wav_fp) {
			perror(wav_path);
			rtlsdr_close(dev);
			return 1;
		}
		/* write placeholder header, fix on close */
		uint8_t hdr[44] = {0};
		memcpy(hdr, "RIFF", 4);
		memcpy(hdr + 8, "WAVEfmt ", 8);
		uint32_t v;
		v = 16;     memcpy(hdr + 16, &v, 4);   /* chunk size */
		uint16_t s;
		s = 1;      memcpy(hdr + 20, &s, 2);   /* PCM */
		s = 1;      memcpy(hdr + 22, &s, 2);   /* mono */
		v = audio_rate; memcpy(hdr + 24, &v, 4);
		v = audio_rate * 2; memcpy(hdr + 28, &v, 4);
		s = 2;      memcpy(hdr + 32, &s, 2);   /* block align */
		s = 16;     memcpy(hdr + 34, &s, 2);   /* bits */
		memcpy(hdr + 36, "data", 4);
		fwrite(hdr, 1, 44, wav_fp);
		fprintf(stderr, "Dumping audio to %s\n", wav_path);
	}

	/* ── squelch state (FM-inverted: low RMS = signal) ── */

	int sq_open = 0;
	int sq_open_cnt = 0, sq_close_cnt = 0;

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

		/* ── dump raw audio to WAV (all audio, pre-squelch) ── */

		if (wav_fp) {
			for (int j = 0; j < audio_pos; j++) {
				float s = audio_buf[j] * 16000.0f;
				if (s > 32767.0f) s = 32767.0f;
				if (s < -32768.0f) s = -32768.0f;
				int16_t pcm = (int16_t)s;
				fwrite(&pcm, 2, 1, wav_fp);
				wav_samples++;
			}
		}

		/* ── squelch: compute RMS of this audio chunk ── */

		int64_t rms_sum = 0;
		for (int j = 0; j < audio_pos; j++) {
			/* scale to int16 range for RMS comparison with
			 * sdr_record.c-style thresholds */
			int32_t v = (int32_t)(audio_buf[j] * 16000.0f);
			rms_sum += (int64_t)v * v;
		}
		int32_t rms = (int32_t)sqrtf((float)(rms_sum / audio_pos));

		/* FM-inverted squelch: low RMS = signal, high RMS = noise */
		if (!sq_open) {
			if (rms < sq_open_th) {
				sq_open_cnt++;
				if (sq_open_cnt >= SQ_DEBOUNCE_OPEN) {
					sq_open = 1;
					sq_close_cnt = 0;
					if (verbose)
						fprintf(stderr, "[squelch] OPEN  rms=%d\n",
						        rms);
					/* reset decoder state on channel open so
					 * stale noise bits don't poison sync */
					pocsag_decoder_reset(&rx.decoder);
					pocsag_demod_reset(&rx.demod);
				}
			} else {
				sq_open_cnt = 0;
			}
		} else {
			if (rms > sq_close_th) {
				sq_close_cnt++;
				if (sq_close_cnt >= SQ_DEBOUNCE_CLOSE) {
					sq_open = 0;
					sq_open_cnt = 0;
					if (verbose)
						fprintf(stderr, "[squelch] CLOSE rms=%d\n",
						        rms);
					pocsag_decoder_flush(&rx.decoder);
				}
			} else {
				sq_close_cnt = 0;
			}
		}

		/* only feed audio when squelch is open */
		if (sq_open) {
			if (use_fsk)
				pocsag_demodulate(&rx.demod, audio_buf,
				                  (size_t)audio_pos);
			else
				pocsag_demod_baseband(&rx.demod, audio_buf,
				                      (size_t)audio_pos);
		}

		/* periodic stats */
		if (verbose && rx.demod.stat_bits > 0 &&
		    rx.demod.stat_bits - rx.last_report >= 5000) {
			fprintf(stderr, "[stats] bits=%u transitions=%u msgs=%u "
			        "bch_ok=%u bch_err=%u rms=%d sq=%s\n",
			        rx.demod.stat_bits, rx.demod.stat_transitions,
			        rx.decoder.stat_messages,
			        rx.decoder.stat_corrected,
			        rx.decoder.stat_errors,
			        rms, sq_open ? "OPEN" : "closed");
			rx.last_report = rx.demod.stat_bits;
		}
	}

	/* flush any partial message */
	pocsag_decoder_flush(&rx.decoder);

	fprintf(stderr, "\nStopped. bits=%u transitions=%u messages=%u\n",
	        rx.demod.stat_bits, rx.demod.stat_transitions,
	        rx.decoder.stat_messages);

	/* fix WAV header with final sizes */
	if (wav_fp) {
		uint32_t data_bytes = wav_samples * 2;
		uint32_t file_size  = 36 + data_bytes;
		fseek(wav_fp, 4, SEEK_SET);
		fwrite(&file_size, 4, 1, wav_fp);
		fseek(wav_fp, 40, SEEK_SET);
		fwrite(&data_bytes, 4, 1, wav_fp);
		fclose(wav_fp);
		fprintf(stderr, "Wrote %u samples to WAV\n", wav_samples);
	}

	free(audio_buf);
	free(iq_buf);
	rtlsdr_close(dev);
	return 0;
}
