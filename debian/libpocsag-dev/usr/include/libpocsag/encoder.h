#ifndef LIBPOCSAG_ENCODER_H
#define LIBPOCSAG_ENCODER_H

#include "error.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	pocsag_msg_t messages[POCSAG_TX_MAX_MESSAGES];
	size_t       count;
} pocsag_encoder_t;

void         pocsag_encoder_init(pocsag_encoder_t *enc);
void         pocsag_encoder_reset(pocsag_encoder_t *enc);

pocsag_err_t pocsag_encoder_add(pocsag_encoder_t *enc,
                                uint32_t address,
                                pocsag_func_t function,
                                pocsag_msg_type_t type,
                                const char *text);

pocsag_err_t pocsag_encode(const pocsag_encoder_t *enc,
                           uint8_t *out, size_t out_cap,
                           size_t *out_len, size_t *out_bits);

pocsag_err_t pocsag_encode_single(uint32_t address,
                                  pocsag_func_t function,
                                  pocsag_msg_type_t type,
                                  const char *text,
                                  uint8_t *out, size_t out_cap,
                                  size_t *out_len, size_t *out_bits);

#ifdef __cplusplus
}
#endif

#endif
