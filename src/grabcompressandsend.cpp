/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


/*
 * This example illustrates using VP8 in a packet loss scenario by xmitting
 * video over UDP with Forward Error Correction,  Packet Resend, and
 * some Unique VP8 functionality.
 *
 */

#include "vpx_network.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>  //for tolower
#include <string.h>
#include <pthread.h>

extern "C" {
#include "rtp.h"
#define VPX_CODEC_DISABLE_COMPAT 1
#include "vpx/vpx_encoder.h"
#include "vpx/vp8cx.h"
#include <libuvc/libuvc.h>
}


const int size_buffer = 1680;
#define FAIL_ON_NONZERO(x) if ((x)) { vpxlog_dbg(ERRORS, # x "\n"); return -1; }
#define FAIL_ON_ZERO(x) if (!(x)) { vpxlog_dbg(ERRORS, # x "\n"); return -1; }
#define FAIL_ON_NEGATIVE(x) if ((x) < 0) { vpxlog_dbg(ERRORS, # x "\n"); return -1; }

bool buffer_has_frame = false;
long long last_time_in_nanoseconds = 0;
int request_recovery = 1;
vpx_codec_ctx_t encoder;
int gold_recovery_seq = 0;
int altref_recovery_seq = 0;

int display_width = 800;
int display_height = 600;
int capture_frame_rate = 30;
int video_bitrate = 400;
int fec_numerator = 6;
int fec_denominator = 5;
unsigned short send_port = 1407;
unsigned short recv_port = 1408;

#define PS 2048
#define PSM  (PS - 1)
#define MAX_NUMERATOR 16

typedef enum {
	NONE,
	XOR,
	RS
} FEC_TYPE;

typedef struct {
	unsigned int	size;
	FEC_TYPE	fecType;
	unsigned int	fec_numerator;
	unsigned int	fec_denominator;
	unsigned int	new_fec_denominator;
	unsigned int	count;
	unsigned int	add_ptr;
	unsigned int	send_ptr;
	unsigned int	max;
	unsigned int	fec_count;
	unsigned short	seq;
	PACKET		packet[PS];
} PACKETIZER;

PACKETIZER packetizer;
tc8 one_packet[8192];

void ctx_exit_on_error(vpx_codec_ctx_t *ctx, const char *s)
{
	if (ctx->err) {
		vpxlog_dbg(FRAME, "%s: %s\n", s, vpx_codec_error(ctx));
		// exit(EXIT_FAILURE);
	}
}


int create_packetizer(
	PACKETIZER  *packetizer,
	FEC_TYPE     fecType,
	unsigned int fec_numerator,
	unsigned int fec_denominator )
{
	packetizer->size = PACKET_SIZE;
	packetizer->fecType = fecType;
	packetizer->fec_numerator = fec_numerator;
	packetizer->fec_denominator = fec_denominator;
	packetizer->new_fec_denominator = fec_denominator;
	packetizer->max       = PS;
	packetizer->count     = 0;
	packetizer->add_ptr   = 0;
	packetizer->send_ptr  = 0;
	packetizer->fec_count = packetizer->fec_denominator;

	packetizer->seq = 7;
	packetizer->send_ptr  = packetizer->add_ptr = (packetizer->seq & PSM);
	return 0; // SUCCESS
}

int make_redundant_packet (
	PACKETIZER   *p,
	unsigned int  end_frame,
	unsigned int  time,
	unsigned int  frametype )
{
	long long *in[MAX_NUMERATOR];
	long long *out = (long long *)p->packet[p->add_ptr].data;
	unsigned int i, j;
	unsigned int max_size = 0;
	unsigned int max_round;

	// make a number of exact duplicates of this packet
	if (p->fec_denominator == 1) {
		int dups = p->fec_numerator - p->fec_denominator;
		void *duplicand = (void *)&p->packet[(p->add_ptr - 1) & PSM];

		while (dups) {
			memcpy((void *)&p->packet[p->add_ptr], duplicand, sizeof(PACKET));
			dups--;
			p->add_ptr++;
			p->add_ptr &= PSM;
		}

		p->fec_denominator = p->new_fec_denominator;
		p->fec_count = p->fec_denominator;
		p->count++;
		return 0;
	}

	p->packet[p->add_ptr].timestamp = time;
	p->packet[p->add_ptr].seq = p->seq;
	p->packet[p->add_ptr].size = max_size;
	p->packet[p->add_ptr].type = XORPACKET;
	p->packet[p->add_ptr].redundant_count = p->fec_denominator;
	p->packet[p->add_ptr].new_frame = 0;
	p->packet[p->add_ptr].end_frame = end_frame;
	p->packet[p->add_ptr].frame_type = frametype;

	// find address of last denominator packets data store in in ptr
	for (i = 0; i < p->fec_denominator; i++) {
		int ptr = ((p->add_ptr - i - 1) & PSM);
		in[i] = (long long *)p->packet[ptr].data;;
		max_size = (max_size > p->packet[ptr].size ? max_size : p->packet[ptr].size);
	}

	// go through a full packet size
	max_round = (max_size + sizeof(long long) - 1) / sizeof(long long);

	for (j = 0; j < max_round; j++) {
		// start with the most recent packet
		*out = *(in[0]);

		// xor all the older packets with out
		for (i = 1; i < p->fec_denominator; i++) {
			*out ^= *(in[i]);
			in[i]++;
		}

		in[0]++;
		out++;
	}

	p->seq++;

	// move to the next packet
	p->add_ptr++;
	p->add_ptr &= PSM;

	// add one to our packet count
	p->count++;

	if (p->count > p->max)
		return -1;  // filled up our packet buffer

	p->fec_denominator = p->new_fec_denominator;
	p->fec_count = p->fec_denominator;
	return 0;
}

int packetize(
	PACKETIZER    *p,
	unsigned int   time,
	unsigned char *data,
	unsigned int   size,
	unsigned int   frame_type )
{
	int new_frame = 1;

	// more bytes to copy around
	while (size > 0) {
		unsigned int psize = (p->size < size ? p->size : size);
		p->packet[p->add_ptr].timestamp = time;
		p->packet[p->add_ptr].seq = p->seq;
		p->packet[p->add_ptr].size = psize;
		p->packet[p->add_ptr].type = DATAPACKET;

		if (p->fec_denominator == 1)
			p->packet[p->add_ptr].redundant_count = 2;
		else
			p->packet[p->add_ptr].redundant_count = p->fec_count;

		p->packet[p->add_ptr].new_frame = new_frame;
		p->packet[p->add_ptr].frame_type = frame_type;
		//vpxlog_dbg(SKIP, "%c", (frame_type==NORMAL?'N':'O'));

		new_frame = 0;

		memcpy(p->packet[p->add_ptr].data, data, psize);

		// make sure rest of packet is 0'ed out for redundancy if necessary.
		if (size < p->size)
			memset(p->packet[p->add_ptr].data + psize, 0, p->size - psize);

		data += psize;
		size -= psize;
		p->packet[p->add_ptr].end_frame = (size == 0);

		p->seq++;
		p->add_ptr++;
		p->add_ptr &= PSM;

		p->count++;

		if (p->count > p->max)
			return -1;  // filled up our packet buffer

		// time for redundancy?
		p->fec_count--;

		if (!p->fec_count)
			make_redundant_packet(p, (size == 0), time, frame_type);
	}

	return 0;
}

int send_packet(PACKETIZER *p, struct vpxsocket *vpxSock, union vpx_sockaddr_x address)
{
	TCRV rc;
	tc32 bytes_sent;

	if (p->send_ptr == p->add_ptr)
		return -1;

	p->packet[p->send_ptr].ssrc = 411;
	vpxlog_dbg(LOG_PACKET,
		"Sent Packet %d, %d, %d : new=%d \n",
		p->packet[p->send_ptr].seq,
		p->packet[p->send_ptr].timestamp,
		p->packet[p->send_ptr].frame_type,
		p->packet[p->send_ptr].new_frame );

	rc = vpx_net_sendto(vpxSock,
		(tc8 *)&p->packet[p->send_ptr],
		PACKET_HEADER_SIZE + p->packet[p->send_ptr].size,
		&bytes_sent,
		address );

	p->send_ptr++;
	p->send_ptr &= PSM;
	p->count--;

	return 0;
}




unsigned int const recovery_flags[] = {
	0,                                              //   NORMAL,
	VPX_EFLAG_FORCE_KF,                             //   KEY,
	VP8_EFLAG_FORCE_GF | VP8_EFLAG_NO_UPD_ARF |
	VP8_EFLAG_NO_REF_LAST | VP8_EFLAG_NO_REF_ARF,   //   GOLD = 2,
	VP8_EFLAG_FORCE_ARF | VP8_EFLAG_NO_UPD_GF |
	VP8_EFLAG_NO_REF_LAST | VP8_EFLAG_NO_REF_GF     //   ALTREF = 3
};

uvc_context_t       *uvc_ctx;
uvc_error_t          uvc_res;
uvc_device_t        *uvc_dev;
uvc_device_handle_t *uvc_devh;
uvc_stream_ctrl_t    uvc_ctrl;

pthread_cond_t  frame_cnd;
pthread_mutex_t frame_mtx;

unsigned i_frame = 0;

void frame_callback(uvc_frame_t *frame, void *ptr) {
	uvc_frame_t *rgb;
	uvc_error_t ret;

	rgb = uvc_allocate_frame(frame->width * frame->height * 3);
	if (!rgb) {
		printf("unable to allocate rgb frame!");
		return;
	}

	ret = uvc_mjpeg2rgb(frame, rgb);
	if (ret) {
		uvc_free_frame(rgb);
		uvc_perror(ret, "uvc_mjpeg2rgb");
		return;
	}

	pthread_mutex_lock(&frame_mtx);

	// do we have room in our packet store for a frame
	if( packetizer.add_ptr - packetizer.send_ptr < 20 ) {
		fprintf(stderr, "frame[%3d]> %d %d %d: %06x",
			(int)i_frame % 1000,
			(int)rgb->width,
			(int)rgb->height,
			(int)rgb->step,
			*((unsigned int*)rgb->data));

		int const flags = recovery_flags[request_recovery];

	/* XXX: I don't care that the frame data is RGB, but the encoder
	 *      is configured for a planar format. I just want to see
	 *      something to fall out of the encoder at all.
	 *      
	 *      Once I've got that I can move on to properly convert
	 *      between color spaces / chroma subsampling. */
		vpx_image_t vpx_img;
		vpx_img.fmt = VPX_IMG_FMT_I420;
		vpx_img.bps = 8;
		vpx_img.d_w = rgb->width,
		vpx_img.d_h = rgb->height,
		vpx_img.w   = rgb->width, // TODO: something something stride
		vpx_img.h   = rgb->height,
		vpx_img.x_chroma_shift = 1;
		vpx_img.y_chroma_shift = 1;
		vpx_img.self_allocd    = 0;
		vpx_img.img_data_owner = 0;
		vpx_img.img_data  = (unsigned char*)rgb->data;
		vpx_img.planes[0] = (unsigned char*)rgb->data;
		vpx_img.planes[1] = (unsigned char*)rgb->data;
		vpx_img.planes[2] = (unsigned char*)rgb->data;
		vpx_img.planes[3] = (unsigned char*)rgb->data;
		vpx_img.stride[0] = rgb->width;
		vpx_img.stride[1] = rgb->width;
		vpx_img.stride[2] = rgb->width;
		vpx_img.stride[3] = rgb->width;

		if( VPX_CODEC_OK != vpx_codec_encode(&encoder,
			&vpx_img,
			i_frame,
			1,
			flags,
			VPX_DL_REALTIME )
		) {
			fputc('!', stderr);
		}
		fprintf(stderr, " %s ", vpx_codec_error(&encoder));

		vpx_codec_iter_t iter;
		vpx_codec_cx_pkt_t const *pkt;
		while( (pkt = vpx_codec_get_cx_data(&encoder, &iter)) ) {
			fputc('.', stderr);
			if( pkt->kind == VPX_CODEC_CX_FRAME_PKT ) {
				int const frame_type = request_recovery;

				// a recovery frame was requested move sendptr to current ptr, so that we
				// don't spend datarate sending packets that won't be used.
				if( request_recovery ) {
					packetizer.send_ptr = packetizer.add_ptr;
					request_recovery = 0;
				}

				if( frame_type == GOLD
				 || frame_type == KEY) {
					gold_recovery_seq = packetizer.seq;
				}

				if( frame_type == ALTREF
				 || frame_type == KEY ) {
					altref_recovery_seq = packetizer.seq;
				}

				packetize(&packetizer,
					i_frame,
					(unsigned char *)pkt->data.frame.buf,
					pkt->data.frame.sz,
					frame_type);

				vpxlog_dbg(FRAME,
					"Frame %d %d %d %10.4g\n",
					packetizer.packet[packetizer.send_ptr].seq,
					pkt->data.frame.sz,
					packetizer.packet[packetizer.send_ptr].timestamp,
					gold_recovery_seq );
			}
		}
		fprintf(stderr, " %s ", vpx_codec_error(&encoder));
		fputc('\n', stderr);

		i_frame++;

		uvc_free_frame(rgb);
	}

	pthread_cond_broadcast(&frame_cnd);
	pthread_mutex_unlock(&frame_mtx);

	return;
}

int start_capture(void)
{
	FAIL_ON_NEGATIVE( uvc_init(&uvc_ctx, NULL) );
	FAIL_ON_NEGATIVE( uvc_find_device(
		uvc_ctx,
		&uvc_dev,
		0, 0, NULL) );
	FAIL_ON_NEGATIVE( uvc_open(uvc_dev, &uvc_devh) );
	uvc_print_diag(uvc_devh, stderr);

	FAIL_ON_NEGATIVE( uvc_get_stream_ctrl_format_size(
		uvc_devh, &uvc_ctrl,
		UVC_FRAME_FORMAT_MJPEG,
		display_width, display_height,
		0 ) );
	uvc_print_stream_ctrl(&uvc_ctrl, stderr);
        
	FAIL_ON_NEGATIVE( uvc_start_streaming(
		uvc_devh,
		&uvc_ctrl,
		frame_callback,
		NULL, /* TODO: make this a per camera UVC/VPX context */
		0) );

	return 0;
}

int stop_capture(void)
{
	uvc_stop_streaming(uvc_devh);
	uvc_close(uvc_devh);
	uvc_unref_device(uvc_dev);
	uvc_exit(uvc_ctx);

	return 0;
}


int main(int argc, char *argv[])
{
	char ip[512];

	strncpy(ip, "127.0.0.1", 512);
	printf("GrabCompressAndSend: (-? for help) \n");

	vpx_codec_enc_cfg_t cfg;
	vpx_codec_enc_config_default(&vpx_codec_vp8_cx_algo, &cfg, 0);

	cfg.g_w = display_width;
	cfg.g_h = display_height;
#if 0
	cfg.rc_target_bitrate = video_bitrate;
	cfg.rc_end_usage = VPX_CBR;
	cfg.g_pass = VPX_RC_ONE_PASS;
	cfg.g_lag_in_frames = 0;
	cfg.rc_min_quantizer = 20;
	cfg.rc_max_quantizer = 50;
	cfg.rc_dropframe_thresh = 1;
	cfg.rc_buf_optimal_sz = 1000;
	cfg.rc_buf_initial_sz = 1000;
	cfg.rc_buf_sz = 1000;
	cfg.g_error_resilient = 1;
	cfg.kf_mode = VPX_KF_DISABLED;
	cfg.kf_max_dist = 999999;
	cfg.g_threads = 1;
#endif

	int cpu_used = -6;
	int static_threshold = 1200;

	if( pthread_mutex_init(&frame_mtx, NULL) 
	 || pthread_cond_init(&frame_cnd, NULL) ) {
		perror("mutex / cond init");
		return -1;
	}

	while (--argc > 0) {
		if (argv[argc][0] == '-') {
			switch (argv[argc][1]) {
			case 'm':
			case 'M':
				cfg.rc_dropframe_thresh = atoi(argv[argc-- + 1]);
				break;
			case 'c':
			case 'C':
				cpu_used = atoi(argv[argc-- + 1]);
				break;
			case 't':
			case 'T':
				static_threshold = atoi(argv[argc-- + 1]);
				break;
			case 'b':
			case 'B':
				cfg.rc_min_quantizer = atoi(argv[argc-- + 1]);
				break;
			case 'q':
			case 'Q':
				cfg.rc_max_quantizer = atoi(argv[argc-- + 1]);
				break;
			case 'i':
			case 'I':
				strncpy(ip, argv[argc-- + 1], 512);
				break;
			case 's':
			case 'S':
				send_port = atoi(argv[argc-- + 1]);
				break;
			case 'r':
			case 'R':
				recv_port = atoi(argv[argc-- + 1]);
				break;
			default:
				puts("========================: \n"
				     "Captures, compresses and sends video to ReceiveDecompressand play sample\n\n"
				     "-m [1] buffer level at which to drop frames 0 shuts it off \n"
				     "-c [12] amount of cpu to leave free of 16 \n"
				     "-t [1200] sad score below which is just a copy \n"
				     "-b [20] minimum quantizer ( best frame quality )\n"
				     "-q [52] maximum frame quantizer ( worst frame quality ) \n"
				     "-d [60] number of frames to drop at the start\n"
				     "-i [127.0.0.1]    Port to send data to. \n"
				     "-s [1408] port to send requests to\n"
				     "-r [1407] port to receive requests on. \n"
				     "\n");
				exit(0);
				break;
			}
		}
	}

	struct vpxsocket      vpx_socket, vpx_socket2;
	union  vpx_sockaddr_x address,    address2;

	TCRV rc;
	int bytes_read;
	int bytes_sent;

	vpx_net_init();

	// data send socket
	FAIL_ON_NONZERO(vpx_net_open(&vpx_socket, vpx_IPv4, vpx_UDP))
	FAIL_ON_NONZERO(vpx_net_get_addr_info(ip, send_port, vpx_IPv4, vpx_UDP, &address))

	// feedback socket
	FAIL_ON_NONZERO(vpx_net_open(&vpx_socket2, vpx_IPv4, vpx_UDP))
	vpx_net_set_read_timeout(&vpx_socket2, 0);
	rc = vpx_net_bind(&vpx_socket2, 0, recv_port);
	vpx_net_set_send_timeout(&vpx_socket, vpx_NET_NO_TIMEOUT);

	// make sure 2 way discussion taking place before getting started
	for(;;) {
		char init_packet[PACKET_SIZE] = "initiate call";
		rc = vpx_net_sendto(&vpx_socket, (tc8 *)&init_packet, PACKET_SIZE, &bytes_sent, address);
		Sleep(200);

		rc = vpx_net_recvfrom(&vpx_socket2, one_packet, sizeof(one_packet), &bytes_read, &address2);

		if (rc != TC_OK && rc != TC_WOULDBLOCK)
			vpxlog_dbg(LOG_PACKET, "error\n");

		if (bytes_read == -1)
			bytes_read = 0;

		if (bytes_read) {
			if (strncmp(one_packet, "configuration ", 14) == 0) {
				sscanf(one_packet + 14,
				       "%d %d %d %d %d %d",
				       &display_width,
				       &display_height,
				       &capture_frame_rate,
				       &video_bitrate,
				       &fec_numerator,
				       &fec_denominator);

				printf("Dimensions: %dx%-d %dfps %dkbps %d/%dFEC\n",
				       display_width,
				       display_height,
				       capture_frame_rate,
				       video_bitrate,
				       fec_numerator,
				       fec_denominator);
				break;
			}
		}
	}

	char init_packet[PACKET_SIZE] = "confirmed";
	rc = vpx_net_sendto(&vpx_socket, (tc8 *)&init_packet, PACKET_SIZE, &bytes_sent, address);
	fputs(init_packet, stderr);
	Sleep(200);
	rc = vpx_net_sendto(&vpx_socket, (tc8 *)&init_packet, PACKET_SIZE, &bytes_sent, address);
	fputs(init_packet, stderr);
	Sleep(200);
	rc = vpx_net_sendto(&vpx_socket, (tc8 *)&init_packet, PACKET_SIZE, &bytes_sent, address);
	fputs(init_packet, stderr);
	fputc('\n', stderr);

	cfg.g_w = display_width;
	cfg.g_h = display_height;

	vpx_codec_enc_init(&encoder, &vpx_codec_vp8_cx_algo, &cfg, 0);
	fprintf(stderr, "init codec: %s\n", vpx_codec_error(&encoder));

#if 0
	vpx_codec_control_(&encoder, VP8E_SET_CPUUSED, cpu_used);
	vpx_codec_control_(&encoder, VP8E_SET_STATIC_THRESHOLD, static_threshold);
	vpx_codec_control_(&encoder, VP8E_SET_ENABLEAUTOALTREF, 0);
#endif

	create_packetizer(&packetizer, XOR, fec_numerator, fec_denominator);

	start_capture();
	vpx_net_set_read_timeout(&vpx_socket2, 1);

	for (int i = 0;; ) {
		if( pthread_mutex_lock(&frame_mtx) ) {
			continue;
		}
		if( pthread_cond_wait(&frame_cnd, &frame_mtx) ) {
			pthread_mutex_unlock(&frame_mtx);
			continue;
		}

		// if there is nothing to send
		rc = vpx_net_recvfrom(&vpx_socket2,
			one_packet,
			sizeof(one_packet),
			&bytes_read,
			&address2 );

		if( rc != TC_OK
		 && rc != TC_WOULDBLOCK
		 && rc != TC_TIMEDOUT ) {
			vpxlog_dbg(LOG_PACKET, "error\n");
		}

		if( bytes_read == -1 ) {
			bytes_read = 0;
		}

		if( bytes_read ) {
			unsigned char command = one_packet[0];
			unsigned short seq = *((unsigned short *)(1 + one_packet));
			int bytes_sent;

			PACKET *tp = &packetizer.packet[seq & PSM];
			vpxlog_dbg(SKIP, "Command :%c Seq:%d FT:%c RecoverySeq:%d AltSeq:%d \n",
				   command,
				   seq,
				   (tp->frame_type == NORMAL ? 'N' : 'G'),
				   gold_recovery_seq,
				   altref_recovery_seq );

			// requested to resend a packet ( ignore if we are about to send a recovery frame)
			if( command == 'r'
			 && request_recovery == 0 ) {
				rc = vpx_net_sendto(&vpx_socket,
					(tc8 *)&packetizer.packet[seq & PSM],
					PACKET_HEADER_SIZE + packetizer.packet[seq & PSM].size,
					&bytes_sent,
					address );

				vpxlog_dbg(SKIP,
					"Sent recovery packet %c:%d, %d,%d\n",
					command,
					tp->frame_type,
					seq,
					tp->timestamp );
			}

			int recovery_seq = gold_recovery_seq;
			int recovery_type = GOLD;
			int other_recovery_seq = altref_recovery_seq;
			int other_recovery_type = ALTREF;

			if( (unsigned short)(recovery_seq - altref_recovery_seq > 32768) ) {
				recovery_seq = altref_recovery_seq;
				recovery_type = ALTREF;
				other_recovery_seq = gold_recovery_seq;
				other_recovery_type = GOLD;
			}

			// if requested to recover but seq is before recovery RESEND
			if( (unsigned short)(seq - recovery_seq) > 32768
			 || command != 'g' ) {
				rc = vpx_net_sendto(
					&vpx_socket,
					(tc8 *)&packetizer.packet[seq & PSM],
					PACKET_HEADER_SIZE + packetizer.packet[seq & PSM].size,
					&bytes_sent,
					address );
				vpxlog_dbg(SKIP,
					"Sent recovery packet %c:%d, %d,%d\n",
					command,
					tp->frame_type,
					seq,
					tp->timestamp );
			} else
			// requested  recovery frame and its a normal frame
			// packet that's lost and seq is after our recovery
			// frame so make a long term ref frame
			if( tp->frame_type == NORMAL
			 && (unsigned short)(seq - recovery_seq) > 0
			 && (unsigned short)(seq - recovery_seq) < 32768 ) {
				request_recovery = recovery_type;
				vpxlog_dbg(SKIP,
					"Requested recovery frame %c:%c,%d,%d\n",
					command,
					(recovery_type == GOLD ? 'G' : 'A'),
					packetizer.packet[gold_recovery_seq & PSM].frame_type,
					seq,
					packetizer.packet[gold_recovery_seq & PSM].timestamp );
			} else
			// so the other one is too old request a recovery frame from our older reference buffer.
			if( (unsigned short)(seq - other_recovery_seq) > 0 
			 && (unsigned short)(seq - other_recovery_seq) < 32768 ) {
				request_recovery = other_recovery_type;

				vpxlog_dbg(SKIP,
					"Requested recovery frame %c:%c,%d,%d\n",
					command,
					(other_recovery_type == GOLD ? 'G' : 'A'),
					packetizer.packet[gold_recovery_seq & PSM].frame_type,
					seq,
					packetizer.packet[gold_recovery_seq & PSM].timestamp );

			}
			else {
				// nothing else we can do ask for a key
				request_recovery = KEY;
				vpxlog_dbg(SKIP, "Requested key frame %c:%d,%d\n", command, tp->frame_type, seq, tp->timestamp);
			}
		}

		fputs("send packet\n", stderr);
		send_packet(&packetizer, &vpx_socket, address);
		vpx_net_set_read_timeout(&vpx_socket2, 1);
		
		pthread_mutex_unlock(&frame_mtx);
	}

	vpx_net_close(&vpx_socket2);
	vpx_net_close(&vpx_socket);
	vpx_net_destroy();

	vpx_codec_destroy(&encoder);
	return 0;
}
