#include <assert.h>

#include "knot/server/xfr-in.h"

#include "knot/common.h"
#include "knot/other/error.h"
#include "dnslib/packet.h"
#include "dnslib/dname.h"
#include "dnslib/zone.h"
#include "dnslib/query.h"
#include "dnslib/error.h"
#include "knot/other/log.h"
#include "knot/server/name-server.h"
#include "dnslib/debug.h"

/*----------------------------------------------------------------------------*/
/* Non-API functions                                                          */
/*----------------------------------------------------------------------------*/

static int xfrin_create_query(const dnslib_dname_t *qname, uint16_t qtype,
                              uint16_t qclass, uint8_t *buffer, size_t *size)
{
	dnslib_packet_t *pkt = dnslib_packet_new(DNSLIB_PACKET_PREALLOC_QUERY);
	CHECK_ALLOC_LOG(pkt, KNOT_ENOMEM);

	/*! \todo Get rid of the numeric constant. */
	int rc = dnslib_packet_set_max_size(pkt, 512);
	if (rc != DNSLIB_EOK) {
		dnslib_packet_free(&pkt);
		return KNOT_ERROR;
	}

	rc = dnslib_query_init(pkt);
	if (rc != DNSLIB_EOK) {
		dnslib_packet_free(&pkt);
		return KNOT_ERROR;
	}

	dnslib_question_t question;

	// this is ugly!!
	question.qname = (dnslib_dname_t *)qname;
	question.qtype = qtype;
	question.qclass = qclass;

	rc = dnslib_query_set_question(pkt, &question);
	if (rc != DNSLIB_EOK) {
		dnslib_packet_free(&pkt);
		return KNOT_ERROR;
	}

	/*! \todo Set some random ID!! */

	/*! \todo OPT RR ?? */

	uint8_t *wire = NULL;
	size_t wire_size = 0;
	rc = dnslib_packet_to_wire(pkt, &wire, &wire_size);
	if (rc != DNSLIB_EOK) {
		dnslib_packet_free(&pkt);
		return KNOT_ERROR;
	}

	if (wire_size > *size) {
		log_answer_warning("Not enough space provided for the wire "
		                   "format of the query.\n");
		dnslib_packet_free(&pkt);
		return KNOT_ESPACE;
	}

	memcpy(buffer, wire, wire_size);
	*size = wire_size;

	debug_ns("Created query of size %zu.\n", *size);
	dnslib_packet_dump(pkt);

	dnslib_packet_free(&pkt);

	return KNOT_EOK;
}

/*----------------------------------------------------------------------------*/

static uint32_t xfrin_serial_difference(uint32_t local, uint32_t remote)
{
	return (((int64_t)remote - local) % ((int64_t)1 << 32));
}

/*----------------------------------------------------------------------------*/
/* API functions                                                              */
/*----------------------------------------------------------------------------*/

int xfrin_create_soa_query(const dnslib_dname_t *zone_name, uint8_t *buffer,
                           size_t *size)
{
	return xfrin_create_query(zone_name, DNSLIB_RRTYPE_SOA,
	                           DNSLIB_CLASS_IN, buffer, size);
}

/*----------------------------------------------------------------------------*/

int xfrin_transfer_needed(const dnslib_zone_t *zone,
                          dnslib_packet_t *soa_response)
{
	// first, parse the rest of the packet
	assert(!dnslib_packet_is_query(soa_response));
	debug_ns("Response - parsed: %zu, total wire size: %zu\n",
	         soa_response->parsed, soa_response->size);
	int ret;

	if (soa_response->parsed < soa_response->size) {
		ret = dnslib_packet_parse_rest(soa_response);
		if (ret != DNSLIB_EOK) {
			return KNOT_EMALF;
		}
	}

	/*
	 * Retrieve the local Serial
	 */
	const dnslib_rrset_t *soa_rrset =
		dnslib_node_rrset(dnslib_zone_apex(zone), DNSLIB_RRTYPE_SOA);
	if (soa_rrset == NULL) {
		char *name = dnslib_dname_to_str(dnslib_node_owner(
				dnslib_zone_apex(zone)));
		log_answer_warning("SOA RRSet missing in the zone %s!\n", name);
		free(name);
		return KNOT_ERROR;
	}

	int64_t local_serial = dnslib_rdata_soa_serial(
		dnslib_rrset_rdata(soa_rrset));
	if (local_serial < 0) {
		char *name = dnslib_dname_to_str(dnslib_rrset_owner(soa_rrset));
		log_answer_warning("Malformed data in SOA of zone %s\n", name);
		free(name);
		return KNOT_EMALF;	// maybe some other error
	}

	/*
	 * Retrieve the remote Serial
	 */
	// the SOA should be the first (and only) RRSet in the response
	soa_rrset = dnslib_packet_answer_rrset(soa_response, 0);
	if (soa_rrset == NULL
	    || dnslib_rrset_type(soa_rrset) != DNSLIB_RRTYPE_SOA) {
		return KNOT_EMALF;
	}

	int64_t remote_serial = dnslib_rdata_soa_serial(
		dnslib_rrset_rdata(soa_rrset));
	if (remote_serial < 0) {
		return KNOT_EMALF;	// maybe some other error
	}

	uint32_t diff = xfrin_serial_difference(local_serial, remote_serial);
	return (diff >= 1 && diff <= (((uint32_t)1 << 31) - 1)) ? 1 : 0;
}

/*----------------------------------------------------------------------------*/

int xfrin_create_axfr_query(const dnslib_dname_t *zone_name, uint8_t *buffer,
                            size_t *size)
{
	return xfrin_create_query(zone_name, DNSLIB_RRTYPE_AXFR,
	                           DNSLIB_CLASS_IN, buffer, size);
}

/*----------------------------------------------------------------------------*/

int xfrin_create_ixfr_query(const dnslib_dname_t *zone_name, uint8_t *buffer,
                            size_t *size)
{
	return xfrin_create_query(zone_name, DNSLIB_RRTYPE_IXFR,
	                           DNSLIB_CLASS_IN, buffer, size);
}

/*----------------------------------------------------------------------------*/

int xfrin_zone_transferred(ns_nameserver_t *nameserver, dnslib_zone_t *zone)
{
	return KNOT_ENOTSUP;
}

/*----------------------------------------------------------------------------*/

int xfrin_process_axfr_packet(const uint8_t *pkt, size_t size,
                              dnslib_zone_t **zone)
{
	if (pkt == NULL || zone == NULL) {
		debug_xfr("Wrong parameters supported.\n");
		return KNOT_EINVAL;
	}

	dnslib_packet_t *packet =
			dnslib_packet_new(DNSLIB_PACKET_PREALLOC_NONE);
	if (packet == NULL) {
		debug_xfr("Could not create packet structure.\n");
		return KNOT_ENOMEM;
	}

	int ret = dnslib_packet_parse_from_wire(packet, pkt, size, 1);
	if (ret != DNSLIB_EOK) {
		debug_xfr("Could not parse packet: %s.\n",
		          dnslib_strerror(ret));
		dnslib_packet_free(&packet);
		/*! \todo Cleanup. */
		return KNOT_EMALF;
	}

	dnslib_rrset_t *rr = NULL;
	ret = dnslib_packet_parse_next_rr_answer(packet, &rr);

	if (ret != DNSLIB_EOK) {
		debug_xfr("Could not parse first Answer RR: %s.\n",
		          dnslib_strerror(ret));
		dnslib_packet_free(&packet);
		/*! \todo Cleanup. */
		return KNOT_EMALF;
	}

	if (rr == NULL) {
		debug_xfr("No RRs in the packet.\n");
		dnslib_packet_free(&packet);
		/*! \todo Cleanup. */
		return KNOT_EMALF;
	}

	dnslib_node_t *node = dnslib_node_new(rr->owner, NULL);
	if (node == NULL) {
		dnslib_packet_free(&packet);
		return KNOT_ENOMEM;
	}

	if (*zone == NULL) {
		// create new zone
		/*! \todo Ensure that the packet is the first one. */
		if (dnslib_rrset_type(rr) != DNSLIB_RRTYPE_SOA) {
			debug_xfr("No zone created, but the first RR in Answer"
			          " is not a SOA RR.\n");
			dnslib_packet_free(&packet);
			dnslib_node_free(&node, 0);
			dnslib_rrset_deep_free(&rr, 1, 1, 1);
			/*! \todo Cleanup. */
			return KNOT_EMALF;
		}

		if (dnslib_dname_compare(dnslib_rrset_owner(rr),
		                         dnslib_packet_qname(packet)) != 0) {
DEBUG_XFR(
			char *rr_owner =
				dnslib_dname_to_str(dnslib_rrset_owner(rr));
			char *qname = dnslib_dname_to_str(
				dnslib_packet_qname(packet));

			debug_xfr("Owner of the first SOA RR (%s) does not "
			          "match QNAME (%s).\n", rr_owner, qname);

			free(rr_owner);
			free(qname);
);
			/*! \todo Cleanup. */
			dnslib_packet_free(&packet);
			dnslib_node_free(&node, 0);
			dnslib_rrset_deep_free(&rr, 1, 1, 1);
			return KNOT_EMALF;
		}

		// the first RR is SOA and its owner and QNAME are the same
		// create the zone
		*zone = dnslib_zone_new(node, 0);
		if (*zone == NULL) {
			dnslib_packet_free(&packet);
			dnslib_node_free(&node, 0);
			dnslib_rrset_deep_free(&rr, 1, 1, 1);
			/*! \todo Cleanup. */
			return KNOT_ENOMEM;
		}
	}

	while (ret == DNSLIB_EOK && rr != NULL) {
		// process the parsed RR

		// now just dump
		debug_xfr("\nNext RR:\n\n");
		dnslib_rrset_dump(rr, 0);
		dnslib_rrset_deep_free(&rr, 0, 1, 1);

		// parse next RR
		ret = dnslib_packet_parse_next_rr_answer(packet, &rr);
	}

	if (ret < 0) {
		// some error in parsing
		debug_xfr("Could not parse next RR: %s.\n",
		          dnslib_strerror(ret));
		dnslib_packet_free(&packet);
		dnslib_node_free(&node, 0);
		dnslib_rrset_deep_free(&rr, 1, 1, 1);
		/*! \todo Cleanup. */
		return KNOT_EMALF;
	}

	assert(ret != DNSLIB_EOK || rr == NULL);
	dnslib_packet_free(&packet);
	//dnslib_node_free(&node, 0);
//	dnslib_rrset_deep_free(&rr, 1, 1, 1);

	// for now, delete the created zone
	dnslib_zone_deep_free(zone, 1);

	return (ret == DNSLIB_EOK) ? KNOT_EOK : KNOT_EMALF;
}