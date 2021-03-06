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

#ifdef HAVE_POLL

#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <stddef.h>

#include "common/fdset_poll.h"

#define OS_FDS_CHUNKSIZE 8   /*!< Number of pollfd structs in a chunk. */

struct fdset_t {
	fdset_base_t _base;
	struct pollfd *fds;
	nfds_t nfds;
	size_t reserved;
	size_t polled;
	size_t begin;
};

fdset_t *fdset_poll_new()
{
	fdset_t *set = malloc(sizeof(fdset_t));
	if (set != NULL) {
		memset(set, 0, sizeof(fdset_t));
	}

	return set;
}

int fdset_poll_destroy(fdset_t * fdset)
{
	if(fdset == NULL) {
		return -1;
	}

	/* OK if NULL. */
	free(fdset->fds);
	free(fdset);
	return 0;
}

int fdset_poll_add(fdset_t *fdset, int fd, int events)
{
	if (fdset == NULL || fd < 0 || events <= 0) {
		return -1;
	}

	/* Realloc needed. */
	if (mreserve((char **)&fdset->fds, sizeof(struct pollfd),
	             fdset->nfds + 1, OS_FDS_CHUNKSIZE, &fdset->reserved) < 0) {
		return -1;
	}

	/* Append. */
	int nid = fdset->nfds++;
	fdset->fds[nid].fd = fd;
	fdset->fds[nid].events = POLLIN;
	fdset->fds[nid].revents = 0;
	return 0;
}

int fdset_poll_remove(fdset_t *fdset, int fd)
{
	if (fdset == NULL || fd < 0) {
		return -1;
	}

	/* Find file descriptor. */
	unsigned found = 0;
	size_t pos = 0;
	for (size_t i = 0; i < fdset->nfds; ++i) {
		if (fdset->fds[i].fd == fd) {
			found = 1;
			pos = i;
			break;
		}
	}

	/* Check. */
	if (!found) {
		return -1;
	}

	/* Overwrite current item. */
	size_t remaining = ((fdset->nfds - pos) - 1) * sizeof(struct pollfd);
	memmove(fdset->fds + pos, fdset->fds + (pos + 1), remaining);
	--fdset->nfds;

	/* Trim excessive memory if possible (retval is not interesting). */
	mreserve((char **)&fdset->fds, sizeof(struct pollfd), fdset->nfds,
	         OS_FDS_CHUNKSIZE, &fdset->reserved);

	return 0;
}

int fdset_poll_wait(fdset_t *fdset, int timeout)
{
	if (fdset == NULL || fdset->nfds < 1 || fdset->fds == NULL) {
		return -1;
	}

	/* Initialize pointers. */
	fdset->polled = 0;
	fdset->begin = 0;

	/* Poll for events. */
	int ret = poll(fdset->fds, fdset->nfds, timeout);
	if (ret < 0) {
		return -1;
	}

	/* Set pointers for iterating. */
	fdset->polled = ret;
	fdset->begin = 0;
	return ret;
}

int fdset_poll_begin(fdset_t *fdset, fdset_it_t *it)
{
	if (fdset == NULL || it == NULL) {
		return -1;
	}

	/* Find first. */
	it->pos = 0;
	return fdset_next(fdset, it);
}

int fdset_poll_end(fdset_t *fdset, fdset_it_t *it)
{
	if (fdset == NULL || it == NULL || fdset->nfds < 1) {
		return -1;
	}

	/* Check for polled events. */
	if (fdset->polled < 1) {
		it->fd = -1;
		it->pos = 0;
		return -1;
	}

	/* Trace last matching item from the end. */
	struct pollfd* pfd = fdset->fds + fdset->nfds - 1;
	while (pfd != fdset->fds) {
		if (pfd->events & pfd->revents) {
			it->fd = pfd->fd;
			it->pos = pfd - fdset->fds;
			return 0;
		}
	}

	/* No end found, ends on the beginning. */
	it->fd = -1;
	it->pos = 0;
	return -1;
}

int fdset_poll_next(fdset_t *fdset, fdset_it_t *it)
{
	if (fdset == NULL || it == NULL || fdset->nfds < 1) {
		return -1;
	}

	/* Find next with matching flags. */
	for (; it->pos < fdset->nfds; ++it->pos) {
		struct pollfd* pfd = fdset->fds + it->pos;
		if (pfd->events & pfd->revents) {
			it->fd = pfd->fd;
			it->events = pfd->revents;
			++it->pos; /* Next will start after current. */
			return 0;
		}
	}

	/* No matching event found. */
	it->fd = -1;
	it->pos = 0;
	return -1;
}

const char* fdset_poll_method()
{
	return "poll";
}

/* Package APIs. */
struct fdset_backend_t FDSET_POLL = {
	.fdset_new = fdset_poll_new,
	.fdset_destroy = fdset_poll_destroy,
	.fdset_add = fdset_poll_add,
	.fdset_remove = fdset_poll_remove,
	.fdset_wait = fdset_poll_wait,
	.fdset_begin = fdset_poll_begin,
	.fdset_end = fdset_poll_end,
	.fdset_next = fdset_poll_next,
	.fdset_method = fdset_poll_method
};

#endif
