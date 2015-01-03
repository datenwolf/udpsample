/* Redundant Datagram Conduit */
#pragma once
#ifndef RDC_H
#define RDC_H

/* TODO: SRDC -- Secure RDC with authentication and confidentialty */

/* protocol version of the RDC library linked */
extern unsigned int const RDC_VERSION;

enum rdcError {
	RDC_NO_ERROR = 0,
	RDC_ERROR_VERSION_MISMATCH,
	RDC_ERROR_PACKET_HEADER_INVALID,
	RDC_ERROR_INVALID_PARAMETER,
};

/*
 * The Redundant Datagram Conduit provides a UDP based datagram
 * channel, that allows for recovery from a specifiable (per datagram)
 * amount of packet loss.
 *
 * Up to 256 datagrams can be in flight simultanously, in a round robin
 * like fashion, of which the most recent 128 ones getting completed are
 * being processed. Effectively this means that once datagram n + 128th
 * has been received any incoming packets for datagrams in flight of
 * index <= n will be discarded.
 */

/* signature of callback function used for filtering incoming
 * packets based on datagram/packet index and source address. */
typedef int(*rdc_packet_cb)(
	void                  *userdata,
	unsigned int           i_datagram,
	unsigned int           i_packet,
	struct sockaddr const *src_addr,
	socklen_t const        addrlen );

/* signature of callback function used for processing complete
 * datagrams. May be invoked in a dedicated / separated thread,
 * so this function _must_ be reentrant. */
typedef int(*rdc_datagram_cb)(
	void   *userdata,
	size_t  dgm_sz,
	void   *datagram );

#define RDC_FLAG(x) (1 << (x))
enum rdcFlags {
	RDC_SEND = RDC_FLAG(0),
	RDC_RECV = RDC_FLAG(1),
	RDC_PARALLEL_PROCESSING = RDC_FLAG(2),
	RDC_PRESERVE_SEQUENCE   = RDC_FLAG(3),
	RDC_ALLOW_OUT_OF_ORDER  = RDC_FLAG(4),
};

typedef struct rdcContext rdcContext;

/* open a redundant datagram conduit */
rdcContext *rdc_open(
	void * const    userdata,
	unsigned int    flags,
	rdc_packet_cb   packet_cb,
	rdc_datagram_cb datagram_cb );

/* close a redundant datagram conduit; imples a flush */
void rdc_close( rdcContext ** const ctx );

/* wait for an incoming packet and process it.
 *
 * timeout after timeout_us microseconds or
 * wait indefinitely if 0 == timeout_us */
int rdc_recvnext( rdcContext * const ctx,
	unsigned int timeout_us,
	unsigned int flags );

/* invoke datagram processing callback on datagrams
 * which have been completely received but not yet
 * processed. */
int rdc_flush( rdcContext * const ctx,
	unsigned int flags );

/* wait for pending datagrams processing to finish;
 * implies a flush. */
int rdc_finish( rdcContext * const ctx,
	unsigned int flags );

/* send a datagram to the specified address. */
int rdc_sendto( rdcContext * const ctx,
	size_t                 dgm_sz,
	void const            *datagram,
	unsigned int           redundancy_nom,
	unsigned int           reduncandy_den,
	struct sockaddr const *src_addr,
	socklen_t const       *addrlen );

#endif/*RDC_H*/
