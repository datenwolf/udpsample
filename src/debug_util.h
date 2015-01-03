#pragma once
#ifndef DEBUG_UTIL_H
#define DEBUG_UTIL_H

#include <vpx/vpx_encoder.h>

void die(const char *fmt, ...);
void die_codec(vpx_codec_ctx_t *ctx, const char *s);

#endif/*DEBUG_UTIL_H*/
