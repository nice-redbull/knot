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
 * \file src/knot/common.h
 *
 * \author Lubos Slovak <lubos.slovak@nic.cz>
 *
 * \brief Common macros, includes and utilities.
 *
 * \addtogroup utils
 * @{
 */

#ifndef _KNOTD_COMMON_H_
#define _KNOTD_COMMON_H_

#include <signal.h>
#include <stdint.h>

/*
 * Common types and constants.
 */

#ifndef UINT_DEFINED
typedef unsigned int uint; /*!< \brief Unsigned. */
#define UINT_DEFINED
#endif

#define PROJECT_EXENAME "knotd"
#define PROJECT_EXEC SBINDIR "/" PROJECT_EXENAME /*!< \brief  Project executable. */
#define PID_FILE "knot.pid" /*!< \brief Server PID file name. */

/*
 * Server.
 */

#define CPU_ESTIMATE_MAGIC 0 /*!< \brief Extra threads to the number of cores.*/
#define DEFAULT_THR_COUNT 2  /*!< \brief Default thread count. */
#define TCP_BACKLOG_SIZE 10   /*!< \brief TCP listen backlog size. */
#define XFR_THREADS_COUNT 3  /*!< \brief Number of threads for XFR handler. */
#define RECVMMSG_BATCHLEN 64 /*!< \brief Define for recvmmsg() batch size. */

///*! \brief If defined, the statistics module will be enabled. */
//#define STAT_COMPILE

/*
 * Common includes.
 */

#include "common/latency.h"
#include "common/print.h"
#include "common/log.h"
#include "common/errcode.h"
#include "knot/other/debug.h"

/*! \brief Eliminate compiler warning with unused parameters. */
#define UNUSED(param) (void)(param)

/* Optimisation macros. */
#ifndef knot_likely
/*! \brief Optimize for x to be true value. */
#define knot_likely(x)       __builtin_expect((x),1)
#endif
#ifndef knot_unlikely
/*! \brief Optimize for x to be false value. */
#define knot_unlikely(x)     __builtin_expect((x),0)
#endif

/*! \todo Refactor theese. We should have an allocator function handling this.*/
#ifndef ERR_ALLOC_FAILED
#define ERR_ALLOC_FAILED fprintf(stderr, "Allocation failed at %s:%d (%s)\n", \
				 __FILE__, __LINE__, PACKAGE_STRING)
#endif

#ifndef CHECK_ALLOC_LOG
#define CHECK_ALLOC_LOG(var, ret) \
	do { \
		if ((var) == NULL) { \
			ERR_ALLOC_FAILED; \
			return (ret); \
		} \
	} while (0)
#endif

#ifndef CHECK_ALLOC
#define CHECK_ALLOC(var, ret) \
	do { \
		if ((var) == NULL) { \
			return (ret); \
		} \
	} while (0)
#endif

/* Workarounds for clock_gettime() not available on some platforms. */
#ifdef HAVE_CLOCK_GETTIME
#define time_now(x) clock_gettime(CLOCK_MONOTONIC, (x))
typedef struct timespec timev_t;
#elif HAVE_GETTIMEOFDAY
#define time_now(x) gettimeofday((x), NULL)
typedef struct timeval timev_t;
#else
#error Neither clock_gettime() nor gettimeofday() found. At least one is required.
#endif

#endif /* _KNOTD_COMMON_H_ */

/*! @} */
