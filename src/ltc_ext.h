#ifndef LTC_EXT_H
#define LTC_EXT_H 1

#include "ltc.h"

void ltc_decoder_write_double(LTCDecoder *d, double *buf, size_t size, ltc_off_t posinfo);

#endif