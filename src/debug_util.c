#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "debug_util.h"

void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	if(fmt[strlen(fmt)-1] != '\n') {
		fputc('\n', stderr);
	}
	exit(EXIT_FAILURE);
}
 
void die_codec(vpx_codec_ctx_t *ctx, const char *s)
{
	const char *detail = vpx_codec_error_detail(ctx);
	fprintf(stderr, "%s: %s\n", s, vpx_codec_error(ctx));
	if( detail ) {
		printf("    %s\n", detail);
	}
	exit(EXIT_FAILURE);
}
 
