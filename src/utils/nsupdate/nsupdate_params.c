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

#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdarg.h>

#include "utils/nsupdate/nsupdate_params.h"
#include "utils/common/msg.h"
#include "utils/common/netio.h"
#include "libknot/util/descriptor.h"
#include "common/errcode.h"
#include "libknot/packet/packet.h"

#define DEFAULT_RETRIES_NSUPDATE	3
#define DEFAULT_TIMEOUT_NSUPDATE	1

static void parse_rr(const scanner_t *s) {
	return; /* Dummy */
}

static void parse_err(const scanner_t *s) {
	ERR("failed to parse RR, %s\n", knot_strerror(s->error_code));
}

static int parser_set_default(scanner_t *s, const char *fmt, ...)
{
	/* Format string. */
	char buf[512]; /* Must suffice for domain name and TTL. */
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (n < 0 || n >= sizeof(buf)) {
		return KNOT_ESPACE;
	}

	/* fmt must contain newline */
	if (scanner_process(buf, buf + n, 0, s) < 0) {
		return KNOT_EPARSEFAIL;
	}

	return KNOT_EOK;
}

static int nsupdate_params_init(params_t *params)
{
	memset(params, 0, sizeof(*params));

	/* Specific data ptr. */
	params->d = malloc(sizeof(nsupdate_params_t));
	if (!params->d) return KNOT_ENOMEM;
	memset(params->d, 0, sizeof(nsupdate_params_t));
	nsupdate_params_t *npar = NSUP_PARAM(params);

	/* Lists */
	init_list(&params->servers);
	init_list(&npar->qfiles);

	/* Default values. */
	params->class_num = KNOT_CLASS_IN;
	params->operation = OPERATION_UPDATE;
	params->protocol = PROTO_ALL;
	params->port = strdup(DEFAULT_DNS_PORT);
	params->udp_size = DEFAULT_UDP_SIZE;
	params->retries = DEFAULT_RETRIES_NSUPDATE;
	params->wait = DEFAULT_TIMEOUT_NSUPDATE;
	params->format = FORMAT_NSUPDATE;
	params->type_num = KNOT_RRTYPE_SOA;

	/* Create default server. */
	if (params_parse_server(DEFAULT_IPV4_NAME, &params->servers,
	                        params->port)
	    != KNOT_EOK) {
		return KNOT_EINVAL;
	}

	/* Initialize RR parser. */
	npar->rrp = scanner_create(".");
	if (!npar->rrp) return KNOT_ENOMEM;
	npar->rrp->process_record = parse_rr;
	npar->rrp->process_error = parse_err;
	npar->rrp->default_class = params->class_num;
	nsupdate_params_set_ttl(params, 0);
	nsupdate_params_set_origin(params, ".");
	return KNOT_EOK;
}

void nsupdate_params_clean(params_t *params)
{
	if (params == NULL) {
		return;
	}

	/* Free specific structure. */
	nsupdate_params_t* npar = NSUP_PARAM(params);
	if (npar) {
		free(npar->zone);
		if (npar->rrp) {
			scanner_free(npar->rrp);
		}

		/* Free qfiles. */
		strnode_t *n = NULL, *nxt = NULL;
		WALK_LIST_DELSAFE(n, nxt, npar->qfiles) {
			free(n);
		}

		free(npar);
		params->d = NULL;
	}

	/* Free server list. */
	server_t *n = NULL, *nxt = NULL;
	WALK_LIST_DELSAFE(n, nxt, params->servers) {
		server_free(n);
	}

	free(params->port);

	/* Free TSIG key. */
	knot_dname_free(&params->key.name);
	free(params->key.secret);

	/* Clean up the structure. */
	memset(params, 0, sizeof(params_t));
}

static void nsupdate_params_help(int argc, char *argv[])
{
	printf("Usage: %s [-d] [-v] [-y [hmac:]name:key] [-p port] "
	       "[-t timeout] [-r retries] [filename]\n",
	       argv[0]);
}

int nsupdate_params_parse(params_t *params, int argc, char *argv[])
{
	int opt = 0;
	int ret = KNOT_EOK;

	if (params == NULL || argv == NULL) {
		return KNOT_EINVAL;
	}

	ret = nsupdate_params_init(params);
	if (ret != KNOT_EOK) {
		return ret;
	}

	/* Fetch default server. */
	server_t *srv = TAIL(params->servers);

	/* Command line options processing. */
	while ((opt = getopt(argc, argv, "dDvp:t:r:y:k:")) != -1) {
		switch (opt) {
		case 'd':
		case 'D': /* Extra debugging. */
			msg_enable_debug(1);
			params_flag_verbose(params);
			break;
		case 'v':
			params_flag_tcp(params);
			break;
		case 'p':
			free(srv->service);
			srv->service = strdup(optarg);
			if (!srv->service) {
				ERR("failed to set default port '%s'\n", optarg);
				return KNOT_ENOMEM;
			}
			break;
		case 'r':
			ret = params_parse_num(optarg, &params->retries);
			if (ret != KNOT_EOK) return ret;
			break;
		case 't':
			ret = params_parse_interval(optarg, &params->wait);
			if (ret != KNOT_EOK) return ret;
			break;
		case 'y':
			ret = params_parse_tsig(optarg, &params->key);
			if (ret != KNOT_EOK) return ret;
			break;
		case 'k':
			ret = params_parse_keyfile(optarg, &params->key);
			if (ret != KNOT_EOK) return ret;
			break;
		default:
			nsupdate_params_help(argc, argv);
			return KNOT_ENOTSUP;
		}
	}

	/* Process non-option parameters. */
	nsupdate_params_t* npar = NSUP_PARAM(params);
	for (; optind < argc; ++optind) {
		strnode_t *n = malloc(sizeof(strnode_t));
		if (!n) { /* Params will be cleaned on exit. */
			return KNOT_ENOMEM;
		}
		n->str = argv[optind];
		add_tail(&npar->qfiles, &n->n);
	}

	return ret;
}

int nsupdate_params_set_ttl(params_t *params, uint32_t ttl)
{
	nsupdate_params_t* npar = NSUP_PARAM(params);
	int ret = parser_set_default(npar->rrp, "$TTL %u\n", ttl);
	if (ret == KNOT_EOK) {
		params->ttl = ttl;
	} else {
		ERR("failed to set default TTL, %s\n", knot_strerror(ret));
	}
	return ret;
}

int nsupdate_params_set_origin(params_t *params, const char *origin)
{
	nsupdate_params_t* npar = NSUP_PARAM(params);
	int ret = parser_set_default(npar->rrp, "$ORIGIN %s\n", origin);
	if (ret == KNOT_EOK) {
		if (npar->zone) free(npar->zone);
		npar->zone = strdup(origin);
	} else {
		ERR("failed to set default origin, %s\n", knot_strerror(ret));
	}
	return ret;
}
