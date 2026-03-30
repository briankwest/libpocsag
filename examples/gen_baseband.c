/*
 * gen_baseband - Generate POCSAG baseband NRZ WAV files using
 *                the library's built-in pocsag_baseband() function.
 *
 * Generates the full matrix: 3 baud rates × 4 sample rates.
 * Output: 16-bit mono PCM WAV.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <libpocsag/pocsag.h>

#define MIN_SPB  5

/* ---- minimal WAV writer ---- */

static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

static int write_wav(const char *path, uint32_t sr,
                     const float *samples, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return -1; }

	uint32_t db = (uint32_t)(n * 2);
	fwrite("RIFF", 1, 4, f); put32(f, 36 + db);
	fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f); put32(f, 16);
	put16(f, 1); put16(f, 1);
	put32(f, sr); put32(f, sr * 2);
	put16(f, 2); put16(f, 16);
	fwrite("data", 1, 4, f); put32(f, db);

	for (size_t i = 0; i < n; i++) {
		float s = samples[i];
		if (s >  1.0f) s =  1.0f;
		if (s < -1.0f) s = -1.0f;
		int16_t pcm = (int16_t)(s * 32767.0f);
		fwrite(&pcm, 2, 1, f);
	}

	fclose(f);
	return 0;
}

/* ----------------------------------------------------------------- */

#define MAX_SAMPLES (1024 * 1024)
static float audio[MAX_SAMPLES];

static int generate(uint32_t sr, uint32_t baud, const char *dir)
{
	pocsag_encoder_t enc;
	pocsag_encoder_init(&enc);
	pocsag_encoder_add(&enc, 1234, POCSAG_FUNC_NUMERIC,
	                   POCSAG_MSG_NUMERIC, "5551234");
	pocsag_encoder_add(&enc, 56789, POCSAG_FUNC_ALPHA,
	                   POCSAG_MSG_ALPHA, "Hello from libpocsag!");
	pocsag_encoder_add(&enc, 100, POCSAG_FUNC_TONE1,
	                   POCSAG_MSG_TONE_ONLY, NULL);

	uint8_t bitstream[POCSAG_BITSTREAM_MAX];
	size_t bs_len = 0, bs_bits = 0;
	pocsag_err_t err = pocsag_encode(&enc, bitstream, sizeof(bitstream),
	                                 &bs_len, &bs_bits);
	if (err != POCSAG_OK) {
		fprintf(stderr, "encode failed: %s\n", pocsag_strerror(err));
		return -1;
	}

	/* 0.25s silence + baseband + 0.25s silence */
	size_t pad = (size_t)(sr * 0.25f);
	memset(audio, 0, pad * sizeof(float));

	size_t sig_len = 0;
	err = pocsag_baseband(bitstream, bs_bits, sr, baud,
	                      audio + pad, MAX_SAMPLES - pad * 2, &sig_len);
	if (err != POCSAG_OK) {
		fprintf(stderr, "baseband failed: %s\n", pocsag_strerror(err));
		return -1;
	}

	size_t total = pad + sig_len + pad;
	memset(audio + pad + sig_len, 0, pad * sizeof(float));

	char path[256];
	snprintf(path, sizeof(path), "%s/pocsag_%ubaud_%uhz.wav",
	         dir, baud, sr);

	if (write_wav(path, sr, audio, total) < 0)
		return -1;

	printf("  %-48s  %6zu samples  %.2fs\n",
	       path, total, (double)total / sr);
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "baseband";
	mkdir(dir, 0755);

	static const uint32_t srates[] = { 8000, 16000, 32000, 48000 };
	static const uint32_t bauds[]  = { 512, 1200, 2400 };
	int ok = 0, skip = 0, total = 0;

	printf("Generating POCSAG baseband NRZ WAV files in %s/\n\n", dir);

	for (int b = 0; b < 3; b++) {
		for (int s = 0; s < 4; s++) {
			if (srates[s] / bauds[b] < MIN_SPB) {
				printf("  skip %u/%u (%.1f spb)\n",
				       srates[s], bauds[b],
				       (double)srates[s] / bauds[b]);
				skip++;
				continue;
			}
			total++;
			if (generate(srates[s], bauds[b], dir) == 0)
				ok++;
		}
		printf("\n");
	}

	printf("%d/%d files written, %d skipped\n", ok, total, skip);
	return ok == total ? 0 : 1;
}
