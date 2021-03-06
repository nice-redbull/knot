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
#ifdef HAVE_KQUEUE

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/event.h>
#include <sys/time.h>
#include <time.h>

#include "fdset_kqueue.h"

#define OS_FDS_CHUNKSIZE 8   /*!< Number of pollfd structs in a chunk. */
#define OS_FDS_KEEPCHUNKS 32 /*!< Will attempt to free memory when reached. */

struct fdset_t {
	fdset_base_t _base;
	int kq;
	struct kevent *events;
	struct kevent *revents;
	size_t nfds;
	size_t reserved;
	size_t rreserved;
	size_t polled;
};

fdset_t *fdset_kqueue_new()
{
	fdset_t *set = malloc(sizeof(fdset_t));
	if (set) {
		/* Blank memory. */
		memset(set, 0, sizeof(fdset_t));

		/* Create kqueue fd. */
		set->kq = kqueue();
		if (set->kq < 0) {
			free(set);
			set = 0;
		}
	}

	return set;
}

int fdset_kqueue_destroy(fdset_t * fdset)
{
	if(fdset == NULL) {
		return -1;
	}

	/* Teardown kqueue. */
	close(fdset->kq);

	/* OK if NULL. */
	free(fdset->revents);
	free(fdset->events);
	free(fdset);
	return 0;
}

int fdset_kqueue_add(fdset_t *fdset, int fd, int events)
{
	if (fdset == NULL || fd < 0 || events <= 0) {
		return -1;
	}

	/* Realloc needed. */
	int ret = 0;
	ret += mreserve((char **)&fdset->events, sizeof(struct kevent),
	                fdset->nfds + 1, OS_FDS_CHUNKSIZE, &fdset->reserved);
	ret += mreserve((char **)&fdset->revents, sizeof(struct kevent),
	                fdset->nfds + 1, OS_FDS_CHUNKSIZE, &fdset->rreserved);
	if (ret != 0) {
		return ret;
	}

	/* Add to kqueue set. */
	int evfilt = EVFILT_READ;
	EV_SET(&fdset->events[fdset->nfds], fd, evfilt,
	       EV_ADD|EV_ENABLE, 0, 0, 0);
	memset(fdset->revents + fdset->nfds, 0, sizeof(struct kevent));

	++fdset->nfds;
	return 0;
}

int fdset_kqueue_remove(fdset_t *fdset, int fd)
{
	if (fdset == NULL || fd < 0) {
		return -1;
	}

	/* Find in set. */
	int pos = -1;
	for (int i = 0; i < fdset->nfds; ++i) {
		if (fdset->events[i].ident == fd) {
			pos = i;
			break;
		}
	}

	if (pos < 0) {
		return -1;
	}

	/* Remove filters. */
	EV_SET(&fdset->events[pos], fd, EVFILT_READ,
	       EV_DISABLE|EV_DELETE, 0, 0, 0);

	/* Attempt to remove from set. */
	size_t remaining = ((fdset->nfds - pos) - 1) * sizeof(struct kevent);
	memmove(fdset->events + pos, fdset->events + (pos + 1), remaining);

	/* Attempt to remove from revents set. */
	pos = -1;
	for (int i = 0; i < fdset->nfds; ++i) {
		if (fdset->events[i].ident == fd) {
			pos = i;
			break;
		}
	}
	if (pos >= 0) {
		size_t remaining = ((fdset->nfds - pos) - 1) * sizeof(struct kevent);
		memmove(fdset->revents + pos, fdset->revents + (pos + 1), remaining);
	}


	/* Overwrite current item. */
	--fdset->nfds;

	/* Trim excessive memory if possible (retval is not interesting). */
	mreserve((char **)&fdset->events, sizeof(struct kevent),
	         fdset->nfds, OS_FDS_CHUNKSIZE, &fdset->reserved);
	mreserve((char **)&fdset->revents, sizeof(struct kevent),
	         fdset->nfds, OS_FDS_CHUNKSIZE, &fdset->rreserved);

	return 0;
}

int fdset_kqueue_wait(fdset_t *fdset, int timeout)
{
	if (fdset == NULL || fdset->nfds < 1 || fdset->events == NULL) {
		return -1;
	}

	/* Set timeout. */
	struct timespec tmval;
	struct timespec *tm = NULL;
	if (timeout == 0) {
		tmval.tv_sec = tmval.tv_nsec = 0;
		tm = &tmval;
	} else if (timeout > 0) {
		tmval.tv_sec = timeout / 1000;     /* ms -> s */
		timeout -= tmval.tv_sec * 1000;    /* Cut off */
		tmval.tv_nsec = timeout * 1000000L; /* ms -> ns */
		tm = &tmval;
	}

	/* Poll new events. */
	fdset->polled = 0;
	int nfds = kevent(fdset->kq, fdset->events, fdset->nfds,
	                  fdset->revents, fdset->nfds, tm);

	/* Check. */
	if (nfds < 0) {
		return -1;
	}

	/* Events array is ordered from 0 to nfds. */
	fdset->polled = nfds;
	return nfds;
}

int fdset_kqueue_begin(fdset_t *fdset, fdset_it_t *it)
{
	if (fdset == NULL || it == NULL) {
		return -1;
	}

	/* Find first. */
	it->pos = 0;
	it->fd = -1;
	return fdset_next(fdset, it);
}

int fdset_kqueue_end(fdset_t *fdset, fdset_it_t *it)
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

	/* No end found, ends on the beginning. */
	size_t nid = fdset->polled - 1;
	it->fd = fdset->revents[nid].ident;
	it->pos = nid;
	it->events = 0; /*! \todo Map events. */
	return -1;
}

int fdset_kqueue_next(fdset_t *fdset, fdset_it_t *it)
{
	if (fdset == NULL || it == NULL || fdset->polled < 1) {
		return -1;
	}

	/* Check boundaries. */
	if (it->pos >= fdset->polled || it->pos >= fdset->nfds) {
		return -1;
	}

	/* Select next. */
	size_t nid = it->pos++;
	it->fd = fdset->revents[nid].ident;
	it->events = 0; /*! \todo Map events. */
	return 0;
}

const char* fdset_kqueue_method()
{
	return "kqueue";
}

/* Package APIs. */
struct fdset_backend_t FDSET_KQUEUE = {
	.fdset_new = fdset_kqueue_new,
	.fdset_destroy = fdset_kqueue_destroy,
	.fdset_add = fdset_kqueue_add,
	.fdset_remove = fdset_kqueue_remove,
	.fdset_wait = fdset_kqueue_wait,
	.fdset_begin = fdset_kqueue_begin,
	.fdset_end = fdset_kqueue_end,
	.fdset_next = fdset_kqueue_next,
	.fdset_method = fdset_kqueue_method
};

#endif
