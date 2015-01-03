#pragma once
#ifndef IVF_H
#define IVF_H

#include <stdio.h>

#include <vpx/vpx_encoder.h>

void ivf_write_file_header(
	FILE *outfile,
	const vpx_codec_enc_cfg_t *cfg,
	int frame_cnt );

void ivf_write_frame_header(
	FILE *outfile,
	const vpx_codec_cx_pkt_t *pkt );

#endif/*IVF_H*/
