/*
 *-----------------------------------------------------------------------------
 * Filename: ias-common.h
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
 *   Common internal functionality shared by both IAS shell and backend.
 *-----------------------------------------------------------------------------
 */

#ifndef __IAS_COMMON_H__
#define __IAS_COMMON_H__

#include "config.h"
#include <wayland-server.h>
#include <xf86drmMode.h>
#include "compositor.h"
#include "libinput-seat.h"
#include "compositor/weston.h"

#include "ias-plugin-framework-definitions.h"
#include "ias-spug.h"

/***
 *** Debugging functionality
 ***/

#define IAS_ERROR(msg, ...) \
weston_log("IAS ERROR: " msg "\n" , ##__VA_ARGS__)

#ifdef IASDEBUG
#define IAS_DEBUG(msg, ...) \
weston_log("[dbg] :: " msg "\n" , ##__VA_ARGS__)
#else
#define IAS_DEBUG(msg, ...)
#endif

/*
 * Mark a variable as 'used' in release builds only.  This is intended
 * to silence unused variable warnings for variables that only get
 * used in debug builds.
 */
#ifdef IASDEBUG
#define UNUSED_IN_RELEASE(var)
#else
#define UNUSED_IN_RELEASE(var)  (void)(var)
#endif

/***
 *** Configuration functionality
 ***/

/* Valid XML elements we can parse */
enum ias_element {
	NONE		= 0,
	IASCONFIG	= 1,
	BACKEND		= 2,
	STARTUP		= 4,
	CRTC		= 8,
	OUTPUT		= 16,
	HMI			= 32,
	PLUGIN		= 64,
	INPUTPLUGIN = 128,
	INPUT		= 256,
	ENV			= 512,
	GLOBAL_ENV	= 1024,
	REM_DISP	= 2048,
};

/* Type to hold element -> handler mapping and hierarchy */
struct xml_element {
	enum ias_element id;
	char *name;
	void (*begin_handler)(void *, const char **);
	unsigned int valid_children;
	enum ias_element return_to;
};

/* Parses the IAS config file using the provided state machine info */
int ias_read_configuration(char *, struct xml_element *, int, void *);

/***
 *** Backend compositor type
 ***/

#define BACKEND_MAGIC 0xDEADBEEF

struct ias_crtc;
struct ias_sprite;
struct ias_mode;
struct ias_plugin;

enum crtc_plane {
	CRTC_PLANE_MAIN = 0,
	CRTC_PLANE_SPRITE_A,
	CRTC_PLANE_SPRITE_B,
};

struct ias_output {
	struct weston_output base;
	char *name;
	struct wl_list link;
	struct backlight *backlight;
	struct ias_crtc *ias_crtc;
	int32_t width;
	int32_t height;
	int32_t rotation;
	struct wl_global *global;
	enum crtc_plane scanout;

	/*
	 * Signal and listener for output changes (resize/move).  The signal
	 * is initialized and emitted by the backend.  The listener is initialized
	 * and hooked up by the shell.  The is_resized flag indicates whether
	 * the update includes a resize (which requires additional processing
	 * by the shell).
	 */
	struct wl_signal update_signal;
	struct wl_listener update_listener;
	int is_resized;

	int disabled;

	/* Fake mode for output (hardware mode is in the CRTC) */
	struct weston_mode mode;

	struct weston_plane fb_plane;

	/*
	 * Output is responsible for intermediate buffers in the dualview case.
	 * These are texture-backed FBO's.
	 */
	GLuint texture;
	GLuint fbo;

	/*
	 * Previous frame's damage.  Since we swap between scanout buffers, we
	 * need to redraw both the current frame's damage and the previous frame's
	 * damage to ensure no artifacts when updating the scanout.
	 */
	pixman_region32_t prev_damage;

	/* Loadable layout plugin */
	struct ias_plugin *plugin;

	/* When a layout plugin is active, when should this output be redrawn? */
	int plugin_redraw_always;

	/* Flag to indicate if VM is enabled or not */
	int vm;

	struct wl_list *input_list;

	/*
	 * If a plugin wants to flip a surface, we will fill this structure on its
	 * behalf
	 */
	struct weston_surface *scanout_surface;

	struct wl_signal printfps_signal;
	struct wl_listener printfps_listener;

#if defined(BUILD_VAAPI_RECORDER) || defined(BUILD_FRAME_CAPTURE)
	struct wl_signal next_scanout_ready_signal;
#endif
#ifdef BUILD_FRAME_CAPTURE
	struct capture_proxy *cp;
	struct wl_listener capture_proxy_frame_listener;
#endif
};

#ifdef BUILD_FRAME_CAPTURE
struct ias_surface_capture {
	struct wl_list link;
	struct capture_proxy *cp;
	struct weston_surface *capture_surface;
	struct wl_listener capture_commit_listener;
	struct wl_listener capture_vsync_listener;
	struct ias_backend *backend;
};
#endif

struct ias_backend {
	struct weston_backend base;
	struct weston_compositor *compositor;

	/*
	 * Magic number; helps shell confirm that it's running on the
	 * IAS backend
	 */
	uint32_t magic;

	struct udev *udev;
	struct wl_event_source *ias_source;

	struct udev_monitor *udev_monitor;
	struct wl_event_source *udev_ias_source;

	struct {
		int id;
		int fd;
		char *filename;
	} drm;
	struct gbm_device *gbm;

#ifdef HYPER_DMABUF
	int hyper_dmabuf_fd;
#endif

	struct wl_list crtc_list;
	uint32_t *kms_crtcs;

	/* How many CRTC's does KMS report (we may not use all of them) */
	int num_kms_crtcs;

	/* How many CRTC's are actually configured/managed by the compositor */
	int num_compositor_crtcs;

	/* Bitfield of which CRTC ID's in use with a connector already */
	uint32_t crtc_allocator;

	struct wl_listener session_listener;
	uint32_t connector_allocator;
	struct tty *tty;
	uint32_t format;
	int use_pixman;

	uint32_t prev_state;
	int private_multiplane_drm;
	int has_nuclear_pageflip;

	/* RandR object for client-initiated driver control */
	struct ias_crtc *ias_crtc;

	clockid_t clock;
	/* If input devices have been provided per output */
	int input_present;

	/* Entrypoints called by plugin manager helper functions */
	int (*get_sprite_list)(struct weston_output *, struct ias_sprite ***);
	struct weston_plane* (*assign_view_to_sprite)(struct weston_view *,
			/*
			struct ias_sprite *,
			*/
			struct weston_output *,
			int *sprite_id,
			int x,
			int y,
			int sprite_width,
			int sprite_height,
			pixman_region32_t *);
	int (*assign_zorder_to_sprite) (struct weston_output *, int, int);
	int (*set_fb_blend_ovl) (struct weston_output *, int );
	int (*assign_constant_alpha_to_sprite) (struct weston_output *, int, float, int);
	struct weston_plane * (*attempt_scanout_for_view) (struct weston_output *,
			struct weston_view *, uint32_t check_xy);
	void (*get_tex_info)(struct weston_view *view, int *num, GLuint *names);
	void (*get_egl_image_info)(struct weston_view *view, int *num, EGLImageKHR *names);
	void (*set_viewport)(int x, int y, int width, int height);
#if BUILD_FRAME_CAPTURE
	int (*start_capture)(struct wl_client *client,
			struct ias_backend *ias_backend, struct wl_resource *resource,
			struct weston_surface *surface, uint32_t output_number,
			int profile, int verbose);
	int (*stop_capture)(struct wl_client *client,
			struct ias_backend *ias_backend, struct wl_resource *resource,
			struct weston_surface *surface, uint32_t output_number);
	int (*release_buffer_handle)(struct ias_backend *ias_backend,
			uint32_t surfid, uint32_t bufid, uint32_t imageid,
			struct weston_surface *surface, uint32_t output_number);

	struct wl_list capture_proxy_list;
#endif

	int print_fps;
	int no_flip_event;
	struct udev_input input;
	int rbc_supported;
	int rbc_enabled;
	int rbc_debug;
	int use_cursor_as_uplane;
};

/*
 * Loadable layout or input plugin
 */
struct ias_plugin {
	/* Version of plugin structure */
	unsigned int version;

	/* Library name to load plugin from */
	char *libname;

	/* Name of plugin broadcast to clients */
	char *name;

	/* Plugin info structure with plugin function pointers */
	union {
		struct ias_plugin_info info;
		struct ias_input_plugin_info input_info;
	};

	/* Link in ias-shell's plugin list */
	struct wl_list link;

	/* When do we initialize this plugin (load time or activation time)? */
	enum {
		INIT_NORMAL = 0,
		INIT_DEFERRED,
	} init_mode;

	/* Has this plugin's initialization function been called yet? */
	int init;

	/*
	 * Comma-separated list of outputs to activate this plugin on immediately
	 * when initialized
	 */
	char *activate_on;

	void (*draw_plugin)(struct ias_output *);
};

void handle_env_common(const char **attrs, struct wl_list *list);
void set_unset_env(struct wl_list *env);

/*
 * a helper macro for shoving 64bit pointers into 32bit ints for IDs.
 */
#define INT32_MASK 0xffffffff
#define SURFPTR2ID(ptr) ((uint32_t)(INT32_MASK & (uintptr_t)ptr))

#endif
