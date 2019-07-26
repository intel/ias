/*
 *---------------------------------------------------------------------------
 * Filename: ias-metrics.c
 *---------------------------------------------------------------------------
 * Copyright (c) 2019, Intel Corporation.
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
 * Simple example on how to use ias_metrics
 *----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>
#include <sys/poll.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "../shared/config-parser.h"
#include "../shared/helpers.h"

#include "ias-shell-client-protocol.h"
#include <sys/types.h>
#include <unistd.h>

struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct ias_metrics *metrics;
};

struct wayland wayland = { 0 };

void ias_metrics_output_info(void *data,
		struct ias_metrics *ias_metrics,
		uint32_t time,
		const char *output,
		uint32_t output_id,
		uint32_t flips)
{
	printf("M %s [id:%d] flips:%d time:%u ms\n", output,  output_id, flips, time);
}

void ias_metrics_process_info(void *data,
		struct ias_metrics *ias_metrics,
		uint32_t surf_id,
		const char *title,
		uint32_t pid,
		const char *pname,
		uint32_t output_id,
		uint32_t time,
		uint32_t frames,
		uint32_t flips)
{
	printf("P s:%8u pid:%5d pname:%-15s fr:%5d flp:%5d id:%u time:%u ms\n",
			surf_id, pid, pname, frames, flips, output_id, time);
}


static const struct ias_metrics_listener listener = {
	ias_metrics_output_info,
	ias_metrics_process_info,
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		const char *interface, uint32_t version)
{
	struct wayland *w = data;
	if (strcmp(interface, "ias_metrics") == 0) {
		w->metrics = wl_registry_bind(registry, id, &ias_metrics_interface, 1);
		ias_metrics_add_listener(w->metrics, &listener, w);
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
};



int
main(int argc, char **argv)
{
	int ret = 0;
	wayland.display = wl_display_connect(NULL);
	if (!wayland.display) {
		fprintf(stderr, "Failed to open wayland display\n");
		return -1;
	}
	wayland.registry = wl_display_get_registry(wayland.display);
	wl_registry_add_listener(wayland.registry, &registry_listener, &wayland);
	wl_display_dispatch(wayland.display);
	wl_display_roundtrip(wayland.display);
	ret = 0;
	while (ret != -1) {
		ret = wl_display_dispatch(wayland.display);
	}
	wl_display_disconnect(wayland.display);
	return ret;
}
