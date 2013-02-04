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
/*!
 * \file netio.h
 *
 * \author Daniel Salzman <daniel.salzman@nic.cz>
 *
 * \brief Networking abstraction for utilities.
 *
 * \addtogroup knot_utils
 * @{
 */

#ifndef _UTILS__NETIO_H_
#define _UTILS__NETIO_H_

#include <stdint.h>			// uint_t

#include "common/lists.h"		// node
#include "utils/common/params.h"	// params_t

/*! \brief Structure containing server information. */
typedef struct {
	/*!< List node (for list container). */
	node	n;
	/*!< Name or address of the server. */
	char	*name;
	/*!< Name or number of the service. */
	char	*service;
} server_t;

server_t* server_create(const char *name, const char *service);

void server_free(server_t *server);

int get_socktype(const params_t *params, const uint16_t type);

int send_msg(const params_t *params, const uint16_t type,
             const server_t *server, const uint8_t *buf, size_t buf_len);

int receive_msg(const params_t *params, const uint16_t type,
                int sockfd, uint8_t *buf, size_t buf_len);

#endif // _UTILS__NETIO_H_