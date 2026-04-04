#define _DEFAULT_SOURCE

/*
 * pocsag_tx — Transmit POCSAG pages via an AIOC / CM108 USB audio device.
 *
 * Pipeline:
 *   libpocsag encoder → modulator (FSK audio) → PortAudio → AIOC
 *   CM108 HID GPIO PTT ──────────────────────────────────────┘
 *
 * Usage:
 *   pocsag_tx -D hw:1,0 -H /dev/hidraw1 -a 1234567 -m "Hello"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <portaudio.h>
#include <libpocsag/pocsag.h>

#define SAMPLE_RATE   48000
#define TX_DELAY_MS   500     /* PTT-to-audio delay (radio ramp-up) */
#define TX_TAIL_MS    500     /* extra hold after audio drain */

/* ── Serial DTR PTT ── */

static int ptt_open(const char *serial_path)
{
	int fd = open(serial_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) return -1;

	/* configure port like pyserial does — raw mode, 9600 baud */
	struct termios tio;
	memset(&tio, 0, sizeof(tio));
	tio.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
	tio.c_iflag = IGNPAR;
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &tio);

	/* clear DTR initially */
	int bits = TIOCM_DTR;
	ioctl(fd, TIOCMBIC, &bits);
	return fd;
}

static int ptt_set(int fd, int on)
{
	if (fd < 0) return -1;
	int bits = TIOCM_DTR;
	int rc = ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &bits);

	/* verify */
	int status = 0;
	ioctl(fd, TIOCMGET, &status);
	int actual = (status & TIOCM_DTR) ? 1 : 0;
	if (actual != on)
		fprintf(stderr, "Warning: DTR %s failed (status=0x%x)\n",
		        on ? "ON" : "OFF", status);
	return rc;
}

/* ── PortAudio playback ── */

static PaStream *g_stream;

static int audio_open(const char *device_name)
{
	PaError err;
	PaDeviceIndex dev = paNoDevice;
	PaStreamParameters out_params;

	err = Pa_Initialize();
	if (err != paNoError) {
		fprintf(stderr, "PortAudio init failed: %s\n", Pa_GetErrorText(err));
		return -1;
	}

	int ndev = Pa_GetDeviceCount();
	for (int i = 0; i < ndev; i++) {
		const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
		if (info && info->maxOutputChannels > 0 &&
		    strstr(info->name, device_name)) {
			dev = i;
			break;
		}
	}
	if (dev == paNoDevice) {
		fprintf(stderr, "Audio device '%s' not found.  Available:\n", device_name);
		for (int i = 0; i < ndev; i++) {
			const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
			if (info && info->maxOutputChannels > 0)
				fprintf(stderr, "  [%d] %s\n", i, info->name);
		}
		Pa_Terminate();
		return -1;
	}

	fprintf(stderr, "Audio: %s\n", Pa_GetDeviceInfo(dev)->name);

	memset(&out_params, 0, sizeof(out_params));
	out_params.device = dev;
	out_params.channelCount = 1;
	out_params.sampleFormat = paInt16;
	out_params.suggestedLatency = Pa_GetDeviceInfo(dev)->defaultLowOutputLatency;

	err = Pa_OpenStream(&g_stream, NULL, &out_params,
	                    SAMPLE_RATE, 256, paClipOff, NULL, NULL);
	if (err != paNoError) {
		fprintf(stderr, "Pa_OpenStream: %s\n", Pa_GetErrorText(err));
		Pa_Terminate();
		return -1;
	}

	err = Pa_StartStream(g_stream);
	if (err != paNoError) {
		fprintf(stderr, "Pa_StartStream: %s\n", Pa_GetErrorText(err));
		Pa_CloseStream(g_stream); g_stream = NULL;
		Pa_Terminate();
		return -1;
	}
	return 0;
}

static int audio_write(const int16_t *samples, size_t nsamples)
{
	PaError err = Pa_WriteStream(g_stream, samples, (unsigned long)nsamples);
	if (err != paNoError && err != paOutputUnderflowed) {
		fprintf(stderr, "Pa_WriteStream: %s\n", Pa_GetErrorText(err));
		return -1;
	}
	return 0;
}

static void audio_close(void)
{
	if (g_stream) {
		Pa_StopStream(g_stream);
		Pa_CloseStream(g_stream);
		g_stream = NULL;
	}
	Pa_Terminate();
}

static void usage(const char *prog)
{
	fprintf(stderr,
	    "Usage: %s [options] -a <address> [-m <message>]\n\n"
	    "  -a addr       Pager address / capcode (required)\n"
	    "  -m message    Alphanumeric message text\n"
	    "  -n digits     Numeric message (digits 0-9 * # - space)\n"
	    "  -t            Tone-only page\n"
	    "  -b baud       Baud rate: 512, 1200 (default), 2400\n"
	    "  -D device     PortAudio device name substring (default: AllInOne)\n"
	    "  -P port       Serial port for DTR PTT (default: /dev/ttyACM0)\n"
	    "  -l level      Audio level 0.0-1.0 (default: 0.5)\n"
	    "  -d ms         TX delay in ms (default: %d)\n"
	    "  -v            Verbose\n",
	    prog, TX_DELAY_MS);
}

int main(int argc, char **argv)
{
	uint32_t address = 0;
	int      have_addr = 0;
	const char *alpha_msg = NULL;
	const char *num_msg = NULL;
	int      tone_only = 0;
	uint32_t baud = 1200;
	const char *audio_dev = "AllInOne";
	const char *ptt_port = "/dev/ttyACM0";
	float    level = 0.5f;
	int      tx_delay = TX_DELAY_MS;
	int      verbose = 0;

	int opt;
	while ((opt = getopt(argc, argv, "a:m:n:tb:D:P:l:d:vh")) != -1) {
		switch (opt) {
		case 'a': address = (uint32_t)atoi(optarg); have_addr = 1; break;
		case 'm': alpha_msg = optarg; break;
		case 'n': num_msg = optarg; break;
		case 't': tone_only = 1; break;
		case 'b': baud = (uint32_t)atoi(optarg); break;
		case 'D': audio_dev = optarg; break;
		case 'P': ptt_port = optarg; break;
		case 'l': level = (float)atof(optarg); break;
		case 'd': tx_delay = atoi(optarg); break;
		case 'v': verbose = 1; break;
		default:  usage(argv[0]); return 1;
		}
	}

	if (!have_addr) {
		fprintf(stderr, "Error: address required (-a)\n");
		usage(argv[0]); return 1;
	}
	if (!alpha_msg && !num_msg && !tone_only) {
		fprintf(stderr, "Error: specify -m, -n, or -t\n");
		usage(argv[0]); return 1;
	}
	if (!pocsag_baud_valid(baud)) {
		fprintf(stderr, "Error: invalid baud rate %u\n", baud);
		return 1;
	}
	if (level <= 0.0f || level > 1.0f) level = 0.5f;

	/* ── encode ── */
	pocsag_msg_type_t type;
	pocsag_func_t func;
	const char *text;

	if (tone_only) {
		type = POCSAG_MSG_TONE_ONLY;
		func = POCSAG_FUNC_TONE1;
		text = "";
	} else if (num_msg) {
		type = POCSAG_MSG_NUMERIC;
		func = POCSAG_FUNC_NUMERIC;
		text = num_msg;
	} else {
		type = POCSAG_MSG_ALPHA;
		func = POCSAG_FUNC_ALPHA;
		text = alpha_msg;
	}

	uint8_t bitstream[POCSAG_BITSTREAM_MAX];
	size_t bs_len, bs_bits;
	pocsag_err_t perr = pocsag_encode_single(address, func, type, text,
	                                          bitstream, sizeof(bitstream),
	                                          &bs_len, &bs_bits);
	if (perr != POCSAG_OK) {
		fprintf(stderr, "Encode failed: %s\n", pocsag_strerror(perr));
		return 1;
	}
	if (verbose)
		fprintf(stderr, "Encoded: %zu bits (%zu bytes)\n", bs_bits, bs_len);

	/* ── modulate ── */
	pocsag_mod_t mod;
	pocsag_mod_init(&mod, SAMPLE_RATE, baud);

	size_t max_samples = pocsag_mod_samples_needed(&mod, bs_bits);
	float *fsamples = (float *)malloc(max_samples * sizeof(float));
	if (!fsamples) { fprintf(stderr, "Out of memory\n"); return 1; }

	size_t nsamples;
	perr = pocsag_modulate(&mod, bitstream, bs_bits,
	                        fsamples, max_samples, &nsamples);
	if (perr != POCSAG_OK) {
		fprintf(stderr, "Modulate failed: %s\n", pocsag_strerror(perr));
		free(fsamples);
		return 1;
	}
	if (verbose)
		fprintf(stderr, "Modulated: %zu samples (%.2f sec) at %d Hz\n",
		        nsamples, (double)nsamples / SAMPLE_RATE, SAMPLE_RATE);

	/* convert float [-1,+1] to int16 with level scaling */
	int16_t *pcm = (int16_t *)malloc(nsamples * sizeof(int16_t));
	if (!pcm) { fprintf(stderr, "Out of memory\n"); free(fsamples); return 1; }
	for (size_t i = 0; i < nsamples; i++) {
		float s = fsamples[i] * level * 32000.0f;
		if (s > 32767.0f) s = 32767.0f;
		if (s < -32768.0f) s = -32768.0f;
		pcm[i] = (int16_t)s;
	}
	free(fsamples);

	/* ── open audio first (slow — scans ALSA/JACK) ── */
	if (audio_open(audio_dev) < 0) {
		free(pcm);
		return 1;
	}

	/* ── PTT on ── */
	int ptt_fd = ptt_open(ptt_port);
	if (ptt_fd < 0) {
		fprintf(stderr, "Warning: cannot open %s for PTT (running without PTT)\n",
		        ptt_port);
	}

	if (verbose) fprintf(stderr, "PTT ON\n");
	ptt_set(ptt_fd, 1);
	usleep((unsigned)(tx_delay * 1000));

	/* ── play audio ── */
	if (verbose) fprintf(stderr, "Transmitting...\n");
	int rc = audio_write(pcm, nsamples);

	/* drain DAC buffer before dropping PTT */
	audio_close();
	usleep(TX_TAIL_MS * 1000);

	/* ── PTT off ── */
	ptt_set(ptt_fd, 0);
	if (verbose) fprintf(stderr, "PTT OFF\n");
	if (ptt_fd >= 0) close(ptt_fd);
	free(pcm);

	if (rc == 0) {
		fprintf(stderr, "Transmitted: addr=%u baud=%u %s\n",
		        address, baud,
		        tone_only ? "(tone)" : text);
	}
	return rc;
}
