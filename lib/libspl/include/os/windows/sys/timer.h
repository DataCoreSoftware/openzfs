/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields elsewhere to your own identifying information: Portions
 * Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#ifndef _LIBSPL_TIMER_H
#define	_LIBSPL_TIMER_H

/*
 * Windows userland timer macros.
 * gethrtime() and nanosleep() are implemented in lib/libspl/os/windows/posix.c
 * hz is defined in sys/param.h (119, matching gethrtime() >> 23 approximation)
 */

#include <sys/time.h>
#include <sys/param.h>

extern int64_t gethrtime(void);

#define	ddi_get_lbolt()		(gethrtime() >> 23)
#define	ddi_get_lbolt64()	(gethrtime() >> 23)

#define	ddi_time_before(a, b)		((clock_t)(a) - (clock_t)(b) < 0)
#define	ddi_time_after(a, b)		ddi_time_before(b, a)
#define	ddi_time_before_eq(a, b)	(!ddi_time_after(a, b))
#define	ddi_time_after_eq(a, b)		ddi_time_before_eq(b, a)

#define	ddi_time_before64(a, b)		((int64_t)(a) - (int64_t)(b) < 0)
#define	ddi_time_after64(a, b)		ddi_time_before64(b, a)
#define	ddi_time_before_eq64(a, b)	(!ddi_time_after64(a, b))
#define	ddi_time_after_eq64(a, b)	ddi_time_before_eq64(b, a)

#define	SEC_TO_TICK(sec)	((sec) * hz)
#define	MSEC_TO_TICK(msec)	(howmany((hrtime_t)(msec) * hz, MILLISEC))
#define	USEC_TO_TICK(usec)	(howmany((hrtime_t)(usec) * hz, MICROSEC))
#define	NSEC_TO_TICK(nsec)	(howmany((hrtime_t)(nsec) * hz, NANOSEC))

#define	TICK_TO_SEC(ticks)	((ticks) / hz)
#define	TICK_TO_MSEC(ticks)	((ticks) * MILLISEC / hz)

#define	usleep_range(min, max)								\
	do {										\
		struct timespec ts;							\
		ts.tv_sec = (min) / MICROSEC;					\
		ts.tv_nsec = USEC2NSEC(min);					\
		(void) nanosleep(&ts, NULL);					\
	} while (0)

#endif	/* _LIBSPL_TIMER_H */
