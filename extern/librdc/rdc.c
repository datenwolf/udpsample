#include "rdc.h"
#include "vdm/fec.h"

#define RDC_3C5IN16(a,b,c) ( \
	  (((unsigned int)(a)&0x1f)      ) \
	| (((unsigned int)(a)&0x1f) << 5 ) \
	| (((unsigned int)(a)&0x1f) << 10) )

unsigned int const RDC_VERSION = 0x0101

enum {
	RDC_MAX_DATAGRAMS = 128,
	RDC_MAGIC   = RDC_3C5IN16('R','D','C')
};

struct rdcDatagramState {
	unsigned int flags;
	unsigned int fragments;
	size_t       packet_size;

	unsigned int *i_packet;
	void  *data;
};

/* Header for the redundant datagram packets. The size of the
 * whole datagram, number of packet and redundancy are replicated
 * over all packets; this causes a small overhead, but simplifies
 * code design and aids in packet loss recovery. 
 *
 * The packet size should be smaller than the Path MTU, since larger
 * values cause fragmentation in which case loss of a single fragment
 * means loss of all the fragments and therby packet, making packet
 * loss more likely. This is exactly what RDC tries to counter,
 * so it's counterproductive to uncover these issues on andother front.
 *
 * The maximum amount of redundancy selectable is 3:2. So with the
 * these settings up to 1/3 of the packets may get lost without
 * loosing the datagram, at the cost of the overhead consuming
 * that bandwidth.
 *
 * Fits nicely into an octet. */
struct rdcPacketHeader {
	unsigned vmagic:  16; /* magic ^ version */
	unsigned pkt_size:11; /* packet size (max 2kiB) */
	unsigned k_packet:10; /* number of effective packets */
	unsigned n_extra:  9: /* number of reduandancy packets */
	unsigned i_packet:11; /* packet index */
	unsigned i_dgm:    7; /* datagram index */
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
	struct fec_parms *fec;
	int rv;

	if( (rv = rdc_context_validate_send(ctx)) ) {
		return rv;
	}

	fec = fec_new(...);
	if( !fec ) {
		rv = RDC_ERROR_OUT_OF_MEMORY;
		goto fail_fec_new;
	}

	
	fec_free(fec);
fail_fec_new:
	return rv;
}

