/*
 * Copyright © 2011 Benjamin Franzke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "config.h"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/timeb.h>

#include <linux/input.h>

#include <wayland-client.h>
#include <wayland-cursor.h>

#include <cairo.h>
#include <time.h>
#include <inttypes.h>

#include "ias-shell-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <sys/types.h>
#include <unistd.h>
#define NS_IN_MS 1000000
#define SURFACE_OPAQUE 0x01
#define SURFACE_SHM    0x02
#define NUM_BUFS       2

#include "shared/platform.h"
#include "shared/os-compatibility.h"

#ifndef EGL_EXT_swap_buffers_with_damage
#define EGL_EXT_swap_buffers_with_damage 1
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC)(EGLDisplay dpy, EGLSurface surface, EGLint *rects, EGLint n_rects);
#endif

#ifndef EGL_EXT_buffer_age
#define EGL_EXT_buffer_age 1
#define EGL_BUFFER_AGE_EXT			0x313D
#endif

struct window;

struct rectangle {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

struct shm_pool {
	struct wl_shm_pool *pool;
	size_t size;
	size_t used;
	void *data;
};

struct shm_surface_data {
	struct wl_buffer *buffer;
	struct shm_pool *pool;
};

struct output {
	struct display *display;
	struct wl_output *output;
	struct wl_list link;
};

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct ias_shell *ias_shell;
	struct xdg_wm_base *wm_base;
	struct wl_shm *shm;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *default_cursor;
	struct wl_surface *cursor_surface;
	struct window *window;
	struct wl_list output_list;
	int zorder;
	int x;
	int y;
	PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC swap_buffers_with_damage;
};

struct geometry {
	int width, height;
};

struct window {
	struct display *display;
	struct geometry geometry, window_size;

	struct wl_surface *surface;
	struct ias_surface *shell_surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct wl_callback *callback;
	int fullscreen, output, cur_buf_num;
	cairo_surface_t *cairo_surface[NUM_BUFS];
	bool wait_for_configure;
	float bgnd_alpha;
	float bgnd_r;
	float bgnd_g;
	float bgnd_b;
	float wr_r;
	float wr_g;
	float wr_b;
};

static int running = 1;
static int time_mode = 0;

static struct output *
get_default_output(struct display *display)
{
	struct output *iter;
	int counter = 0;
	wl_list_for_each(iter, &display->output_list, link) {
		if(counter++ == display->window->output)
			return iter;
	}

	// Unreachable, but avoids compiler warning
	return NULL;
}

static void
draw_string(cairo_t *cr,
	const char *fmt, ...) __attribute__((format (gnu_printf, 2, 3)));


static void
draw_string(cairo_t *cr, const char *fmt, ...)
{
	char buffer[4096];
	char *p, *end;
	va_list argp;
	cairo_text_extents_t text_extents;
	cairo_font_extents_t font_extents;

	cairo_save(cr);

	cairo_select_font_face(cr, "sans",
				CAIRO_FONT_SLANT_NORMAL,
				CAIRO_FONT_WEIGHT_NORMAL);
	if (time_mode) {
		cairo_set_font_size(cr, 200);
	} else {
		cairo_set_font_size(cr, 96);
	}
	cairo_font_extents(cr, &font_extents);

	va_start(argp, fmt);

	vsnprintf(buffer, sizeof(buffer), fmt, argp);

	p = buffer;
	while (*p) {
		end = strchr(p, '\n');
		if (end)
			*end = 0;

		cairo_show_text(cr, p);
		cairo_text_extents(cr, p, &text_extents);
		cairo_rel_move_to(cr, -text_extents.x_advance,
						font_extents.height);

		if (end)
			p = end + 1;
		else
			break;
	}

	va_end(argp);

	cairo_restore(cr);
}

static void
set_window_background_colour(cairo_t *cr, struct window *window)
{
	cairo_set_source_rgba(cr, window->bgnd_r , window->bgnd_g,
			window->bgnd_b, window->bgnd_alpha);
}

static void
handle_surface_configure(void *data, struct xdg_surface *surface,
			 uint32_t serial)
{
	struct window *window = data;

	xdg_surface_ack_configure(surface, serial);

	window->wait_for_configure = false;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_surface_configure
};

static void
handle_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
			  int32_t width, int32_t height,
			  struct wl_array *states)
{
	struct window *window = data;
	uint32_t *p;

	window->fullscreen = 0;
	wl_array_for_each(p, states) {
		uint32_t state = *p;
		switch (state) {
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			window->fullscreen = 1;
			break;
		}
	}

	if (width > 0 && height > 0) {
		if (!window->fullscreen) {
			window->window_size.width = width;
			window->window_size.height = height;
		}
		window->geometry.width = width;
		window->geometry.height = height;
	} else if (!window->fullscreen) {
		window->geometry = window->window_size;
	}
}

static void
handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close,
};

static void
ias_handle_ping(void *data, struct ias_surface *ias_surface,
	    uint32_t serial)
{
	ias_surface_pong(ias_surface, serial);
}

static void
ias_handle_configure(void *data, struct ias_surface *ias_surface,
		 int32_t width, int32_t height)
{
	struct window *window = data;

	window->geometry.width = width;
	window->geometry.height = height;

	if (!window->fullscreen)
		window->window_size = window->geometry;
}

static struct ias_surface_listener ias_surface_listener = {
	ias_handle_ping,
	ias_handle_configure,
};

static void
create_xdg_surface(struct window *window, struct display *display)
{
	window->xdg_surface = xdg_wm_base_get_xdg_surface(display->wm_base,
							    window->surface);

	xdg_surface_add_listener(window->xdg_surface,
				     &xdg_surface_listener, window);

	window->xdg_toplevel =
		xdg_surface_get_toplevel(window->xdg_surface);
	xdg_toplevel_add_listener(window->xdg_toplevel,
				  &xdg_toplevel_listener, window);

	xdg_toplevel_set_title(window->xdg_toplevel, "clock");

	window->wait_for_configure = true;
	wl_surface_commit(window->surface);
}

static void
create_ias_surface(struct window *window, struct display *display)
{
	window->shell_surface = ias_shell_get_ias_surface(display->ias_shell,
			window->surface, "clock");
	ias_surface_add_listener(window->shell_surface,
			&ias_surface_listener, window);

	if (window->fullscreen) {
		ias_surface_set_fullscreen(window->shell_surface,
				get_default_output(display)->output);
	} else {
		ias_surface_unset_fullscreen(display->window->shell_surface, 1200, 400);
		ias_shell_set_zorder(display->ias_shell,
				window->shell_surface, display->zorder);
	}
}

static struct wl_shm_pool *
make_shm_pool(struct display *display, int size, void **data)
{
	struct wl_shm_pool *pool;
	int fd;

	fd = os_create_anonymous_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
			size);
		return NULL;
	}

	*data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (*data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	pool = wl_shm_create_pool(display->shm, fd, size);

	close(fd);

	return pool;
}

static struct shm_pool *
shm_pool_create(struct display *display, size_t size)
{
	struct shm_pool *pool = malloc(sizeof *pool);

	if (!pool)
		return NULL;

	pool->pool = make_shm_pool(display, size, &pool->data);
	if (!pool->pool) {
		free(pool);
		return NULL;
	}

	pool->size = size;
	pool->used = 0;

	return pool;
}

static void *
shm_pool_allocate(struct shm_pool *pool, size_t size, int *offset)
{
	if (pool->used + size > pool->size)
		return NULL;

	*offset = pool->used;
	pool->used += size;

	return (char *) pool->data + *offset;
}

static const cairo_user_data_key_t shm_surface_data_key;

static void
shm_pool_destroy(struct shm_pool *pool)
{
	munmap(pool->data, pool->size);
	wl_shm_pool_destroy(pool->pool);
	free(pool);
}

static void
shm_surface_data_destroy(void *p)
{
	struct shm_surface_data *data = p;

	wl_buffer_destroy(data->buffer);
	if (data->pool)
		shm_pool_destroy(data->pool);

	free(data);
}

static cairo_surface_t *
display_create_shm_surface_from_pool(struct display *display,
				     struct rectangle *rectangle,
				     uint32_t flags, struct shm_pool *pool)
{
	struct shm_surface_data *data;
	uint32_t format;
	cairo_surface_t *surface;
	cairo_format_t cairo_format;
	int stride, length, offset;
	void *map;

	data = malloc(sizeof *data);
	if (data == NULL)
		return NULL;

	cairo_format = CAIRO_FORMAT_ARGB32;

	stride = cairo_format_stride_for_width (cairo_format, rectangle->width);
	length = stride * rectangle->height;
	data->pool = NULL;
	map = shm_pool_allocate(pool, length, &offset);

	if (!map) {
		free(data);
		return NULL;
	}

	surface = cairo_image_surface_create_for_data (map,
						       cairo_format,
						       rectangle->width,
						       rectangle->height,
						       stride);

	cairo_surface_set_user_data(surface, &shm_surface_data_key,
				    data, shm_surface_data_destroy);

	if (flags & SURFACE_OPAQUE)
		format = WL_SHM_FORMAT_XRGB8888;
	else
		format = WL_SHM_FORMAT_ARGB8888;

	data->buffer = wl_shm_pool_create_buffer(pool->pool, offset,
						 rectangle->width,
						 rectangle->height,
						 stride, format);

	return surface;
}

static void
shm_pool_reset(struct shm_pool *pool)
{
	pool->used = 0;
}

static int
data_length_for_shm_surface(struct rectangle *rect)
{
	int stride;

	stride = cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32,
						rect->width);
	return stride * rect->height;
}


static cairo_surface_t *
display_create_shm_surface(struct display *display,
			   struct rectangle *rectangle, uint32_t flags,
			   struct shm_pool *alternate_pool,
			   struct shm_surface_data **data_ret)
{
	struct shm_surface_data *data;
	struct shm_pool *pool;
	cairo_surface_t *surface;

	if (alternate_pool) {
		shm_pool_reset(alternate_pool);
		surface = display_create_shm_surface_from_pool(display,
							       rectangle,
							       flags,
							       alternate_pool);
		if (surface) {
			data = cairo_surface_get_user_data(surface,
							   &shm_surface_data_key);
			goto out;
		}
	}

	pool = shm_pool_create(display,
			       data_length_for_shm_surface(rectangle));
	if (!pool)
		return NULL;

	surface =
		display_create_shm_surface_from_pool(display, rectangle,
						     flags, pool);

	if (!surface) {
		shm_pool_destroy(pool);
		return NULL;
	}

	/* make sure we destroy the pool when the surface is destroyed */
	data = cairo_surface_get_user_data(surface, &shm_surface_data_key);
	data->pool = pool;

out:
	if (data_ret)
		*data_ret = data;

	return surface;
}

static int
check_size(struct rectangle *rect)
{
	if (rect->width && rect->height)
		return 0;

	fprintf(stderr, "tried to create surface of "
		"width: %d, height: %d\n", rect->width, rect->height);
	return -1;
}

static cairo_surface_t *
display_create_surface(struct display *display,
		       struct wl_surface *surface,
		       struct rectangle *rectangle,
		       uint32_t flags)
{
	if (check_size(rectangle) < 0)
		return NULL;

	assert(flags & SURFACE_SHM);
	return display_create_shm_surface(display, rectangle, flags,
					  NULL, NULL);
}


static void
create_surface(struct window *window)
{
	struct display *display = window->display;
	struct rectangle rect;
	int i;

	window->surface = wl_compositor_create_surface(display->compositor);

	if (display->wm_base) {
		create_xdg_surface(window, display);
	} else if (display->ias_shell) {
		create_ias_surface(window, display);
	}
	else {
		assert(0);
	}

	rect.width = window->geometry.width;
	rect.height = window->geometry.height;

	for(i = 0; i < NUM_BUFS; i++) {
		window->cairo_surface[i] =
			display_create_surface(display, NULL, &rect, SURFACE_SHM);
	}
}

static void
destroy_surface(struct window *window)
{
	int i;

	if (window->xdg_toplevel)
		xdg_toplevel_destroy(window->xdg_toplevel);
	if (window->xdg_surface)
		xdg_surface_destroy(window->xdg_surface);
	if (window->display->ias_shell) {
		ias_surface_destroy(window->shell_surface);
	}
	wl_surface_destroy(window->surface);

	if (window->callback)
		wl_callback_destroy(window->callback);

	for(i = 0; i < NUM_BUFS; i++) {
		cairo_surface_destroy(window->cairo_surface[i]);
	}
}

static const struct wl_callback_listener frame_listener;

static void
redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct window *window = data;
	cairo_t *cr;
	long milliseconds;
	time_t seconds;
	struct timespec spec;
	struct shm_surface_data *d;

	assert(window->callback == callback);
	window->callback = NULL;

	if (callback)
		wl_callback_destroy(callback);

	cr = cairo_create(window->cairo_surface[window->cur_buf_num % NUM_BUFS]);
	cairo_translate(cr, 0, 0);

	/* Draw background. */
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	set_window_background_colour(cr, window);
	cairo_rectangle(cr, 0, 0, window->geometry.width, window->geometry.height);
	cairo_fill(cr);

	/* Print the time... */
	cairo_set_source_rgb(cr, window->wr_r, window->wr_g, window->wr_b);

	switch(time_mode) {
	case 0:
		{
		/* Get the current time... */
		cairo_move_to(cr, 5, 100);
		clock_gettime(CLOCK_REALTIME, &spec);
		seconds  = spec.tv_sec;
		milliseconds = round(spec.tv_nsec / (double)NS_IN_MS);
		draw_string(cr,
			"Time since the Epoch:\n"
			"%"PRIdMAX".%03ld s\n",
			(intmax_t)seconds, milliseconds);
		}
		break;
	case 1:
		{
		struct timeb t_Time;
		struct tm *t_Local;
		ftime(&t_Time);
		t_Local = localtime(&t_Time.time);
		cairo_move_to(cr, 5, 150);
		draw_string(cr,
			"%02d:%02d:%02d.%03d", t_Local->tm_hour, t_Local->tm_min, t_Local->tm_sec, t_Time.millitm);
		}
		break;
	}

	d = cairo_surface_get_user_data(
			window->cairo_surface[window->cur_buf_num++ % NUM_BUFS],
							   &shm_surface_data_key);

	window->callback = wl_surface_frame(window->surface);
	wl_callback_add_listener(window->callback, &frame_listener, window);

	wl_surface_attach(window->surface, d->buffer, 0, 0);
	wl_surface_damage(window->surface,
			  0, 0, window->geometry.width, window->geometry.height);
	wl_surface_commit(window->surface);

	cairo_destroy(cr);

}

static const struct wl_callback_listener frame_listener = {
	redraw
};


static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	xdg_wm_base_ping,
};

static void
display_add_output(struct display *d, uint32_t id)
{
	struct output *output;

	output = malloc(sizeof *output);
	if (output == NULL)
		return;

	memset(output, 0, sizeof *output);
	output->display = d;
	output->output =
		wl_registry_bind(d->registry, id, &wl_output_interface, 1);
	wl_list_insert(d->output_list.prev, &output->link);
}

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t name, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface, 1);
	} else if (strcmp(interface, "xdg_wm_base") == 0) {
		if (!d->ias_shell) {
			d->wm_base = wl_registry_bind(registry, name,
							&xdg_wm_base_interface, 1);
			xdg_wm_base_add_listener(d->wm_base, &wm_base_listener, d);
		}
	} else if (strcmp(interface, "ias_shell") == 0) {
		if (!d->wm_base) {
			d->ias_shell = wl_registry_bind(registry, name,
					&ias_shell_interface, 1);
		}
	} else if (strcmp(interface, "wl_output") == 0) {
		display_add_output(d, name);
	} else if (strcmp(interface, "wl_shm") == 0) {
		d->shm = wl_registry_bind(registry, name,
					  &wl_shm_interface, 1);
		d->cursor_theme = wl_cursor_theme_load(NULL, 32, d->shm);
		if (!d->cursor_theme) {
			fprintf(stderr, "unable to load default theme\n");
			return;
		}
		d->default_cursor =
			wl_cursor_theme_get_cursor(d->cursor_theme, "left_ptr");
		if (!d->default_cursor) {
			fprintf(stderr, "unable to load default left pointer\n");
			// TODO: abort ?
		}
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void
signal_int(int signum)
{
	running = 0;
}

static void
usage(int error_code)
{
	fprintf(stderr, "Usage: simple-clock [OPTIONS]\n\n"
			"  Background color (0..255)\n"
			"   -a <color>\n"
			"   -r <color>\n"
			"   -g <color>\n"
			"   -b <color>\n"
			"  Text color:\n"
			"   -wr <color>\n"
			"   -wg <color>\n"
			"   -wb <color>\n"
			"  Other options:\n"
			"  -t <mode>    0: Epoch 1: TimeStamp\n"
			"  -z <zorder>  Default zorder\n"
			"  -x <posx>    Pos X\n"
			"  -y <posy>    Pos Y\n"
			"  -h           This help text\n\n");
	exit(error_code);
}

static float cnv_color(uint32_t v)
{
	v &= 0xff;
	return (float)((float)v/(float)0xFF);
}

int
main(int argc, char **argv)
{
	struct sigaction sigint;
	struct display display = { 0 };
	struct window  window  = { 0 };
	int ret = 0;
	struct output *iter, *next;

	window.display = &display;
	display.window = &window;
	window.geometry.width  = 1200;
	window.geometry.height = 400;
	window.window_size = window.geometry;
	window.bgnd_alpha = 1.0;
	window.wr_r = 1.0;
	window.wr_g = 1.0;
	window.wr_b = 1.0;

	for (int i = 1; i < argc; i++) {
		if (strcmp("-a", argv[i]) == 0 && i+1 < argc) {
			window.bgnd_alpha = cnv_color(atoi(argv[++i]));
		} else if (strcmp("-r", argv[i]) == 0 && i+1 < argc) {
			window.bgnd_r = cnv_color(atoi(argv[++i]));
		} else if (strcmp("-g", argv[i]) == 0 && i+1 < argc) {
			window.bgnd_g = cnv_color(atoi(argv[++i]));
		} else if (strcmp("-b", argv[i]) == 0 && i+1 < argc) {
			window.bgnd_b = cnv_color(atoi(argv[++i]));
		} else if (strcmp("-wr", argv[i]) == 0 && i+1 < argc) {
			window.wr_r = cnv_color(atoi(argv[++i]));
		} else if (strcmp("-wg", argv[i]) == 0 && i+1 < argc) {
			window.wr_g = cnv_color(atoi(argv[++i]));
		} else if (strcmp("-wb", argv[i]) == 0 && i+1 < argc) {
			window.wr_b = cnv_color(atoi(argv[++i]));
		} else if (strcmp("-z", argv[i]) == 0 && i+1 < argc) {
			display.zorder = atoi(argv[++i]);
		} else if (strcmp("-x", argv[i]) == 0 && i+1 < argc) {
			display.x = atoi(argv[++i]);
		} else if (strcmp("-y", argv[i]) == 0 && i+1 < argc) {
			display.y = atoi(argv[++i]);
		} else if (strcmp("-t", argv[i]) == 0) {
			time_mode = 1;
			window.geometry.height = 200;
		} else if (strcmp("-h", argv[i]) == 0)
			usage(EXIT_SUCCESS);
		else
			usage(EXIT_FAILURE);
	}

	display.display = wl_display_connect(NULL);
	assert(display.display);
	wl_list_init(&display.output_list);

	display.registry = wl_display_get_registry(display.display);
	wl_registry_add_listener(display.registry,
				 &registry_listener, &display);

	wl_display_dispatch(display.display);

	create_surface(&window);

	display.cursor_surface =
		wl_compositor_create_surface(display.compositor);

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	redraw(&window, NULL, 0);

	/* The mainloop here is a little subtle.  Redrawing will cause
	 * EGL to read events so we can just call
	 * wl_display_dispatch_pending() to handle any events that got
	 * queued up as a side effect. */
	while (running && ret != -1) {
		ret = wl_display_dispatch(display.display);
	}

	fprintf(stderr, "clock exiting\n");

	destroy_surface(&window);

	wl_surface_destroy(display.cursor_surface);
	if (display.cursor_theme)
		wl_cursor_theme_destroy(display.cursor_theme);

	if (display.wm_base)
		xdg_wm_base_destroy(display.wm_base);

	if (display.ias_shell) {
		ias_shell_destroy(display.ias_shell);
	}

	if (display.compositor)
		wl_compositor_destroy(display.compositor);

	wl_registry_destroy(display.registry);

	wl_list_for_each_safe(iter, next, &display.output_list, link) {
		wl_list_remove(&iter->link);
		free(iter);
	}

	wl_display_flush(display.display);
	wl_display_disconnect(display.display);

	return 0;
}
