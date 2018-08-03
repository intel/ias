/*
 *-----------------------------------------------------------------------------
 * Filename: trace-reporter.h
 *-----------------------------------------------------------------------------
 * Copyright 2012-2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *-----------------------------------------------------------------------------
 * Description:
 *   Weston module for reporting timing/tracing information.
 *-----------------------------------------------------------------------------
 */

#ifndef _WAYLAND_TRACE_REPORTER_H_
#define _WAYLAND_TRACE_REPORTER_H_

#include "config.h"


/*
 * Lightweight tracing support.
 *
 * We want the ability to measure timing information about various components
 * of compositor startup while having a minimal impact on the actual startup
 * time.  We support low-overhead tracepoints by just storing a pointer to
 * a string literal and a timestamp into a fixed size circular buffer.  This
 * circular buffer behaves similarly to dmesg; if too much tracing information
 * is stored, the older entries are replaced by new ones.
 *
 * It is expected that this tracing information will be analyzed later
 * (either via a loaded weston module or manually via gdb) to determine
 * bottlenecks in system startup.
 */
struct trace_info {
	const char *msg;
	struct timeval timestamp;
};

extern struct trace_info *__trace_log;
extern unsigned int *__tstart, *__tend;

/*
 * TRACEPOINT()
 *
 * Records a timestamp and message in the system tracing buffer.  This
 * information can be analyzed later to determine startup bottlenecks.
 */
static inline void
TRACEPOINT(const char *msg)
{
#ifdef ENABLE_TRACING
	__trace_log[*__tend].msg = msg;
	gettimeofday(&__trace_log[*__tend].timestamp, NULL);

	(*__tend)++;
	(*__tend) %= TRACE_BUFFER_SIZE;

	/*
	 * If we've filled the buffer, each new entry replaces the oldest
	 * entry in the buffer.
	 */
	if (*__tend == *__tstart) {
		(*__tstart)++;
		(*__tstart) %= TRACE_BUFFER_SIZE;
	}
#endif
}

/*
 * TRACEPOINT_ONCE()
 *
 * Records tracing information only the first time this tracepoint
 * (or any other tracepoint sharing the same name) is hit.
 */
#define TRACEPOINT_ONCE(msg) {                       \
	static int first_ ## __FILE__ ## __LINE__ = 1;   \
	if (first_ ## __FILE__ ## __LINE__) {            \
		TRACEPOINT(msg);                             \
		first_ ## __FILE__ ## __LINE__ = 0;          \
	}                                                \
}

/*
 * TRACING_MODULE_DECLARATIONS
 *
 * Should be placed in the global variable section of any module
 * dlopen()'d by weston that wants to make use of the tracepoint
 * framework.  Provides local pointers for the compositor's
 * global symbols.
 */
#define TRACING_DECLARATIONS        \
	struct trace_info *__trace_log; \
	unsigned int *__tstart, *__tend;

/*
 * TRACING_MODULE_INIT()
 *
 * Should be called by any weston module using tracepoints, before
 * the first TRACEPOINT() call is hit.
 */
#define TRACING_MODULE_INIT() {                                   \
	void *thisprog = dlopen(NULL, RTLD_LAZY | RTLD_GLOBAL);       \
	__tstart = (unsigned int *)dlsym(thisprog, "__trace_start");  \
	assert(__tstart);                                             \
	__tend = (unsigned int *)dlsym(thisprog, "__trace_end");      \
	assert(__tend);                                               \
	__trace_log = dlsym(thisprog, "__trace_buffer");              \
	assert(__trace_log);                                          \
}

#endif //_WAYLAND_TRACE_REPORTER_H_
