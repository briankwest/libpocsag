#ifndef LIBPOCSAG_ERROR_H
#define LIBPOCSAG_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	POCSAG_OK           =  0,
	POCSAG_ERR_PARAM    = -1,
	POCSAG_ERR_OVERFLOW = -2,
	POCSAG_ERR_BADCHAR  = -3,
	POCSAG_ERR_BCH      = -4,
	POCSAG_ERR_SYNC     = -5,
	POCSAG_ERR_STATE    = -6
} pocsag_err_t;

const char *pocsag_strerror(pocsag_err_t err);

#ifdef __cplusplus
}
#endif

#endif
