/*
 * Copyright © 2018 Intel Corporation
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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>

#include <va/va_drm.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "../shared/config-parser.h"
#include "../shared/timespec-util.h"
#include "../shared/helpers.h"
#include "ias-shell-client-protocol.h"

#include "encoder.h"
#include "main.h"
#include "input_receiver.h"

enum {
	STOP_DISPLAY = 0,
	START_DISPLAY,
	INVALID_DISPLAY_STATE
};

struct app_state app_state = { 0 };

static void geometry_event(void *data, struct wl_output *wl_output,
		int x, int y, int w, int h,
		int subpixel, const char *make, const char *model, int transform)
{
	struct output *output = data;

	output->width = w;
	output->height = h;
	output->x = x;
	output->y = y;
	if (app_state.verbose > 2) {
		printf("Geometry event received. %d by %d output at %d,%d.\n", w, h, x, y);
	}
}

static void mode_event(void *data, struct wl_output *wl_output,
	uint32_t flags, int width, int height, int refresh)
{
	if (app_state.verbose > 2) {
		printf("Mode event received. Output of %d by %d.\n", width, height);
	}
}

static const struct wl_output_listener output_listener = {
	geometry_event,
	mode_event
};

static void
handle_surface_info(void *data,
		 struct ias_hmi *ias_hmi,
		 uint32_t id,
		 const char *name,
		 uint32_t zorder,
		 int32_t x,
		 int32_t y,
		 uint32_t width,
		 uint32_t height,
		 uint32_t alpha,
		 uint32_t behavior_bits,
		 uint32_t pid,
		 const char *pname,
		 uint32_t output,
		 uint32_t flipped)
{
	struct surf_list *s, *existing;
	struct app_state *app_state = data;

	wl_list_for_each_reverse(existing, &app_state->surface_list, link) {
		if(id == existing->surf_id) {
			break;
		}
	}
	if (&existing->link == &app_state->surface_list) {
		existing = NULL;
	}

	if (!existing) {
		s = calloc(1, (sizeof(*s)));
		if(!s) {
			fprintf(stderr, "Couldn't allocate a surface list\n");
			return;
		}
	} else {
		s = existing;
		free(s->name);
	}

	s->surf_id = id;
	s->name = strdup(name);
	s->x = x;
	s->y = y;
	s->width = width;
	s->height = height;
	s->zorder = zorder;
	s->alpha = alpha;

	if (!existing) {
		wl_list_insert(&app_state->surface_list, &s->link);
	}
}

static void
handle_surface_destroyed(void *data,
			struct ias_hmi *hmi,
			uint32_t id,
			const char *name,
			uint32_t pid,
			const char *pname)
{
	struct app_state *app_state = data;
	struct surf_list *s, *tmp;

	/* Find the surface and remove it from our surface list */
	wl_list_for_each_safe(s, tmp, &app_state->surface_list, link) {
		if (s->surf_id == id) {
			wl_list_remove(&s->link);
			free(s->name);
			free(s);
			return;
		}
	}
}

static void *
encoder_init_thread_function(void * const data)
{
	struct app_state *app_state = data;
	int err;

	app_state->encoder_state = ENC_STATE_INIT;

	err = rd_encoder_init(app_state->rd_encoder,
				app_state->src_width, app_state->src_height,
				app_state->x, app_state->y,
				app_state->w, app_state->h,
				app_state->encoder_tu,
				app_state->surfid, app_state->hmi,
				app_state->display,
				app_state->output_number);

	if (err == 0) {
		app_state->encoder_state = ENC_STATE_RUN;
	} else {
		fprintf(stderr, "Encoder init failed! : %d\n", err);
		app_state->encoder_state = ENC_STATE_ERROR;
	}

	return NULL;
}

static int
init_encoder(struct app_state *app_state)
{
	int err = 0;

	/* The default is to record the whole surface... */
	if (app_state->w == 0) {
		app_state->w = app_state->src_width - app_state->x;
	}
	if (app_state->h == 0 ) {
		app_state->h = app_state->src_height - app_state->y;
	}

	if ((app_state->x < 0) || (app_state->y < 0)
		|| (app_state->w < 0) || (app_state->h < 0)
		|| (app_state->x + app_state->w > app_state->src_width)
		|| (app_state->y + app_state->h > app_state->src_height)) {
		fprintf(stderr, "Bad region values.\n");
		return -1;
	}

	err = pthread_create(&app_state->encoder_init_thread, NULL,
				encoder_init_thread_function, app_state);
	if (err != 0) {
		fprintf(stderr, "Encoder init thread creation failure: %d\n", err);
		return err;
	}

	return err;
}


static void
handle_raw_buffer_handle(void *data,
		struct ias_hmi *ias_hmi,
		int32_t handle,
		uint32_t timestamp,
		uint32_t frame_number,
		uint32_t stride0,
		uint32_t stride1,
		uint32_t stride2,
		uint32_t format,
		uint32_t out_width,
		uint32_t out_height,
		uint32_t shm_surf_id,
		uint32_t buf_id,
		uint32_t image_id)
{
	struct app_state *app_state = data;

	if (app_state->verbose > 1) {
		printf("RemoteDisplay: ias_hmi_raw_buffer_handle:\n"
				"handle: %d\n"
				"timestamp: %u\n"
				"frame_number: %u\n"
				"stride0: %u\n"
				"stride1: %u\n"
				"stride2: %u\n"
				"format: %u\n"
				"width: %u\n"
				"height: %u\n"
				"shm_surf_id: %u\n"
				"buf_id: %u\n"
				"image_id: %u\n",
				handle, timestamp, frame_number, stride0, stride1,
				stride2, format, out_width, out_height, shm_surf_id,
				buf_id, image_id);
	}

	switch (app_state->encoder_state) {
	case ENC_STATE_NONE:
	case ENC_STATE_INIT:
		if (app_state->verbose) {
			printf("Encoder init not complete, dropping frame\n");
		}
		ias_hmi_release_buffer_handle(ias_hmi, shm_surf_id, buf_id, image_id,
						app_state->surfid, 0);
		break;
	case ENC_STATE_RUN:
		rd_encoder_frame(app_state->rd_encoder, handle, -1,
					stride0, stride1, stride2, timestamp, format,
					frame_number, shm_surf_id, buf_id, image_id);
		break;
	case ENC_STATE_ERROR:
		app_state->recording = 0;
		break;
	}
}


static void
handle_raw_buffer_fd(void *data,
		struct ias_hmi *ias_hmi,
		int32_t prime_fd,
		uint32_t timestamp,
		uint32_t frame_number,
		uint32_t stride0,
		uint32_t stride1,
		uint32_t stride2,
		uint32_t format,
		uint32_t out_width,
		uint32_t out_height)
{
	struct app_state *app_state = data;

	if (app_state->verbose > 1) {
		printf("RemoteDisplay: handle_raw_buffer_fd:\n"
				"prime_fd: %d\n"
				"timestamp: %u\n"
				"frame_number: %u\n"
				"stride0: %u\n"
				"stride1: %u\n"
				"stride2: %u\n"
				"format: %u\n"
				"width: %u\n"
				"height: %u\n",
				prime_fd, timestamp, frame_number,
				stride0, stride1, stride2, format,
				out_width, out_height);
	}

	switch(app_state->encoder_state) {
	case ENC_STATE_NONE:
	case ENC_STATE_INIT:
		if (app_state->verbose) {
			printf("Encoder init not complete, dropping frame\n");
		}
		if (app_state->surfid) {
			ias_hmi_release_buffer_handle(ias_hmi, 0, 0, 0,
							app_state->surfid, 0);
		} else {
			ias_hmi_release_buffer_handle(ias_hmi, 0, 0, 0, 0,
							app_state->output_number);
		}
		break;
	case ENC_STATE_RUN:
		rd_encoder_frame(app_state->rd_encoder, 0, prime_fd,
					stride0, stride1, stride2, timestamp, format,
					frame_number, 0, 0, 0);
		break;
	case ENC_STATE_ERROR:
		app_state->recording = 0;
		break;
	}
}

static void
handle_capture_error(void *data,
		struct ias_hmi *hmi,
		int32_t pid,
		int32_t error)
{
	if (getpid() == pid) {
		switch(error) {
		case IAS_HMI_FCAP_ERROR_NO_CAPTURE_PROXY:
			fprintf(stderr, "Capture proxy error: No proxy.\n");
			break;
		case IAS_HMI_FCAP_ERROR_DUPLICATE:
			fprintf(stderr, "Capture error: Duplicate "
							"surface/output requested.\n");
			break;
		case IAS_HMI_FCAP_ERROR_NOT_BUILT_IN:
			fprintf(stderr, "Capture proxy not built into Weston!\n");
			break;
		case IAS_HMI_FCAP_ERROR_INVALID:
			fprintf(stderr, "Capture proxy error: Invalid parameter\n");
			break;
		case IAS_HMI_FCAP_ERROR_OK:
			/* No actual error. */
			break;
		}

		if (error) {
			struct app_state *app_state = data;

			app_state->recording = 0;
		}
	}
}

static const struct ias_hmi_listener hmi_listener = {
	handle_surface_info,
	handle_surface_destroyed,
	NULL,
	handle_raw_buffer_handle,
	handle_raw_buffer_fd,
	handle_capture_error,
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		      const char *interface, uint32_t version)
{
	struct app_state *app_state = data;
	struct output *new_output;

  printf("%s : %s.\n", __func__, interface);
	if (strcmp(interface, "ias_hmi") == 0) {
		app_state->hmi = wl_registry_bind(registry, id, &ias_hmi_interface, 1);
		ias_hmi_add_listener(app_state->hmi, &hmi_listener, app_state);
	} else if (strcmp(interface, "ias_relay_input") == 0) {
		printf("Bind ias_relay_input.\n");
		app_state->ias_in = wl_registry_bind(registry, id, &ias_relay_input_interface, 1);
	} else if (strcmp(interface, "wl_output") == 0) {
		new_output = calloc(1, sizeof *new_output);
		if (!new_output) {
			fprintf(stderr, "Failed to handle new output: out of memory.\n");
			return;
		}
		wl_list_insert(&app_state->output_list, &new_output->link);
		new_output->output = wl_registry_bind(registry, id, &wl_output_interface, 1);
		wl_output_add_listener(new_output->output, &output_listener, new_output);
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
};

static void
stop_recording(struct app_state *app_state)
{
	ias_hmi_stop_capture(app_state->hmi, app_state->surfid,
			app_state->output_number);
}

static void
start_recording(struct app_state *app_state)
{
	if (app_state->verbose) {
		printf("Start recording from output %d.\n",
			app_state->output_number);
	}

	ias_hmi_start_capture(app_state->hmi, app_state->surfid,
		app_state->output_number, app_state->profile, app_state->verbose);

	app_state->recording = 1;
}

static void
on_term_signal(int signal_number)
{
	if (app_state.verbose) {
		printf("\nCaught signal %d, stopping recording.\n",
				signal_number);
		}

	app_state.recording = 0;
}

static int
get_surface_size(struct app_state *app_state, int *width, int *height)
{
	struct surf_list *s;

	wl_list_for_each(s, &app_state->surface_list, link) {
		if (app_state->surfid) {
			if (s->surf_id == (uint32_t) app_state->surfid) {
				*width = s->width;
				*height = s->height;

				if (app_state->verbose > 1) {
					printf("Surface %d found %dx%d\n",
						app_state->surfid, *width, *height);
				}

				return 0;
			}
		} else {
			fprintf(stderr, "Invalid surfid %d\n", app_state->surfid);
			return -1;
		}
	}

	if (*width == 0 || *height == 0) {
		fprintf(stderr, "Surface size is 0.\n");
		return -1;
	}

	return 0;
}


static int
get_output_size(struct app_state *app_state, int *width, int *height)
{
	struct output *output;
	int output_num = 0;

	wl_list_for_each(output, &app_state->output_list, link) {
		if (app_state->output_number == output_num) {
			app_state->output_origin_x = output->x;
			app_state->output_origin_y = output->y;
			app_state->output_width = output->width;
			app_state->output_height = output->height;
			*width = output->width;
			*height = output->height;

			if (app_state->verbose > 0) {
				printf("Output %d found %dx%d\n",
					app_state->output_number,
					output->width, output->height);
			}

			return 0;
		}
		output_num++;
	}

	if (*width == 0 || *height == 0) {
		fprintf(stderr, "Output size is 0.\n");
		return -1;
	}

	return 0;
}


static void
plugin_print_help(void)
{
	void (*plugin_help_fptr)(void);
	void *plugin_handle = NULL;

	if (app_state.transport_plugin) {
		plugin_handle = dlopen(app_state.plugin_fullname, RTLD_LAZY | RTLD_LOCAL);
	}
	if (!plugin_handle) {
		fprintf(stderr, "Failed to load transport plugin.\n");
		return;
	}

	plugin_help_fptr = dlsym(plugin_handle, "help");
	if (plugin_help_fptr) {
		(*plugin_help_fptr)();
	} else {
		fprintf(stderr, "Failed to locate help in transport plugin: %s\n", dlerror());
	}

	dlclose(plugin_handle);
}


static void
plugin_fullname_helper(void)
{
	char *prefix = "transport_plugin_";
	char *suffix = ".so";
	size_t fullnamelen = strlen(prefix)
				+ strlen(app_state.transport_plugin)
				+ strlen(suffix) + 1;

	app_state.plugin_fullname = malloc(fullnamelen);
	if (app_state.plugin_fullname == NULL) {
		fprintf(stderr, "plugin_fullname_helper : name allocation failure.\n");
	} else {
		snprintf(app_state.plugin_fullname, fullnamelen, "transport_plugin_%s%s",
				app_state.transport_plugin, suffix);
	}
}


static void
usage(int error_code)
{
	if (error_code != 0) {
		fprintf(stderr, "Exiting with error code of %d...\n",
				error_code);
	}

	printf("\nUsage:\n\tremote-display [options]\n"
		"Controls the frame capture in weston and handles the frames passed"
		" by weston, recording\nto file or sending to a remote device as a"
		" video stream.\n\n");
	printf("Options:\n");
	printf("\t--plugin=<transport_plugin>\tTransport plugin to use."
		" Examples are avb, file, tcp and stub.\n");
	printf("\t--state=0\t\t\tstop frame capture, e.g. if another client did"
		" not close cleanly\n"
		"\t--state=1\t\t\tstart frame capture (this is the default)\n");
	printf("\t--verbose=<level>\t\tenable extra trace at levels 1, 2 or 3\n");
	printf("\t--profile=<level>\t\tenable profiling trace at levels 1 or 2\n");
	printf("\t--surfid=<surfid>\t\tweston surface ID of the surface "
		"to be captured\n");
	printf("\t--output=<output_number>\tweston output to capture, starting"
		" from 0 - ignored if surfid is given\n");
	printf("\t--x=<x_coordinate>\t\tx coordinate of region of surface"
		" to be captured\n"
		"\t--y=<x_coordinate>\t\ty coordinate of region of surface "
		"to be captured\n"
		"\t--w=<width>\t\t\twidth of region of surface to be captured\n"
		"\t--h=<height>\t\t\theight of region of surface "
		"to be captured\n");
	printf("\t--help\t\t\t\tshow this help text and exit\n\n");
	printf("Note that all options other than state default to zero.\n"
		"A width or height of zero is taken to mean that the entire "
		"width or height of the surface should be captured.\n"
		"A surface ID of zero means that the whole framebuffer for the "
		"selected output will be captured.\n");

	printf("\nTransport plugin help:\n");

	plugin_print_help();

	exit(error_code);
}

static int
init(struct app_state *app_state, int *argc, char **argv)
{
	wl_list_init(&app_state->surface_list);
	wl_list_init(&app_state->output_list);

	app_state->display = wl_display_connect(NULL);
	if (!app_state->display) {
		fprintf(stderr, "Failed to open wayland display.\n");
		return -1;
	}

	/* Listen for global object broadcasts */
	app_state->registry = wl_display_get_registry(app_state->display);
	wl_registry_add_listener(app_state->registry, &registry_listener,
			app_state);
	wl_display_dispatch(app_state->display);
	wl_display_roundtrip(app_state->display);

	int ret;
	if (app_state->surfid) {
		printf("Controlling recording from surface %d...\n", app_state->surfid);
		ret = get_surface_size(app_state, &app_state->src_width,
				&app_state->src_height);
		 if (ret != 0) {
			return -1;
		 }
	} else {
		printf("Controlling recording from output %d...\n", app_state->output_number);
		ret = get_output_size(app_state, &app_state->src_width,
				&app_state->src_height);
		if (ret != 0) {
			return -1;
		}
	}

	app_state->rd_encoder =
		rd_encoder_create(app_state->verbose, app_state->plugin_fullname, argc, argv);

	if (app_state->rd_encoder == NULL) {
		fprintf(stderr, "Failed to create Remote Display encoder\n");
		return -1;
	}

	if (app_state->profile) {
		rd_encoder_enable_profiling(app_state->rd_encoder, app_state->profile);
	}

	app_state->encoder_state = ENC_STATE_NONE;

	if (init_encoder(app_state) != 0) {
		fprintf(stderr, "RD-Encoder error: Bad init\n");
		app_state->recording = 0;
		return -1;
	}

	return 0;
}

static int
destroy(struct app_state *app_state)
{
	struct surf_list *s, *tmp;
	struct output *output, *temp_output;

	if (app_state->verbose) {
		printf("Flushing...\n");
	}
	wl_display_flush(app_state->display);

	if (app_state->verbose) {
		printf("Waiting for completion...\n");
	}
	wl_display_roundtrip(app_state->display);

	if (app_state->rd_encoder) {
		if (app_state->verbose) {
			printf("Destroying encoder...\n");
		}
		rd_encoder_destroy(app_state->rd_encoder);
	}

	if (app_state->plugin_fullname) {
		free(app_state->plugin_fullname);
	}

	if (app_state->verbose) {
		printf("Disconnecting...\n");
	}
	wl_display_disconnect(app_state->display);

	if (app_state->verbose) {
		printf("Freeing surface list...\n");
	}
	wl_list_for_each_safe(s, tmp, &app_state->surface_list, link) {
		wl_list_remove(&s->link);
		free(s->name);
		free(s);
	}

	if (app_state->verbose) {
		printf("Freeing output list...\n");
	}
	wl_list_for_each_safe(output, temp_output, &app_state->output_list, link) {
		free(output);
	}

	if (app_state->encoder_init_thread) {
		if (app_state->verbose) {
			printf("Freeing encoder init thread\n");
		}
		pthread_join(app_state->encoder_init_thread, NULL);
	}

	if (app_state->verbose) {
		printf("App state destroyed.\n");
	}

	return 0;
}

int
main(int argc, char **argv)
{
	int state = INVALID_DISPLAY_STATE;
	int help = 0;
	int err = 0;

	const struct weston_option options[] = {
		{ WESTON_OPTION_INTEGER, "state", 0, &state},
		{ WESTON_OPTION_INTEGER, "verbose", 0, &app_state.verbose},
		{ WESTON_OPTION_INTEGER, "profile", 0, &app_state.profile},
		{ WESTON_OPTION_STRING,  "plugin", 0, &app_state.transport_plugin},
		{ WESTON_OPTION_UNSIGNED_INTEGER, "surfid", 0, &app_state.surfid},
		{ WESTON_OPTION_INTEGER, "output", 0, &app_state.output_number},
		{ WESTON_OPTION_INTEGER, "x", 0, &app_state.x},
		{ WESTON_OPTION_INTEGER, "y", 0, &app_state.y},
		{ WESTON_OPTION_INTEGER, "w", 0, &app_state.w},
		{ WESTON_OPTION_INTEGER, "h", 0, &app_state.h},
		{ WESTON_OPTION_INTEGER, "tu", 0, &app_state.encoder_tu},
		{ WESTON_OPTION_BOOLEAN, "help", 0, &help },
	};

	parse_options(options, ARRAY_LENGTH(options), &argc, argv);

	if (help) {
		if (app_state.transport_plugin == NULL) {
			fprintf(stderr, "No transport plugin name given.\n");
		} else {
			plugin_fullname_helper();
		}
		usage(0);
	}

	if (state == INVALID_DISPLAY_STATE) {
		state = START_DISPLAY;
		printf("Defaulting to starting display...\n");
	}

	if (app_state.transport_plugin == NULL) {
		/* Transport plugin name is not needed to stop displaying. */
		if (state == START_DISPLAY) {
			fprintf(stderr, "No transport plugin name given.\n");
			usage(-EINVAL);
		}
	} else {
		plugin_fullname_helper();
	}

	if (app_state.encoder_tu == 0) {
		/* Default to fastest encode mode. */
		app_state.encoder_tu = 7;
	}

	err = init(&app_state, &argc, argv);
	if ((err == 0) && state) {
		/* Catch SIGINT / Ctrl+C to stop recording. */
		signal(SIGINT, on_term_signal);
		/* Catch SIGTERM (pkill) to stop recording. */
		signal(SIGTERM, on_term_signal);

		printf("Starting event listener...\n");
		start_event_listener(&app_state, &argc, argv);

		printf("Starting recording...\n");
		start_recording(&app_state);
		wl_display_roundtrip(app_state.display);

		while (app_state.recording) {
			wl_display_dispatch(app_state.display);
		}
	}

	printf("Stopping recording...\n");
	stop_recording(&app_state);

	printf("Stopping event listener...\n");
	stop_event_listener(app_state.ir_priv);

	destroy(&app_state);

	printf("Exiting %s...\n", argv[0]);
	return 0;
}
