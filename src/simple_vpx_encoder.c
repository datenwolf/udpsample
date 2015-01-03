/*
  Copyright (c) 2010 The WebM project authors. All Rights Reserved.

  Use of this source code is governed by a BSD-style license
  that can be found in the LICENSE file in the root of the source
  tree. An additional intellectual property rights grant can be found
  in the file PATENTS.  All contributing project authors may
  be found in the AUTHORS file in the root of the source tree.
 */
 
 
/*
 This is an example of a simple encoder loop. It takes an input file in
 YV12 format, passes it through the encoder, and writes the compressed
 frames to disk in IVF format. Other decoder examples build upon this
 one.
 
 The details of the IVF format have been elided from this example for
 simplicity of presentation, as IVF files will not generally be used by
 your application. In general, an IVF file consists of a file header,
 followed by a variable number of frames. Each frame consists of a frame
 header followed by a variable length payload. The length of the payload
 is specified in the first four bytes of the frame header. The payload is
 the raw compressed data.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define VPX_CODEC_DISABLE_COMPAT 1
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#define vpx_codec_interface (vpx_codec_vp8_cx())

#define VP8_FOURCC 0x30385056
#define VP9_FOURCC 0x30395056

#define ivf_fourcc VP8_FOURCC

#include <libuvc/libuvc.h>
#include <libyuv.h>

#include "debug_util.h"
#include "ivf.h"
 
FILE *outfile;

struct rsFrameDatagramHeader {
	unsigned  width:  12;
	unsigned  height: 12; 
	unsigned  counter: 7;  /* rolling frame counter */
	unsigned  lefteye: 1;  /* this is a frame for the left eye */
	float     imu_quat[4]; /* orientation reported by the IMU */
	/* the size is implied from the datagram size minus the headers */
};

pthread_cond_t  frame_cnd;
pthread_mutex_t frame_mtx;

struct uvc_to_vpx_context {
	vpx_codec_ctx_t codec;
	vpx_image_t     frame;

	uvc_device_t        *uvc_dev;
	uvc_device_handle_t *uvc_devh;
	uvc_stream_ctrl_t    uvc_ctrl;

	unsigned int frame_cnt;
	unsigned int width;
	unsigned int height;
};

int32_t fourcc_from_uvcformat(enum uvc_frame_format const uvcformat)
{
	uint32_t fourcc = 0;
	switch(uvcformat) {
	default:
	case UVC_FRAME_FORMAT_ANY:
		return -1;

	case UVC_FRAME_FORMAT_YUYV:  fourcc = FOURCC_YUYV; break;
	case UVC_FRAME_FORMAT_RGB:   fourcc = FOURCC_RAW;  break;
	case UVC_FRAME_FORMAT_MJPEG: fourcc = FOURCC_MJPG; break;
	}

	return CanonicalFourCC(fourcc);
}

void frame_callback(uvc_frame_t *frame, void *ptr)
{
	struct uvc_to_vpx_context *uvpx = ptr;
	
	uvc_error_t ret;

	if( ConvertToI420(
		frame->data, frame->data_bytes,
		uvpx->frame.planes[0], uvpx->frame.stride[0],
		uvpx->frame.planes[1], uvpx->frame.stride[1],
		uvpx->frame.planes[2], uvpx->frame.stride[2],
		0, 0, /* crop x, y */
		frame->width, frame->height,
		uvpx->frame.w, uvpx->frame.h,
		0, /* rotation */
		fourcc_from_uvcformat(frame->frame_format) )
	) {
		fputs("colourspace conversion failed\n", stderr);
	}

	pthread_mutex_lock(&frame_mtx);

	if( vpx_codec_encode(
		&uvpx->codec,
		&uvpx->frame,
		get_time(),
		1,
		0,
		VPX_DL_REALTIME )
	) {
		die_codec(&uvpx->codec, "Failed to encode frame");
	}

	vpx_codec_iter_t iter = NULL;
	const vpx_codec_cx_pkt_t *pkt;
	while( (pkt = vpx_codec_get_cx_data(
		&uvpx->codec,
		&iter ))
	) {
		switch(pkt->kind) {
		default: break;

		case VPX_CODEC_CX_FRAME_PKT:
			write_ivf_frame_header(outfile, pkt);
			fwrite(	pkt->data.frame.buf,
				1,
				pkt->data.frame.sz,
				outfile );
			break;
		}
		fputc(	   (pkt->kind == VPX_CODEC_CX_FRAME_PKT)
			&& (pkt->data.frame.flags & VPX_FRAME_IS_KEY) ? 'K' : '.',
			stdout);
		fflush(stdout);
	}
	++uvpx->frame_cnt;

	pthread_cond_broadcast(&frame_cnd);
	pthread_mutex_unlock(&frame_mtx);

	return;
}


 
int main(int argc, char **argv)
{
	uvc_context_t *uvc_ctx;
	struct uvc_to_vpx_context uvpx = {0};

	vpx_codec_enc_cfg_t  cfg;
	vpx_codec_err_t      res;
 
	/* Open files */
	if(argc!=4)
		die("Usage: %s <width> <height> <outfile>\n", argv[0]);

	uvpx.width  = strtol(argv[1], NULL, 0);
	uvpx.height = strtol(argv[2], NULL, 0);

	if( uvpx.width  < 16
	 || uvpx.width  % 2
	 || uvpx.height < 16
	 || uvpx.height % 2 ) {
     		die("Invalid resolution: %ldx%ld", uvpx.width, uvpx.height);
	}

	if( !vpx_img_alloc(
		&uvpx.frame,
		VPX_IMG_FMT_I420,
		uvpx.width,
		uvpx.height,
		1)
	) {
		die("Failed to allocate image", uvpx.width, uvpx.height);
	}

	if( !(outfile = fopen(argv[3], "wb")) ) {
		die("Failed to open %s for writing", argv[4]);
	}
 
	printf("Using %s\n",
	       vpx_codec_iface_name(vpx_codec_interface) );
 
	/* Populate encoder configuration */
	if( (res = vpx_codec_enc_config_default(
		vpx_codec_interface,
		&cfg,
		0))
	) {
		printf("Failed to get config: %s\n", vpx_codec_err_to_string(res));
		return EXIT_FAILURE;
	}
 
	/* Update the default configuration with our settings */
	cfg.rc_target_bitrate =
		( uvpx.width
		* uvpx.height
		* 2 * cfg.rc_target_bitrate )
		/ ( cfg.g_w * cfg.g_h );

	cfg.g_w = uvpx.width;
	cfg.g_h = uvpx.height;
	cfg.rc_end_usage = VPX_CBR;
	cfg.g_pass = VPX_RC_ONE_PASS;
	cfg.g_lag_in_frames = 0;
	cfg.g_timebase.num = 1;
	cfg.g_timebase.den = 1000;
	cfg.rc_min_quantizer = 10;
	cfg.rc_max_quantizer = 40;
	cfg.rc_dropframe_thresh = 1;
	cfg.rc_buf_optimal_sz = 1000;
	cfg.rc_buf_initial_sz = 1000;
	cfg.rc_buf_sz = 1000;
	cfg.g_error_resilient = 1;
	cfg.kf_mode = VPX_KF_AUTO;
	cfg.kf_max_dist = 150;
	cfg.g_threads = 2;
 
	uvc_init(&uvc_ctx, NULL);
	uvc_find_device(
		uvc_ctx,
		&uvpx.uvc_dev,
		0, 0, NULL);
	uvc_open(uvpx.uvc_dev, &uvpx.uvc_devh);
	uvc_print_diag(uvpx.uvc_devh, stderr);

	write_ivf_file_header(outfile, &cfg, 0);

        /* Initialize codec */
        if( vpx_codec_enc_init(
		&uvpx.codec,
		vpx_codec_interface,
		&cfg,
		0)
	) {
            die_codec(&uvpx.codec, "Failed to initialize encoder");
	}

	uvc_get_stream_ctrl_format_size(
		uvpx.uvc_devh, &uvpx.uvc_ctrl,
		UVC_FRAME_FORMAT_MJPEG,
		uvpx.width, uvpx.height,
		0 );
	uvc_print_stream_ctrl(&uvpx.uvc_ctrl, stderr);
	uvc_start_streaming(
		uvpx.uvc_devh,
		&uvpx.uvc_ctrl,
		frame_callback,
		&uvpx,
		0 );

	while(!_kbhit());

	uvc_stop_streaming(uvpx.uvc_devh);
	uvc_close(uvpx.uvc_devh);
	uvc_unref_device(uvpx.uvc_dev);
 
	printf("\nProcessed %d frames.\n",
	       uvpx.frame_cnt-1);

	vpx_img_free(&uvpx.frame);
	if( vpx_codec_destroy(&uvpx.codec) ) {
		die_codec(&uvpx.codec, "Failed to destroy codec");
	}

	/* Try to rewrite the file header with the actual frame count */
	if( !fseek(outfile, 0, SEEK_SET) ) {
		write_ivf_file_header(
			outfile,
			&cfg,
			uvpx.frame_cnt-1 );
	}
	fclose(outfile);

	uvc_exit(uvc_ctx);
	return EXIT_SUCCESS;
}
