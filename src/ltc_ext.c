#include "ltc.c"

#define LTC_CONVERSION_BUF_SIZE 1024

LTCWRITE_TEMPLATE(double, double, 128 + (buf[copyStart + i] * 127.0))

#undef LTC_CONVERSION_BUF_SIZE
