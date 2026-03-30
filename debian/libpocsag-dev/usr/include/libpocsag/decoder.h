#ifndef LIBPOCSAG_DECODER_H
#define LIBPOCSAG_DECODER_H

#include "error.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pocsag_msg_cb_t)(const pocsag_msg_t *msg, void *user);

typedef enum {
	POCSAG_DEC_HUNTING = 0,
	POCSAG_DEC_BATCH   = 1
} pocsag_dec_state_t;

#define POCSAG_DEC_MAX_DATA 40

typedef struct {
	pocsag_msg_cb_t     callback;
	void               *user;

	pocsag_dec_state_t  state;
	uint32_t            shift_reg;

	uint32_t            cw_accum;
	int                 cw_bits;
	int                 cw_count;

	int                 msg_active;
	uint32_t            msg_address;
	pocsag_func_t       msg_function;
	uint32_t            msg_data[POCSAG_DEC_MAX_DATA];
	int                 msg_data_count;

	uint32_t            stat_codewords;
	uint32_t            stat_corrected;
	uint32_t            stat_errors;
	uint32_t            stat_messages;
} pocsag_decoder_t;

void         pocsag_decoder_init(pocsag_decoder_t *dec,
                                 pocsag_msg_cb_t callback, void *user);
void         pocsag_decoder_reset(pocsag_decoder_t *dec);
pocsag_err_t pocsag_decoder_feed_bits(pocsag_decoder_t *dec,
                                      const uint8_t *bits, size_t count);
pocsag_err_t pocsag_decoder_feed_bytes(pocsag_decoder_t *dec,
                                       const uint8_t *data, size_t nbytes);
void         pocsag_decoder_flush(pocsag_decoder_t *dec);

#ifdef __cplusplus
}
#endif

#endif
