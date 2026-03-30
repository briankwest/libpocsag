#include "libpocsag/error.h"

const char *pocsag_strerror(pocsag_err_t err)
{
	switch (err) {
	case POCSAG_OK:           return "success";
	case POCSAG_ERR_PARAM:    return "invalid parameter";
	case POCSAG_ERR_OVERFLOW: return "buffer overflow";
	case POCSAG_ERR_BADCHAR:  return "invalid character";
	case POCSAG_ERR_BCH:      return "uncorrectable BCH error";
	case POCSAG_ERR_SYNC:     return "sync lost";
	case POCSAG_ERR_STATE:    return "invalid state";
	}
	return "unknown error";
}
