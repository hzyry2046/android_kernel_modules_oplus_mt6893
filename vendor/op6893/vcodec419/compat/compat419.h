/* SPDX-License-Identifier: GPL-2.0 */
/* 4.19 -> 6.6 API shims for the forward-ported mt6885 vcodec/vcu. */
#ifndef _COMPAT419_H
#define _COMPAT419_H

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include "v4l2-419-extra.h"

#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>

/* struct timeval / do_gettimeofday() were removed in 5.x (y2038). */
struct timeval {
	long tv_sec;
	long tv_usec;
};

static inline void do_gettimeofday(struct timeval *tv)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}

/* struct v4l2_buffer.timestamp is a __kernel_v4l2_timeval in-kernel on 6.6, and
 * the uapi v4l2_timeval_to_ns() helper is hidden from kernel code. */
static inline u64 c419_v4l2_timeval_to_ns(const struct __kernel_v4l2_timeval *tv)
{
	return (u64)tv->tv_sec * NSEC_PER_SEC + (u64)tv->tv_usec * NSEC_PER_USEC;
}

#endif
