#include "rdc.h"
#include <libvdm/fec.h>

#define RDC_3C5IN16(a,b,c) ( \
	  (((unsigned int)(a)&0x1f)      ) \
	| (((unsigned int)(a)&0x1f) << 5 ) \
	| (((unsigned int)(a)&0x1f) << 10) )

unsigned int const RDC_VERSION = 0x0101

enum {
	RDC_MAX_DATAGRAMS = 0xff,
	RDC_MAGIC   = RDC_3C5IN16('R','D','C')
};

struct rdcDatagramState {
	unsigned int flags;
	unsigned int fragments;
	size_t       packet_size;

	unsigned int *i_packet;
	void  *data;
};

/* header for the redundant datagram packets. The size of the
 * whole datagram, number of packet and redundancy are replicated
 * over all packets; this causes a small overhead, but simplifies
 * code design and aids in packet loss recovery. 
 *
 * fits nicely into an octet. */
struct rdcPacketHeader {
	unsigned vmagic:  16; /* magic ^ version */
	unsigned pkt_size:10; /* packet size (max 1kiB) */
	unsigned k_packet:10; /* number of effective fragments */
	unsigned n_extra:  9: /* number of reduandancy packets */
	unsigned i_packet:11; /* packet index */
	unsigned i_dgm:    8; /* datagram index */
};

struct rdcContext {
	int fd_sock;
	unsigned int flags;

	rdc_packet_cb    packet_cb;
	rcc_datagram_cb  datagram_cb;
	struct rdcDatagramState dgm_state[RDC_MAX_DATAGRAMS];
};

static rdcContext*
rdc_context_alloc(void)
{	
	rdcContext *ctx;
	size_t i_ctx;
	
	ctx = calloc(1, sizeof(rdcContext));
	if( !ctx ) {
		return NULL;
	}

	for(i_ctx = 0; i_ctx < RDC_MAX_DATAGRAMS; ++i_ctx) {
		struct rdcDatagramState * const dgm_state =
			&ctx->dgm_state[i_ctx];

		dgm_state->i_packet = NULL;
		dgm_state->data = NULL;
	}

	return ctx;
}

static void
rdc_context_free(rdcContext ** const ctx)
{
	assert(ctx);

	free(*ctx);
	*ctx = NULL;
}

/* validate that the context is generally fit */
static int
rdc_context_validate(rdcContext const * const ctx)
{
	return 0;
}

/* validate that the context is fit for receiving datagrams */
static int
rdc_context_validate_recv(rdcContext const * const ctx)
{
	int rv;
	if( (rv = rdc_context_validate(ctx)) ) {
		return rv;
	}

	return 0;
}

/* validate that the context is fit for sending datagrams */
static int
rdc_context_validate_send(rdcContext const * const ctx)
{
	int rv;
	if( (rv = rdc_context_validate(ctx)) ) {
		return rv;
	}

	return 0;
}

rdcContext *rdc_open(
	void * const    userdata,
	unsigned int    flags,
	rdc_packet_cb   packet_cb,
	rdc_datagram_cb datagram_cb )
{
	/* test for invalud parameter (combinations) */
	if( (RDC_ALLOW_OUT_OF_ORDER & flags)
	 || (RDC_RECV & flags) && !datagram_cb ) {
		return RDC_ERROR_INVALID_PARAMETER;
	}

	/* Open a UDP socket */

	if( RDC_PARALLEL_PROCESSING & flags ) {
		/* allocate synchronization objects */
	}

	return NULL;
}

void rdc_close( rdcContext ** const ctx )
{
	if( rdc_context_validate(ctx) ) {
		return;
	}

	rdc_finish(ctx);

	if( 0 /* may have processing thread */) {
	}
}

int rdc_recvnext( rdcContext * const ctx,
	unsigned int timeout_us,
	unsigned int flags )
{
	return 0;
}

int rdc_flush( rdcContext * const ctx,
	unsigned int flags )
{
	return 0;
}

int rdc_sendto( rdcContext * const ctx,
	size_t                 dgm_sz,
	void const            *datagram,
	unsigned int           redundancy_nom,
	unsigned int           reduncandy_den,
	struct sockaddr const *src_addr,
	socklen_t const       *addrlen )
{
	int rv;
	if( (rv = rdc_context_validate_send(ctx)) ) {
		return rv;
	}

	struct fec_parms = fec_new(...);



}

