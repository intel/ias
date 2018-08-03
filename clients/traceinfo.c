/*
 *---------------------------------------------------------------------------
 * Filename: traceinfo.c
 *---------------------------------------------------------------------------
 * Copyright (c) 2012, Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *----------------------------------------------------------------------------
 * Simple Wayland client that displays timing information from the compositor
 * trace log.
 *----------------------------------------------------------------------------
 */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include "../shared/config-parser.h"

#include "trace-reporter-client-protocol.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct trace_reporter *reporter;
};

struct trace_event {
	char *msg;
	struct timeval evtime;
	int level;

	struct trace_event *parent;
	struct wl_list subevents;
	struct wl_list link;
};

static struct trace_event *first_event;
static struct trace_event *last_event;

#define wl_list_last(head, type, member) \
	wl_list_empty(head) \
       ? NULL \
       : ({ type *iter; wl_list_for_each(iter, head, member) { break; } iter; })

/*
 * print_event()
 *
 * Prints event timing information for event and all children.  Three times
 * are given:
 *   - total      -- time since first event
 *   - self       -- time of this event since last event of any level
 *   - w/children -- time of this event since last event of same level (i.e.,
 *                   including child event times)
 *
 * 'total' is calculated as the difference between this event's timestamp and
 * first_event's timestamp.
 *
 * 'self' is calculated as the difference between this event's timestamp and
 * the last child event's timestamp.  If there are no child events, then
 * the value 'w/children' is used instead.
 *
 * 'w/children' is calculated as the difference between this event's timestamp
 * and the incoming 'prevtime' parameter, which represents the time of the
 * last sibling event.
 */
static void
print_event(struct trace_event *ev, struct timeval *prevtime)
{
	struct trace_event *child;
	struct timeval t;
	struct timeval *childprev = prevtime;

	/* Recurse over child events first */
	wl_list_for_each_reverse(child, &ev->subevents, link) {
		print_event(child, childprev);
		childprev = &child->evtime;
	}

	/* Total time */
	timersub(&ev->evtime, &first_event->evtime, &t);
	printf("%3lu.%03lu  ", t.tv_sec, (t.tv_usec + 500) / 1000);

	/* Time w/ children included */
	timersub(&ev->evtime, prevtime, &t);
	printf("%3lu.%03lu  ", t.tv_sec, (t.tv_usec + 500) / 1000);

	/* Self time */
	timersub(&ev->evtime, childprev, &t);
	printf("%3lu.%03lu  ", t.tv_sec, (t.tv_usec + 500) / 1000);

	printf("%s\n", ev->msg);
}

/*
 * trace_reporter_tracepoint()
 *
 * Handle a tracepoint event.  We assume that the amount of whitespace at the
 * beginning of a message indicates event/subevent relationships.  Note that
 * this is somewhat counter-intuitive --- parent events don't appear until
 * after all of their children events.  E.g., you might se a sequence like
 *
 *    ...
 *    Do something unrelated
 *     - open config
 *     - read config
 *     - close config
 *    Load configuration
 *    ...
 */
static void
trace_reporter_tracepoint(void *data,
		struct trace_reporter *reporter,
		const char *message,
		uint32_t time_sec,
		uint32_t time_usec)
{
	struct trace_event *event, *parent;
	int level = 0;

	/* Figure out event nesting level from leading whitespace */
	while (message[level] && isspace(message[level])) {
		level++;
	}

	/*
	 * Is this a shallower event level than the last event we saw?  If so, it
	 * means that this is a parent event to the previous event and an event
	 * structure has already been allocated; we just need to fill it in.
	 * Otherwise, we should allocate a new event structure to hold this event.
	 */
	if (last_event && level < last_event->level) {
		event = last_event->parent;
	} else {
		event = calloc(1, sizeof *event);
		if (!event) {
			fprintf(stderr, "Out of memory!\n");
			exit(1);
		}

		wl_list_init(&event->subevents);
	}

	/*
	 * Is this a deeper event level than the last event we saw?  If so, open
	 * a new parent event for it (we'll finish filling it in when we actually
	 * encounter the parent event.
	 */
	if (!last_event || level > last_event->level) {
		parent = calloc(1, sizeof *parent);
		if (!parent) {
			fprintf(stderr, "Out of memory!\n");
			exit(1);
		}

		parent->level = -1;
		event->parent = parent;
		wl_list_init(&parent->subevents);
		wl_list_insert(&parent->subevents, &event->link);
		if (last_event) {
			parent->parent = last_event->parent;
			wl_list_insert(&last_event->parent->subevents, &parent->link);
		}
	}

	/*
	 * If this event is the same level as the last event we saw then it is
	 * a sibling event and shares the same parent (which we've already
	 * created a structure for).  Just add ourselves to the parent's
	 * subevent list.
	 */
	if (last_event && level == last_event->level) {
		event->parent = last_event->parent;
		wl_list_insert(&event->parent->subevents, &event->link);
	}

	/* Fill in the event */
	event->msg = strdup(message);
	if (!event->msg) {
		fprintf(stderr, "Out of memory!\n");
		exit(1);
	}
	event->level = level;
	event->evtime.tv_sec = time_sec;
	event->evtime.tv_usec = time_usec;

	last_event = event;
	if (!first_event) {
		first_event = event;
	}
}

/*
 * trace_reporter_trace_end()
 *
 * Handle completion of a trace log.  When we receive this event we will grab
 * the top level parent event (the implicit parent which was never actually
 * encountered in the log and has a level of -1) and then traverse its tree
 * of children, accumulating and printing timing information.
 */
static void
trace_reporter_trace_end(void *data,
		struct trace_reporter *reporter)
{
	struct trace_event *ev = first_event;
	struct trace_event *child;
	struct timeval *prevtime;

	if (!ev) {
		printf("No timing information logged.\n");
		return;
	}

	/*
	 * Get the parent of all top-level events (this event is never actually
	 * present in the log, but we created it automatically to be the parent
	 * when we encountered the first toplevel event.
	 */
	while (ev->parent) {
		ev = ev->parent;
	}
	assert(ev->level == -1);

	printf("   Timing Info\n");
	printf("  Total  w/Child     Self  Event\n");
	printf("=======  =======  =======  =====================================================\n");

	/* Walk the child events and print them out */
	prevtime = &first_event->evtime;
	wl_list_for_each_reverse(child, &ev->subevents, link) {
		print_event(child, prevtime);
		prevtime = &child->evtime;
	}
}

static const struct trace_reporter_listener listener = {
	trace_reporter_tracepoint,
	trace_reporter_trace_end,
};

/*
 * display_handle_global()
 *
 * Handle global object advertisements from the compositor.  We only care
 * about the trace reporter object here.
 */
static void
display_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		      const char *interface, uint32_t version)
{
	struct wayland *w = data;

	if (!strcmp(interface, "trace_reporter")) {
		w->reporter = wl_registry_bind(registry,
				id,
				&trace_reporter_interface,
				1);
		trace_reporter_add_listener(w->reporter, &listener, w);
	}
}

static const struct wl_registry_listener registry_listener = {
	display_handle_global,
};

int
main(int argc, char **argv)
{
	struct wayland wayland = { 0 };
	int remaining_argc;
	uint32_t clearmode;

	/* cmdline options */
	int32_t dump_stdout = 0;
	int32_t clear = 0;

	const struct weston_option options[] = {
		{ WESTON_OPTION_BOOLEAN, "stdout", 0, &dump_stdout },
		{ WESTON_OPTION_BOOLEAN, "clear", 'c', &clear },
	};

	remaining_argc = parse_options(options, ARRAY_LENGTH(options), &argc, argv);

	if (remaining_argc > 1 || argc > 3) {
		printf("Usage:\n");
		printf("  traceinfo [--dump-stdout] [--clear | -c]\n");

		return -1;
	}

	wayland.display = wl_display_connect(NULL);
	if (!wayland.display) {
		fprintf(stderr, "Failed to open wayland display\n");
		return -1;
	}

	/* Listen for global object broadcasts */
	wayland.registry = wl_display_get_registry(wayland.display);
	wl_registry_add_listener(wayland.registry, &registry_listener, &wayland);
	wl_display_dispatch(wayland.display);
	wl_display_roundtrip(wayland.display);

	/* Make sure the trace reporter module is present */
	if (!wayland.reporter) {
		fprintf(stderr, "Trace reporter interface not advertised by compositor\n");
		wl_display_disconnect(wayland.display);
		return -1;
	}

	if (clear) {
		clearmode = TRACE_REPORTER_LOG_REPORT_CLEAR;
	} else {
		clearmode = TRACE_REPORTER_LOG_REPORT_PRESERVE;
	}

	if (dump_stdout) {
		trace_reporter_stdout_report(wayland.reporter, clearmode);
	} else {
		trace_reporter_event_report(wayland.reporter, clearmode);
	}

	wl_display_roundtrip(wayland.display);
	wl_display_disconnect(wayland.display);

	return 0;
}
