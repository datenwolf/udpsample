#include <stdlib.h>
#include <stdarg.h>

#include "ivf.h"

#define IVF_FILE_HDR_SZ  (32)
#define IVF_FRAME_HDR_SZ (12)

static void mem_put_le16(char *mem, unsigned int val) {
    mem[0] = val;
    mem[1] = val>>8;
}
 
static void mem_put_le32(char *mem, unsigned int val) {
    mem[0] = val;
    mem[1] = val>>8;
    mem[2] = val>>16;
    mem[3] = val>>24;
}
 
void ivf_write_file_header(
	FILE *outfile,
	const vpx_codec_enc_cfg_t *cfg,
	int frame_cnt )
{
	char header[32];

	if( cfg->g_pass != VPX_RC_ONE_PASS
	 && cfg->g_pass != VPX_RC_LAST_PASS ) {
		return;
	}

	header[0] = 'D';
	header[1] = 'K';
	header[2] = 'I';
	header[3] = 'F';
	mem_put_le16(header+4,  0);                   /* version */
	mem_put_le16(header+6,  32);                  /* headersize */
	mem_put_le32(header+8,  ivf_fourcc);          /* fourcc */
	mem_put_le16(header+12, cfg->g_w);            /* width */
	mem_put_le16(header+14, cfg->g_h);            /* height */
	mem_put_le32(header+16, cfg->g_timebase.den); /* rate */
	mem_put_le32(header+20, cfg->g_timebase.num); /* scale */
	mem_put_le32(header+24, frame_cnt);           /* length */
	mem_put_le32(header+28, 0);                   /* unused */

	fwrite(header, 1, 32, outfile);
}
 
 
void ivf_write_frame_header(
	FILE *outfile,
	const vpx_codec_cx_pkt_t *pkt )
{
	char             header[12];
	vpx_codec_pts_t  pts;

	if( pkt->kind != VPX_CODEC_CX_FRAME_PKT ) {
		return;
	}

	fprintf(stderr, "frame size: %u\n",
	(unsigned int)pkt->data.frame.sz);

	pts = pkt->data.frame.pts;
	mem_put_le32(header, pkt->data.frame.sz);
	mem_put_le32(header+4, pts&0xFFFFFFFF);
	mem_put_le32(header+8, pts >> 32);

	fwrite(header, 1, 12, outfile);
}

