/*
 *-----------------------------------------------------------------------------
 * Filename: ias-backend.h
 *-----------------------------------------------------------------------------
 * Copyright 2013-2018 Intel Corporation
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
 *   Backend type definitions.
 *-----------------------------------------------------------------------------
 */

#ifndef __IAS_BACKEND_H__
#define __IAS_BACKEND_H__


#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <i915_drm.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <assert.h>

#include <gbm.h>
#include <libudev.h>


#include "gl-renderer.h"
#include "compositor.h"
#include "ias-shell.h"
#include "ias-sprite.h"
#include "ias-common.h"
#include "ias-backend-server-protocol.h"
#include "presentation-time-server-protocol.h"
#include "launcher-util.h"
#include "config-parser.h"

#define MAX_OUTPUTS_PER_CRTC 4

struct ias_fb;
struct ias_configured_output;
struct ias_configured_crtc;

/*
 * IAS output model.  Each backend output model submodule (classic,
 * GPU dualview, stereo HDMI, custom visteon, etc.) should expose a
 * structure of this type to the main backend code.
 */
struct ias_output_model {
	char *name;

	/* How many outputs does this output model create per CRTC? */
	int outputs_per_crtc;

	/*
	 * How many scanout buffers does this output model use?  This should only
	 * include scanouts that are composited by the compositor, not sprite
	 * planes or cursor planes that will be used exclusively for flipping
	 * a single client buffer.
	 */
	int scanout_count;

	/*
	 * Should rendering be flipped in the y direction?  If we use FBO's
	 * internally, y coordinates grow downward (flipped), whereas if we use
	 * window system buffers, y coordinates grow upwards.
	 */
	int render_flipped;

	/* Does this model include the use of a hardware cursor? */
	int hw_cursor;

	/*
	 * Does this output model allow flipping client buffers directly onto the
	 * display and/or sprite planes?
	 */
	int can_client_flip;

	/* Are sprite planes made available to the customer for use in plugins? */
	int sprites_are_usable;

	/* Does this output need driver HDMI stereoscopic 3D support */
	int stereoscopic;

	/* Output model initialization function */
	void (*init)(struct ias_crtc *);

	/* CRTC initialization function (optional) */
	int (*init_crtc)(struct ias_crtc *);

	/* Initialize output for CRTC */
	void (*init_output)(struct ias_output *, struct ias_configured_output *);

	/* CRTC scanout generation function */
	int (*generate_crtc_scanout)(struct ias_crtc *, struct ias_output *,
			pixman_region32_t *);

	/* Pre and post render operations (buffer binding and such) */
	int (*pre_render)(struct ias_output *);
	void (*post_render)(struct ias_output *);

	/* CRTC modeswitch function (optional) */
	void (*switch_mode)(struct ias_crtc *, struct ias_mode *);

	/* Enable/Disable output */
	void (*disable_output)(struct ias_output *);
	void (*enable_output)(struct ias_output *);

	/* Allocate scanout buffer(s) */
	int (*allocate_scanout)(struct ias_crtc *, struct ias_mode *);

	/* Attach a client surface as the next scanout buffer */
	void (*set_next_fb)(struct ias_output *, struct ias_fb *);

	/* Get the current client surface used as the scanout buffer */
	struct ias_fb* (*get_next_fb)(struct ias_output *);

	/* Configure the CRTC with a new display timing */
	int (*set_mode)(struct ias_crtc *);

	/* Page flip handling */
	/* void (*flip_handler)(struct ias_crtc *, unsigned int, unsigned int); */
	void (*flip_handler)(struct ias_crtc *, unsigned int, unsigned int,
			uint32_t, uint32_t);

	/* Schedule a flip if there's a scanout buffer ready */
	void (*flip)(int, struct ias_crtc *, int);

	/* Update sprite plane properties in preperation of a flip */
	void (*update_sprites)(struct ias_crtc *);

	/* Check if a surface is flippable in this output model */
	uint32_t (*is_surface_flippable)(
			struct weston_view* view,
			struct weston_output *output,
			uint32_t check_xy);
};

struct ias_properties {
	uint32_t type;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;

	uint32_t crtc_x;
	uint32_t crtc_y;
	uint32_t crtc_w;
	uint32_t crtc_h;

	uint32_t fb_id;
	uint32_t crtc_id;
	uint32_t blend_func;
	uint32_t blend_color;

	uint32_t rotation;
};

#if 0
struct ias_flip_data {
	struct ias_crtc *ias_crtc;
	int mask;
};
#endif

enum output_position {
	OUTPUT_POSITION_UNDEFINED = 0,
	OUTPUT_POSITION_ORIGIN,
	OUTPUT_POSITION_RIGHTOF,
	OUTPUT_POSITION_BELOW,
	OUTPUT_POSITION_CUSTOM,
};

enum crtc_config {
	CRTC_CONFIG_PREFERRED = 0,
	CRTC_CONFIG_CURRENT,
	CRTC_CONFIG_MODE
};

struct ias_configured_crtc {
	char *name;
	int32_t width, height;
	int32_t x, y;
	uint32_t refresh;
	enum crtc_config config;
	char *model;
	struct ias_configured_output* output[MAX_OUTPUTS_PER_CRTC];
	int output_num;
	int found;

	struct wl_list link;

};

struct ias_configured_output {
	char *name;
	char *size;
	int32_t x, y;
	int32_t rotation;
	enum output_position position;

	/* Name of output that our position is relative to */
	char *position_target;

	/*
	 * True if position if fully known.  False if we still need to calculate
	 * it based off the position of another output.
	 */
	int position_done;
	int vm;

	char **attrs;

	struct wl_list link;
};

struct ias_crtc_properties {
	uint32_t gamma_lut;
	uint32_t mode_id;
	uint32_t active;
};

struct ias_crtc {
	struct wl_list link;
	struct ias_backend *backend;
	struct wl_global *global;

	char *name;
	uint32_t crtc_id;
	uint32_t connector_id;
	drmModeCrtcPtr original_crtc;
	drmModeSubPixel subpixel;

	/*
	 * Outputs associated with this CRTC.  There will only be one output for
	 * a normal 'enabled' CRTC and that output will render directly to the
	 * CRTC's scanout buffer.  For dualview CRTC's, there will be two outputs
	 * and both will be hooked to intermediate buffers that are later combined
	 * to form the real scanout.  For stereo CRTC's, there will also be
	 * two outputs, but each output will be hooked up to a real scanout that
	 * will be flipped onto each of the CRTC's sprite planes.
	 */
	int num_outputs;
	struct ias_output *output[MAX_OUTPUTS_PER_CRTC];

	/* CRTC mode list */
	struct wl_list mode_list;
	struct ias_mode *current_mode;

	/*
	 * The drm framebuffers are tied to the crtc, but are configured,
	 * allocated, and controlled by the output model. This and any other
	 * output model instance data is stored at the output_model_priv
	 * pointer.
	 */
	void *output_model_priv;

	int vblank_pending;
	int page_flip_pending;
	int request_set_mode;
	int request_color_correction_reset;

	/* Pointer to CRTC configuration from config file */
	struct ias_configured_crtc *configuration;

	int current_cursor, cursor_x, cursor_y;
	struct weston_view *cursor_view, *last_cursor_view;
	struct gbm_bo *cursor_bo[2];
	int cursors_are_broken;

	/*
	 * Projection matrix for CRTC.  This only gets used when doing dualview,
	 * while doing the rendering of the real scanout buffer (which combines
	 * the contents of the outputs' intermediate buffers).
	 */
	struct weston_matrix dualview_projection;

	/* Vertex buffer object data for dualview */
	GLfloat dualview_verts[4][4];
	GLuint dualview_vbo;

	/* Weston plane objects */
	struct weston_plane cursor_plane;

	struct wl_list sprite_list;
	int num_sprites;
	int sprites_are_broken;

	/* Output model info */
	struct ias_output_model *output_model;

	/* Output properties -- atomic mode/pageflip */
	drmModeAtomicReqPtr prop_set;
	struct ias_crtc_properties prop;

	uint32_t brightness;
	uint32_t contrast;
	uint32_t gamma;
	/* Id of drm blob with color correction table to be updated atomically */
	uint32_t color_correction_blob_id;
	int index;
};

enum ias_fb_type {
	IAS_FB_SCANOUT = 0x100,
	IAS_FB_OVERLAY  = 0x101,
	IAS_FB_CURSOR  = 0x102
};


struct ias_mode {
	struct weston_mode base;
	struct wl_list link;
	drmModeModeInfo mode_info;
	uint32_t id;
};

struct ias_fb {
	struct gbm_bo *bo;
	struct gbm_bo *stereo_bo[2];
	struct ias_output *output;
	uint32_t fb_id;
	int is_compressed;
	int is_client_buffer;
	struct weston_buffer_reference buffer_ref;
};

/*
 * An output has a primary display plane plus zero or more sprites for
 * blending display contents.
 */

enum sprite_dirty_bit {
	SPRITE_DIRTY_ZORDER			= 0x1,
	SPRITE_DIRTY_BLENDING = 0x2,
	SPRITE_DIRTY_FB_BLEND_OVL	= 0x4
};

struct ias_sprite {
	struct wl_list link;

	struct ias_fb *current, *next;
	uint32_t page_flip_pending;
	struct weston_view *view;
	int locked;
	enum sprite_dirty_bit sprite_dirty;
	int zorder;

	int blending_enabled;
	float blending_value;
	int blending_src_factor;
	int blending_dst_factor;

	struct ias_backend *compositor;
	struct ias_crtc *ias_crtc;
	struct ias_properties prop;

	uint32_t possible_crtcs;
	int plane_id;
	int pipe_id;
	int index;
	struct weston_plane plane;
	int output_id;

	int type;
	int rotation;
	int32_t src_x, src_y;
	uint32_t src_w, src_h;
	uint32_t dest_w, dest_h;

	uint32_t supports_rbc;

	uint32_t count_formats;
	uint32_t formats[];
};

struct ias_fb *
ias_fb_get_from_bo(struct gbm_bo *bo, struct weston_buffer *buffer,
		   struct ias_output *output, enum ias_fb_type type);

void
ias_output_render(struct ias_output *output, pixman_region32_t *new_damage);

void
ias_set_dpms(struct ias_crtc *ias_crtc, enum dpms_enum level);

void
ias_fb_destroy_callback(struct gbm_bo *bo, void *data);

void
ias_output_scale(struct ias_output *ias_output, uint32_t width, uint32_t height);

int num_views_on_output(struct weston_output *output);

int surface_covers_output(struct weston_surface *surface,
		struct weston_output *output);
void
add_connector_id(struct ias_crtc *ias_crtc);

#endif
