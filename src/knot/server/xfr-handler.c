/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <urcu.h>

#include "knot/common.h"
#include "knot/server/xfr-handler.h"
#include "libknot/nameserver/name-server.h"
#include "knot/server/socket.h"
#include "knot/server/udp-handler.h"
#include "knot/server/tcp-handler.h"
#include "libknot/updates/xfr-in.h"
#include "libknot/util/wire.h"
#include "knot/server/zones.h"
#include "libknot/tsig-op.h"
#include "common/evsched.h"
#include "common/prng.h"
#include "common/descriptor.h"
#include "libknot/rrset.h"

/* Constants */
#define XFR_CHUNKLEN 3 /*! Number of requests assigned in a single pass. */
#define XFR_SWEEP_INTERVAL 2 /*! [seconds] between sweeps. */
#define XFR_BUFFER_SIZE 65535 /*! Do not change this - maximum value for UDP packet length. */
#define XFR_MSG_DLTTR 9 /*! Index of letter differentiating IXFR/AXFR in log msg. */

/* Messages */

static knot_lookup_table_t xfr_type_table[] = {
        { XFR_TYPE_AIN, "Incoming AXFR of '%s' with %s:" },
        { XFR_TYPE_IIN, "Incoming IXFR of '%s' with %s:" },
        { XFR_TYPE_AOUT, "Outgoing AXFR of '%s' to %s:" },
        { XFR_TYPE_IOUT, "Outgoing IXFR of '%s' to %s:" },
        { XFR_TYPE_NOTIFY, "NOTIFY of '%s' to %s:" },
        { XFR_TYPE_SOA, "SOA query of '%s' to %s:" },
        { XFR_TYPE_FORWARD, "UPDATE forwarded query of '%s' to %s:" },
        { XFR_TYPE_AIN, NULL }
};
static knot_lookup_table_t xfr_result_table[] = {
        { XFR_TYPE_AIN, "Started" },
        { XFR_TYPE_IIN, "Started" },
        { XFR_TYPE_SOA, "Query issued" },
        { XFR_TYPE_NOTIFY, "Query issued" },
        { XFR_TYPE_FORWARD, "Forwarded query" },
        { XFR_TYPE_AIN, NULL }
};

/* I/O wrappers */

static int xfr_send_tcp(int fd, sockaddr_t *addr, uint8_t *msg, size_t msglen)
{ return tcp_send(fd, msg, msglen); }

static int xfr_send_udp(int fd, sockaddr_t *addr, uint8_t *msg, size_t msglen)
{ return sendto(fd, msg, msglen, 0, (struct sockaddr *)addr, addr->len); }

static int xfr_recv_tcp(int fd, sockaddr_t *addr, uint8_t *buf, size_t buflen)
{ return tcp_recv(fd, buf, buflen, addr); }

static int xfr_recv_udp(int fd, sockaddr_t *addr, uint8_t *buf, size_t buflen)
{ return recvfrom(fd, buf, buflen, 0, (struct sockaddr *)addr, &addr->len); }

/* Context fetching. */

static knot_ns_xfr_t* xfr_task_get(xfrworker_t *w, int fd)
{
	value_t *val = ahtable_tryget(w->pool.t, (const char*)&fd, sizeof(int));
	if (!val) return NULL;
	return *val;
}

static void xfr_task_set(xfrworker_t *w, int fd, knot_ns_xfr_t *rq)
{
	fdset_add(w->pool.fds, fd, OS_EV_READ);
	value_t *val = ahtable_get(w->pool.t, (const char*)&fd, sizeof(int));
	*val = rq;
}

static void xfr_task_clear(xfrworker_t *w, int fd)
{
	ahtable_del(w->pool.t, (const char*)&fd, sizeof(int));
	fdset_remove(w->pool.fds, fd);
}

/*! \brief Wrapper function for answering AXFR/OUT. */
static int xfr_answer_axfr(knot_nameserver_t *ns, knot_ns_xfr_t *xfr)
{
	int ret = knot_ns_answer_axfr(ns, xfr);
	dbg_xfr("xfr: ns_answer_axfr() = %d.\n", ret);
	return ret;
}

/*! \brief Wrapper function for answering IXFR/OUT. */
static int xfr_answer_ixfr(knot_nameserver_t *ns, knot_ns_xfr_t *xfr)
{
	/* Check serial differences. */
	int ret = KNOT_EOK;
	uint32_t serial_from = 0;
	uint32_t serial_to = 0;
	ret = ns_ixfr_load_serials(xfr, &serial_from, &serial_to);
	dbg_xfr_verb("xfr: loading changesets for IXFR %u-%u\n",
	             serial_from, serial_to);
	log_server_info("%s Started (serial %u -> %u).\n",
	                xfr->msg, serial_from, serial_to);
	if (ret != KNOT_EOK) {
		return ret;
	}

	/* Load changesets from journal. */
	int chsload = zones_xfr_load_changesets(xfr, serial_from, serial_to);
	if (chsload != KNOT_EOK) {
		/* History cannot be reconstructed, fallback to AXFR. */
		if (chsload == KNOT_ERANGE || chsload == KNOT_ENOENT) {
			log_server_info("%s Incomplete history, "
			                "fallback to AXFR.\n",
			                xfr->msg);
			xfr->type = XFR_TYPE_AOUT;
			xfr->msg[XFR_MSG_DLTTR] = 'A';
			return xfr_answer_axfr(ns, xfr);
		} else if (chsload == KNOT_EMALF) {
			xfr->rcode = KNOT_RCODE_FORMERR;
		} else {
			xfr->rcode = KNOT_RCODE_SERVFAIL;
		}

		/* Mark all as generic error. */
		ret = KNOT_ERROR;
	}

	/* Finally, answer. */
	if (chsload == KNOT_EOK) {
		ret = knot_ns_answer_ixfr(ns, xfr);
		dbg_xfr("xfr: ns_answer_ixfr() = %s.\n", knot_strerror(ret));
	}

	return ret;
}

/*! \brief Build string for logging related to given xfer descriptor. */
static int xfr_task_setmsg(knot_ns_xfr_t *rq, const char *keytag)
{
	/* Check */
	if (rq == NULL) {
		return KNOT_EINVAL;
	}

	knot_lookup_table_t *xd = knot_lookup_by_id(xfr_type_table, rq->type);
	if (!xd) {
		return KNOT_EINVAL;
	}
	
	/* Zone is refcounted, no need for RCU. */
	char *kstr = NULL;
	if (keytag) {
		kstr = xfr_remote_str(&rq->addr, keytag);
	} else if (rq->tsig_key) {
		char *tag = knot_dname_to_str(rq->tsig_key->name);
		kstr = xfr_remote_str(&rq->addr, tag);
		free(tag);
	} else {
		kstr = xfr_remote_str(&rq->addr, NULL);
	}

	/* Prepare log message. */
	const char *zname = rq->zname;
	if (zname == NULL && rq->zone != NULL) {
		zonedata_t *zd = (zonedata_t *)knot_zone_data(rq->zone);
		if (zd == NULL) {
			free(kstr);
			return KNOT_EINVAL;
		} else {
			zname = zd->conf->name;
		}
	}

	rq->msg = sprintf_alloc(xd->name, zname, kstr ? kstr : "'unknown'");
	free(kstr);
	return KNOT_EOK;
}

static int xfr_task_setsig(knot_ns_xfr_t *rq, knot_tsig_key_t *key)
{
	if (rq == NULL || key == NULL) {
		return KNOT_EINVAL;
	}

	int ret = KNOT_EOK;
	rq->tsig_key = key;
	rq->tsig_size = tsig_wire_maxsize(key);
	rq->digest_max_size = knot_tsig_digest_length(key->algorithm);
	rq->digest = malloc(rq->digest_max_size);
	if (rq->digest == NULL) {
		rq->tsig_key = NULL;
		rq->tsig_size = 0;
		rq->digest_max_size = 0;
		return KNOT_ENOMEM;
	}
	memset(rq->digest, 0 , rq->digest_max_size);
	rq->tsig_data = malloc(KNOT_NS_TSIG_DATA_MAX_SIZE);
	if (rq->tsig_data) {
		dbg_xfr("xfr: using TSIG for XFR/IN\n");
		rq->tsig_data_size = 0;
	} else {
		free(rq->digest);
		rq->digest = NULL;
		rq->tsig_key = NULL;
		rq->tsig_size = 0;
		rq->digest_max_size = 0;
		return KNOT_ENOMEM;
	}
	dbg_xfr("xfr: found TSIG key (MAC len=%zu), adding to transfer\n",
		rq->digest_max_size);

	return ret;
}

static int xfr_task_connect(knot_ns_xfr_t *rq)
{
	/* Create socket by type. */
	int stype = SOCK_DGRAM;
	if (rq->flags & XFR_FLAG_TCP) {
		stype = SOCK_STREAM;
	}
	int fd = socket_create(sockaddr_family(&rq->addr), stype, 0);
	if (fd < 0) {
		return KNOT_ERROR;
	}

	/* Bind to specific address - if set. */
	if (rq->saddr.len > 0) {
		if (bind(fd, (struct sockaddr *)&rq->saddr, rq->saddr.len) < 0) {
			socket_close(fd);
			return KNOT_ERROR;
		}
	}
	/* Connect if TCP. */
	if (rq->flags & XFR_FLAG_TCP) {
		if (connect(fd, (struct sockaddr *)&rq->addr, rq->addr.len) < 0) {
			socket_close(fd);
			return KNOT_ECONNREFUSED;
		}
	}

	/* Store new socket descriptor. */
	rq->session = fd;
	return KNOT_EOK;
}

/*! \brief Clean pending transfer data. */
static void xfr_task_cleanup(knot_ns_xfr_t *rq)
{
	dbg_xfr_verb("Cleaning up after XFR-in.\n");
	if (rq->type == XFR_TYPE_AIN) {
		if (rq->flags & XFR_FLAG_AXFR_FINISHED) {
			knot_zone_contents_deep_free(&rq->new_contents);
		} else if (rq->data) {
			xfrin_constructed_zone_t *constr_zone = rq->data;
			knot_zone_contents_deep_free(&(constr_zone->contents));
			xfrin_free_orphan_rrsigs(&(constr_zone->rrsigs));
			free(rq->data);
			rq->data = NULL;
		}
	} else if (rq->type == XFR_TYPE_IIN) {
		knot_changesets_t *chs = (knot_changesets_t *)rq->data;
		knot_free_changesets(&chs);
		rq->data = NULL;
		assert(rq->new_contents == NULL);
	} else if (rq->type == XFR_TYPE_FORWARD) {
		knot_packet_free(&rq->query);
	}

	/* Cleanup other data - so that the structure may be reused. */
	rq->packet_nr = 0;
	rq->tsig_data_size = 0;
}

/*! \brief Close and free task. */
static int xfr_task_close(xfrworker_t *w, int fd)
{
	knot_ns_xfr_t *rq = xfr_task_get(w, fd);
	if (!rq) return KNOT_ENOENT;

	/* Update xfer state. */
	if (rq->type == XFR_TYPE_AIN || rq->type == XFR_TYPE_IIN) {
		zonedata_t *zd = (zonedata_t *)knot_zone_data(rq->zone);
		pthread_mutex_lock(&zd->lock);
		if (zd->xfr_in.state == XFR_PENDING) {
			zd->xfr_in.state = XFR_IDLE;
		}
		pthread_mutex_unlock(&zd->lock);
	}

	/* Close socket and free task. */
	xfr_task_clear(w, fd);
	xfr_task_free(rq);
	socket_close(fd);
	return KNOT_EOK;
}

/*! \brief Timeout handler. */
static int xfr_task_expire(fdset_t *fds, knot_ns_xfr_t *rq)
{
	/* Fetch related zone (refcounted, no RCU). */
	knot_zone_t *zone = (knot_zone_t *)rq->zone;
	const knot_zone_contents_t *contents = knot_zone_contents(zone);

	/* Process timeout. */
	rq->wire_size = rq->wire_maxlen;
	switch(rq->type) {
	case XFR_TYPE_NOTIFY:
		if ((long)--rq->data > 0) { /* Retries */
			notify_create_request(contents, rq->wire, &rq->wire_size);
			fdset_set_watchdog(fds, rq->session, NOTIFY_TIMEOUT);
			rq->send(rq->session, &rq->addr, rq->wire, rq->wire_size);
			log_zone_info("%s Query issued (serial %u).\n",
			              rq->msg, knot_zone_serial(contents));
			rq->packet_nr = knot_wire_get_id(rq->wire);
			return KNOT_EOK; /* Keep state. */
		}
		break;
	default:
		break;
	}

	log_zone_info("%s Failed, timeout exceeded.\n", rq->msg);
	return KNOT_ECONNREFUSED;
}

/*! \brief Start pending request. */
static int xfr_task_start(knot_ns_xfr_t *rq)
{
	if (!rq || !rq->zone) {
		return KNOT_EINVAL;
	}

	/* Zone is refcounted, no need for RCU. */
	int ret = KNOT_EOK;
	knot_zone_t *zone = (knot_zone_t *)rq->zone;

	/* Fetch zone contents. */
	const knot_zone_contents_t *contents = knot_zone_contents(zone);
	if (!contents && rq->type == XFR_TYPE_IIN) {
		log_server_warning("%s Refusing to start IXFR on zone with no "
				   "contents.\n", rq->msg);
		return KNOT_ECONNREFUSED;
	}

	/* Connect to remote. */
	if (rq->session <= 0) {
		ret = xfr_task_connect(rq);
		if (ret != KNOT_EOK) {
			return ret;
		}
	}

	/* Prepare TSIG key if set. */
	int add_tsig = 0;
	if (rq->tsig_key) {
		ret = xfr_task_setsig(rq, rq->tsig_key);
		add_tsig = (ret == KNOT_EOK);
	}

	/* Create XFR query. */
	switch(rq->type) {
	case XFR_TYPE_AIN:
		ret = xfrin_create_axfr_query(zone->name, rq, &rq->wire_size, add_tsig);
		break;
	case XFR_TYPE_IIN:
		ret = xfrin_create_ixfr_query(contents, rq, &rq->wire_size, add_tsig);
		break;
	case XFR_TYPE_SOA:
		ret = xfrin_create_soa_query(zone->name, rq, &rq->wire_size);
		break;
	case XFR_TYPE_NOTIFY:
		rq->wire_size = 0;
		ret = KNOT_EOK; /* Will be sent on first timeout. */
		break;
	case XFR_TYPE_FORWARD:
		ret = knot_ns_create_forward_query(rq->query, rq->wire, &rq->wire_size);
		break;
	default:
		ret = KNOT_EINVAL;
		break;
	}

	/* Handle errors. */
	if (ret != KNOT_EOK) {
		dbg_xfr("xfr: failed to create XFR query type %d: %s\n",
		        rq->type, knot_strerror(ret));
		return ret;
	}

	/* Start transfer. */
	gettimeofday(&rq->t_start, NULL);
	if (rq->wire_size > 0) {
		ret = rq->send(rq->session, &rq->addr, rq->wire, rq->wire_size);
		if (ret != rq->wire_size) {
			char ebuf[256] = {0};
			strerror_r(errno, ebuf, sizeof(ebuf));
			log_server_info("%s Failed to send query (%s).\n",
			                rq->msg, ebuf);
			return KNOT_ECONNREFUSED;
		}
	}

	/* If successful. */
	if (rq->type == XFR_TYPE_SOA) {
		rq->packet_nr = knot_wire_get_id(rq->wire);
	}

	return KNOT_EOK;
}

static int xfr_task_process(xfrworker_t *w, knot_ns_xfr_t* rq, uint8_t *buf, size_t buflen)
{
	/* Check if not already processing, zone is refcounted. */
	zonedata_t *zd = (zonedata_t *)knot_zone_data(rq->zone);

	/* Check if the zone is not discarded. */
	if (!zd || knot_zone_flags(rq->zone) & KNOT_ZONE_DISCARDED) {
		dbg_xfr_verb("xfr: request on a discarded zone, ignoring\n");
		return KNOT_EINVAL;
	}

	/* Update XFR message prefix. */
	xfr_task_setmsg(rq, NULL);

	/* Update request. */
	rq->wire = buf;
	rq->wire_size = buflen;
	rq->wire_maxlen = buflen;
	rq->send = &xfr_send_udp;
	rq->recv = &xfr_recv_udp;
	if (rq->flags & XFR_FLAG_TCP) {
		rq->send = &xfr_send_tcp;
		rq->recv = &xfr_recv_tcp;
	}

	/* Handle request. */
	dbg_xfr("%s processing request type '%d'\n", rq->msg, rq->type);
	int ret = xfr_task_start(rq);
	const char *msg = knot_strerror(ret);
	knot_lookup_table_t *xd = knot_lookup_by_id(xfr_result_table, rq->type);
	if (xd && ret == KNOT_EOK) {
		msg = xd->name;
	}

	int bootstrap_fail = 0;
	switch(rq->type) {
	case XFR_TYPE_AIN:
	case XFR_TYPE_IIN:
		rq->lookup_tree = hattrie_create();
		if (ret != KNOT_EOK) {
			pthread_mutex_lock(&zd->lock);
			zd->xfr_in.state = XFR_IDLE;
			pthread_mutex_unlock(&zd->lock);
			bootstrap_fail = !knot_zone_contents(rq->zone);
		}
		break;
	case XFR_TYPE_NOTIFY: /* Send on first timeout <0,5>s. */
		fdset_set_watchdog(w->pool.fds, rq->session, (int)(tls_rand() * 5));
		xfr_task_set(w, rq->session, rq);
		return KNOT_EOK;
	case XFR_TYPE_SOA:
	case XFR_TYPE_FORWARD:
		fdset_set_watchdog(w->pool.fds, rq->session, conf()->max_conn_reply);
		break;
	default:
		break;
	}

	if (ret == KNOT_EOK) {
		xfr_task_set(w, rq->session, rq);
		log_server_info("%s %s.\n", rq->msg, msg);
	} else if (bootstrap_fail){
		int tmr_s = AXFR_BOOTSTRAP_RETRY * tls_rand();
		event_t *ev = zd->xfr_in.timer;
		if (ev) {
			evsched_cancel(ev->parent, ev);
			evsched_schedule(ev->parent, ev, tmr_s);
		}
		log_zone_notice("%s Bootstrap failed, next "
		                "attempt in %d seconds.\n",
		                rq->msg, tmr_s / 1000);
	} else {
		log_server_error("%s %s.\n", rq->msg, msg);
	}

	return ret;
}

/*! \brief Finalize XFR/IN transfer. */
static int xfr_task_finalize(xfrworker_t *w, knot_ns_xfr_t *rq)
{
	int ret = KNOT_EINVAL;
	knot_nameserver_t *ns = w->master->ns;

	if (rq->type == XFR_TYPE_AIN) {
		ret = zones_save_zone(rq);
		if (ret == KNOT_EOK) {
			ret = knot_ns_switch_zone(ns, rq);
			if (ret != KNOT_EOK) {
				log_zone_error("%s %s\n", rq->msg, knot_strerror(ret));
				log_zone_error("%s Failed to switch in-memory "
				               "zone.\n", rq->msg);
			}
		} else {
			log_zone_error("%s %s\n",
			               rq->msg, knot_strerror(ret));
			log_zone_error("%s Failed to save zonefile.\n",
			               rq->msg);
		}
	} else if (rq->type == XFR_TYPE_IIN) {
		knot_changesets_t *chs = (knot_changesets_t *)rq->data;
		ret = zones_store_and_apply_chgsets(chs, rq->zone,
		                                    &rq->new_contents,
		                                    rq->msg,
		                                    XFR_TYPE_IIN);
		rq->data = NULL; /* Freed or applied in prev function. */
	}

	if (ret == KNOT_EOK) {
		struct timeval t_end;
		gettimeofday(&t_end, NULL);
		log_zone_info("%s Finished in %.02fs "
		              "(finalization %.02fs).\n",
		              rq->msg,
		              time_diff(&rq->t_start, &t_end) / 1000.0,
		              time_diff(&rq->t_end, &t_end) / 1000.0);
		rq->new_contents = NULL; /* Do not free. */
	}

	return ret;
}

/*! \brief Query response event handler function. */
static int xfr_task_resp(xfrworker_t *w, knot_ns_xfr_t *rq)
{
	knot_nameserver_t *ns = w->master->ns;
	knot_packet_t *re = knot_packet_new(KNOT_PACKET_PREALLOC_RESPONSE);
	if (re == NULL) {
		return KNOT_ENOMEM;
	}

	knot_packet_type_t rt = KNOT_RESPONSE_NORMAL;
	int ret = knot_ns_parse_packet(rq->wire, rq->wire_size, re, &rt);
	if (ret != KNOT_EOK) {
		knot_packet_free(&re);
		return KNOT_EOK; /* Ignore */
	}

	/* Ignore other packets. */
	switch(rt) {
	case KNOT_RESPONSE_NORMAL:
	case KNOT_RESPONSE_NOTIFY:
	case KNOT_RESPONSE_UPDATE:
		break;
	default:
		knot_packet_free(&re);
		return KNOT_EOK; /* Ignore */
	}

	ret = knot_packet_parse_rest(re, 0);
	if (ret != KNOT_EOK) {
		knot_packet_free(&re);
		return KNOT_EOK; /* Ignore */
	}

	/* Check TSIG. */
	const knot_rrset_t * tsig_rr = knot_packet_tsig(re);
	if (rq->tsig_key != NULL) {
		/*! \todo Not sure about prev_time_signed, but this is the first
		 *        reply and we should pass query sign time as the time
		 *        may be different. Leaving to 0.
		 */
		ret = knot_tsig_client_check(tsig_rr, rq->wire, rq->wire_size,
		                             rq->digest, rq->digest_size,
		                             rq->tsig_key, 0);
		if (ret != KNOT_EOK) {
			log_server_error("%s %s\n", rq->msg, knot_strerror(ret));
			knot_packet_free(&re);
			return KNOT_ECONNREFUSED;
		}

	}

	/* Process response. */
	size_t rlen = rq->wire_size;
	switch(rt) {
	case KNOT_RESPONSE_NORMAL:
		ret = zones_process_response(ns, rq->packet_nr, &rq->addr,
		                             re, rq->wire, &rlen);
		break;
	case KNOT_RESPONSE_NOTIFY:
		ret = notify_process_response(re, rq->packet_nr);
		break;
	case KNOT_RESPONSE_UPDATE:
		ret = zones_process_update_response(rq, rq->wire, &rlen);
		if (ret == KNOT_EOK) {
			log_server_info("%s Forwarded response.\n", rq->msg);
		}
		break;
	default:
		ret = KNOT_EINVAL;
		break;
	}

	knot_packet_free(&re);
	if (ret == KNOT_EUPTODATE) {  /* Check up-to-date zone. */
		log_server_info("%s %s (serial %u)\n", rq->msg,
		                knot_strerror(ret),
		                knot_zone_serial(knot_zone_contents(rq->zone)));
		ret = KNOT_ECONNREFUSED;
	} else if (ret == KNOT_EOK) { /* Disconnect if everything went well. */
		ret = KNOT_ECONNREFUSED;
	}

	return ret;
}

static int xfr_task_xfer(xfrworker_t *w, knot_ns_xfr_t *rq)
{
	/* Process incoming packet. */
	int ret = KNOT_EOK;
	knot_nameserver_t *ns = w->master->ns;
	switch(rq->type) {
	case XFR_TYPE_AIN:
		ret = knot_ns_process_axfrin(ns, rq);
		break;
	case XFR_TYPE_IIN:
		ret = knot_ns_process_ixfrin(ns, rq);
		break;
	default:
		ret = KNOT_EINVAL;
		break;
	}


	/* AXFR-style IXFR. */
	if (ret == KNOT_ENOIXFR) {
		assert(rq->type == XFR_TYPE_IIN);
		log_server_notice("%s Fallback to AXFR.\n", rq->msg);
		xfr_task_cleanup(rq);
		rq->type = XFR_TYPE_AIN;
		rq->msg[XFR_MSG_DLTTR] = 'A';
		ret = knot_ns_process_axfrin(ns, rq);
	}

	/* Check return code for errors. */
	dbg_xfr_verb("xfr: processed XFR pkt (%s)\n", knot_strerror(ret));

	/* IXFR refused, try again with AXFR. */
	if (rq->type == XFR_TYPE_IIN && ret == KNOT_EXFRREFUSED) {
		log_server_notice("%s Transfer failed, fallback to AXFR.\n", rq->msg);
		rq->wire_size = rq->wire_maxlen; /* Reset maximum bufsize */
		ret = xfrin_create_axfr_query(rq->zone->name, rq, &rq->wire_size, 1);
		/* Send AXFR/IN query. */
		if (ret == KNOT_EOK) {
			ret = rq->send(rq->session, &rq->addr,
			               rq->wire, rq->wire_size);
			/* Switch to AXFR and return. */
			if (ret == rq->wire_size) {
				xfr_task_cleanup(rq);
				rq->type = XFR_TYPE_AIN;
				rq->msg[XFR_MSG_DLTTR] = 'A';
				return KNOT_EOK;
			} else {
				ret = KNOT_ERROR;
			}
		}
		return ret; /* Something failed in fallback. */
	}

	/* Handle errors. */
	if (ret == KNOT_ENOXFR) {
		log_server_warning("%s Finished, %s\n", rq->msg, knot_strerror(ret));
	} else if (ret < 0) {
		log_server_error("%s %s\n", rq->msg, knot_strerror(ret));
	}

	/* Only for successful xfers. */
	if (ret > 0) {
		ret = xfr_task_finalize(w, rq);

		/* AXFR bootstrap timeout. */
		if (ret != KNOT_EOK && !knot_zone_contents(rq->zone)) {
			zonedata_t *zd = (zonedata_t *)knot_zone_data(rq->zone);
			int tmr_s = AXFR_BOOTSTRAP_RETRY * tls_rand();
			zd->xfr_in.bootstrap_retry = tmr_s;
			log_zone_info("%s Next attempt to bootstrap "
			              "in %d seconds.\n",
			              rq->msg, tmr_s / 1000);
		} else {
			zones_schedule_notify(rq->zone); /* NOTIFY */
		}


		/* Update REFRESH/RETRY */
		zones_schedule_refresh(rq->zone);
		ret = KNOT_ECONNREFUSED; /* Disconnect */
	}

	return ret;
}

/*! \brief Incoming packet handling function. */
static int xfr_process_event(xfrworker_t *w, knot_ns_xfr_t *rq)
{
	/* Check if zone is valid. */
	if (knot_zone_flags(rq->zone) & KNOT_ZONE_DISCARDED) {
		return KNOT_ECONNREFUSED;
	}

	/* Receive msg. */
	int n = rq->recv(rq->session, &rq->addr, rq->wire, rq->wire_maxlen);
	if (n < 0) { /* Disconnect */
		n = knot_map_errno(errno);
		log_server_error("%s %s\n", rq->msg, knot_strerror(n));
		return n;
	} else if (n == 0) {
		return KNOT_ECONNREFUSED;
	} else {
		rq->wire_size = n;
	}

	/* Handle SOA/NOTIFY responses. */
	switch(rq->type) {
	case XFR_TYPE_NOTIFY:
	case XFR_TYPE_SOA:
	case XFR_TYPE_FORWARD:
		return xfr_task_resp(w, rq);
	default:
		return xfr_task_xfer(w, rq);
	}
}

/*! \brief Sweep non-replied connection. */
static void xfr_sweep(fdset_t *set, int fd, void *data)
{
	dbg_xfr("xfr: sweeping fd=%d\n", fd);
	if (!set || !data) {
		dbg_xfr("xfr: invalid sweep operation on NULL worker or set\n");
		return;
	}
	xfrworker_t *w = (xfrworker_t *)data;
	knot_ns_xfr_t *rq = xfr_task_get(w, fd);
	if (!rq) {
		dbg_xfr("xfr: NULL data to sweep\n");
		return;
	}

	/* Skip non-sweepable types. */
	int ret = KNOT_ECONNREFUSED;
	switch(rq->type) {
	case XFR_TYPE_SOA:
	case XFR_TYPE_NOTIFY:
	case XFR_TYPE_FORWARD:
		ret = xfr_task_expire(set, rq);
		break;
	default:
		break;
	}

	if (ret != KNOT_EOK) {
		xfr_task_close(w, fd);
		--w->pending;
	}
}

/*! \brief Check TSIG if exists. */
static int xfr_check_tsig(knot_ns_xfr_t *xfr, knot_rcode_t *rcode, char **tag)
{
	/* Parse rest of the packet. */
	int ret = KNOT_EOK;
	knot_packet_t *qry = xfr->query;
	knot_tsig_key_t *key = 0;
	const knot_rrset_t *tsig_rr = 0;

	/* Find TSIG key name from query. */
	const knot_dname_t* kname = 0;
	int tsig_pos = knot_packet_additional_rrset_count(qry) - 1;
	if (tsig_pos >= 0) {
		tsig_rr = knot_packet_additional_rrset(qry, tsig_pos);
		if (knot_rrset_type(tsig_rr) == KNOT_RRTYPE_TSIG) {
			dbg_xfr("xfr: found TSIG in AR\n");
			kname = knot_rrset_owner(tsig_rr);
			if (tag) {
				*tag = knot_dname_to_str(kname);

			}
		} else {
			tsig_rr = 0;
		}
	}
	if (!kname) {
		dbg_xfr("xfr: TSIG not found in AR\n");
		char *name = knot_dname_to_str(
		                        knot_zone_name(xfr->zone));
		free(name);

		// return REFUSED
		xfr->tsig_key = 0;
		*rcode = KNOT_RCODE_REFUSED;
		return KNOT_EDENIED;
	}
	if (tsig_rr) {
		knot_tsig_algorithm_t alg = tsig_rdata_alg(tsig_rr);
		if (knot_tsig_digest_length(alg) == 0) {
			*rcode = KNOT_RCODE_NOTAUTH;
			xfr->tsig_key = NULL;
			xfr->tsig_rcode = KNOT_RCODE_BADKEY;
			xfr->tsig_prev_time_signed =
			                tsig_rdata_time_signed(tsig_rr);
			return KNOT_TSIG_EBADKEY;
		}
	}

	/* Evaluate configured key for claimed key name.*/
	key = xfr->tsig_key; /* Expects already set key (check_zone) */
	xfr->tsig_key = 0;
	if (key && kname && knot_dname_compare(key->name, kname) == 0) {
		dbg_xfr("xfr: found claimed TSIG key for comparison\n");
	} else {

		/* TSIG is mandatory if configured for interface. */
		/* Configured, but doesn't match. */
		dbg_xfr("xfr: no claimed key configured or not received"
		        ", treating as bad key\n");
		*rcode = KNOT_RCODE_NOTAUTH;
		ret = KNOT_TSIG_EBADKEY;
		xfr->tsig_rcode = KNOT_RCODE_BADKEY;
		key = NULL; /* Invalidate, ret already set to BADKEY */
	}

	/* Validate with TSIG. */
	if (key) {
		/* Prepare variables for TSIG */
		xfr_task_setsig(xfr, key);

		/* Copy MAC from query. */
		dbg_xfr("xfr: validating TSIG from query\n");
		const uint8_t* mac = tsig_rdata_mac(tsig_rr);
		size_t mac_len = tsig_rdata_mac_length(tsig_rr);
		if (mac_len > xfr->digest_max_size) {
			ret = KNOT_EMALF;
			dbg_xfr("xfr: MAC length %zu exceeds digest "
			        "maximum size %zu\n",
			        mac_len, xfr->digest_max_size);
		} else {
			memcpy(xfr->digest, mac, mac_len);
			xfr->digest_size = mac_len;

			/* Check query TSIG. */
			ret = knot_tsig_server_check(
			                        tsig_rr,
			                        knot_packet_wireformat(qry),
			                        knot_packet_size(qry),
			                        key);
			dbg_xfr("knot_tsig_server_check() returned %s\n",
			        knot_strerror(ret));
		}

		/* Evaluate TSIG check results. */
		switch(ret) {
		case KNOT_EOK:
			*rcode = KNOT_RCODE_NOERROR;
			break;
		case KNOT_TSIG_EBADKEY:
			xfr->tsig_rcode = KNOT_RCODE_BADKEY;
			xfr->tsig_key = NULL;
			*rcode = KNOT_RCODE_NOTAUTH;
			break;
		case KNOT_TSIG_EBADSIG:
			xfr->tsig_rcode = KNOT_RCODE_BADSIG;
			xfr->tsig_key = NULL;
			*rcode = KNOT_RCODE_NOTAUTH;
			break;
		case KNOT_TSIG_EBADTIME:
			xfr->tsig_rcode = KNOT_RCODE_BADTIME;
			// store the time signed from the query
			assert(tsig_rr != NULL);
			xfr->tsig_prev_time_signed =
			                tsig_rdata_time_signed(tsig_rr);
			*rcode = KNOT_RCODE_NOTAUTH;
			break;
		case KNOT_EMALF:
			*rcode = KNOT_RCODE_FORMERR;
			break;
		default:
			*rcode = KNOT_RCODE_SERVFAIL;
		}
	}


	return ret;
}

int xfr_worker(dthread_t *thread)
{
	assert(thread != NULL && thread->data != NULL);
	xfrworker_t *w = (xfrworker_t *)thread->data;
	xfrhandler_t *xfr = w->master;

	/* Buffer for answering. */
	size_t buflen = XFR_BUFFER_SIZE;
	uint8_t* buf = malloc(buflen);
	if (buf == NULL) {
		dbg_xfr("xfr: failed to allocate buffer for XFR worker\n");
		return KNOT_ENOMEM;
	}

	/* Next sweep time. */
	timev_t next_sweep;
	time_now(&next_sweep);
	next_sweep.tv_sec += XFR_SWEEP_INTERVAL;

	int limit = XFR_CHUNKLEN * 3;
	if (conf() && conf()->xfers > 0) {
		limit = conf()->xfers;
	}
	unsigned thread_capacity = limit / w->master->unit->size;
	if (thread_capacity < 1) thread_capacity = 1;
	w->pool.fds = fdset_new();
	w->pool.t = ahtable_create();
	w->pending = 0;

	/* Accept requests. */
	int ret = 0;
	dbg_xfr_verb("xfr: worker=%p starting\n", w);
	for (;;) {

		/* Populate pool with new requests. */
		if (w->pending <= thread_capacity) {
			pthread_mutex_lock(&xfr->mx);
			unsigned was_pending = w->pending;
			while (!EMPTY_LIST(xfr->queue)) {
				knot_ns_xfr_t *rq = HEAD(xfr->queue);
				rem_node(&rq->n);

				/* Unlock queue and process request. */
				pthread_mutex_unlock(&xfr->mx);
				ret = xfr_task_process(w, rq, buf, buflen);
				if (ret == KNOT_EOK)  ++w->pending;
				else                  xfr_task_free(rq);
				pthread_mutex_lock(&xfr->mx);

				if (w->pending - was_pending > XFR_CHUNKLEN)
					break;
			}
			pthread_mutex_unlock(&xfr->mx);
		}

		/* Check pending threads. */
		if (dt_is_cancelled(thread) || w->pending == 0) {
			break;
		}

		/* Poll fdset. */
		int nfds = fdset_wait(w->pool.fds, (XFR_SWEEP_INTERVAL/2) * 1000);
		if (nfds < 0) {
			if (errno == EINTR) continue;
			break;
		}

		/* Iterate fdset. */
		fdset_it_t it;
		fdset_begin(w->pool.fds, &it);
		while(nfds > 0) {

			/* Find data. */
			knot_ns_xfr_t *rq = xfr_task_get(w, it.fd);
			dbg_xfr_verb("xfr: worker=%p processing event on "
			             "fd=%d data=%p.\n",
			             w, it.fd, rq);
			if (rq) {
				ret = xfr_process_event(w, rq);
				if (ret != KNOT_EOK) {
					xfr_task_close(w, it.fd);
					--w->pending;
					--it.pos; /* Reset iterator */
				}
			}

			/* Next fd. */
			if (fdset_next(w->pool.fds, &it) < 0) {
				break;
			}
		}

		/* Sweep inactive. */
		timev_t now;
		if (time_now(&now) == 0) {
			if (now.tv_sec >= next_sweep.tv_sec) {
				fdset_sweep(w->pool.fds, &xfr_sweep, w);
				memcpy(&next_sweep, &now, sizeof(next_sweep));
				next_sweep.tv_sec += XFR_SWEEP_INTERVAL;
			}
		}
	}

	/* Cancel existing connections. */
	size_t keylen = 0;
	ahtable_iter_t i;
	ahtable_iter_begin(w->pool.t, &i, false);
	while (!ahtable_iter_finished(&i)) {
		int *key = (int *)ahtable_iter_key(&i, &keylen);
		xfr_task_close(w, *key);
		ahtable_iter_next(&i);
	}
	ahtable_iter_free(&i);

	/* Destroy data structures. */
	fdset_destroy(w->pool.fds);
	ahtable_free(w->pool.t);
	free(buf);

	dbg_xfr_verb("xfr: worker=%p finished.\n", w);
	return KNOT_EOK;
}

/*
 * Public APIs.
 */

xfrhandler_t *xfr_create(size_t thrcount, knot_nameserver_t *ns)
{
	/* Create XFR handler data. */
	const size_t total_size = sizeof(xfrhandler_t) + thrcount * sizeof(xfrworker_t);
	xfrhandler_t *xfr = malloc(total_size);
	if (xfr == NULL) {
		return NULL;
	}
	memset(xfr, 0, total_size);
	xfr->ns = ns;

	/* Create threading unit. */
	xfr->unit = dt_create(thrcount);
	if (xfr->unit == NULL) {
		free(xfr);
		return NULL;
	}

	/* Create worker threads. */
	for (unsigned i = 0; i < thrcount; ++i) {
		xfrworker_t *w = xfr->workers + i;
		w->master = xfr;
	}

	/* Create tasks structure and mutex. */
	pthread_mutex_init(&xfr->mx, 0);
	init_list(&xfr->queue);

	/* Assign worker threads. */
	dthread_t **threads = xfr->unit->threads;
	for (unsigned i = 0; i < thrcount; ++i) {
		dt_repurpose(threads[i], xfr_worker, xfr->workers + i);
	}

	return xfr;
}

int xfr_free(xfrhandler_t *xfr)
{
	if (!xfr) {
		return KNOT_EINVAL;
	}

	/* Free RR mutex. */
	pthread_mutex_destroy(&xfr->mx);

	/* Free pending queue. */
	knot_ns_xfr_t *n, *nxt;
	WALK_LIST_DELSAFE(n, nxt, xfr->queue) {
		xfr_task_free(n);
	}

	/* Delete unit. */
	dt_delete(&xfr->unit);
	free(xfr);

	return KNOT_EOK;
}

int xfr_stop(xfrhandler_t *xfr)
{
	if (!xfr) {
		return KNOT_EINVAL;
	}

	xfr_enqueue(xfr, NULL);
	return dt_stop(xfr->unit);
}

int xfr_join(xfrhandler_t *xfr) {
	return dt_join(xfr->unit);
}

int xfr_enqueue(xfrhandler_t *xfr, knot_ns_xfr_t *rq)
{
	if (!xfr) {
		return KNOT_EINVAL;
	}

	if (rq) {
		pthread_mutex_lock(&xfr->mx);
		add_tail(&xfr->queue, &rq->n);
		pthread_mutex_unlock(&xfr->mx);
	}

	/* Notify threads. */
	for (unsigned i = 0; i < xfr->unit->size; ++i) {
		dt_activate(xfr->unit->threads[i]);
	}

	return KNOT_EOK;
}

int xfr_answer(knot_nameserver_t *ns, knot_ns_xfr_t *rq)
{
	if (!ns || !rq) {
		return KNOT_EINVAL;
	}

	gettimeofday(&rq->t_start, NULL);
	rcu_read_lock(); /* About to guess zone from QNAME, so needs RCU. */
	int ret = knot_ns_init_xfr(ns, rq);
	rcu_read_unlock(); /* Now, the zone is either refcounted or NULL. */
	
	/* Use the QNAME as the zone name. */
	const knot_dname_t *qname = knot_packet_qname(rq->query);
	if (qname != NULL) {
		rq->zname = knot_dname_to_str(qname);
	} else {
		rq->zname = strdup("(unknown)");
	}

	/* Check requested zone. */
	if (ret == KNOT_EOK) {
		ret = zones_xfr_check_zone(rq, &rq->rcode);
	}

	/* Check TSIG. */
	char *keytag = NULL;
	if (ret == KNOT_EOK && rq->tsig_key != NULL) {
		ret = xfr_check_tsig(rq, &rq->rcode, &keytag);
	}
	if (xfr_task_setmsg(rq, keytag) != KNOT_EOK) {
		rq->msg = strdup("XFR:");
	}
	free(keytag);

	/* Initialize response. */
	if (ret == KNOT_EOK) {
		ret = knot_ns_init_xfr_resp(ns, rq);
	}

	/* Update request. */
	rq->send = &xfr_send_udp;
	rq->recv = &xfr_recv_udp;
	if (rq->flags & XFR_FLAG_TCP) {
		rq->send = &xfr_send_tcp;
		rq->recv = &xfr_recv_tcp;
	}

	/* Announce. */
	switch (ret) {
	case KNOT_EDENIED:
		log_server_info("%s TSIG required, but not found in query.\n",
		                rq->msg);
		break;
	default:
		break;
	}

	/* Finally, answer AXFR/IXFR. */
	const knot_zone_contents_t *cont = knot_zone_contents(rq->zone);
	if (ret == KNOT_EOK) {
		switch(rq->type) {
		case XFR_TYPE_AOUT:
			log_server_info("%s Started (serial %u).\n", rq->msg,
			                knot_zone_serial(cont));
			ret = xfr_answer_axfr(ns, rq);
			break;
		case XFR_TYPE_IOUT:
			ret = xfr_answer_ixfr(ns, rq);
			break;
		default:
			ret = KNOT_ENOTSUP;
			break;
		}
	} else {
		/*! \todo Sign with TSIG for some errors. */
		knot_ns_error_response_from_query(ns, rq->query,  rq->rcode,
		                                  rq->wire, &rq->wire_size);
		rq->send(rq->session, &rq->addr, rq->wire, rq->wire_size);
	}

	/* Check results. */
	gettimeofday(&rq->t_end, NULL);
	if (ret != KNOT_EOK) {
		log_server_notice("%s %s\n", rq->msg, knot_strerror(ret));
	} else {
		log_server_info("%s Finished in %.02fs.\n",
		                rq->msg,
		                time_diff(&rq->t_start, &rq->t_end) / 1000.0);
	}

	/* Cleanup. */
	knot_packet_free(&rq->response);  /* Free response. */
	knot_free_changesets((knot_changesets_t **)(&rq->data));
	free(rq->zname);

	/* Free request. */
	xfr_task_free(rq);
	return ret;
}

knot_ns_xfr_t *xfr_task_create(knot_zone_t *z, int type, int flags)
{
	knot_ns_xfr_t *rq = malloc(sizeof(knot_ns_xfr_t));
	if (rq == NULL) return NULL;
	memset(rq, 0, sizeof(knot_ns_xfr_t));

	/* Initialize. */
	rq->type = type;
	rq->flags = flags;
	if (z) {
		rq->zone = z;
		knot_zone_retain(rq->zone);
	}
	return rq;
}

int xfr_task_free(knot_ns_xfr_t *rq)
{
	if (!rq) {
		return KNOT_EINVAL;
	}

	/* Free DNAME trie. */
	hattrie_free(rq->lookup_tree);

	/* Free TSIG buffers. */
	free(rq->digest);
	rq->digest = NULL;
	rq->digest_size = 0;
	free(rq->tsig_data);
	rq->tsig_data = NULL;
	rq->tsig_data_size = 0;

	/* Cleanup transfer-specifics. */
	xfr_task_cleanup(rq);

	/* No further access to zone. */
	knot_zone_release(rq->zone);
	free(rq->msg);
	rq->msg = NULL;
	free(rq);
	return KNOT_EOK;
}

int xfr_task_setaddr(knot_ns_xfr_t *rq, sockaddr_t *to, sockaddr_t *from)
{
	if (!rq) {
		return KNOT_EINVAL;
	}

	memcpy(&rq->addr, to, sizeof(sockaddr_t));
	if (from) memcpy(&rq->saddr, from, sizeof(sockaddr_t));
	return KNOT_EOK;
}

char *xfr_remote_str(const sockaddr_t *addr, const char *key)
{
	if (!addr) {
		return NULL;
	}

	/* Prepare address strings. */
	char r_addr[SOCKADDR_STRLEN];
	int r_port = sockaddr_portnum(addr);
	sockaddr_tostr(addr, r_addr, sizeof(r_addr));

	/* Prepare key strings. */
	char *tag = "";
	char *q = "'";
	if (key) {
		tag = " key "; /* Prefix */
	} else {
		key = tag; /* Both empty. */
		q = tag;
	}

	return sprintf_alloc("'%s@%d'%s%s%s%s", r_addr, r_port, tag, q, key, q);
}
