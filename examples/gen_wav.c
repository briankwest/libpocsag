/*
 * gen_wav - Generate POCSAG test WAV files at every supported
 *           sample-rate / baud-rate combination.
 *
 * Output directory: ./wav/ (created automatically)
 *
 * Each file is 16-bit mono PCM and named:
 *   pocsag_<baud>baud_<srate>hz.wav
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <libpocsag/pocsag.h>

/* ---- minimal WAV writer ----------------------------------------- */

static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

static int write_wav(const char *path, uint32_t sr,
                     const float *samples, size_t nsamples)
{
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return -1; }

	uint32_t data_bytes = (uint32_t)(nsamples * 2);   /* 16-bit mono */
	uint32_t file_size  = 36 + data_bytes;

	/* RIFF header */
	fwrite("RIFF", 1, 4, f);
	put32(f, file_size);
	fwrite("WAVE", 1, 4, f);

	/* fmt chunk */
	fwrite("fmt ", 1, 4, f);
	put32(f, 16);           /* chunk size */
	put16(f, 1);            /* PCM */
	put16(f, 1);            /* mono */
	put32(f, sr);           /* sample rate */
	put32(f, sr * 2);       /* byte rate (sr * channels * bps/8) */
	put16(f, 2);            /* block align (channels * bps/8) */
	put16(f, 16);           /* bits per sample */

	/* data chunk */
	fwrite("data", 1, 4, f);
	put32(f, data_bytes);

	for (size_t i = 0; i < nsamples; i++) {
		float s = samples[i];
		if (s >  1.0f) s =  1.0f;
		if (s < -1.0f) s = -1.0f;
		int16_t pcm = (int16_t)(s * 32767.0f);
		fwrite(&pcm, 2, 1, f);
	}

	fclose(f);
	return 0;
}

/* ---- pad silence before and after ------------------------------- */

static size_t prepend_silence(float *buf, size_t cap, uint32_t sr,
                              float seconds)
{
	size_t n = (size_t)(sr * seconds);
	if (n > cap) n = cap;
	for (size_t i = 0; i < n; i++)
		buf[i] = 0.0f;
	return n;
}

/* ----------------------------------------------------------------- */

#define MAX_AUDIO (1024 * 1024)   /* 1 M samples */
static float audio[MAX_AUDIO];

static int generate(uint32_t sr, uint32_t baud, const char *dir)
{
	/* encode three messages */
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
		fprintf(stderr, "encode failed: %s\n", pocsag_strerror(err));
		return -1;
	}

	/* 0.25 s silence → modulated signal → 0.25 s silence */
	pocsag_mod_t mod;
	pocsag_mod_init(&mod, sr, baud);

	size_t off = prepend_silence(audio, MAX_AUDIO, sr, 0.25f);

	size_t sig_len = 0;
	err = pocsag_modulate(&mod, bitstream, bs_bits,
	                      audio + off, MAX_AUDIO - off, &sig_len);
	if (err != POCSAG_OK) {
		fprintf(stderr, "modulate failed (%u/%u): %s\n",
		        sr, baud, pocsag_strerror(err));
		return -1;
	}
	off += sig_len;

	size_t tail = prepend_silence(audio + off, MAX_AUDIO - off,
	                              sr, 0.25f);
	off += tail;

	/* write wav */
	char path[256];
	snprintf(path, sizeof(path), "%s/pocsag_%ubaud_%uhz.wav",
	         dir, baud, sr);

	if (write_wav(path, sr, audio, off) < 0)
		return -1;

	double dur = (double)off / sr;
	printf("  %-42s  %6zu samples  %.2fs  mark=%u space=%u\n",
	       path, off, dur, (unsigned)mod.mark_freq,
	       (unsigned)mod.space_freq);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "wav";

	mkdir(dir, 0755);

	static const uint32_t srates[] = { 8000, 16000, 32000, 48000 };
	static const uint32_t bauds[]  = { 512, 1200, 2400 };
	int ok = 0, skip = 0;

	printf("Generating POCSAG WAV test files in %s/\n\n", dir);

	for (int b = 0; b < 3; b++) {
		for (int s = 0; s < 4; s++) {
			if (srates[s] / bauds[b] < 5) {
				printf("  skip %u/%u (%.1f spb < 5)\n",
				       srates[s], bauds[b],
				       (double)srates[s] / bauds[b]);
				skip++;
				continue;
			}
			if (generate(srates[s], bauds[b], dir) == 0)
				ok++;
		}
		printf("\n");
	}

	printf("%d files written, %d skipped\n", ok, skip);
	return ok > 0 ? 0 : 1;
}
