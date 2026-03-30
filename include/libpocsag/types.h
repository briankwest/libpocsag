#ifndef LIBPOCSAG_TYPES_H
#define LIBPOCSAG_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POCSAG_SYNC_CODEWORD    0x7CD215D8u
#define POCSAG_IDLE_CODEWORD    0x7A89C197u
#define POCSAG_PREAMBLE_BITS    576
#define POCSAG_FRAMES_PER_BATCH 8
#define POCSAG_SLOTS_PER_FRAME  2
#define POCSAG_CODEWORDS_PER_BATCH 16
#define POCSAG_BCH_POLY         0x769u

#define POCSAG_MSG_MAX          256
#define POCSAG_TX_MAX_MESSAGES  16
#define POCSAG_BITSTREAM_MAX    4096

typedef enum {
	POCSAG_MSG_NUMERIC   = 0,
	POCSAG_MSG_ALPHA     = 1,
	POCSAG_MSG_TONE_ONLY = 2
} pocsag_msg_type_t;

typedef enum {
	POCSAG_FUNC_NUMERIC  = 0,
	POCSAG_FUNC_TONE1    = 1,
	POCSAG_FUNC_TONE2    = 2,
	POCSAG_FUNC_ALPHA    = 3
} pocsag_func_t;

typedef struct {
	uint32_t          address;
	pocsag_func_t     function;
	pocsag_msg_type_t type;
	char              text[POCSAG_MSG_MAX];
	size_t            text_len;
} pocsag_msg_t;

#ifdef __cplusplus
}
#endif

#endif
