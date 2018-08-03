/*
 *---------------------------------------------------------------------------
 * Filename: extension_test_client.c
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
 * Simple test app to exercise the Wayland protocol extension exposed by
 * the example layout plugin.
 *----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include "new-extension-client-protocol.h"

struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct my_plugin *plugin;
	struct wl_callback *selection_callback;
	uint32_t mask;
};

static void selection_event(void *data,
		struct my_plugin *plugin,
		uint32_t cell)
{
	printf("Cell %d selected\n", cell);
}

static const struct my_plugin_listener plugin_listener = {
	selection_event
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		      const char *interface, uint32_t version)
{
	struct wayland *w = data;

	if (strcmp(interface, "my_plugin") == 0) {
		printf("Plugin-provided protocol extension detected.\n");
		w->plugin = wl_registry_bind(registry, id, &my_plugin_interface, 1);
		my_plugin_add_listener(w->plugin, &plugin_listener, NULL);
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
};

int
main(int argc, char **argv)
{
	struct wayland wayland = { 0 };

	/* Connect to compositor */
	wayland.display = wl_display_connect(NULL);
	if (!wayland.display) {
		fprintf(stderr, "Failed to open wayland display\n");
		return -1;
	}

	/* Listen for global object broadcasts */
	wayland.registry = wl_display_get_registry(wayland.display);
	wl_registry_add_listener(wayland.registry, &registry_listener, NULL);
	wl_display_dispatch(wayland.display);
	wl_display_roundtrip(wayland.display);

	/* Is the plugin's extension present?  If not, give up. */
	if (!wayland.plugin) {
		printf("Plugin-provided extension not present.  Terminating.\n");
		return 0;
	}

	/* What are we supposed to do? */
	if (argc == 1) {
		/* No cmdline params; just listen for events */
		while (1) {
			wl_display_dispatch(wayland.display);
		}
	} else if (strcmp(argv[1], "cw") == 0) {
		my_plugin_rotate_cells(wayland.plugin, MY_PLUGIN_ROTATE_CELLS_METHOD_CW);
	} else if (strcmp(argv[1], "ccw") == 0) {
		my_plugin_rotate_cells(wayland.plugin, MY_PLUGIN_ROTATE_CELLS_METHOD_CCW);
	} else if (strcmp(argv[1], "none") == 0) {
		my_plugin_rotate_cells(wayland.plugin, MY_PLUGIN_ROTATE_CELLS_METHOD_NONE);
	}

	wl_display_flush(wayland.display);
	wl_display_disconnect(wayland.display);

	return 0;
}
