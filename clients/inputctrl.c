/*
 *---------------------------------------------------------------------------
 * Filename: inputctrl.c
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
 * Simple Wayland client that lets the user control plugins.
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

#include "../shared/config-parser.h"

#include "ias-input-manager-client-protocol.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])



struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct ias_input_manager *ias_input_manager;
	struct wl_list input_list;
	struct wl_callback *mode_callback;
	uint32_t mask;

	unsigned int modeset_retcode;
};

struct input_list {
	uint32_t id;
	char *name;

	struct wl_list link;
};



static void
ias_input_manager_add_input(void *data,
		struct ias_input_manager *ias_input_manager,
		uint32_t id,
		const char *name)
{
	struct input_list *l;
	struct wayland *w = data;

	l = malloc(sizeof(*l));
	if(!l) {
		printf("Couldn't allocate a input list node\n");
		return;
	}

	l->id = id;
	l->name = strdup(name);

	wl_list_insert(&w->input_list, &l->link);
}

static const struct ias_input_manager_listener listener = {
	ias_input_manager_add_input,
};



static void
display_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		      const char *interface, uint32_t version)
{
	struct wayland *w = data;


	if (strcmp(interface, "ias_input_manager") == 0) {
		w->ias_input_manager = wl_registry_bind(registry, id,
				&ias_input_manager_interface, 1);
		ias_input_manager_add_listener(w->ias_input_manager, &listener, w);
	}
}

static const struct wl_registry_listener registry_listener = {
	display_handle_global,
};

static int
set_input(struct wayland *wayland, uint32_t input_id)
{
	if (input_id) {
		ias_input_manager_set_input(wayland->ias_input_manager, input_id);
	}
	else { /* default input */
		ias_input_manager_set_input(wayland->ias_input_manager, input_id);
	}
	return 0;
}


static int
display_inputs(struct wayland *wayland_data)
{
	struct input_list *l;

	printf("Input list\n");
	printf("ID: NAME\n");
	printf("-------------------------\n");

	wl_list_for_each_reverse(l, &wayland_data->input_list, link) {
		printf("%d: %s\n", l->id, l->name);
	}

	return 0;
}

int
main(int argc, char **argv)
{
	struct wayland wayland = { 0 };
	int remaining_argc;
	unsigned int ret = 0;
	struct input_list *l, *ltmp;

	/* cmdline options */
	int32_t input_id = -1;

	const struct weston_option options[] = {
		{ WESTON_OPTION_INTEGER, "set-input", 0, &input_id },
	};

	remaining_argc = parse_options(options, ARRAY_LENGTH(options), &argc, argv);

	if (remaining_argc > 1 || argc > 3) {
		printf("Usage:\n");
		printf("  inputctrl [--set-input=0/1  (Use 0 to deactivate plugin, 1 to activate)\n");
		return -1;
	}

	wl_list_init(&wayland.input_list);

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

	/* Make sure the input manager module is present */
	if (!wayland.ias_input_manager) {
		fprintf(stderr, "Input manager interface not advertised by compositor\n");
		wl_display_disconnect(wayland.display);
		return -1;
	}

	if (input_id >= 0) {
		ret = set_input(&wayland, input_id);
	} else {
		ret = display_inputs(&wayland);
	}

	wl_list_for_each_safe(l, ltmp, &wayland.input_list, link) {
		wl_list_remove(&l->link);
		free(l->name);
		free(l);
	}

	wl_display_roundtrip(wayland.display);
	wl_display_disconnect(wayland.display);

	return ret;
}
