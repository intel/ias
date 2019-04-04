/*
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
 *
 * Portions Copyright © 2011 Kristian Høgsberg
 * Portions Copyright © 2011 Collabora, Ltd.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 *
 * Unless otherwise agreed by Intel in writing, you may not remove or alter
 * this notice or any other notice embedded in Materials by Intel or Intel’s
 * suppliers or licensors in any way.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

#include <wayland-client.h>
#include "window.h"
#include "../shared/cairo-util.h"
#include <libweston/config-parser.h>

#include "ias-shell-client-protocol.h"

#define POPUP_WIDTH  500.0
#define POPUP_HEIGHT 200.0
#define POPUP_OK_X   200
#define POPUP_OK_Y   130
#define POPUP_OK_W   100
#define POPUP_OK_H    40

#define INFO_DEFAULT_ZORDER 0
#define INFO_WINDOW_DEFAULT_ZORDER 0

#define UNUSED(a) (void) a

struct desktop {
	struct display *display;
	struct ias_shell *ias_shell;
	struct ias_hmi *hmi;
	struct wl_list outputs;
	struct info *info;
	struct ias_surface *ias_surface;
	struct wl_list surface_list;
};

struct desktop desktop = { 0 };

struct surface {
	void (*configure)(void *data,
			struct ias_shell *ias_shell,
			struct window *window,
			int32_t width, int32_t height);
};

struct background {
	struct surface base;
	struct window *window;
	struct widget *widget;
	struct desktop *desktop;
};

static struct background *
background_create(struct desktop *desktop);

struct popup {
	struct surface base;
	struct window *window;
	struct widget *widget;
	struct desktop *desktop;
	char *message;
	int width, height;
};

struct info {
	struct surface base;
	struct window *window;
	struct widget *widget;
	struct desktop *desktop;
	char *info_txt;

	int x;
	int y;
	unsigned int w;
	unsigned int h;
};

struct surflist {
	uint32_t surf_id;
	char *name;
	struct wl_list link;
};

struct output {
	struct wl_output *output;
	struct wl_list link;

	int x;
	int y;
	unsigned int width;
	unsigned int height;

	struct background *background;
	struct popup *popup;
};

static char *key_background_image = DATADIR "/ias/intel.png";
static char *key_background_type = "center";
static uint32_t key_background_color = 0xffffffff;
//static int popup_priority = 0;

static void
sigchild_handler(int s)
{
	int status;
	pid_t pid;

	while (pid = waitpid(-1, &status, WNOHANG), pid > 0)
		fprintf(stderr, "child %d exited\n", pid);
}

enum {
	BACKGROUND_SCALE,
	BACKGROUND_TILE,
	BACKGROUND_CENTER,
};

static void
set_hex_color(cairo_t *cr, uint32_t color)
{
	cairo_set_source_rgba(cr,
			((color >> 16) & 0xff) / 255.0,
			((color >>  8) & 0xff) / 255.0,
			((color >>  0) & 0xff) / 255.0,
			((color >> 24) & 0xff) / 255.0);
}

static void
background_draw(struct widget *widget, void *data)
{
	struct background *background = data;
	cairo_surface_t *surface, *image;
	cairo_pattern_t *pattern;
	cairo_matrix_t matrix;
	cairo_t *cr;
	double sx, sy;
	struct rectangle allocation;
	int type = -1;
	struct display *display;
	struct wl_region *opaque;

	surface = window_get_surface(background->window);

	cr = cairo_create(surface);

	widget_get_allocation(widget, &allocation);
	image = NULL;
	if (key_background_image)
		image = load_cairo_surface(key_background_image);

	if (strcmp(key_background_type, "scale") == 0)
		type = BACKGROUND_SCALE;
	else if (strcmp(key_background_type, "tile") == 0)
		type = BACKGROUND_TILE;
	else if (strcmp(key_background_type, "center") == 0)
		type = BACKGROUND_CENTER;
	else
		fprintf(stderr, "invalid background-type: %s\n",
			key_background_type);

	if (image && type != -1) {
		pattern = cairo_pattern_create_for_surface(image);
		switch (type) {
		case BACKGROUND_SCALE:
			sx = (double) cairo_image_surface_get_width(image) /
				allocation.width;
			sy = (double) cairo_image_surface_get_height(image) /
				allocation.height;
			cairo_matrix_init_scale(&matrix, sx, sy);
			cairo_pattern_set_matrix(pattern, &matrix);
			cairo_set_source(cr, pattern);
			break;
		case BACKGROUND_TILE:
			cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
			cairo_set_source(cr, pattern);
			break;
		case BACKGROUND_CENTER:
			cairo_set_source_surface(cr, image,
					(double) (allocation.width -
					cairo_image_surface_get_width(image)) / 2,
					(double) (allocation.height -
					cairo_image_surface_get_height(image)) / 2);
			break;
		}
		cairo_pattern_destroy (pattern);
		cairo_surface_destroy(image);
	} else {
		set_hex_color(cr, key_background_color);
	}

	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	/* Make the background surface opaque */
	display = window_get_display(background->window);
	opaque = wl_compositor_create_region(display_get_compositor(display));
	wl_region_add(opaque, allocation.x, allocation.y,
			allocation.width, allocation.height);
	wl_surface_set_opaque_region(window_get_wl_surface(background->window), opaque);
	wl_region_destroy(opaque);
}

static void
background_configure(void *data,
		struct ias_shell *ias_shell,
		struct window *window,
		int32_t width, int32_t height)
{
	struct background *background =
		(struct background *) window_get_user_data(window);

	widget_schedule_resize(background->widget, width, height);
}

static void
ias_shell_configure(void *data,
			struct ias_shell *ias_shell,
			struct ias_surface *ias_surface,
			int32_t width, int32_t height)
{
	struct window *window = ias_surface_get_user_data(ias_surface);
	struct surface *s = window_get_user_data(window);

	s->configure(data, ias_shell, window, width, height);
}

static void
ias_hmi_surface_info(void *data,
			struct ias_hmi *hmi,
			uint32_t id,
			const char *name,
			uint32_t zorder,
			int32_t x, int32_t y,
			uint32_t width, uint32_t height,
			uint32_t alpha,
			uint32_t behavior_bits,
			uint32_t pid,
			const char *pname,
			uint32_t dispno,
			uint32_t flipped)
{
	struct surflist *s;
	struct desktop *desktop = data;
	struct output *output;
	struct info *info = desktop->info;

	s = malloc(sizeof(*s));
	if(!s) {
		printf("Couldn't allocate a surface list\n");
		return;
	}
	s->surf_id = id;
	s->name = strdup(name);

	wl_list_insert(&desktop->surface_list, &s->link);

	/*
	 * Get the primary (first) output.  Note that we walk the list in
	 * reverse since new items are added at the front.
	 */
	wl_list_for_each_reverse(output, &desktop->outputs, link) {
		break;
	}
	if (!output) {
		return;
	}

	if (strncmp(name, "info", 4) == 0) {
		/*
		 * Figure out how the info bar should be positioned and sized (bottom
		 * of primary screen, full screen width, 50 pixels high).
		 */

		info->x = output->x;
		info->y = output->y + output->height - 50;
		info->w = output->width;
		info->h = 50;
		fprintf(stderr, "INFO: %d,%d\n", info->x, info->y);

		if (x != info->x || y != info->y) {
			ias_hmi_move_surface(hmi, id, info->x, info->y);
		}
		if (width != info->w || height != info->h) {
			ias_hmi_resize_surface(hmi, id, info->w, info->h);
		}
		if (zorder != INFO_DEFAULT_ZORDER) {
			ias_hmi_zorder_surface(hmi, id, INFO_DEFAULT_ZORDER);
		}
	} else if (strcasecmp(name, "desktop") == 0) {
		// Do nothing with the desktop surface
	}
}

static void
ias_hmi_surface_destroyed(void *data,
			struct ias_hmi *hmi,
			uint32_t id,
			const char *name,
			uint32_t pid,
			const char *pname)
{
	struct surflist *s, *tmp;
	struct desktop *desktop = data;

	/* Find the surface and remove it from our surface list */
	wl_list_for_each_safe(s, tmp, &desktop->surface_list, link) {
		if (s->surf_id == id) {
			wl_list_remove(&s->link);
			free(s->name);
			free(s);
			return;
		}
	}
}

static void
ias_hmi_surface_sharing_info(void *data,
			     struct ias_hmi *hmi,
			     uint32_t id,
			     const char *name,
			     uint32_t shareable,
			     uint32_t pid,
			     const char *pname)
{
	UNUSED(data);
	UNUSED(hmi);
	UNUSED(id);
	UNUSED(name);
	UNUSED(shareable);
	UNUSED(pid);
	UNUSED(pname);
}

static void
output_geometry(void *data,
		struct wl_output *wl_output,
		int x,
		int y,
		int w,
		int h,
		int subpixel,
		const char *make,
		const char *model,
		int transform)
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

	/* Required incase a new output has been created after initial setup */
	if (output->background == NULL) {
		output->background = background_create(&desktop);
		if (!output->background) {
			fprintf(stderr, "new output contains no background data \n");
			exit(1);
		}

		struct ias_surface *s;

		output->x = x;
		output->y = y;

		s = window_get_ias_surface(output->background->window);
		ias_shell_set_background(desktop.ias_shell, output->output, s);

		output->popup = NULL;
	}

	window_schedule_resize(output->background->window, w, h);
	window_schedule_resize(output->background->desktop->info->window, w, h);
}

static void
output_mode(void *data,
		struct wl_output *wl_output,
		uint32_t flags,
		int width,
		int height,
		int refresh)
{
	/* No-op */
}


static const struct ias_hmi_listener listener = {
	ias_hmi_surface_info,
	ias_hmi_surface_destroyed,
	ias_hmi_surface_sharing_info,
};

static const struct ias_shell_listener shell_listener = {
	ias_shell_configure,
};

static const struct wl_output_listener output_listener = {
	output_geometry,
	output_mode,
};


/**************************************************************************
 * Info Panel
 *
 * The Info panel is a small area at the bottom of the screen that display
 * info.  The content depends on what source is currently active.
 * If navigation is current source, then display media info
 * If media is current source, then display navigation info
 */

static void
info_configure(void *data,
		struct ias_shell *ias_shell,
		struct window *window,
		int32_t width, int32_t height)
{
	struct info *info =
		(struct info *) window_get_user_data(window);

	/* Ignore new size; just redraw ourselves at the existing size */
	window_schedule_resize(info->window, width, 64);
}


static void
info_draw(struct widget *widget, void *data)
{
	struct info *info = data;
	cairo_surface_t *surface;
	cairo_t *cr;
	cairo_text_extents_t extents;

	surface = window_get_surface(info->window);

	cr = cairo_create(surface);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

	/* Fill info with backround color */
	cairo_set_source_rgba(cr, .4, .4, 1, 1);
	cairo_paint(cr);

	/* Draw some text */
	if (info->info_txt != NULL) {
		cairo_set_source_rgba(cr, 0.1, 0.1, 0.6, 1);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
				CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 20.0);
		cairo_text_extents(cr, info->info_txt, &extents);

		cairo_move_to(cr, 25, (info->h + extents.height) / 2);
		cairo_show_text(cr, info->info_txt);
	}

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

static struct info *
info_create (struct desktop *desktop)
{
	struct info *info;

	info = malloc(sizeof(struct info));
	if (!info) {
		return NULL;
	}
	memset(info, 0, sizeof(struct info));

	info->base.configure = info_configure;
	info->window = window_create(desktop->display, "info");
	info->widget = window_add_widget(info->window, info);
	info->desktop = desktop;
	info->info_txt = strdup("IAS HMI");
	desktop->info = info;

	window_set_user_data(info->window, info);
	widget_set_redraw_handler(info->widget, info_draw);

	//widget_schedule_resize(info->widget, 1024, 48);

	return info;
}


/*
 * toggle_handler
 *
 * This handles mouse button events on the shell background.  If the mouse
 * is clicked an alert is created.
 */
static void
toggle_handler(struct widget *widget,
		struct input *input, uint32_t time,
		uint32_t button,
		enum wl_pointer_button_state state, void *data)
{
	struct background *background = data;
	struct desktop *desktop = background->desktop;
	struct surflist *surf;

	/*
	 * Some code to test set position API.  This walks the surface list,
	 * picks one based on name and moves it to the position where
	 * the mouse was clicked.
	 */
	if (state == 1) {
		wl_list_for_each(surf, &desktop->surface_list, link) {
			if (strcmp(surf->name, "Wayland gears") == 0) {
				int x, y;

				input_get_position(input, &x, &y);
				ias_hmi_move_surface(desktop->hmi, surf->surf_id, x, y);
				return;
			}

		}
	}
}


static void
background_destroy(struct background *background)
{
	widget_destroy(background->widget);
	window_destroy(background->window);

	free(background);
}

static struct background *
background_create(struct desktop *desktop)
{
	struct background *background;

	background = malloc(sizeof *background);
	if (!background) {
		return NULL;
	}
	memset(background, 0, sizeof *background);

	background->base.configure = background_configure;
	background->window = window_create(desktop->display, "desktop");
	background->widget = window_add_widget(background->window, background);
	background->desktop = desktop;
	window_set_user_data(background->window, background);
	widget_set_redraw_handler(background->widget, background_draw);
	widget_set_transparent(background->widget, 0);

	/* Install a button handler for the background surface */
	widget_set_button_handler(background->widget, toggle_handler);

	return background;
}


static void
output_destroy(struct output *output)
{
	background_destroy(output->background);
	wl_output_destroy(output->output);
	wl_list_remove(&output->link);

	free(output);
}

static void
desktop_destroy_outputs(struct desktop *desktop)
{
	struct output *tmp;
	struct output *output;

	wl_list_for_each_safe(output, tmp, &desktop->outputs, link)
		output_destroy(output);
}

static void
create_output(struct desktop *desktop, uint32_t id)
{
	struct output *output;

	output = calloc(1, sizeof *output);
	if (!output)
		return;

	output->output = display_bind(desktop->display, id, &wl_output_interface, 1);

	wl_list_insert(&desktop->outputs, &output->link);

	wl_output_add_listener(output->output, &output_listener, output);
}

static void
global_handler(struct display *display, uint32_t id,
		const char *interface, uint32_t version, void *data)
{
	struct desktop *desktop = data;

	if (!strcmp(interface, "ias_surface")) {
	} else if (!strcmp(interface, "ias_shell")) {
		desktop->ias_shell =
			display_bind(desktop->display, id, &ias_shell_interface, 1);
		ias_shell_add_listener(desktop->ias_shell, &shell_listener, desktop);
	} else if (!strcmp(interface, "ias_hmi")) {
		desktop->hmi =
			display_bind(desktop->display, id, &ias_hmi_interface, 1);
		ias_hmi_add_listener(desktop->hmi, &listener, desktop);
	} else if (!strcmp(interface, "wl_output")) {
		create_output(desktop, id);
	}
}



/* IAS Shell specific functions
 *
 *   ias_shell_set_background(shell, output, surface, method)
 *   ias_shell_set_parent(shell, surface, parent)
 *   ias_shell_popup(shell, surface, priority)
 *   ias_shell_set_zorder(shell, surface, zorder)
 *   ias_shell_set_behavior(shell, surface, behavior)
 */
int main(int argc, char *argv[])
{
	struct output *output;
	struct info *info;
	struct ias_surface *s;
	struct surflist *surfinfo, *tmp;
	struct weston_config *config;
	struct weston_config_section *section;

	wl_list_init(&desktop.outputs);
	wl_list_init(&desktop.surface_list);

	desktop.display = display_create(&argc, argv);
	if (desktop.display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}

	display_set_user_data(desktop.display, &desktop);
	display_set_global_handler(desktop.display, global_handler);

	wl_list_for_each(output, &desktop.outputs, link) {
		output->background = background_create(&desktop);
		if (!output->background) {
			exit(1);
		}
		s = window_get_ias_surface(output->background->window);
		ias_shell_set_background(desktop.ias_shell, output->output, s);

		output->popup = NULL;

	}

	/* My info bar */
	info = info_create(&desktop);
	if (!info) {
		exit(1);
	}
	s = window_get_ias_surface(info->window);
	info_draw(info->widget, info);
	ias_shell_set_zorder(desktop.ias_shell, s, INFO_WINDOW_DEFAULT_ZORDER);

	config = weston_config_parse("weston.ini");
	section = weston_config_get_section(config, "shell", NULL, NULL);
	weston_config_section_get_string(section, "background-image", &key_background_image, DATADIR "/ias/intel.png");
	weston_config_section_get_uint(section, "background-color", &key_background_color, 0xffffffff);
	weston_config_section_get_string(section, "background-type", &key_background_type, "center");
	weston_config_destroy(config);

	signal(SIGCHLD, sigchild_handler);

	display_run(desktop.display);

	/* Cleanup */
	wl_list_for_each_safe(surfinfo, tmp, &desktop.surface_list, link) {
		wl_list_remove(&surfinfo->link);
		free(surfinfo->name);
		free(surfinfo);
	}
	desktop_destroy_outputs(&desktop);

	return 0;
}
