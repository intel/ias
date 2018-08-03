/*
 *-----------------------------------------------------------------------------
 * Filename: trace-reporter.c
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

#include "compositor.h"
#include "trace-reporter.h"
#include "trace-reporter-server-protocol.h"

TRACING_DECLARATIONS;

/*
 * clear_log()
 *
 * Clears the tracing log data by setting start = end = 0.  This may be called
 * upon completion of either an event log or a stdout log if requested.
 *
 * Note that actual messages/timing information won't be cleared from the
 * log array, so this won't disrupt inspection of the log via an attached
 * gdb session.
 */
static void
clear_log(void)
{
    *__tstart = *__tend = 0;
}

/*
 * event_report()
 *
 * Send tracing report information via protocol events to the client.
 */
static void
event_report(struct wl_client *client,
		struct wl_resource *r,
		uint32_t clear)
{
    unsigned int i;

    for (i = *__tstart; i != *__tend; i++, i %= TRACE_BUFFER_SIZE) {
	trace_reporter_send_tracepoint(r,
		__trace_log[i].msg,
		__trace_log[i].timestamp.tv_sec,
		__trace_log[i].timestamp.tv_usec);
    }

    /* Send a completion event to let clients know we reached the end */
    trace_reporter_send_trace_end(r);

    if (clear) {
	clear_log();
    }
}

/*
 * stdout_report()
 *
 * Dump tracing report with minimal formatting to stdout.
 */
static void
stdout_report(struct wl_client *client,
		struct wl_resource *r,
		uint32_t clear)
{
    unsigned int i;
	unsigned long lastsec, lastusec;
	unsigned long firstsec, firstusec;
	unsigned long cursec, curusec;
	const char *msg;

	/* Set time since last event to timestamp of first event */
	lastsec  = firstsec  = __trace_log[*__tstart].timestamp.tv_sec;
	lastusec = firstusec = __trace_log[*__tstart].timestamp.tv_usec;

	printf("   Time  Cumulative  Event\n");
	printf("=======  ==========  ===========================================================\n");
	for (i = *__tstart; i != *__tend; i++, i %= TRACE_BUFFER_SIZE) {
		cursec  = __trace_log[i].timestamp.tv_sec;
		curusec = __trace_log[i].timestamp.tv_usec;
		msg     = __trace_log[i].msg;

		if (curusec < lastusec) {
			curusec += 1000000;
			cursec--;
		}

		/* Print times rounded to ms */
		printf("%3lu.%03lu     %3lu.%03lu  %s\n",
				(cursec - lastsec),
				(curusec - lastusec + 500) / 1000,
				(cursec - firstsec),
				(curusec - firstusec + 500) / 1000,
				msg);

		if (curusec >= 1000000) {
			curusec -= 1000000;
			cursec++;
		}

		lastsec = cursec;
		lastusec = curusec;
	}

	if (clear) {
		clear_log();
	}
}


/*
 * log_tracepoint()
 *
 * Requests that the compositor create an entry in the trace log.  This can be
 * useful for adding client-side events to the timing log to get a full-system
 * view of timing (e.g., how long after the compositor starts can the HMI
 * client provide its first graphical buffer?).
 *
 * Note that the tracepoint on the compositor side is triggered with a constant
 * string ("Client-requested tracepoint") rather than a string provided by
 * the client.  Trying to provide a string from the client would require
 * memory allocation and copying, which breaks the assumption that all trace
 * log messages are constant strings that don't need to be freed and can't
 * leak.
 */
static void
log_tracepoint(struct wl_client *client,
		struct wl_resource *r)
{
	TRACEPOINT_ONCE("Client-requested tracepoint");
}


static const struct trace_reporter_interface trace_reporter_implementation = {
	event_report,
	stdout_report,
	log_tracepoint,
};


/*
 * bind_trace_reporter()
 *
 * Bind the trace reporter object to a client-specific object ID.
 */
static void
bind_trace_reporter(struct wl_client *client,
		void *data,
		uint32_t version,
		uint32_t id)
{
	struct wl_resource *resource;
	resource = wl_resource_create(client, &trace_reporter_interface, 1, id);
	if (resource) {
		wl_resource_set_implementation(resource,
				&trace_reporter_implementation, data, NULL);
	}
}


/*
 * module_init()
 *
 * Initialization function for the trace reporter module.
 */
int wet_module_init(struct weston_compositor *compositor,
			int *argc, char *argv[]);
WL_EXPORT int wet_module_init(struct weston_compositor *compositor,
			int *argc, char *argv[])
{
	TRACING_MODULE_INIT();

	/* Expose the tracing_manager interface to clients */
	if (!wl_global_create(compositor->wl_display,
				&trace_reporter_interface,
				1,
				compositor,
				bind_trace_reporter)) {
		weston_log("Failed to add global trace reporter object!\n");
		return -1;
	}

	return 0;
}
