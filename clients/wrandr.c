/*
 *---------------------------------------------------------------------------
 * Filename: wrandr.c
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
 * Simple Wayland client that behaves like the xrandr client on X.
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

#include "../shared/config-parser.h"

#include "ias-backend-client-protocol.h"

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])

struct mode {
	unsigned int id;
	unsigned int flags;
	unsigned int width;
	unsigned int height;
	unsigned int refresh;

	struct wl_list link;
};

struct output {
	struct wl_output *output;
	struct wl_list mode_list;
	int width, height;
	int x, y;
	char *make, *model;

	struct wl_list link;
};

struct new_output {
	struct ias_output *ias_output;
	struct wl_list link;
	char *name;
};

struct crtc {
	struct ias_crtc *ias_crtc;
	struct wl_list mode_list;
	struct wl_list link;
	unsigned int gamma[3];
	unsigned int brightness[3];
	unsigned int contrast[3];
};

struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_list output_list;
	struct wl_list crtc_list;
	struct wl_list ias_output_list;
	struct wl_callback *mode_callback;

	unsigned int modeset_retcode;
};

static void geometry_event(void *data, struct wl_output *wl_output,
		int x, int y, int w, int h,
		int subpixel, const char *make, const char *model, int transform)
{
	struct output *output = data;

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
	struct output *output = data;
	struct mode *mode = calloc(1, sizeof *mode);

	if (!mode) {
		fprintf(stderr, "Failed to handle mode event: out of memory\n");
		return;
	}

	mode->flags = flags;
	mode->width = width;
	mode->height = height;
	mode->refresh = refresh;

	wl_list_insert(&output->mode_list, &mode->link);
}

static const struct wl_output_listener output_listener = {
	geometry_event,
	mode_event
};

static void modelist_event(void *data,
		struct ias_crtc *ias_crtc,
		uint32_t mode_number,
		uint32_t width, uint32_t height, uint32_t refresh)
{
	struct crtc *crtc = data;
	struct mode *mode = calloc(1, sizeof *mode);

	if (!mode) {
		fprintf(stderr, "Failed to handle modelist event: out of memory\n");
		return;
	}

	mode->id = mode_number;
	mode->width = width;
	mode->height = height;
	mode->refresh = refresh;

	wl_list_insert(&crtc->mode_list, &mode->link);

}

static void gamma_event(void *data,
		struct ias_crtc *ias_crtc,
		uint32_t red,
		uint32_t green,
		uint32_t blue)
{
	struct crtc *crtc = data;

	crtc->gamma[0] = red;
	crtc->gamma[1] = green;
	crtc->gamma[2] = blue;
}

static void contrast_event(void *data,
		struct ias_crtc *ias_crtc,
		uint32_t red,
		uint32_t green,
		uint32_t blue)
{
	struct crtc *crtc = data;

	crtc->contrast[0] = red;
	crtc->contrast[1] = green;
	crtc->contrast[2] = blue;
}


static void brightness_event(void *data,
		struct ias_crtc *ias_crtc,
		uint32_t red,
		uint32_t green,
		uint32_t blue)
{
	struct crtc *crtc = data;

	crtc->brightness[0] = red;
	crtc->brightness[1] = green;
	crtc->brightness[2] = blue;
}


static const struct ias_crtc_listener ias_crtc_listener = {
	modelist_event,
	gamma_event,
	contrast_event,
	brightness_event,
};


static void name_event(void *data,
		struct ias_output *ias_output,
		const char *name)
{
	struct new_output *output = data;

	if (name) {
		output->name = strdup(name);
	} else {
		printf("output name is NULL!\n");
	}
}

static const struct ias_output_listener ias_output_listener = {
	name_event
};


static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		      const char *interface, uint32_t version)
{
	struct wayland *w = data;
	struct output *o;
	struct crtc *c;
	struct new_output *i;

	if (strcmp(interface, "wl_output") == 0) {
		o = calloc(1, sizeof *o);
		if (!o) {
			fprintf(stderr, "Failed to handle global event: out of memory\n");
			return;
		}

		wl_list_init(&o->mode_list);
		wl_list_insert(&w->output_list, &o->link);

		o->output = wl_registry_bind(registry, id, &wl_output_interface, 1);
		wl_output_add_listener(o->output, &output_listener, o);
	} else if (strcmp(interface, "ias_crtc") == 0) {
		c = calloc(1, sizeof *c);
		if (!c) {
			fprintf(stderr, "Failed to handle global event: out of memory\n");
			return;
		}

		wl_list_init(&c->mode_list);
		wl_list_insert(&w->crtc_list, &c->link);

		c->ias_crtc = wl_registry_bind(registry, id, &ias_crtc_interface, 1);
		ias_crtc_add_listener(c->ias_crtc, &ias_crtc_listener, c);
	} else if (strcmp(interface, "ias_output") == 0) {
		i = calloc(1, sizeof *i);
		if (!i) {
			fprintf(stderr, "Failed to handle global event: out of memory\n");
			return;
		}

		wl_list_insert(&w->ias_output_list, &i->link);
		i->ias_output = wl_registry_bind(registry, id, &ias_output_interface, 1);
		ias_output_add_listener(i->ias_output, &ias_output_listener, i);
	} else  {
		printf("Unhandled Global interface %s\n", interface);
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
};

static void
display_crtc (struct  wayland *wayland)
{
	struct mode *mode;
	struct crtc *crtc;
	int i = 1;

	wl_list_for_each(crtc, &wayland->crtc_list, link) {
		printf("Mode ID      W      H      R        CRTC = %d\n", i++);
		printf("----------   ----   ----   --\n");

		wl_list_for_each(mode, &crtc->mode_list, link) {
			printf("%10d:  %4d x %4d @ %d\n", mode->id, mode->width,
					mode->height, mode->refresh);
		}

		printf("\nColor Correction:\n");
		printf("  Gamma:      0x%02x 0x%02x 0x%02x\n",
				crtc->gamma[0],
				crtc->gamma[1],
				crtc->gamma[2]);
		printf("  Contrast:   0x%02x 0x%02x 0x%02x\n",
				crtc->contrast[0],
				crtc->contrast[1],
				crtc->contrast[2]);
		printf("  Brightness: 0x%02x 0x%02x 0x%02x\n",
				crtc->brightness[0],
				crtc->brightness[1],
				crtc->brightness[2]);
		printf("\n");
	}
}

static int
set_mode(struct wayland *wayland,
	unsigned int output_num,
	unsigned int id)
{
	struct crtc *crtc;
	struct mode *mode;
	unsigned int found = 0;
	unsigned int i = 1;

	wl_list_for_each(crtc, &wayland->crtc_list, link) {
		if (output_num == i) {
			/* Verify that mode id is valid */
			wl_list_for_each(mode, &crtc->mode_list, link) {
				if (id == mode->id) {
					found = 1;
				}
			}
			if (found) {
				ias_crtc_set_mode(crtc->ias_crtc, id);
			} else {
				printf("Mode ID %d is invalid\n", id);
			}

			return 0;
		}
		i++;
	}

	return -1;
}

static int
set_gamma(struct wayland *wayland,
	unsigned int output_num,
	char *gamma)
{
	struct crtc *crtc;
	unsigned int i = 1;
	unsigned int red, green, blue;

	if (sscanf(gamma, "%d:%d:%d", &red, &green, &blue) == 3) {

		wl_list_for_each(crtc, &wayland->crtc_list, link) {
			if (output_num == i) {
				printf("Get gamma to %u:%u:%u\n", red, green, blue);
				ias_crtc_set_gamma(crtc->ias_crtc, red, green, blue);
				return 0;
			}
			i++;
		}
	}

	return -1;
}

static int
set_contrast(struct wayland *wayland,
	unsigned int output_num,
	char *contrast)
{
	struct crtc *crtc;
	unsigned int i = 1;
	unsigned int red, green, blue;

	if (sscanf(contrast, "%d:%d:%d", &red, &green, &blue) == 3) {
		wl_list_for_each(crtc, &wayland->crtc_list, link) {
			if (output_num == i) {
				ias_crtc_set_contrast(crtc->ias_crtc, red, green, blue);
				return 0;
			}
			i++;
		}
	}

	return -1;
}

static int
set_brightness(struct wayland *wayland,
	unsigned int output_num,
	char *brightness)
{
	struct crtc *crtc;
	unsigned int i = 1;
	unsigned int red, green, blue;

	if (sscanf(brightness, "%d:%d:%d", &red, &green, &blue) == 3) {
			wl_list_for_each(crtc, &wayland->crtc_list, link) {
			if (output_num == i) {
				ias_crtc_set_brightness(crtc->ias_crtc, red, green, blue);
				return 0;
			}
			i++;
		}
	}

	return -1;
}

static int
set_transparency(struct wayland *wayland,
	unsigned int output_num,
	bool fb_transparency)
{
	struct new_output *output;
	unsigned int i = 1;

	wl_list_for_each(output, &wayland->ias_output_list, link) {
		if (output_num == i) {
			printf("Set transparency to %d\n", fb_transparency);
			ias_output_set_fb_transparency(output->ias_output, (int)fb_transparency);
			return 0;
		}
		i++;
	}

	return -1;
}


static void
output_scale_to(struct wayland *wayland,
		unsigned int width,
		unsigned int height)
{
	struct new_output *output;

	wl_list_for_each(output, &wayland->ias_output_list, link) {
		printf("Scaling %s to %d, %d\n", output->name, width, height);
		ias_output_scale_to(output->ias_output, width, height);
	}
}

static int
output_set_xy(struct wayland *wayland,
		unsigned int x, unsigned int y)
{
	struct new_output *output;

	wl_list_for_each(output, &wayland->ias_output_list, link) {
		printf("Moving %s to %d, %d\n", output->name, x, y);
		ias_output_set_xy(output->ias_output, x, y);
	}

	return 0;
}


static void
disable(struct wayland *wayland)
{
	struct new_output *output;

	wl_list_for_each(output, &wayland->ias_output_list, link) {
		printf("Disable output %s\n", output->name);
		ias_output_disable(output->ias_output);
	}
}

static void
enable(struct wayland *wayland)
{
	struct new_output *output;

	wl_list_for_each(output, &wayland->ias_output_list, link) {
		printf("Enable output %s\n", output->name);
		ias_output_enable(output->ias_output);
	}
}


static char *gamma_str = "";
static char *brightness_str = "";
static char *contrast_str = "";
static char *pos_str = "";
static char *scale_str = "";
static int enable_fb_transparency = 0;
static int disable_fb_transparency = 0;

int
main(int argc, char **argv)
{
	struct wayland wayland = { 0 };
	int modeid = -1;
	unsigned int test = 0;
	unsigned int on = 0;
	unsigned int off = 0;
	int remaining_argc;
	unsigned int ret = 0;
	struct crtc *crtc, *temp_crtc;
	struct mode *mode, *temp_mode;
	struct output *output, *temp_output;
	struct new_output *ias_output, *temp_ias_output;
	char color[15];
	int list = 1;

	/* cmdline options */
	int crtc_num = 0;

	const struct weston_option options[] = {
		{ WESTON_OPTION_INTEGER, "crtc", 'c', &crtc_num },
		{ WESTON_OPTION_INTEGER, "mode", 'm', &modeid },
		{ WESTON_OPTION_STRING, "gamma", 0, &gamma_str },
		{ WESTON_OPTION_STRING, "brightness", 0, &brightness_str },
		{ WESTON_OPTION_STRING, "contrast", 0, &contrast_str },
		{ WESTON_OPTION_BOOLEAN, "enable-fb-transparency", 0, &enable_fb_transparency},
		{ WESTON_OPTION_BOOLEAN, "disable-fb-transparency", 0, &disable_fb_transparency},
		{ WESTON_OPTION_STRING, "pos", 0, &pos_str },
		{ WESTON_OPTION_INTEGER, "test", 't', &test },
		{ WESTON_OPTION_INTEGER, "on", 'n', &on },
		{ WESTON_OPTION_INTEGER, "off", 'f', &off },
		{ WESTON_OPTION_STRING, "scale", 0, &scale_str },
	};

	remaining_argc = parse_options(options, ARRAY_LENGTH(options), &argc, argv);

	if (remaining_argc > 1 || (crtc_num == 0 && argc > 1)) {
		printf("Usage:\n");
		printf("  wrandr --crtc=<#> [--mode=<#>] [--pos=xxy]\n");
		printf("         [--gamma=red:green:blue]\n");
		printf("         [--brightness=red:green:blue]\n");
		printf("         [--contrast=red:green:blue]\n");
		printf("         [--enable=true|false]\n");
		printf("         [--enable-fb-transparency]\n");
		printf("         [--disable-fb-transparency]\n");
		printf("         [--scale=#x#]\n");

		printf("or\n");
		printf("  wrandr -c<#> [-m<#>]\n");
		printf("or\n");
		printf("  wrandr\n");
		return -1;
	}

	wl_list_init(&wayland.output_list);
	wl_list_init(&wayland.crtc_list);
	wl_list_init(&wayland.ias_output_list);

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

	/* If gamma option, set gamma */
	if (strlen(gamma_str) > 0) {
		ret = set_gamma(&wayland, crtc_num, gamma_str);
		list = 0;
	}

	/* If brightness option, set brightness */
	if (strlen(brightness_str) > 0) {
		ret = set_brightness(&wayland, crtc_num, brightness_str);
		list = 0;
	}

	/* If contrast option, set contrast */
	if (strlen(contrast_str) > 0) {
		ret = set_contrast(&wayland, crtc_num, contrast_str);
		list = 0;
	}

	/* If enable-fb-transparency option, set transparency */
	if (enable_fb_transparency == 1) {
		ret = set_transparency(&wayland, crtc_num, true);
		list = 0;
	}

	/* If disable-fb-transparency option, set transparency */
	if (disable_fb_transparency == 1) {
		ret = set_transparency(&wayland, crtc_num, false);
		list = 0;
	}

	/* Handle moving output */
	if (strlen(pos_str) > 0) {
		int x, y;

		if (sscanf(pos_str, "%dx%d", &x, &y) == 2) {
			//ret = set_xy(&wayland, crtc_num, x, y);
			ret = output_set_xy(&wayland, x, y);
		}
		list = 0;
	}

	if (strlen(scale_str) > 0) {
		int w, h;

		if (sscanf(scale_str, "%dx%d", &w, &h) == 2) {
			output_scale_to(&wayland, w, h);
		}
		list = 0;
	}


	if (modeid >= 0) {
		if (crtc_num > wl_list_length(&wayland.crtc_list)) {
			fprintf(stderr, "No such crtc #%d\n", crtc_num);
			return -1;
		}

		display_crtc(&wayland);

		ret = set_mode(&wayland, crtc_num, modeid);
		list = 0;
	} else if (on) {
		enable(&wayland);
		list = 0;
	} else if (off) {
		disable(&wayland);
		list = 0;
	} else if (test) {
		for (test = 0; test < 256; test++) {
			snprintf(color, sizeof(color), "%d:%d:%d", test, test, test);
			ret = set_brightness(&wayland, crtc_num, color);
			ret = set_contrast(&wayland, crtc_num, color);
		}
		for (test = 255; test > 0; test--) {
			snprintf(color, sizeof(color), "%d:%d:%d", 255, test, 255);
			ret = set_brightness(&wayland, crtc_num, color);
			ret = set_contrast(&wayland, crtc_num, color);
		}

		list = 0;
	}

	if (list) {
		/* Display the mode list for the CRTC */
		display_crtc(&wayland);
	}

	wl_list_for_each_safe(crtc, temp_crtc, &wayland.crtc_list, link) {
		ias_crtc_destroy(crtc->ias_crtc);
		wl_list_remove(&crtc->link);
		wl_list_for_each_safe(mode, temp_mode, &crtc->mode_list, link) {
			wl_list_remove(&mode->link);
			free(mode);
		}
		free(crtc);
	}

	wl_list_for_each_safe(output, temp_output, &wayland.output_list, link) {
		wl_list_remove(&output->link);
		wl_list_for_each_safe(mode, temp_mode, &output->mode_list, link) {
			wl_list_remove(&mode->link);
			free(mode);
		}
		free(output);
	}

	wl_list_for_each_safe(ias_output, temp_ias_output, &wayland.ias_output_list, link) {
		wl_list_remove(&ias_output->link);
		free(ias_output);
	}


	wl_display_roundtrip(wayland.display);
	wl_display_disconnect(wayland.display);

	return ret;
}
