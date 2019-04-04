/*
 *---------------------------------------------------------------------------
 * Filename: layoutctrl.c
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

#include <libweston/config-parser.h>

#include "ias-layout-manager-client-protocol.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct ias_layout_manager *ias_layout_manager;
	struct wl_list layout_list;
	struct wl_list output_list;
	struct wl_callback *mode_callback;
	uint32_t mask;

	unsigned int modeset_retcode;
};

struct layout_list {
	uint32_t id;
	char *name;

	struct wl_list link;
};

struct output_list {
	uint32_t id;
	struct wl_output *output;
	int width, height;
	int x, y;
	char *make, *model;

	struct wl_list link;
};

static void
ias_layout_manager_layout(void *data,
		struct ias_layout_manager *ias_layout_manager,
		uint32_t id,
		const char *name)
{
	struct layout_list *l;
	struct wayland *w = data;

	l = malloc(sizeof(*l));
	if(!l) {
		printf("Couldn't allocate a layout list node\n");
		return;
	}

	l->id = id;
	l->name = strdup(name);

	wl_list_insert(&w->layout_list, &l->link);
}

static void
ias_layout_manager_layout_switched(void *data,
		struct ias_layout_manager *ias_layout_manager,
		uint32_t id,
		struct wl_output *output)
{
	printf("Layout switched to: %d\n", id);
}

static const struct ias_layout_manager_listener listener = {
	ias_layout_manager_layout,
	ias_layout_manager_layout_switched,
};

static void geometry_event(void *data, struct wl_output *wl_output,
        int x, int y, int w, int h,
        int subpixel, const char *make, const char *model, int transform)
{
    struct output_list *output = data;

    output->width = w;
    output->height = h;

	/*
	 * If this output is rotated by 90 or 270, then we need to swap
	 * the output's width and height similar to how
	 * weston_output_transform_init() does it.
	 */
	switch (transform) {
		case WL_OUTPUT_TRANSFORM_90:
		case WL_OUTPUT_TRANSFORM_270:
		case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		case WL_OUTPUT_TRANSFORM_FLIPPED_270:
			output->width = h;
			output->height = w;
	        break;
	}

    output->x = x;
    output->y = y;
    output->make = strdup(make);
    output->model = strdup(model);
}

static void mode_event(void *data, struct wl_output *wl_output,
    uint32_t flags, int width, int height, int refresh)
{
}

static const struct wl_output_listener output_listener = {
	geometry_event,
	mode_event
};

static void
display_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		      const char *interface, uint32_t version)
{
	struct wayland *w = data;

	if (strcmp(interface, "wl_output") == 0) {
		struct output_list *output;
		output = malloc(sizeof *output);
		if (output == NULL) {
			printf("Could not allocate memory for output.\n");
			return;
		}
		output->id = id;
		output->output = wl_registry_bind(registry, id, &wl_output_interface, 1);
		wl_list_insert(&w->output_list, &output->link);
		wl_output_add_listener(output->output, &output_listener, output);
	} else	if (strcmp(interface, "ias_layout_manager") == 0) {
		w->ias_layout_manager = wl_registry_bind(registry, id,
				&ias_layout_manager_interface, 1);
		ias_layout_manager_add_listener(w->ias_layout_manager, &listener, w);
	}
}

static const struct wl_registry_listener registry_listener = {
	display_handle_global,
};

static int
set_layout(struct wayland *wayland, uint32_t layout_id, uint32_t output_id)
{
	struct layout_list *l;
	struct output_list *o;

	wl_list_for_each(o, &wayland->output_list, link) {
		if (o->id == output_id || output_id == 0) {

			if (layout_id) {
				wl_list_for_each(l, &wayland->layout_list, link) {
					if(l->id == layout_id) {
						ias_layout_manager_set_layout(wayland->ias_layout_manager,
								o->output, layout_id);
					}
				}
			} else {
				ias_layout_manager_set_layout(wayland->ias_layout_manager,
						o->output, layout_id);
			}
		}
	}

	return 0;
}

static int
display_outputs(struct wayland *wayland)
{
	struct output_list *o;

	printf("\n\nOutput list\n");
	printf("ID: width x height (x,y) make model\n");
	printf("-------------------------\n");
	wl_list_for_each_reverse(o, &wayland->output_list, link) {
		printf("%d: %dx%d (%d,%d) %s %s\n", o->id, o->width, o->height,
				o->x, o->y, o->make, o->model);
	}

	return 0;
}

static int
display_layouts(struct wayland *wayland)
{
	struct layout_list *l;

	printf("Layout list\n");
	printf("ID: NAME\n");
	printf("-------------------------\n");

	wl_list_for_each_reverse(l, &wayland->layout_list, link) {
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
	struct layout_list *l, *ltmp;
	struct output_list *o, *otmp;

	/* cmdline options */
	int32_t layout_id = -1;
	int32_t output_id = 0;

	const struct weston_option options[] = {
		{ WESTON_OPTION_INTEGER, "set-layout", 0, &layout_id },
		{ WESTON_OPTION_INTEGER, "output", 0, &output_id },
	};

	remaining_argc = parse_options(options, ARRAY_LENGTH(options), &argc, argv);

	if (remaining_argc > 1 || argc > 3) {
		printf("Usage:\n");
		printf("  layoutctrl [--set-layout=X  (Use 0 to deactivate plugin)\n");
		printf("              [--output=Y] ]  (Use 0 for all outputs)\n");

		return -1;
	}

	wl_list_init(&wayland.layout_list);
	wl_list_init(&wayland.output_list);

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

	/* Make sure the layout manager module is present */
	if (!wayland.ias_layout_manager) {
		fprintf(stderr, "Layout manager interface not advertised by compositor\n");
		wl_display_disconnect(wayland.display);
		return -1;
	}

	if (layout_id >= 0) {
		ret = set_layout(&wayland, layout_id, output_id);
	} else {
		ret = display_layouts(&wayland);
		ret = display_outputs(&wayland);
	}

	wl_list_for_each_safe(l, ltmp, &wayland.layout_list, link) {
		wl_list_remove(&l->link);
		free(l->name);
		free(l);
	}

	wl_list_for_each_safe(o, otmp, &wayland.output_list, link) {
		wl_list_remove(&o->link);
		free(o);
	}

	wl_display_roundtrip(wayland.display);
	wl_display_disconnect(wayland.display);

	return ret;
}
