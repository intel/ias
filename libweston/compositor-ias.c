/*
 *-----------------------------------------------------------------------------
 * Filename: compositor-ias.c
 *-----------------------------------------------------------------------------
 * Copyright 2011-2018 Intel Corporation
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
 * Portions Copyright © 2008-2011 Kristian Høgsberg
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Unless otherwise agreed by Intel in writing, you may not remove or alter
 * this notice or any other notice embedded in Materials by Intel or Intel’s
 * suppliers or licensors in any way.
 *-----------------------------------------------------------------------------
 * Description:
 *   IAS Backend
 *-----------------------------------------------------------------------------
 */

#include "compositor-ias.h"
#include "config.h"
#include "ias-backend.h"
#include "launcher-util.h"
#include "trace-reporter.h"
#include <EGL/egl.h>
#include <dlfcn.h>
#include <time.h>
#include "linux-dmabuf.h"

#include <EGL/eglext.h>

#ifdef HYPER_DMABUF
#include <hyper_dmabuf.h>
#endif

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

#ifdef BUILD_FRAME_CAPTURE
#include "capture-proxy.h"
#include "../shared/timespec-util.h"
#include "ias-shell-server-protocol.h"
#endif

#ifndef DRM_CAP_TIMESTAMP_MONOTONIC
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x6
#endif

#ifndef DRM_CAP_RENDER_COMPRESSION
#define DRM_CAP_RENDER_COMPRESSION     0x11
#endif

struct drm_parameters {
	int connector;
	int tty;
	int use_pixman;
	const char *seat_id;
};

extern struct ias_output_model output_model_classic;
extern struct ias_output_model output_model_flexible;


/* Maximum sprites supported per CRTC */
#define MAX_SPRITE_PER_CRTC 2

struct ias_output;
struct ias_crtc;

struct gl_renderer_interface *gl_renderer;

struct ias_configured_crtc *cur_crtc;
static struct wl_list configured_crtc_list;
static struct wl_list configured_output_list;
static struct wl_list global_env_list;
static struct wl_list output_list;

#define HYPER_DMABUF_PATH "/dev/hyper_dmabuf"
#define HYPER_DMABUF_PATH_LEGACY "/dev/xen/hyper_dmabuf"
#define HYPER_DMABUF_UNEXPORT_DELAY 250;

static int need_depth;
static int need_stencil;
static int vm_exec = 0;
static int vm_dbg = 0;
static int vm_unexport_delay = HYPER_DMABUF_UNEXPORT_DELAY;
static int vm_share_only = 1;
static char vm_plugin_path[256];
static char vm_plugin_args[256];

/*
 * Keyboard devices don't use a keymap and just deliver raw evdev keycodes
 * to clients (this is the expected case in real automotive settings where
 * the devices that show up as keyboards aren't typical 101-key KB's).
 *
 * TODO: Consider changing this default to 'true' in the future.
 */
static int use_xkbcommon;

/*
 * When normalized rotation is turned on, the compositor will send
 * swapped width and height in the output geometry event.
 */
static int normalized_rotation;

static int print_fps = 0;
static int use_nuclear_flip = 1;
static int no_flip_event = 0;
static int no_color_correction = 0;
static int use_rbc = 0;
static int rbc_debug = 0;
static int damage_outputs_on_init = 1;
static int use_cursor_as_uplane = 0;

TRACING_DECLARATIONS;

#define SYSFS_LOCATION "/sys/module/emgd"
#define SYSFS_BUF_SIZE 100
#define SYSFS_CRTCID_ADJUSTMENT 3


/* Config element handler functions */
void backend_begin(void *userdata, const char **attrs);
void crtc_begin(void *userdata, const char **attrs);
void output_begin(void *userdata, const char **attrs);
void input_begin(void *userdata, const char **attrs);
void env_begin(void *userdata, const char **attrs);
void capture_begin(void *userdata, const char **attrs);

/* Config element mapping for state machine */
static struct xml_element backend_parse_data[] = {
	{ NONE,			NULL,			NULL,			IASCONFIG,	NONE },
	{ IASCONFIG,	"iasconfig",	NULL,			BACKEND,	NONE },
	{ BACKEND,		"backend",		backend_begin,	STARTUP | GLOBAL_ENV | REM_DISP,	IASCONFIG },
	{ STARTUP,		"startup",		NULL,			CRTC,		BACKEND },
	{ CRTC,			"crtc",			crtc_begin,		OUTPUT,		STARTUP },
	{ OUTPUT,		"output",		output_begin,	INPUT,		CRTC },
	{ INPUT,		"input",		input_begin,	NONE,		OUTPUT },
	{ GLOBAL_ENV,	"env",			env_begin,		NONE,		BACKEND },
	{ REM_DISP,		"capture",		capture_begin,	NONE,		BACKEND },
};

struct ias_configured_input {
	char *devnode;
	struct wl_list link;
};

static const char default_seat[] = "seat0";

struct ias_connector {
	drmModeConnector *connector;
	int used;
};

/* Output model name table.  */
static struct ias_output_model *output_model_table[] = {
	&output_model_classic,
	&output_model_flexible,
};

static int output_model_table_size =
	sizeof(output_model_table) / sizeof(output_model_table[0]);

static drmModePropertyPtr
ias_get_prop(int fd, drmModeConnectorPtr connector, const char *name);

static int
get_sprite_list(struct weston_output *output,
		struct ias_sprite ***sprite_list);
static struct weston_plane *
assign_view_to_sprite(struct weston_view *view,
		/*
		struct ias_sprite *sprite,
		*/
		struct weston_output *output,
		int *sprite_id,
		int x,
		int y,
		int sprite_width,
		int sprite_height,
		pixman_region32_t *surface_region);

static int
assign_zorder_to_sprite(struct weston_output *output,
		int sprite_id,
		int position);


static uint32_t
is_surface_flippable_on_sprite(struct weston_view *view,
		struct weston_output *output);

static int
assign_blending_to_sprite(struct weston_output *output,
		int sprite_id,
		int src_factor,
		int dst_factor,
		float blend_color,
		int enable);

/* retrieve the number of GL textures associated with a view and their names.
 * names must point to an array at least big enough to hold num elements */
static void
get_tex_info(struct weston_view *view,
		int *num,
		GLuint *names);

/* retrieve the number of EGLImages associated with a view and their names.
 * names must point to an array at least big enough to hold num elements */
static void
get_egl_image_info(struct weston_view *view,
		int *num,
		EGLImageKHR *names);

/* have gl-renderer call glViewport */
static void
set_viewport(int x, int y, int width, int height);

void
ias_get_object_properties(int fd,
		struct ias_properties *drm_props,
		uint32_t obj_id, uint32_t obj_type,
		struct ias_sprite *sprite);

void
ias_get_crtc_object_properties(int fd,
		struct ias_crtc_properties *drm_props,
		uint32_t obj_id);

/* Set of display components required to build a new display */
struct connector_components {
	char *connector_name;
	struct ias_configured_crtc *conf_crtc;
	struct ias_connector *connector;
	struct wl_list link;
};

/* List containing a set of display components ready to build */
struct components_list {
	int count_connectors;
	int num_conf_components;
	struct ias_connector **connector;
	struct wl_list list;
};

/* Init components list */
static int
components_list_create(struct ias_backend *backend,
		drmModeRes *resources,
		struct components_list *components_list);

/* Free the memory used by components list */
static void
components_list_destroy(struct components_list *components_list);

/* Creates and adds display components, return successful of not */
static int
components_list_add(struct components_list *components_list,
		char *connector_name,
		struct ias_configured_crtc *conf_crtc,
		struct ias_connector *connector);

/* Construct the display components list, returns successful or not */
static int
components_list_build(struct ias_backend *backend,
		drmModeRes *resources,
		struct components_list *components_list);

/* Checks if there are any overlapping outputs */
static int
has_overlapping_outputs(struct ias_backend *backend);

static int
ias_crtc_find_by_name(struct ias_backend *backend, char *requested_name);

static void
centre_pointer(struct ias_backend *backend);

static struct ias_crtc *
create_single_crtc(struct ias_backend *backend,
		drmModeRes *resources,
		struct connector_components *components);

/* Update function implemented during hotplugging */
static void
ias_update_outputs(struct ias_backend *backend,
		struct udev_device *event);

static int emgd_has_multiplane_drm(struct ias_backend *backend)
{
		struct drm_i915_getparam param;
		int overlay = 0;
		int ret;

		/*
		 * Value 30 is a private definition in EMGD that allows
		 * checking whether multiplane drm is available or not
		 */
		param.param = I915_PARAM_HAS_MULTIPLANE_DRM;
		param.value = &overlay;

		ret = drmCommandWriteRead(backend->drm.fd, DRM_I915_GETPARAM, &param,
		                sizeof(param));
		if (ret) {
		        weston_log("DRM reports no overlay support!\n");
		        return -1;
		}

		return overlay;
}

void
ias_get_object_properties(int fd,
		struct ias_properties *drm_props,
		uint32_t obj_id, uint32_t obj_type,
		struct ias_sprite *sprite)
{
	drmModeObjectPropertiesPtr props;
	drmModePropertyPtr prop;
	unsigned int i, j;
	uint32_t *p;

#define F(field) offsetof(struct ias_properties, field)
	const struct {
		char *name;
		int offset;
	} prop_map[] = {
		{ "type", F(type) },
		{ "SRC_X", F(src_x) },
		{ "SRC_Y", F(src_y) },
		{ "SRC_W", F(src_w) },
		{ "SRC_H", F(src_h) },

		{ "CRTC_X", F(crtc_x) },
		{ "CRTC_Y", F(crtc_y) },
		{ "CRTC_W", F(crtc_w) },
		{ "CRTC_H", F(crtc_h) },

		{ "FB_ID", F(fb_id) },
		{ "CRTC_ID", F(crtc_id) },
		{ "rotation", F(rotation) },

		{ "alpha", F(alpha) },
		{ "pixel blend mode", F(pixel_blend_mode) },
	};
#undef F

	memset(drm_props, 0, sizeof *drm_props);

	props = drmModeObjectGetProperties(fd, obj_id, obj_type);
	if (!props) {
		return;
	}

	weston_log("drm object %u (type 0x%x) properties: ", obj_id, obj_type);

	for (i = 0; i < props->count_props; i++) {
		prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop) {
			continue;
		}

		for (j = 0; j < ARRAY_LENGTH(prop_map); j++) {
			if (strcmp(prop->name, prop_map[j].name) != 0) {
				continue;
			}

			p = (uint32_t *) ((char *) drm_props + prop_map[j].offset);
			*p = prop->prop_id;

			if (!strcmp(prop->name, "type")) {
				sprite->type = props->prop_values[i];
			}

			if (!strcmp(prop->name, "rotation")) {
				sprite->rotation = props->prop_values[i];
			}

			weston_log_continue("%s (%u), ", prop->name, prop->prop_id);
			break;
		}
	}

	weston_log_continue("\n");
}

void
ias_get_crtc_object_properties(int fd,
		struct ias_crtc_properties *drm_props,
		uint32_t obj_id)
{
	drmModeObjectPropertiesPtr props;
	drmModePropertyPtr prop;
	unsigned int i, j;
	uint32_t *p;

#define F(field) offsetof(struct ias_crtc_properties, field)
	const struct {
		char *name;
		int offset;
	} prop_map[] = {
		{ "GAMMA_LUT", F(gamma_lut) },
		{ "MODE_ID", F(mode_id) },
		{ "ACTIVE", F(active) },
	};
#undef F

	memset(drm_props, 0, sizeof *drm_props);

	props = drmModeObjectGetProperties(fd, obj_id, DRM_MODE_OBJECT_CRTC);
	if (!props) {
		return;
	}

	weston_log("drm crtc object %u (type 0x%x) properties: ", obj_id, DRM_MODE_OBJECT_CRTC);

	for (i = 0; i < props->count_props; i++) {
		prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop) {
			continue;
		}

		for (j = 0; j < ARRAY_LENGTH(prop_map); j++) {
			if (strcmp(prop->name, prop_map[j].name) != 0) {
				continue;
			}

			p = (uint32_t *) ((char *) drm_props + prop_map[j].offset);
			*p = prop->prop_id;

			weston_log_continue("%s (%u), ", prop->name, prop->prop_id);
			break;
		}
	}

	weston_log_continue("\n");
}

static uint32_t
is_rbc_resolve_possible_on_sprite(uint32_t rotation, uint32_t format);

/* Update output's coordinates
 *
 * Function check if output's coordinates related with current CRTC needs to be
 * adjusted.
 * The Coordinate adjustment occurs only for extended mode.
 */
static void
ias_update_outputs_coordinate(struct ias_crtc *ias_crtc, struct ias_output *output)
{
	struct ias_configured_output *cfg = NULL;
	struct ias_crtc *other_crtc = NULL;
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_output *relative_output = NULL;
	int x = 0, y = 0, i = 0;

	wl_list_for_each(other_crtc, &backend->crtc_list, link) {
		if (ias_crtc == other_crtc)
			continue;

		for(i = 0; i < other_crtc->output_model->outputs_per_crtc; ++i) {
			cfg = other_crtc->configuration->output[i];

			if (cfg->position_target && !strcmp(cfg->position_target,
				output->name)) {
				relative_output = other_crtc->output[i];
				struct weston_output weston_output = output->base;
				struct weston_output *rel_base = &relative_output->base;

				x = rel_base->x;
				y = rel_base->y;
				if (cfg->position == OUTPUT_POSITION_RIGHTOF) {
					x = weston_output.x + weston_output.width;
					y = weston_output.y;
				} else if (cfg->position == OUTPUT_POSITION_BELOW) {
						x = weston_output.x;
						y = weston_output.y + weston_output.height;
				}

				if (x == rel_base->x && y == rel_base->y)
					continue;

				rel_base->x = x;
				rel_base->y = y;

				/* Do scaling and adjusting of the coordinates */
				ias_output_scale(relative_output,
				rel_base->current_mode->width,
				rel_base->current_mode->height);

				/* Update any output relative to the newly moved output */
				ias_update_outputs_coordinate(other_crtc, relative_output);
			}
		}
	}
}

/*
 * ias CRTC set mode
 *
 * Change the mode on the CRTC and update the scanout buffers as
 * appropriate.
 *
 * HDMI Stereo dual-view mode uses two scanout buffers, one for each "eye".
 * The current assumption is that these will be attached to sprite planes.
 * But what get's attached to the main display plane?  The main display
 * plane still needs a scanout buffer even if we're not going to render
 * anything to it.
 */
static void
ias_crtc_set_mode(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t mode_id)
{
	struct ias_crtc *ias_crtc = wl_resource_get_user_data(resource);
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_mode *m, *old_mode;

	/* lookup the mode in the list */
	wl_list_for_each(m, &ias_crtc->mode_list, link) {
		if (m->id == mode_id) {
			weston_log("Found mode to set: %dx%d @ %.1f\n",
					m->base.width, m->base.height, m->base.refresh/1000.0);


			/* Set the mode, m, on the CRTC */

			/*
			 * If the new mode has the same width/height as the current mode
			 * then we don't need to do much other than call drmModeSetCrtc()
			 * However, it is isn't the same, we'll need to allocate new
			 * buffer objects.
			 */

			/* What is the current mode on the crtc? */
			if (m == ias_crtc->current_mode) {
				return;
			} else if ((m->base.width == ias_crtc->current_mode->base.width) &&
					(m->base.height == ias_crtc->current_mode->base.height)) {
				old_mode = ias_crtc->current_mode;
				ias_crtc->current_mode = m;
				if (ias_crtc->output_model->set_mode(ias_crtc) == 0){
					ias_crtc->current_mode->base.flags &=
						~WL_OUTPUT_MODE_CURRENT;
					ias_crtc->current_mode->base.flags |=
						WL_OUTPUT_MODE_CURRENT;
				} else {
					/* restore old mode in ias_crtc */
					ias_crtc->current_mode = old_mode;
				}
				return;
			}

			if (ias_crtc->output_model->allocate_scanout(ias_crtc, m) == -1) {
				return;
			}

			ias_crtc->current_mode->base.flags &= ~WL_OUTPUT_MODE_CURRENT;
			ias_crtc->current_mode = m;
			ias_crtc->current_mode->base.flags |= WL_OUTPUT_MODE_CURRENT;
			ias_crtc->request_set_mode = 1;

			weston_compositor_damage_all(backend->compositor);

			if (ias_crtc->output_model->switch_mode) {
				ias_crtc->output_model->switch_mode(ias_crtc, m);
			}

			ias_update_outputs_coordinate(ias_crtc, ias_crtc->output[0]);

			return;  /* Sucess */
		}
	}

	IAS_ERROR("Failed to find matching mode for ID %d\n", mode_id);
	return;
}

static int32_t
ias_get_object_prop(int fd, uint32_t id, uint32_t object_type,
		const char *name, uint32_t *prop_id, uint64_t *prop_value)
{
	int32_t ret = -1;
	uint32_t i;
	drmModePropertyPtr prop;
	drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, id, object_type);
	for (i = 0; i < props->count_props; i++) {
		prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop) {
			continue;
		}
		if (!strcmp(prop->name, name)) {
			if (prop_id) {
				*prop_id = props->props[i];
			}
			if (prop_value) {
				*prop_value = props->prop_values[i];
			}
			ret = 1;
			drmModeFreeProperty(prop);
			break;
		}

		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);
	return ret;
}

static void
ias_set_crtc_lut(int fd, uint32_t crtc_id, const char *lut_name,
                 struct drm_color_lut* lut, uint32_t lut_size)
{
	uint32_t blob_id;
	uint32_t lut_id;
	int32_t ret;
	ret = drmModeCreatePropertyBlob(fd, lut, sizeof(struct drm_color_lut)* lut_size, &blob_id);
	if (ret < 0) {
		IAS_ERROR("Cannot create blob property");
		return;
	}

	ret = ias_get_object_prop(fd, crtc_id, DRM_MODE_OBJECT_CRTC, lut_name, &lut_id, NULL);

	if (ret < 0) {
		IAS_ERROR("Cannot find %s property of CRTC %d\n", lut_name, crtc_id);
	} else {
		drmModeObjectSetProperty(fd, crtc_id, DRM_MODE_OBJECT_CRTC, lut_id, blob_id);
	}
	drmModeDestroyPropertyBlob(fd, blob_id);
}

static float
transform_contrast_brightness(float value, float brightness, float contrast)
{
	float result;
	result = (value - 0.5) * contrast + 0.5 + brightness;

	if (result < 0.0) result = 0.0;
	if (result > 1.0) result = 1.0;
	return result;
}

static float
transform_gamma(float value, float gamma)
{
	float result;

	result = pow(value, 1.0 - gamma);
	if (result < 0.0) result = 0.0;
	if (result > 1.0) result = 1.0;

	return result;
}

/*
 * update_color_correction()
 *
 * Color correction settings are controlled via DRM properties.
 * Each CRTC exposes two properties representing lookup table
 * that should be use for color correction (one for gamma and one for degamma .
 * We need to build correct tables for gamma, brightness and
 * contrast corrections.
 * If color correction needs to be updated atomically, color correction table is
 * built and then wrapped around DRM blob and saved to be used during atomic
 * pageflip by backend.
 */
static void
update_color_correction(struct ias_crtc *ias_crtc, uint32_t atomic)
{
	struct ias_backend *backend = ias_crtc->backend;
	uint64_t lut_size;
	struct drm_color_lut *lut;
	float brightness[3];
	float contrast[3];
	float gamma[3];
	uint8_t temp[3];
	int32_t ret;
	uint32_t i;

	ret = ias_get_object_prop(backend->drm.fd, ias_crtc->crtc_id, DRM_MODE_OBJECT_CRTC,
			"GAMMA_LUT_SIZE", NULL, &lut_size);

	if (ret < 0) {
		IAS_ERROR("Cannot query LUT size");
		return;
	}

	lut = malloc(lut_size* sizeof(struct drm_color_lut));
	if (!lut) {
		IAS_ERROR("Cannot allocate LUT memory");
		return;
	}

	/* Unpack brightness values for each channel */
	temp[0] = (ias_crtc->brightness >> 16) & 0xFF;
	temp[1] = (ias_crtc->brightness >> 8) & 0xFF;
	temp[2] = (ias_crtc->brightness) & 0xFF;

	/* Map brightness from -128 - 127 range into -0.5 - 0.5 range */
	brightness[0] = (float)(temp[0] - 128)/255;
	brightness[1] = (float)(temp[1] - 128)/255;
	brightness[2] = (float)(temp[2] - 128)/255;

	/* Unpack contrast values for each channel */
	temp[0] = (ias_crtc->contrast >> 16) & 0xFF;
	temp[1] = (ias_crtc->contrast >> 8) & 0xFF;
	temp[2] = (ias_crtc->contrast) & 0xFF;

	/* Map contrast from 0 - 255 range into 0.0 - 2.0 range */
	contrast[0] = (float)(temp[0])/128;
	contrast[1] = (float)(temp[1])/128;
	contrast[2] = (float)(temp[2])/128;

	/* Unpack gamma values for each channel */
	temp[0] = (ias_crtc->gamma >> 16) & 0xFF;
	temp[1] = (ias_crtc->gamma >> 8) & 0xFF;
	temp[2] = (ias_crtc->gamma) & 0xFF;

	/* Map gamma from 0 - 255 range into -0.5 - 0.5 */
	gamma[0] = (float)(temp[0] - 128)/255;
	gamma[1] = (float)(temp[1] - 128)/255;
	gamma[2] = (float)(temp[2] - 128)/255;

	for (i = 0; i < lut_size; i++) {
		lut[i].red =
			(lut_size - 1) * transform_gamma(transform_contrast_brightness((float)(i)/(lut_size-1),
						brightness[0], contrast[0]), gamma[0]);
		lut[i].green =
			(lut_size - 1) * transform_gamma(transform_contrast_brightness((float)(i)/(lut_size-1),
						brightness[1], contrast[1]), gamma[1]);
		lut[i].blue =
			(lut_size - 1) * transform_gamma(transform_contrast_brightness((float)(i)/(lut_size-1),
						brightness[2],  contrast[2]), gamma[2]);

		/*
		 * When calculating LUT values, integer values from 0 - (lut_size-1) range
		 * are mapped into 0.0 - 1.0 float range and after that converted back to 0 - (lut_size -1) range.
		 * DRM expects LUT values in 0 - 65535 range, so here expand it accordingly.
		 * When that setp is combined with previous one it may produce slighty different values than
		 * expected due to working on float.
		 */
		lut[i].red *= (0xFFFF +1)/lut_size;
		lut[i].green *= (0xFFFF +1)/lut_size;
		lut[i].blue *= (0xFFFF +1)/lut_size;
	}

	if (atomic) {
		/*
		 * For atomic update of color correction, just prepare LUT table and create DRM
		 * blob id out of it, then just pass that id to backend to be used during atomic pageflip.
		 * After flip is done, backend should destroy that blob, but in case that it was not done,
		 * free it here to prevent memory leaks.
		 */
		if (ias_crtc->color_correction_blob_id) {
			drmModeDestroyPropertyBlob(backend->drm.fd, ias_crtc->color_correction_blob_id);
			ias_crtc->color_correction_blob_id = 0;
		}

		ret = drmModeCreatePropertyBlob(backend->drm.fd, lut, sizeof(struct drm_color_lut)* lut_size,
						&ias_crtc->color_correction_blob_id);

		if (ret < 0) {
			IAS_ERROR("Cannot create blob property");
			free(lut);
			return;
		}
	} else {
		ias_set_crtc_lut(backend->drm.fd, ias_crtc->crtc_id, "GAMMA_LUT", lut, lut_size);
	}

	free(lut);
}

static void
ias_crtc_set_gamma(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t red, uint32_t green, uint32_t blue)
{
	struct ias_crtc *ias_crtc = wl_resource_get_user_data(resource);

	red &= 0xFF;
	green &= 0xFF;
	blue &= 0xFF;

	ias_crtc->gamma = (red << 16) | (green << 8) | (blue);

	update_color_correction(ias_crtc, 0);
	ias_crtc_send_gamma(resource, red, green, blue);
}


static void
ias_crtc_set_contrast(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t red, uint32_t green, uint32_t blue)
{
	struct ias_crtc *ias_crtc = wl_resource_get_user_data(resource);

	red &= 0xFF;
	green &= 0xFF;
	blue &= 0xFF;

	ias_crtc->contrast = (red << 16) | (green << 8) | (blue);

	update_color_correction(ias_crtc, 0);
	ias_crtc_send_contrast(resource, red, green, blue);
}

static void
ias_crtc_set_brightness(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t red, uint32_t green, uint32_t blue)
{
	struct ias_crtc *ias_crtc = wl_resource_get_user_data(resource);

	red &= 0xFF;
	green &= 0xFF;
	blue &= 0xFF;

	ias_crtc->brightness = (red << 16) | (green << 8) | (blue);

	update_color_correction(ias_crtc, 0);
	ias_crtc_send_brightness(resource, red, green, blue);
}

static int
cp_handler(void *data)
{
	struct wl_resource *resource = data;
	struct ias_crtc *ias_crtc = wl_resource_get_user_data(resource);
	int32_t ret;
	struct ias_backend *backend;
	uint32_t cp_prop_id;
	uint64_t cp_val;

	backend = ias_crtc->backend;

	ret = ias_get_object_prop(backend->drm.fd, ias_crtc->connector_id,
			DRM_MODE_OBJECT_CONNECTOR, "Content Protection",
			&cp_prop_id, &cp_val);

	if (ret < 0) {
		IAS_ERROR("Cannot query content protection");
		return 0;
	}

	/*
	 * We have set a limit of 10 for cp_handler to be called. Either
	 * CP has been enabled by the driver by then or there is some problem
	 * and we should unnecessarily wait for the driver to turn on CP so we
	 * will turn this timer off.
	 */
	if(++ias_crtc->cp_timer_index < 10 && !cp_val) {
		wl_event_source_timer_update(ias_crtc->cp_timer, 16);
	} else {
		if(cp_val) {
			ias_crtc_send_content_protection_enabled(resource);
		}
		wl_event_source_remove(ias_crtc->cp_timer);
	}

	return 1;
}

static void
ias_output_set_fb_transparency(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t enabled)
{
	struct ias_output *ias_output = wl_resource_get_user_data(resource);

	ias_output->transparency_enabled = enabled;

	return;
}

static void
ias_crtc_set_content_protection(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t enabled)
{
	struct ias_crtc *ias_crtc = wl_resource_get_user_data(resource);
	int32_t ret;
	struct ias_backend *backend;
	uint32_t cp_prop_id;
	uint64_t cp_val;
	struct wl_event_loop *loop;

	if(!ias_crtc) {
		IAS_ERROR("Invalid crtc provided");
		return;
	}

	/* Make this value a 1 or a 0 */
	enabled = !!enabled;

	backend = ias_crtc->backend;

	ret = ias_get_object_prop(backend->drm.fd, ias_crtc->connector_id,
			DRM_MODE_OBJECT_CONNECTOR, "Content Protection",
			&cp_prop_id, &cp_val);

	if (ret < 0) {
		IAS_ERROR("Cannot query content protection");
		return;
	}

	/*
	 * If the client is asking to enable/disable CP, but the driver already has
	 * that value set, then reject this request. Also, make sure that we can
	 * use nuclear pageflipping.
	 */
	if((uint32_t) cp_val != enabled &&
			backend->has_nuclear_pageflip) {

		cp_val = (uint64_t) enabled;
		ret = drmModeObjectSetProperty(backend->drm.fd, ias_crtc->connector_id,
				DRM_MODE_OBJECT_CONNECTOR, cp_prop_id, cp_val);
		if (ret < 0) {
			IAS_ERROR("Cannot set content protection property");
			return;
		}

		/*
		 * According to the spec, userspace can only request to turn on CP by setting the flag
		 * DRM_MODE_CONTENT_PROTECTION_DESIRED, but it is up to driver to decide if it really
		 * will be enabled and change that property to DRM_MODE_CONTENT_PROTECTION_ENABLED.
		 * We must check to see if the driver changed this DESIRED to ENABLED and then report
		 * back to our client that CP indeed got turned on.
		 * Here we start polling and this timer function will be called every 16 ms.
		 */
		if(enabled) {

			/*
			 * Check once to see if the driver already turned on CP. If it did, perfect! We
			 * will inform the client about it. If not, then we have to poll
			 */
			ret = ias_get_object_prop(backend->drm.fd, ias_crtc->connector_id,
					DRM_MODE_OBJECT_CONNECTOR, "Content Protection",
					&cp_prop_id, &cp_val);

			if (ret < 0) {
				IAS_ERROR("Cannot query content protection");
				return;
			}

			if(cp_val) {
				ias_crtc_send_content_protection_enabled(resource);
			} else {
				loop = wl_display_get_event_loop(backend->compositor->wl_display);
				ias_crtc->cp_timer_index = 0;
				ias_crtc->cp_timer = wl_event_loop_add_timer(loop, cp_handler, resource);
				wl_event_source_timer_update(ias_crtc->cp_timer, 16);
			}
		}
	}
}

/*
 * After moving an output, check to see if any pointers also need
 * to be moved.
 *
 * If they do, move them just enough to get them back on the screen.
 */
static void
ias_move_pointer(struct weston_compositor *compositor,
		struct ias_output *ias_output, int x, int y, int w, int h)
{
	struct ias_backend *backend = (struct ias_backend *)
		ias_output->base.compositor->backend;
	struct weston_seat *seat;
	struct weston_pointer *pointer;
	struct ias_output *output;
	struct ias_crtc *crtc;
	int i, px, py;
	int valid;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		/* Make sure that this seat is for this output only */
		if (!(seat->output_mask & (1 << ias_output->base.id))) {
			continue;
		}

		/* Make sure this seat actually has a pointer device */
		if (!seat->pointer_device_count) {
			continue;
		}

		pointer = weston_seat_get_pointer(seat);
		if (!pointer) {
			continue;
		}

		valid = 0;
		px = wl_fixed_to_int(pointer->x);
		py = wl_fixed_to_int(pointer->y);

		wl_list_for_each(crtc, &backend->crtc_list, link) {
			for (i = 0; i < crtc->num_outputs; i++) {
				output = crtc->output[i];
				if (pixman_region32_contains_point(&output->base.region,
							px, py, NULL)) {
					valid = 1;
				}
			}
		}

		if (!valid) {
			/*
			 * The pointer is off screen. Ideally we want to move
			 * it just enough to bring it back on screen.
			 */
			if (px < x) {
				px = x;
			} else if (px > x + w) {
				px = x + w - 1;
			}

			if (py < y) {
				py = y;
			} else if (py > y + h) {
				py = y + h - 1;
			}

			pointer->x = wl_fixed_from_int(px);
			pointer->y = wl_fixed_from_int(py);
			if (pointer->sprite) {
				weston_view_set_position(pointer->sprite,
						px - pointer->hotspot_x,
						px - pointer->hotspot_y);
				weston_compositor_schedule_repaint(backend->compositor);
			}
		}
	}
}


struct ias_crtc_interface ias_crtc_implementation = {
	ias_crtc_set_mode,
	ias_crtc_set_gamma,
	ias_crtc_set_contrast,
	ias_crtc_set_brightness,
	ias_crtc_set_content_protection,
};

static int
ias_add_mode(struct ias_crtc *crtc,
		drmModeModeInfo *info,
		uint32_t mode_number)
{
	struct ias_mode *mode;
	uint64_t refresh;

	mode = malloc(sizeof *mode);
	if (mode == NULL)
		return -1;

	mode->base.flags = 0;
	mode->base.width = info->hdisplay;
	mode->base.height = info->vdisplay;

	/* Calculate higher precision (mHz) refresh rate */
	refresh = (info->clock * 1000000LL / info->htotal +
			info->vtotal / 2) / info->vtotal;

	if (info->flags & DRM_MODE_FLAG_INTERLACE)
		refresh *= 2;
	if (info->flags & DRM_MODE_FLAG_DBLSCAN)
		refresh /= 2;
	if (info->vscan > 1)
		refresh /= info->vscan;

	mode->base.refresh = refresh;
	memcpy(&mode->mode_info, info, sizeof *info);
	mode->id = mode_number;

	if (info->type & DRM_MODE_TYPE_PREFERRED)
		mode->base.flags |= WL_OUTPUT_MODE_PREFERRED;

	wl_list_insert(crtc->mode_list.prev, &mode->link);

	return 0;
}

static void
bind_ias_crtc(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	struct ias_crtc *ias_crtc = data;
	struct ias_mode *m;
	int i;

	resource = wl_resource_create(client,
			&ias_crtc_interface, 1, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &ias_crtc_implementation,
			data, NULL);

	/* Loop through the mode list and send each mode to the client */
	i = 1;
	wl_list_for_each(m, &ias_crtc->mode_list, link) {
#if 0
		weston_log_continue("  mode %dx%d @ %.1f%s%s\n",
				m->base.width, m->base.height, m->base.refresh / 1000.0,
				m->base.flags & WL_OUTPUT_MODE_PREFERRED ?
				", preferred" : "",
				m->base.flags & WL_OUTPUT_MODE_CURRENT ?
				", current" : "");
#endif

		/* Send actual refresh or refresh * 1000 for more precision? */
		ias_crtc_send_mode(resource, m->id, m->base.width, m->base.height,
				(uint32_t)(m->base.refresh / 1000));
		i++;
	}

	ias_crtc_send_gamma(resource,
			    (ias_crtc->gamma >> 16) & 0xFF,
			    (ias_crtc->gamma >> 8) & 0xFF,
			    ias_crtc->gamma & 0xFF);

	ias_crtc_send_contrast(resource,
			       (ias_crtc->contrast >> 16) & 0xFF,
			       (ias_crtc->contrast >> 8) & 0xFF,
			       ias_crtc->contrast & 0xFF);

	ias_crtc_send_brightness(resource,
			         (ias_crtc->brightness >> 16) & 0xFF,
			         (ias_crtc->brightness >> 8) & 0xFF,
			         ias_crtc->brightness & 0xFF);

	ias_crtc_send_id(resource, ias_crtc->connector_id);
}

static struct
ias_crtc* ias_crtc_create(struct ias_backend *backend)
{
	struct ias_crtc *ias_crtc = calloc(1, sizeof *ias_crtc);
	if (!ias_crtc) {
		IAS_ERROR("Failed to allocate CRTC: out of memory");
		exit(1);
	}

	ias_crtc->backend = backend;
	ias_crtc->global = wl_global_create(backend->compositor->wl_display,
			&ias_crtc_interface, 1, ias_crtc, bind_ias_crtc);

	wl_list_init(&ias_crtc->mode_list);

	return ias_crtc;
}

static void
ias_crtc_destroy(struct ias_crtc *ias_crtc)
{
	drmModeCrtcPtr origcrtc = ias_crtc->original_crtc;

	if (ias_crtc->prop_set) {
		drmModeAtomicFree(ias_crtc->prop_set);
		ias_crtc->prop_set = NULL;
	}

	/* Restore original CRTC state */
	if (origcrtc) {
		drmModeSetCrtc(ias_crtc->backend->drm.fd,
				origcrtc->crtc_id, origcrtc->buffer_id,
				origcrtc->x, origcrtc->y,
				&ias_crtc->connector_id, 1, &origcrtc->mode);
		drmModeFreeCrtc(origcrtc);
	}


	wl_global_destroy(ias_crtc->global);
	free(ias_crtc);
}

static void
ias_output_set_xy(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t x, uint32_t y)
{
	struct ias_output *ias_output = wl_resource_get_user_data(resource);
	struct weston_compositor *compositor = ias_output->base.compositor;

	ias_output->base.dirty = 1;
	weston_output_damage(&ias_output->base);
	weston_output_move(&ias_output->base, x, y);
	ias_move_pointer(compositor, ias_output, x, y,
			ias_output->base.current_mode->width,
			ias_output->base.current_mode->height);

	wl_signal_emit(&ias_output->update_signal, ias_output);

	weston_compositor_damage_all(compositor);
}


static void
ias_output_disable(struct wl_client *client,
		struct wl_resource *resource)
{
	struct ias_output *ias_output = wl_resource_get_user_data(resource);

	ias_output->ias_crtc->output_model->disable_output(ias_output);

}

static void
ias_output_enable(struct wl_client *client,
		struct wl_resource *resource)
{
	struct ias_output *ias_output = wl_resource_get_user_data(resource);

	ias_output->ias_crtc->output_model->enable_output(ias_output);
}




/*
 * Scale the output.
 *
 * This allows us to have an output that has different dimensions from
 * the actual display resolution.
 *
 * Note that this is modifying the output transforms based on IAS API's.
 * The Weston core isn't aware of our modifications and will overwrite
 * them if the weston_output is ever marked 'dirty'.
 */
void
ias_output_scale(struct ias_output *ias_output,
		uint32_t width,
		uint32_t height)
{
	struct weston_output *output;
	struct weston_compositor *compositor = ias_output->base.compositor;

	output = &ias_output->base;
	/* If current output size is the same as requested one, do nothing */
	if ((uint32_t)output->width == width && (uint32_t)output->height == height)
		return;

	output->dirty = 1;

	/* Set the output to the new (scaled ) width / height */
	output->width = width;
	output->height = height;

	/*
	 * If this output is rotated by 90 or 270, then we need to swap
	 * the output's width and height similar to how
	 * weston_output_transform_init() does it.
	 */
	switch (output->transform) {
		case WL_OUTPUT_TRANSFORM_90:
		case WL_OUTPUT_TRANSFORM_270:
		case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		case WL_OUTPUT_TRANSFORM_FLIPPED_270:
			output->width = height;
			output->height = width;

			/*
			 * If normalized_rotation is turned on, then we update mm_width
			 * and mm_height so that all the clients that bind to this output
			 * will receive the swapped width and height.
			 */
			if (compositor->normalized_rotation) {
				output->mm_width = height;
				output->mm_height = width;
			}

			break;
	}

	/*
	 * "Move" the output to its current position; this will help update
	 * some internal structures like the output region.
	 */
	weston_output_move(&ias_output->base, output->x, output->y);

	weston_output_damage(&ias_output->base);

	/*
	 * The previous frame's damage is also completely invalid now that we've
	 * resized.  Re-initialize it to the entire (new) output size.
	 */
	pixman_region32_fini(&ias_output->prev_damage);
	pixman_region32_init(&ias_output->prev_damage);
	pixman_region32_copy(&ias_output->prev_damage, &output->region);

	ias_move_pointer(compositor, ias_output, output->x, output->y,
			ias_output->base.current_mode->width,
			ias_output->base.current_mode->height);

	ias_output->is_resized = 1;
	wl_signal_emit(&ias_output->update_signal, ias_output);

	weston_compositor_damage_all(compositor);
}

static void
ias_output_scale_to(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t width,
		uint32_t height)
{
	struct ias_output *ias_output = wl_resource_get_user_data(resource);

	ias_output_scale(ias_output, width, height);
}


struct ias_output_interface ias_output_implementation = {
	ias_output_set_xy,
	ias_output_disable,
	ias_output_enable,
	ias_output_scale_to,
	ias_output_set_fb_transparency,
};

static void
bind_ias_output(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;
	struct ias_output *ias_output = data;

	resource = wl_resource_create(client, &ias_output_interface, 1, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &ias_output_implementation,
			data, NULL);

	ias_output_send_name(resource, ias_output->name);
}

static struct
ias_output* ias_output_create(struct ias_backend *backend)
{
	struct ias_output *ias_output = calloc(1, sizeof *ias_output);
	if (!ias_output) {
		IAS_ERROR("Failed to allocate output: out of memory");
		exit(1);
	}

	ias_output->global = wl_global_create(backend->compositor->wl_display,
			&ias_output_interface, 1, ias_output, bind_ias_output);

	ias_output->plugin_redraw_always = 1;
	return ias_output;
}

void
ias_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	struct ias_fb *fb = data;
	struct gbm_device *gbm = gbm_bo_get_device(bo);

	if (fb->fb_id)
		drmModeRmFB(gbm_device_get_fd(gbm), fb->fb_id);

	weston_buffer_reference(&fb->buffer_ref, NULL);

	free(data);
}

#define DRM_MODE_FB_AUX_PLANE   (1<<2)

#ifndef EGL_STRIDE
#define EGL_STRIDE 0x3060
#endif

#ifndef EGL_OFFSET
#define EGL_OFFSET 0x3061
#endif

struct ias_fb *
ias_fb_get_from_bo(struct gbm_bo *bo, struct weston_buffer *buffer,
		   struct ias_output *output, enum ias_fb_type fb_type)
{
	struct ias_fb *fb = gbm_bo_get_user_data(bo);
	struct ias_backend *backend =
		(struct ias_backend *) output->base.compositor->backend;
	uint32_t width, height;
	uint32_t format, strides[4] = {0}, handles[4] = {0}, offsets[4] = {0};
	uint64_t modifiers[4] = {0};
	uint32_t stride, handle;
	int ret;
	int flags = 0;
	struct ias_crtc *ias_crtc = output->ias_crtc;
	struct linux_dmabuf_buffer *dmabuf = NULL;
	int i;

	if (fb) {
		if (fb->fb_id)
			drmModeRmFB(backend->drm.fd, fb->fb_id);
		/* TODO: need to find better way instead of creating fb again */
		free(fb);
	}

	fb = malloc(sizeof *fb);
	if (!fb) {
		IAS_ERROR("Failed to allocate fb: out of memory");
		exit(1);
	}

	fb->bo = bo;
	fb->output = output;
	fb->is_client_buffer = 0;
	fb->buffer_ref.buffer = NULL;
	fb->is_compressed = 0;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	format = gbm_bo_get_format(bo);

	if ((!ias_crtc->sprites_are_broken && fb_type != IAS_FB_CURSOR) ||
	    (!ias_crtc->cursors_are_broken && fb_type == IAS_FB_CURSOR)) {

		/* if transparency is not enabled disable alpha channel only
		 * when scanout == true, otherwise leave format unchanged
		 */
		if (fb_type == IAS_FB_SCANOUT && !output->transparency_enabled &&
		    format == GBM_FORMAT_ARGB8888) {
			format = GBM_FORMAT_XRGB8888;
		}

		if (bo == ias_crtc->cursor_bo[0] || bo == ias_crtc->cursor_bo[1]) {
			format = GBM_FORMAT_ARGB8888;
		}

		if (buffer)
			dmabuf = linux_dmabuf_buffer_get(buffer->resource);

		if (dmabuf) {
			for (i = 0; i < dmabuf->attributes.n_planes; i++) {
				handles[i] = gbm_bo_get_handle(bo).u32;
				strides[i] = dmabuf->attributes.stride[i];
				offsets[i] = dmabuf->attributes.offset[i];
				modifiers[i] = dmabuf->attributes.modifier[i];
				if (modifiers[i] > 0) {
					flags |= DRM_MODE_FB_MODIFIERS;
					if (modifiers[i] == I915_FORMAT_MOD_Y_TILED_CCS	||
					    modifiers[i] == I915_FORMAT_MOD_Yf_TILED_CCS)
						fb->is_compressed = 1;
				}
			}
		} else {
			if (format == GBM_FORMAT_NV12) {
				if (buffer != NULL) {
					gl_renderer->query_buffer(backend->compositor,
						buffer->legacy_buffer, EGL_STRIDE, (EGLint *)strides);
					gl_renderer->query_buffer(backend->compositor,
						buffer->legacy_buffer, EGL_OFFSET, (EGLint *)offsets);
				}
				handles[0] = gbm_bo_get_handle(bo).u32;
				handles[1] = gbm_bo_get_handle(bo).u32;
			} else if (format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888) {
				strides[0] = gbm_bo_get_stride(bo);
				handles[0] = gbm_bo_get_handle(bo).u32;
				offsets[0] = 0;
			}
		}

		ret = drmModeAddFB2WithModifiers(backend->drm.fd, width, height,
						 format, handles, strides, offsets,
						 modifiers, &fb->fb_id, flags);

		if (ret) {
			ias_crtc->sprites_are_broken = 1;
			weston_log("failed to create kms fb: %m, trying support for "
					"older kernels.\n");
		}
	}


	if ((ias_crtc->sprites_are_broken && fb_type != IAS_FB_CURSOR) ||
	    (ias_crtc->cursors_are_broken && fb_type == IAS_FB_CURSOR)) {
		stride = gbm_bo_get_stride(bo);
		handle = gbm_bo_get_handle(bo).u32;

		ret = drmModeAddFB(backend->drm.fd, width, height, 24, 32,
				stride, handle, &fb->fb_id);
		if (ret) {
			/* Try depth = 32 FB */
			ret = drmModeAddFB(backend->drm.fd, width, height, 32, 32,
					stride, handle, &fb->fb_id);
			if (ret) {
				weston_log("failed to create kms fb: %m\n");
				free(fb);
				return NULL;
			}
		}
	}

	fb->format = format;

	gbm_bo_set_user_data(bo, fb, ias_fb_destroy_callback);

	return fb;
}


/*
 * ias_attempt_scanout_for_view()
 *
 * Attempts to use a client buffer as the scanout for an output.  This
 * function is only used when running in non-dualview/non-stereo mode.
 *
 * This function tries to use a surface (weston_surface *es) directly
 * as a scanout buffer (gbm_bo *bo).  This is an optimization to try and
 * get the display to directly flip to this buffer, rather than have to
 * composite this surface with other surfaces. This will only work if the
 * geometry matches exactly, so the surface takes up the full screen.  Also
 * the surface must be fully opaque, so you don't have to blend it with
 * surfaces that may be below this surface. There are some additional cases
 * where this will not work, and some that might get added over time as they
 * are discovered. Some of these additional cases are:
 * Transformed surfaces, surfaces with non-standard shaders, surfaces with SHM
 * buffers.
 * Where this optimization will not work, we do not bother creating the gbm_bo
 * scanout buffer (or discarding the one we started creating), since we'll have
 * to do some compositing to another gbm_bo scanout buffer later.
 *
 * This function returns the weston_plane for the scanout if the
 * surface is suitable.  Otherwise it returns NULL.
 *
 */
static struct weston_plane *
ias_attempt_scanout_for_view(struct weston_output *_output,
		struct weston_view *ev, uint32_t check_xy)
{
	struct ias_output *output = (struct ias_output *) _output;
	struct ias_backend *c =
		(struct ias_backend *) output->base.compositor->backend;
	struct gbm_bo *bo = NULL;
	struct ias_crtc *ias_crtc = output->ias_crtc;
	struct weston_buffer *buffer = ev->surface->buffer_ref.buffer;
	struct linux_dmabuf_buffer *dmabuf;
	struct ias_fb *ias_fb;
	uint32_t format;
	uint32_t resolve_needed = 0;
	struct ias_sprite *ias_sprite;
	int i;

	if (ias_crtc->output_model->get_next_fb(output)) {
		return NULL;
	}

	/*
	 * Check if this surface overlaps with output region, if no just skip it,
	 * there is no way that it could be used as scanout buffer.
	 * Additionally it may cause problems with RBC, as the same surface will be
	 * checked for different outputs and on one of them it can be resolved in display
	 * controller, but not in the other one, depending on order of outputs checked,
	 * RBC may be disabled for that surface even if it will be possible to resolve it
	 */
	pixman_region32_t output_overlap;
	pixman_region32_init(&output_overlap);
	pixman_region32_intersect(&output_overlap, &ev->transform.boundingbox, &output->base.region);
	if (!pixman_region32_not_empty(&output_overlap)) {
		return NULL;
	}

	if (buffer) {
		if ((dmabuf = linux_dmabuf_buffer_get(buffer->resource))) {
			struct gbm_import_fd_data gbm_dmabuf = {
				.fd = dmabuf->attributes.fd[0],
				.width = dmabuf->attributes.width,
				.height = dmabuf->attributes.height,
				.stride = dmabuf->attributes.stride[0],
				.format = dmabuf->attributes.format
			};

			for (i = 0; i < dmabuf->attributes.n_planes; i++) {
				if (dmabuf->attributes.modifier[i] == I915_FORMAT_MOD_Y_TILED_CCS ||
				    dmabuf->attributes.modifier[i] == I915_FORMAT_MOD_Yf_TILED_CCS) {
					resolve_needed = 1;
					break;
				}
			}

			bo = gbm_bo_import(c->gbm, GBM_BO_IMPORT_FD,
					   &gbm_dmabuf, GBM_BO_USE_SCANOUT);
		} else {
			bo = gbm_bo_import(c->gbm, GBM_BO_IMPORT_WL_BUFFER,
					   buffer->resource, GBM_BO_USE_SCANOUT);
		}
	}

	if (!bo) {
		return NULL;
	}

	format = gbm_bo_get_format(bo);

	wl_list_for_each(ias_sprite, &ias_crtc->sprite_list, link) {
		if ((c->use_cursor_as_uplane || ias_sprite->type != DRM_PLANE_TYPE_CURSOR) &&
				(uint32_t)ias_sprite->output_id == output->scanout) {
			break;
		}
	}

	/*
	 * Make output specific call to check if this surface is flippable.
	 * Additionaly check if sprite used for given output is able to resolve given buffer if there is such need.
	 */
	if(!ias_crtc->output_model->is_surface_flippable ||
			!ias_crtc->output_model->is_surface_flippable(ev, _output, check_xy) ||
			(resolve_needed &&
				 !(c->rbc_enabled &&
				   ias_sprite->supports_rbc &&
				   is_rbc_resolve_possible_on_sprite(0, format)))) {
		if (resolve_needed && c->rbc_debug) {
			weston_log("[RBC] Cannot handle compressed buffer on scanout %d, requesing resolve in DRI\n",
				   output->scanout);
		}
		gbm_bo_destroy(bo);
		return NULL;
	}

	ias_fb = ias_fb_get_from_bo(bo, buffer, output, IAS_FB_SCANOUT);

	if (!ias_fb) {
		gbm_bo_destroy(bo);
		return NULL;
	}

	ias_fb->is_client_buffer = 1;
	weston_buffer_reference(&ias_fb->buffer_ref, buffer);

	ias_crtc->output_model->set_next_fb(output, ias_fb);
	return &output->fb_plane;
}


/*
 * ias_output_render()
 *
 * Render an output buffer for presentation.  Note that this function may be
 * skipped in non-dualview cases where we determine that we have a suitable
 * fullscreen, top-level client buffer that we can just flip to directly.
 */
void
ias_output_render(struct ias_output *output, pixman_region32_t *new_damage)
{
	struct ias_backend *b = (struct ias_backend *)
		output->base.compositor->backend;
	struct ias_crtc *ias_crtc = output->ias_crtc;
	//GLfloat saved_clearcolor[4];
	int ret;
	pixman_region32_t damage;

	/*
	 * Clear out scanout_surface
	 */
	output->scanout_surface = NULL;

	/*
	 * Combine this frame's damage with previous frame's damage to figure out
	 * which areas of the scanout need to be drawn (since we swap between
	 * scanout buffers, we need to accumulate two frames of damage).
	 */
	pixman_region32_init(&damage);
	pixman_region32_union(&damage, new_damage, &output->prev_damage);

	/* Save away our new damage so that it can be re-used next frame */
	pixman_region32_copy(&output->prev_damage, new_damage);

	/* Bind buffers/contexts in preparation for rendering */
	ret = ias_crtc->output_model->pre_render(output);
	if (ret) {
		return;
	}

	if (output->disabled) {
		/*
		 * If the output is disabled, we want to clear it to black rather than
		 * drawing it.
		 */
		/*glGetFloatv(GL_COLOR_CLEAR_VALUE, saved_clearcolor);
		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);
		glClearColor(saved_clearcolor[0],
				saved_clearcolor[1],
				saved_clearcolor[2],
				saved_clearcolor[3]);*/
	} else {
		if (output->plugin) {
			/* This output might be scaled.  Setup the real viewport for the output */
			//glViewport(0, 0, output->width, output->height);

			/* Call the plugin-loaded draw function */
			output->plugin->draw_plugin(output);
		} else {
			/* Standard output drawing function */
			b->compositor->renderer->repaint_output_base(&output->base, &damage);
		}
	}

	pixman_region32_fini(&damage);

	wl_signal_emit(&output->base.frame_signal, output);

	/* Complete rendering process */
	ias_crtc->output_model->post_render(output);
	TRACEPOINT_ONCE("Backend post render complete");
}

static void
vblank_handler(int fd,
		unsigned int frame,
		unsigned int sec,
		unsigned int usec,
		void *data)
{
	struct ias_sprite *s = (struct ias_sprite *)data;
	struct ias_backend *c = s->compositor;
	struct ias_crtc *ias_crtc = s->ias_crtc;
	struct timespec ts;
	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION |
			WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

	ias_crtc->vblank_pending = 0;

	wl_list_for_each(s, &ias_crtc->sprite_list, link) {

		/* TODO: Mimicing similar functionality from compositor-drm.c;
		 * need to verify.
		 */
		drmModeRmFB(c->drm.fd, s->current->fb_id);
		s->current = s->next;
		s->next = NULL;
	}

	if (!ias_crtc->page_flip_pending) {
		ts.tv_sec = sec;
		ts.tv_nsec = usec * 1000;
		weston_output_finish_frame(&ias_crtc->output[0]->base, &ts, flags);
	}
}

/*
 * page_flip_handler()
 *
 * Handle page flip completion on a CRTC.
 *
 * There may be more than one, for example there could be flis pending
 * on the main plane and both sprite planes (stereo or other dual view
 * modes).
 */
static void
page_flip_handler(int fd, unsigned int frame,
		unsigned int sec, unsigned int usec, void *data)
{
	struct ias_crtc *ias_crtc = (struct ias_crtc *) data;

	TRACEPOINT_ONCE("First frame visible");

	if (ias_crtc->output_model->flip_handler) {
		ias_crtc->output_model->flip_handler(ias_crtc, sec, usec, 0, 0);
	} else {
		IAS_ERROR("Page flip handling not supported yet for this output model");
	}
}

#if 0
static void
atomic_handler(int fd, unsigned int frame,
		unsigned int sec, unsigned int usec,
		uint32_t obj_id, uint32_t old_fb_id, void *data)
{
	struct ias_crtc *ias_crtc = (struct ias_crtc *)data;

	if (ias_crtc->output_model->flip_handler) {
		ias_crtc->output_model->flip_handler(ias_crtc, sec, usec, old_fb_id,
				obj_id);
	} else {
		IAS_ERROR("Atomic page flip handling not supported yet for this output model");
	}
}
#endif

static void
drm_set_cursor(struct ias_backend *c,
		struct ias_crtc *ias_crtc,
		struct ias_sprite *s, uint32_t w, uint32_t h)
{
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.crtc_id, ias_crtc->crtc_id);
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.src_x, 0 << 16);
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.src_y, 0 << 16);
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.src_w, w << 16);
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.src_h, h << 16);
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.crtc_w, w);
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.crtc_h, h);

}

static void
drm_move_cursor(struct ias_backend *c,
		struct ias_crtc *ias_crtc,
		struct ias_sprite *s, int32_t x, int32_t y)
{
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.crtc_x, x);
	drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
			s->prop.crtc_y, y);
}

/*
 * ias_update_cursor()
 *
 * Move/set the cursor image during repaint.
 */
static void
ias_update_cursor(struct ias_crtc *ias_crtc)
{
	struct weston_view *ev = ias_crtc->cursor_view;
	struct ias_backend *c = (struct ias_backend *)ias_crtc->backend;
	struct ias_output *output = ias_crtc->output[0];
	struct weston_buffer *buffer;
	EGLint handle, stride;
	struct gbm_bo *bo;
	struct ias_fb *fb;
	uint32_t buf[64 * 64];
	unsigned char *shm_buf;
	struct ias_sprite *s;
	int i, x, y;

	/* Clear cursor surface in prep for next frame */
	ias_crtc->cursor_view = NULL;

	/* If HW cursors are not available, abort */
	if (ias_crtc->cursors_are_broken) {
		return;
	}

	wl_list_for_each(s, &ias_crtc->sprite_list, link) {
		if (s->type == DRM_PLANE_TYPE_CURSOR) {
			break;
		}
	}

	/*
	 * If no surface was assigned for this frame, turn off the hardware cursor
	 */
	if (ev == NULL) {
		if (ias_crtc->output_model->hw_cursor) {
			if (!c->has_nuclear_pageflip) {
				drmModeSetCursor(c->drm.fd, ias_crtc->crtc_id, 0, 0, 0);
			} else {
				drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
					s->prop.fb_id, 0);
				drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
					s->prop.crtc_id, 0);
			}
		}
		return;
	}

	buffer = ev->surface->buffer_ref.buffer;

	/*
	 * If the cursor image has been damaged, generate a new GEM bo we can
	 * set as the cursor image.
	 */
	if (buffer &&
			pixman_region32_not_empty(&ias_crtc->cursor_plane.damage))
	{
		/* Clear damage */
		pixman_region32_fini(&ias_crtc->cursor_plane.damage);
		pixman_region32_init(&ias_crtc->cursor_plane.damage);

		/* Switch back/front buffers for cursor */
		ias_crtc->current_cursor ^= 1;
		bo = ias_crtc->cursor_bo[ias_crtc->current_cursor];

		memset(buf, 0, sizeof buf);
		stride = wl_shm_buffer_get_stride(buffer->shm_buffer);
		shm_buf = wl_shm_buffer_get_data(buffer->shm_buffer);
		wl_shm_buffer_begin_access(buffer->shm_buffer);
		for (i = 0; i < ev->surface->height; i++) {
			memcpy(buf + i * 64, shm_buf + i * stride, ev->surface->width * 4);
		}
		wl_shm_buffer_end_access(buffer->shm_buffer);

		if (gbm_bo_write(bo, buf, sizeof buf) < 0) {
			IAS_ERROR("Failed to update cursor: %m");
			/* Ignore error and try to continue... */
		}

		fb = ias_fb_get_from_bo(bo, buffer, output, IAS_FB_CURSOR);
		if (!fb) {
			IAS_ERROR("Failed to get fb for cursor: %m");
			ias_crtc->cursors_are_broken = 1;
		} else {
			drmModeAtomicAddProperty(ias_crtc->prop_set, s->plane_id,
				s->prop.fb_id, fb->fb_id);
		}

		if (c->has_nuclear_pageflip) {
			drm_set_cursor(c, ias_crtc, s, 64, 64);
		} else {
			/* Call KMS to set cursor image */
			handle = gbm_bo_get_handle(bo).s32;

			drmModeSetCursor(c->drm.fd, ias_crtc->crtc_id, handle, 64, 64);
		}
	}

	/* Program cursor plane to surface position */
	x = ev->geometry.x - output->base.x;
	y = ev->geometry.y - output->base.y;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (ias_crtc->cursor_plane.x != x || ias_crtc->cursor_plane.y != y) {
		if (c->has_nuclear_pageflip) {
			drm_move_cursor(c, ias_crtc, s, x, y);
		} else {
			drmModeMoveCursor(c->drm.fd, ias_crtc->crtc_id, x, y);
		}

		ias_crtc->cursor_plane.x = x;
		ias_crtc->cursor_plane.y = y;
	}
}

static int32_t
is_surface_flippable_on_cursor(struct ias_crtc *ias_crtc,
		struct weston_view *ev)
{
	struct ias_output *ias_output = ias_crtc->output[0];
	struct weston_transform *transform;
	struct weston_buffer *buffer;

	/* If HW cursors are not available, abort */
	if (ias_crtc->cursors_are_broken) {
		return 0;
	}

	/* Make sure the surface is visible on this output and this output only */
	if (ev->output_mask != (1u << ias_output->base.id)) {
		return 0;
	};

	wl_list_for_each(transform, &ev->geometry.transformation_list, link) {
		if (transform != &ev->transform.position) {
			return 0;
		}
	}

	buffer = ev->surface->buffer_ref.buffer;

	/*
	 * If there's no buffer attached, or the buffer is too big, we can't
	 * use the cursor plane.
	 */
	if (buffer == NULL ||
			ev->surface->width > 64 ||
			ev->surface->height > 64)
	{
		return 0;
	}

	/*
	 * We only handle CPU-rendered shm buffers for cursors.  Don't try to use
	 * a GPU-rendered cursor on the cursor plane.
	 */
	if (!wl_shm_buffer_get(buffer->resource)) {
		return 0;
	}

	/* Shouldn't use hardware cursor on scaled outputs */
	if (ias_crtc->current_mode->base.width != ias_output->width ||
			ias_crtc->current_mode->base.height != ias_output->height)
	{
		return 0;
	}

	return 1;
}

/*
 * ias_attempt_cursor_for_view()
 *
 * Attempts to use a CRTC's cursor plane to display a surface.  This function
 * should only be used for non-dualview, non-stereo configurations.
 */
static struct weston_plane *
ias_attempt_cursor_for_view(struct ias_crtc *ias_crtc,
		struct weston_view *ev)
{
#if 0
	struct ias_output *ias_output = ias_crtc->output[0];
	struct ias_backend *backend = ias_crtc->backend;
	struct weston_buffer *buffer;
	struct gbm_bo *bo;
	uint32_t buf[64 * 64];
	uint32_t stride;
	int32_t handle;
	unsigned char *s;
	int i, x, y;
#endif

	assert(ias_crtc->output_model != NULL);

	/* If we've already assigned something to the cursor plane, bail out */
	if (ias_crtc->cursor_view) {
		return NULL;
	}

	if (!is_surface_flippable_on_cursor(ias_crtc, ev)) {
		return NULL;
	}

	/* Okay, looks like this is a suitable surface. */
	ias_crtc->cursor_view = ev;
	return &ias_crtc->cursor_plane;

#if 0
	/*
	 * If this surface wasn't
	 * on the cursor plane before, or if it was, but has been damaged, we
	 * need to update the cursor bo with the new surface contents.
	 */
	if (ias_crtc->last_cursor_view != ev ||
			pixman_region32_not_empty(&ev->surface->damage))
	{
		/* Switch back/front buffers for cursor */
		ias_crtc->current_cursor ^= 1;
		bo = ias_crtc->cursor_bo[ias_crtc->current_cursor];

		memset(buf, 0, sizeof buf);
		stride = wl_shm_buffer_get_stride(buffer->shm_buffer);
		s = wl_shm_buffer_get_data(buffer->shm_buffer);
		wl_shm_buffer_begin_access(buffer->shm_buffer);
		for (i = 0; i < ev->surface->height; i++)
			memcpy(buf + i * 64, s + i * stride, ev->surface->width * 4);
		wl_shm_buffer_end_access(buffer->shm_buffer);

		if (gbm_bo_write(bo, buf, sizeof buf) < 0)
			return NULL;

		handle = gbm_bo_get_handle(bo).s32;
		if (drm_set_cursor(compositor, ias_crtc, handle, 64, 64)) {
			IAS_ERROR("failed to set cursor: %m");
			return NULL;
		}
	}

	/* Now position the cursor plane on the screen */
	x = (ev->geometry.x - ias_output->base.x);
	y = (ev->geometry.y - ias_output->base.y);
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	if (ias_crtc->cursor_x != x || ias_crtc->cursor_y != y) {
		if (drm_move_cursor(compositor, ias_crtc, x, y)) {
			weston_log("failed to move cursor: %m\n");
			return NULL;
		}
		ias_crtc->cursor_x = x;
		ias_crtc->cursor_y = y;
	}

	ias_crtc->cursor_view = ev;

	return &ias_crtc->cursor_plane;
#endif
}


/*
 * ias_assign_planes()
 *
 * Try to assign surfaces to hardware planes.  We may assign surfaces to a
 * cursor or sprite plane, or we may decide to flip to a client buffer directly
 * onto the display plane.
 */
static void
ias_assign_planes(struct weston_output *output, void *repaint_data)
{
	struct weston_view *ev, *next;
	pixman_region32_t overlap, surface_overlap;
	struct weston_plane *primary_plane, *next_plane;
	struct ias_output *ias_output = (struct ias_output *)output;
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	struct ias_output_model *output_model = ias_crtc->output_model;
	struct ias_backend *backend = ias_crtc->backend;

	/*
	 * If this output model can neither flip client surfaces or use a hardware
	 * cursor, there's nothing we can do here.
	 */
	if (!output_model->hw_cursor && !output_model->can_client_flip) {
		return;
	}

	/*
	 * Assuming this output model supports it, the cursor is available for use
	 * for matching surfaces
	 */
	ias_crtc->last_cursor_view = ias_crtc->cursor_view;
	ias_crtc->cursor_view = NULL;

	/*
	 * Track the area of all surfaces above that will overlap with future
	 * surfaces.
	 */
	pixman_region32_init(&overlap);

	/* Walk surface list from top/highest to bottom/lowest */
	primary_plane = &backend->compositor->primary_plane;
	wl_list_for_each_safe(ev, next, &backend->compositor->view_list, link) {
		if (!(ev->output_mask & (1 <<output->id)))
			continue;
		/*
		 * Surfaces that can be flipped onto the display plane or the cursor plane
		 * need to have their buffer kept around.
		 */
		if((ias_crtc->output_model->is_surface_flippable &&
			ias_crtc->output_model->is_surface_flippable(ev, output, 1)) ||
			is_surface_flippable_on_cursor(ias_crtc, ev)) {
			ev->surface->keep_buffer = 1;
		}

		/*
		 * If a plugin is active, then we cannot let surfaces be assigned to
		 * hardware planes.
		 */
		if (!ias_output->plugin) {
			/*
			 * Figure out what areas of this surface (after transformations are
			 * applied) are covered by higher surfaces that we've already
			 * iterated over.
			 */
			pixman_region32_init(&surface_overlap);
			pixman_region32_intersect(&surface_overlap, &overlap,
					&ev->transform.boundingbox);

			next_plane = NULL;

			/*
			 * If this surface is clipped by any higher surfaces, it's not a
			 * candidate for the cursor plane or flipping directly to the display
			 * plane.
			 */
			if (pixman_region32_not_empty(&surface_overlap)) {
				next_plane = primary_plane;
			}

			/* See if it looks like a cursor */
			if (next_plane == NULL && output_model->hw_cursor) {
				next_plane = ias_attempt_cursor_for_view(ias_crtc, ev);
			}

			/* See if it can be flipped directly as a scanout */
			if (next_plane == NULL && output_model->can_client_flip) {
				next_plane = ias_attempt_scanout_for_view(output, ev, 1);
			}

			/* All other options failed; we're going to blit it to the main fb */
			if (next_plane == NULL) {
				next_plane = primary_plane;
			}

			/*
			 * Let weston figure out what needs to be damaged when using this
			 * plane to present this surface.
			 */
			weston_view_move_to_plane(ev, next_plane);

			/* Update region of main FB being redrawn */
			if (next_plane == primary_plane) {
				pixman_region32_union(&overlap, &overlap,
						&ev->transform.boundingbox);
			}

			pixman_region32_fini(&surface_overlap);
		} else {
			/*
			 * If this surface can be flipped onto the sprite plane,
			 * then it's buffer needs to be kep around.
			 */
			if (is_surface_flippable_on_sprite(ev, output)) {
				ev->surface->keep_buffer = 1;
			}
		}

		if(!ev->surface->keep_buffer
				&& ev->surface->role_name
				&& !strcmp(ev->surface->role_name, "ivi_surface")) {
			ev->surface->keep_buffer = 1;
		}
	}
	pixman_region32_fini(&overlap);
}

static void
ias_output_start_repaint_loop(struct weston_output *output_base)
{
	struct timespec ts;
	struct ias_backend *backend = (struct ias_backend *)
		output_base->compositor->backend;

	clock_gettime(backend->compositor->presentation_clock, &ts);
	weston_output_finish_frame(output_base, &ts, WP_PRESENTATION_FEEDBACK_INVALID);
}


/*
 * Does the ias_output have a link to the CRTC?  If so, then check
 * the crtc to see how many outputs are attached. Will have to behave
 * differently if there are multiple outputs attached.
 *
 * Should all the next/current info be moved to the crtc?
 *
 */
static int
ias_output_repaint(struct weston_output *output_base,
		   pixman_region32_t *damage,
		   void *repaint_data)
{
	struct ias_output *output = (struct ias_output *) output_base;
	struct ias_backend *backend =
		(struct ias_backend *) output->base.compositor->backend;
	struct ias_crtc *ias_crtc = output->ias_crtc;

	if (output->disabled) {
		return 1;
	}

	if (!ias_crtc->output_model->generate_crtc_scanout(
				ias_crtc, output, damage)) {
		return 1;
	}

	if (ias_crtc->request_color_correction_reset) {
		ias_crtc->request_color_correction_reset = 0;
		update_color_correction(ias_crtc, backend->has_nuclear_pageflip);
	}

	/*
	 * The order we do the cursor/sprite/display plane updates depends on
	 * if we have atomic page flip capabilites.
	 */
	if (backend->has_nuclear_pageflip) {
		/* If hardware cursor enabled, program that now.  */
		ias_update_cursor(ias_crtc);

		/* Prepare the sprite planes for flipping. */
		if(ias_crtc->output_model->update_sprites) {
			ias_crtc->output_model->update_sprites(ias_crtc);
		}

		/*
		 * generate_crtc_scanout() should setup the next scanout buffer(s)
		 * that need to be flipped. Call the output model's flip function
		 * to schedule the actual flip(s).
		 */

		ias_crtc->output_model->flip(backend->drm.fd, ias_crtc,
				output->scanout);
	} else {
		/*
		 * generate_crtc_scanout() should setup the next scanout buffer(s)
		 * that need to be flipped. Call the output model's flip function
		 * to schedule the actual flip(s).
		 */
		ias_crtc->output_model->flip(backend->drm.fd, ias_crtc,
				output->scanout);

		/* If hardware cursor enabled, program that now.  */
		ias_update_cursor(ias_crtc);
	}

	return 0;
}

static void
ias_output_destroy(struct weston_output *output_base)
{
	struct ias_output *output = (struct ias_output *) output_base;
	struct ias_backend *c =
		(struct ias_backend *) output->base.compositor->backend;
	struct ias_mode *ias_mode, *next;

	/*
	 * If this was a non-dualview / non-stereo setup, we might have been
	 * using the hardware cursor.  Turn it off.
	 */
	drmModeSetCursor(c->drm.fd, output->ias_crtc->crtc_id, 0, 0, 0);

	weston_plane_release(&output->fb_plane);
	weston_output_release(&output->base);

	wl_list_for_each_safe(ias_mode, next, &output->ias_crtc->mode_list, link) {
		wl_list_remove(&ias_mode->link);
		free(ias_mode);
	}
	//wl_list_remove(&output->base.link);
	wl_list_remove(&output->link);

	wl_global_destroy(output->global);
	free(output);
}

/*
 * ias_repeat_redraw()
 *
 * Indicates whether this output should immediately schedule another redraw
 * after finishing the previous redraw.  This is the default behavior when
 * using a layout plugin, but can be changed by a plugin via the
 * ias_set_plugin_redraw_behavior() helper function.
 */
static int
ias_repeat_redraw(struct weston_output *output)
{
	struct ias_output *ias_output = (struct ias_output *)output;

	return (ias_output->plugin != NULL && ias_output->plugin_redraw_always);
}


static int
on_ias_input(int fd, uint32_t mask, void *data)
{
	drmEventContext evctx;

	memset(&evctx, 0, sizeof evctx);
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.page_flip_handler = page_flip_handler;
	evctx.page_flip_handler = page_flip_handler;
	evctx.vblank_handler = vblank_handler;
	drmHandleEvent(fd, &evctx);

	return 1;
}


static int
ias_subpixel_to_wayland(int ias_value)
{
	switch (ias_value) {
	default:
	case DRM_MODE_SUBPIXEL_UNKNOWN:
		return WL_OUTPUT_SUBPIXEL_UNKNOWN;
	case DRM_MODE_SUBPIXEL_NONE:
		return WL_OUTPUT_SUBPIXEL_NONE;
	case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
		return WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB;
	case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
		return WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR;
	case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
		return WL_OUTPUT_SUBPIXEL_VERTICAL_RGB;
	case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
		return WL_OUTPUT_SUBPIXEL_VERTICAL_BGR;
	}
}

static drmModePropertyPtr
ias_get_prop(int fd, drmModeConnectorPtr connector, const char *name)
{
	drmModePropertyPtr props;
	int i;

	for (i = 0; i < connector->count_props; i++) {
		props = drmModeGetProperty(fd, connector->props[i]);
		if (!props)
			continue;

		if (!strcmp(props->name, name))
			return props;

		drmModeFreeProperty(props);
	}

	return NULL;
}

void
add_connector_id(struct ias_crtc *ias_crtc)
{
	struct ias_backend *backend = ias_crtc->backend;
	drmModeConnectorPtr connector = NULL;
	drmModePropertyPtr prop;

	connector = drmModeGetConnector(backend->drm.fd, ias_crtc->connector_id);
	if (connector) {
		prop = ias_get_prop(backend->drm.fd, connector, "CRTC_ID");
		if (prop) {
			drmModeAtomicAddProperty(ias_crtc->prop_set,
				ias_crtc->connector_id,
				prop->prop_id,
				ias_crtc->crtc_id);

			drmModeFreeProperty(prop);
		}

		drmModeFreeConnector(connector);
	}
}

void
ias_set_dpms(struct ias_crtc *ias_crtc, enum dpms_enum level)
{
	struct ias_backend *c = ias_crtc->backend;
	drmModeConnectorPtr connector;
	drmModePropertyPtr prop;

	connector = drmModeGetConnector(c->drm.fd, ias_crtc->connector_id);
	if (!connector)
		return;

	prop = ias_get_prop(c->drm.fd, connector, "DPMS");
	if (!prop) {
		drmModeFreeConnector(connector);
		return;
	}

	drmModeConnectorSetProperty(c->drm.fd, connector->connector_id,
			prop->prop_id, level);
	drmModeFreeProperty(prop);
	drmModeFreeConnector(connector);
}

static const char *connector_type_names[] = {
	"None",
	"VGA",
	"DVI",
	"DVI",
	"DVI",
	"Composite",
	"TV",
	"LVDS",
	"CTV",
	"DIN",
	"DP",
	"HDMI",
	"HDMI",
	"TV",
	"eDP",
	"Virtual",
	"DSI",
	"DPI"
};

/*
 * create_crtc_for_connector()
 *
 * Creates a CRTC object for a connector.  This needs to pick an appropriate
 * hardware CRTC to use.  If there's already a CRTC associated with the
 * connector by the initial DRM configuration, we'll continue to use that.
 * Otherwise we'll pick one that's compatible.
 */
static struct ias_crtc *
create_crtc_for_connector(struct ias_backend *backend,
		drmModeRes *resources,
		struct ias_crtc *ias_crtc,
		drmModeConnector *connector)
{
	drmModeEncoder *encoder;
	int i;

	/*
	 * Grab the connector's encoder since it's really the encoder
	 * that's associated with a CRTC.
	 */
	encoder = drmModeGetEncoder(backend->drm.fd, connector->encoders[0]);
	if (encoder == NULL) {
		weston_log("No encoder for connector.\n");
		return NULL;
	}

	/* Is there already a CRTC associated? */
	if (encoder->crtc_id) {
		ias_crtc->crtc_id = encoder->crtc_id;
		goto found_crtc;
	}

	/* No CRTC associated with the connector yet.  Find a suitable one. */
	for (i = 0; i < resources->count_crtcs; i++) {
		if (encoder->possible_crtcs & (1 << i) &&
		    !(backend->crtc_allocator & (1 << resources->crtcs[i])))
			break;
	}

	/* Did we find a suitable CRTC? that isn't already in use? */
	if (i == resources->count_crtcs) {
		/* Couldn't find a suitable CRTC */
		drmModeFreeEncoder(encoder);
		weston_log("No suitable CRTC for connector\n");
		return NULL;
	} else {
		ias_crtc->crtc_id = resources->crtcs[i];
	}

found_crtc:
	ias_crtc->connector_id = connector->connector_id;
	ias_crtc->subpixel = connector->subpixel;

	/*
	 * Mark the CRTC as used.  This is a bit strange since crtc_id will
	 * generally already be a power of 2, but at least we'll still have
	 * a unique bit in the allocator field.
	 */
	backend->crtc_allocator |= (1 << ias_crtc->crtc_id);

	/* Save original settings for CRTC */
	ias_crtc->original_crtc = drmModeGetCrtc(backend->drm.fd, ias_crtc->crtc_id);

	drmModeFreeEncoder(encoder);
	return ias_crtc;
}

static void
destroy_sprites_atomic(struct ias_backend *backend)
{
	struct ias_sprite *sprite, *next;
	struct ias_crtc *ias_crtc;
	int ret;

	wl_list_for_each(ias_crtc, &backend->crtc_list, link) {
		if (!ias_crtc->prop_set) {
			ias_crtc->prop_set = drmModeAtomicAlloc();
		}

		wl_list_for_each_safe(sprite, next, &ias_crtc->sprite_list, link) {
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					sprite->plane_id,
					sprite->prop.fb_id,
					0);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					sprite->plane_id,
					sprite->prop.crtc_id,
					0);

			if (sprite->current) {
				drmModeRmFB(backend->drm.fd, sprite->current->fb_id);
			}

			free(sprite);
		}

		ret = drmModeAtomicCommit(backend->drm.fd, ias_crtc->prop_set, 0, ias_crtc);
		if (ret) {
			IAS_ERROR("Queueing atomic pageflip failed: %m (%d)", ret);
			IAS_ERROR("This failure will prevent clients from updating.");
		}

		drmModeAtomicFree(ias_crtc->prop_set);
		ias_crtc->prop_set = NULL;
	}
}

static void
destroy_sprites(struct ias_backend *backend)
{
	struct ias_sprite *sprite, *next;
	struct ias_crtc *ias_crtc;

	if (backend->has_nuclear_pageflip) {
		destroy_sprites_atomic(backend);
		return;
	}

	wl_list_for_each(ias_crtc, &backend->crtc_list, link) {
		wl_list_for_each_safe(sprite, next, &ias_crtc->sprite_list, link) {
			/*  drmModeSetPlane(compositor->drm.fd, */
			DRM_SET_PLANE(backend, backend->drm.fd,
				sprite->plane_id,
				ias_crtc->crtc_id, 0, 0,
				0, 0, 0, 0, 0, 0, 0);

			if (sprite->current) {
				drmModeRmFB(backend->drm.fd, sprite->current->fb_id);
			}

			free(sprite);
		}
	}
}
#if 0
static int ias_is_input_per_output(struct weston_output *output)
{
	return ((struct ias_output *) output)->ias_crtc->backend->input_present;
}
#endif

static int
ias_backend_output_enable(struct weston_output *base)
{
	struct ias_output *ias_output = (struct ias_output *)base;
	struct ias_output *output;
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	struct ias_configured_output *cfg;
	int i;
	int found = 0;

	for (i = 0; i < ias_crtc->num_outputs; i++) {
		if (ias_crtc->output[i] == ias_output)
			break;
	}

	if (i == ias_crtc->num_outputs) {
		IAS_ERROR("Couldn't find output\n");
		return -1;
	}

	cfg = ias_crtc->configuration->output[i];

	/* By default set position of output to 0x0 */
	ias_output->base.x = 0;
	ias_output->base.y = 0;

	if (cfg->position == OUTPUT_POSITION_CUSTOM) {
		ias_output->base.x = cfg->x;
		ias_output->base.y = cfg->y;
	} else if (cfg->position == OUTPUT_POSITION_RIGHTOF &&
			cfg->position_target) {
		/* Look for the relative output */
		found = 0;
		wl_list_for_each(output, &output_list, link) {
			if (strcmp(output->name, cfg->position_target) == 0) {
				/* use the scaled / transformed width */
				ias_output->base.x = output->base.x + output->base.width;
				ias_output->base.y = output->base.y;
				found = 1;
				break;
			}
		}

		if (!found)
			/* Didn't find the specified output */
			IAS_ERROR("Couldn't find output named '%s'",
					cfg->position_target);
	} else if (cfg->position == OUTPUT_POSITION_BELOW &&
			cfg->position_target) {
		/* Look for the relative output */
		found = 0;
		wl_list_for_each(output, &output_list, link) {
			if (strcmp(output->name, cfg->position_target) == 0) {
				/* use the scaled / transformed height */
				ias_output->base.x = output->base.x;
				ias_output->base.y = output->base.y + output->base.height;
				found = 1;
				break;
			}
		}
		if (!found)
			/* Didn't find the specified output */
			IAS_ERROR("Couldn't find output named '%s'",
					cfg->position_target);
	} else {
		if (cfg->position != OUTPUT_POSITION_ORIGIN) 
			IAS_ERROR("Unknown output position %d for %s!", cfg->position,
					ias_output->name);
	}

	ias_output->base.mm_width = ias_output->width;
	ias_output->base.mm_height = ias_output->height;

	/* Hook up implementation */
	ias_output->base.repaint = ias_output_repaint;
	ias_output->base.destroy = ias_output_destroy;
	ias_output->base.start_repaint_loop = ias_output_start_repaint_loop;
	ias_output->base.assign_planes = ias_assign_planes;
	ias_output->base.set_dpms = NULL;
	ias_output->base.repeat_redraw = ias_repeat_redraw;

	return 0;
}

static int
ias_backend_output_disable(struct weston_output *base)
{
	return 0;
}

/*
 * create_outputs_for_crtc()
 *
 * Creates outputs associated with a CRTC.
 */
static void
create_outputs_for_crtc(struct ias_backend *backend, struct ias_crtc *ias_crtc)
{
	struct ias_output *ias_output;
	struct ias_configured_output *cfg;
	int i, scale;

	for (i = 0; i < ias_crtc->num_outputs; i++) {
		if (ias_crtc->configuration == NULL) {
			IAS_ERROR("Configuration not set for CRTC '%s'",
					ias_crtc->name);
			return;
		}
		cfg = ias_crtc->configuration->output[i];
		if (!cfg) {
			IAS_ERROR("Lacking configuration for CRTC '%s' output #%d",
					ias_crtc->name, i);
			return;
		}
		weston_log("Doing configuration for CRTC '%s' output #%d (%s)\n",
					ias_crtc->name, i, cfg->name);
		/* Create Wayland output object */
		ias_output = ias_output_create(backend);
		/* Associate output and CRTC */
		ias_crtc->output[i] = ias_output;
		ias_output->ias_crtc = ias_crtc;
		ias_output->vm = ias_crtc->configuration->output[i]->vm;

		/*
		 * Initialize output size according to output model.  The general case
		 * that we assume here is that the output inherits its dimensions from
		 * the CRTC in an output model-specific manner.  We may override the
		 * sizing farther below if this output is supposed to be scaled.
		 */
		ias_crtc->output_model->init_output(ias_output, cfg);

		/*
		 * Generate a fake mode object for the output in dualview/stereo where
		 * we can't just use the CRTC's mode.
		 */
		ias_output->mode.width = ias_output->width;
		ias_output->mode.height = ias_output->height;

		ias_output->mode.refresh = ias_crtc->current_mode->base.refresh;
		ias_output->mode.flags = WL_OUTPUT_MODE_CURRENT |
								 WL_OUTPUT_MODE_PREFERRED;

		ias_output->base.current_mode = &ias_output->mode;
		ias_output->base.original_mode = &ias_output->mode;
		ias_output->base.subpixel = ias_subpixel_to_wayland(ias_crtc->subpixel);
		ias_output->base.make = "unknown";
		ias_output->base.model = "unknown";

		wl_signal_init(&ias_output->update_signal);
		wl_signal_init(&ias_output->printfps_signal);
#if defined(BUILD_VAAPI_RECORDER) || defined(BUILD_FRAME_CAPTURE)
		wl_signal_init(&ias_output->next_scanout_ready_signal);
#endif
#ifdef BUILD_FRAME_CAPTURE
		wl_signal_init(&ias_output->base.commit_signal);
#endif

		/* Position output */

		switch (cfg->rotation) {
			case 0:
				ias_output->rotation = ias_crtc->output_model->render_flipped ?
					WL_OUTPUT_TRANSFORM_FLIPPED_180 :
					WL_OUTPUT_TRANSFORM_NORMAL;
				break;
			case 90:
				ias_output->rotation = WL_OUTPUT_TRANSFORM_90;
				break;
			case 180:
				ias_output->rotation = ias_crtc->output_model->render_flipped ?
					WL_OUTPUT_TRANSFORM_FLIPPED :
					WL_OUTPUT_TRANSFORM_180;
				break;
			case 270:
				ias_output->rotation = WL_OUTPUT_TRANSFORM_270;
				break;
			default:
				ias_output->rotation = ias_crtc->output_model->render_flipped ?
					WL_OUTPUT_TRANSFORM_FLIPPED_180 :
					WL_OUTPUT_TRANSFORM_NORMAL;
				IAS_ERROR("Unknown output rotation setting '%d', defaulting to 0",
						cfg->rotation);
				break;
		}

		/* General output initialization */
		scale = 1;

		/* Setup the output name.  Use CRTC's name if not configured */
		if (cfg->name) {
			ias_output->base.name = strdup(cfg->name);
		} else if (ias_crtc->num_outputs == 1) {
			ias_output->base.name = strdup(ias_crtc->name);
		} else {
			ias_output->base.name = calloc(1, strlen(ias_crtc->name) + 3);
			if (!ias_output->base.name) {
				IAS_ERROR("Failed to allocate output name: out of memory");
				exit(1);
			}
			sprintf(ias_output->base.name, "%s-%d", ias_crtc->name, i+1);
		}

		ias_output->name = ias_output->base.name;
		ias_output->base.enable = ias_backend_output_enable;
		ias_output->base.destroy = ias_output_destroy;
		ias_output->base.disable = ias_backend_output_disable;

		weston_output_init(&ias_output->base, backend->compositor, "unknown");

		ias_output->base.make = "unknown";
		ias_output->base.model = "unknown";
		ias_output->base.serial_number = "unknown";

		wl_list_insert(&ias_output->base.mode_list, &ias_output->mode.link);

		weston_output_set_scale(&ias_output->base, scale);
		weston_output_set_transform(&ias_output->base, ias_output->rotation);

		weston_output_enable(&ias_output->base);

		weston_plane_init(&ias_output->fb_plane, backend->compositor, ias_output->base.x,
				ias_output->base.y);

		/* Setup scaling if requested by config */
		ias_output_scale(ias_output, ias_output->width, ias_output->height);

		wl_list_insert(output_list.prev, &ias_output->link);
	}
}


/*
 * expose_3d_modes()
 *
 * Make libdrm expose 3D modes for a connector.  The current S3D
 * patches on the mailing list require this to be performed in order to
 * see any stereo modes on a connector.
 *
 * TODO:  Double check this when the patches go upstream.  This may change!
 */
static void
expose_3d_modes(struct ias_backend *backend, drmModeConnector **connector, uint32_t connector_id)
{
	drmModePropertyRes *property;
	int i, ret;

	for (i = 0; i < (*connector)->count_props; i++) {
		property = drmModeGetProperty(backend->drm.fd, (*connector)->props[i]);
		if (!property) {
			continue;
		}

		IAS_DEBUG("Connector property: '%s'", property->name);

		/* Is this the stereo mode property?  If so, set it on. */
		if (strcmp(property->name, "expose 3D modes") == 0) {
			ret = drmModeConnectorSetProperty(backend->drm.fd,
					(*connector)->connector_id,
					property->prop_id,
					1);
			IAS_DEBUG("Set 'expose 3D' property returns '%d'", ret);
			drmModeFreeProperty(property);
			return;
		}

		drmModeFreeProperty(property);
	}

	/* Release and re-grab the connector for 3D modes to show up. */
	drmModeFreeConnector(*connector);
	*connector = drmModeGetConnector(backend->drm.fd, connector_id);

	UNUSED_IN_RELEASE(ret);
}

static struct ias_crtc *
create_single_crtc(struct ias_backend *backend,
		drmModeRes *resources,
		struct connector_components *components)
{
	struct ias_crtc *ias_crtc;
	int j, ret;
	int is_stereo;
	drmModeCrtc *crtc;
	struct ias_mode *ias_mode, *crtc_mode;
	struct ias_connector *connector;

	connector = components->connector;

	/* Create the compositor CRTC object */
	ias_crtc = ias_crtc_create(backend);
	if (!ias_crtc) {
		weston_log("Failed to create IAS CRTC object\n");
		return NULL;
	}

	TRACEPOINT("    * Created CRTC for connector");

	ias_crtc->configuration = components->conf_crtc;
	for (j = 0; j < output_model_table_size; j++) {
		if (strcasecmp(ias_crtc->configuration->model,
					output_model_table[j]->name) == 0) {
			ias_crtc->output_model = output_model_table[j];
			if (ias_crtc->output_model->stereoscopic) {
				/* Ask for stereo modes if we're setup for stereo output */
				expose_3d_modes(backend, &connector->connector, resources->connectors[j]);
			}
			weston_log("Using the %s output model.\n",
				   ias_crtc->output_model->name);
			break;
		}
	}

	/*
	* The config file referred to this connector.  Create the IAS crtc
	* object for it.
	*/
	if (!create_crtc_for_connector(backend, resources, ias_crtc, connector->connector)) {
		ias_crtc_destroy(ias_crtc);
		return NULL;
	}


	/* If this CRTC is disabled, turn it off with KMS and return */
	if (ias_crtc->output_model == NULL) {
		IAS_DEBUG("Disabling CRTC %s", connector_name);
		drmModeSetCrtc(backend->drm.fd, ias_crtc->crtc_id, 0, 0, 0, 0, 0, NULL);
		wl_list_remove(&ias_crtc->link);
		backend->crtc_allocator &= ~(1 << ias_crtc->crtc_id);
		ias_crtc_destroy(ias_crtc);
		return NULL;
	}

	for (j = 0; j < resources->count_crtcs; j++) {
		if (ias_crtc->crtc_id == resources->crtcs[j]) {
			break;
		}
	}
	if (j == resources->count_crtcs) {
		/* Couldn't find a suitable CRTC */
		weston_log("No suitable CRTC found\n");
		ias_crtc->index=0;
	} else {
		ias_crtc->index = j;
	}

	ias_crtc->name = strdup(ias_crtc->configuration->name);

	wl_list_init(&ias_crtc->sprite_list);

	if (ias_crtc->output_model) {
		ias_crtc->output_model->init(ias_crtc);
	} else {
		weston_log("No output model exists for the CRTC configuration\n");
		ias_crtc_destroy(ias_crtc);
		return NULL;
	}

	/* Make sure configuration matches model requirements */
	/*if (ias_crtc->configuration->output_num !=
			ias_crtc->output_model->outputs_per_crtc) {
		IAS_ERROR("Invalid output configuration for CRTC.");
		ias_crtc_destroy(ias_crtc);
		return NULL;
	}*/

	/* Build the CRTC mode list */
	wl_list_init(&ias_crtc->mode_list);
	for (j = 0; j < connector->connector->count_modes; j++) {
		is_stereo = connector->connector->modes[j].flags & DRM_MODE_FLAG_3D_MASK;

		/*
		* When configured for stereo, only add stereo modes.  When not
		* configured for stereo mode, skip the stereo modes.
		*
		* TODO:  Current patches on the mailing list indicate we won't
		* even see the 3D modes unless we set the "expose 3D modes"
		* property on the connector, but that may change.  We'll assume
		* for now that they'll always show up and we can remove some
		* of this logic later if it isn't necessary.
		*/
		if (ias_crtc->output_model->stereoscopic && is_stereo) {
			ias_add_mode(ias_crtc, &connector->connector->modes[j], j);
		} else if (!ias_crtc->output_model->stereoscopic && !is_stereo) {
			ias_add_mode(ias_crtc, &connector->connector->modes[j], j);
		}
	}

	TRACEPOINT("    * Build mode list for CRTC");

	/* Determine current CRTC mode */
	crtc = drmModeGetCrtc(backend->drm.fd, ias_crtc->crtc_id);
	if (crtc) {
		wl_list_for_each(ias_mode, &ias_crtc->mode_list, link) {
			if (memcmp(&crtc->mode,
						&ias_mode->mode_info,
						sizeof crtc->mode) == 0) {
				ias_crtc->current_mode = ias_mode;
				break;
			}
		}
		drmModeFreeCrtc(crtc);
	}

	TRACEPOINT("    * Got current CRTC mode");

	/* Determine desired mode and allocate scanout buffer for this CRTC */
	crtc_mode = NULL;
	switch (ias_crtc->configuration->config) {
	case CRTC_CONFIG_PREFERRED:
		wl_list_for_each(ias_mode, &ias_crtc->mode_list, link) {
			if (ias_mode->base.flags & WL_OUTPUT_MODE_PREFERRED) {
				crtc_mode = ias_mode;
				break;
			}
		}

		/*
		* If there's no preferred mode in the mode list, we'll
		* fall through to just using the current mode.
		*/
		if (crtc_mode) {
			break;
		}
		__attribute__((fallthrough));
	case CRTC_CONFIG_CURRENT:
		/* Use current mode */
		crtc_mode = ias_crtc->current_mode;
		break;

	case CRTC_CONFIG_MODE:
		wl_list_for_each(ias_mode, &ias_crtc->mode_list, link) {
			if (
					(ias_mode->base.width ==
					 ias_crtc->configuration->width) &&
					(ias_mode->base.height ==
					 ias_crtc->configuration->height) &&
					(
						(ias_mode->base.refresh / 1000 ==
						 ias_crtc->configuration->refresh) ||
						(ias_crtc->configuration->refresh == 0)
					))
			{
				crtc_mode = ias_mode;
				break;
			}
		}
		break;
	}

	/*
	* We should have a mode now.  If we don't for some reason, we'll just
	* have to treat the CRTC as disabled.
	*/
	if (!crtc_mode) {
		IAS_ERROR("Failed to get mode for CRTC %s; disabling", components->connector_name);
		drmModeSetCrtc(backend->drm.fd, ias_crtc->crtc_id, 0, 0, 0, 0, 0, NULL);
		backend->crtc_allocator &= ~(1 << ias_crtc->crtc_id);
		ias_crtc_destroy(ias_crtc);
		return NULL;
	}

	ias_crtc->current_mode = crtc_mode;

	/* Allocate cursor scanout buffers */
	ias_crtc->cursor_bo[0] =
		gbm_bo_create(backend->gbm, 64, 64, GBM_FORMAT_ARGB8888,
				GBM_BO_USE_CURSOR_64X64 | GBM_BO_USE_WRITE);
	ias_crtc->cursor_bo[1] =
		gbm_bo_create(backend->gbm, 64, 64, GBM_FORMAT_ARGB8888,
				GBM_BO_USE_CURSOR_64X64 | GBM_BO_USE_WRITE);
	if (backend->use_cursor_as_uplane || !ias_crtc->cursor_bo[0] || !ias_crtc->cursor_bo[1]) {
		IAS_ERROR("Cursor buffers could not be created; using software cursor");
		ias_crtc->cursors_are_broken = 1;
	}

	TRACEPOINT("    * Allocated cursor buffers");

	/* Initialize plane objects */
	weston_plane_init(&ias_crtc->cursor_plane, backend->compositor, 0, 0);

	/* Get the initial state of the output properties */
	ias_get_crtc_object_properties(backend->drm.fd, &ias_crtc->prop,
			ias_crtc->crtc_id);

	ias_crtc->prop_set = drmModeAtomicAlloc();

	/* Create outputs associated with this CRTC */
	create_outputs_for_crtc(backend, ias_crtc);

	TRACEPOINT("    * Created outputs for CRTC");

	/* Create scanout buffer(s) */
	if (ias_crtc->output_model->allocate_scanout(ias_crtc, crtc_mode) == -1) {
		backend->crtc_allocator &= ~(1 << ias_crtc->crtc_id);

		for (j = 0; j < ias_crtc->num_outputs; j++) {
			ias_output_destroy(&ias_crtc->output[j]->base);
		}

		ias_crtc_destroy(ias_crtc);
		return NULL;
	}

	/* Make sure we do the drmModeSetCrtc on the next repaint */
	ias_crtc->request_set_mode = 1;

	ias_crtc->brightness = 0x808080;
	ias_crtc->contrast = 0x808080;
	ias_crtc->gamma = 0x808080;

	if (no_color_correction == 0) {
		ias_crtc->request_color_correction_reset = 1;
	}

	/*
	* Perform any CRTC initialization which is specific to the current
	* output model.  Some output models perform GL operations here,
	* which is why this is handled separately from the create_crtcs
	* call previously.
	*/
	if (ias_crtc->output_model->init_crtc) {
		ret = ias_crtc->output_model->init_crtc(ias_crtc);
		if (ret) {
			wl_list_remove(&ias_crtc->link);
			backend->crtc_allocator &= ~(1 << ias_crtc->crtc_id);
			ias_crtc_destroy(ias_crtc);
		}

		return NULL;
	}

	return ias_crtc;
}


/*
 * Query the DRM and use config settings to setup the CRTC objects.
 */
static int
create_crtcs(struct ias_backend *backend)
{
	struct components_list components_list;
	int count_crtcs = 0;
	struct ias_crtc *ias_crtc;
	struct connector_components *connector_components;
	drmModeRes *resources;
	int ret;

	TRACEPOINT("    * Create CRTCs entry");
	resources = drmModeGetResources(backend->drm.fd);
	if (!resources) {
		weston_log("drmModeGetResources failed\n");
		goto err_res_failed;
	}
	TRACEPOINT("    * After getting resources");

	ret = components_list_create(backend, resources, &components_list);
	if (ret) {
		IAS_DEBUG("Failed to allocate memory and initialize"
				" display components list");
		goto err_comp_failed;
	}

	/* Match any suitable display components and store in a list. If any
	 * are found, loop through the list and build them
	 */
	ret = components_list_build(backend, resources, &components_list);
	if (ret) {
		IAS_DEBUG("Failed to allocate memory and build"
				" display components list");
		goto err_comp_failed;
	}

	wl_list_for_each_reverse(connector_components, &components_list.list, link) {
		ias_crtc = create_single_crtc(backend, resources,
				connector_components);

		/* CRTC creation failed, skip */
		if (ias_crtc == NULL) {
			continue;
		}

		/* Increment CRTC counts and add the new CRTC to the list */
		count_crtcs++;
		backend->num_compositor_crtcs++;
		wl_list_insert(&backend->crtc_list, &ias_crtc->link);

		weston_log("CRTC created: name = %s, ID = %d,"
				" connector ID = %d \n",
				ias_crtc->name,
				ias_crtc->crtc_id,
				ias_crtc->connector_id);
	}

	/* Check if the final compositor CRTC list has any data */
	if (wl_list_empty(&backend->crtc_list)) {
		IAS_ERROR("Could not build any suitable CRTC's");
		goto err_list_empty;
	}

	TRACEPOINT(" - CRTC's setup");

	/* If the compositor contains more than one CRTC then the coordinates
	 * may need to be updated if the new display is in extended mode.
	 * Correct alignment is given in each CRTC's config file i.e. rightof.
	 */
	if (backend->num_compositor_crtcs > 1) {
		ias_crtc = NULL;

		wl_list_for_each(ias_crtc, &backend->crtc_list, link) {
			ias_update_outputs_coordinate(ias_crtc, ias_crtc->output[0]);
		}

	}

err_list_empty:
	/* Free memory use by components list */
	components_list_destroy(&components_list);

err_comp_failed:
	drmModeFreeResources(resources);

err_res_failed:
	return count_crtcs;
}

static void
ias_destroy(struct weston_compositor *compositor)
{
	struct ias_backend *d = (struct ias_backend *)compositor->backend;
	struct ias_configured_output *o, *n;
	struct ias_crtc *ias_crtc, *next_crtc;

	udev_input_destroy(&d->input);

	wl_event_source_remove(d->udev_ias_source);
	wl_event_source_remove(d->ias_source);

	destroy_sprites(d);

	wl_list_for_each_safe(o, n, &configured_output_list, link)
		free(o);

	weston_compositor_shutdown(compositor);

	wl_list_for_each_safe(ias_crtc, next_crtc, &d->crtc_list, link) {
		weston_log("Destorying crtc %d\n", ias_crtc->crtc_id);
		ias_crtc_destroy(ias_crtc);
	}

	if (d->gbm)
		gbm_device_destroy(d->gbm);

	weston_launcher_destroy(compositor->launcher);

	close(d->drm.fd);

	free(d);
}

static int
atomic_ioctl_supported(int fd)
{
	int ret;
	drmModeAtomicReqPtr prop;
	/*struct drm_set_client_cap client_cap;

	client_cap.capability = DRM_CLIENT_CAP_ATOMIC;
	client_cap.value = 1;

	ret = drmIoctl(fd,
			DRM_IOCTL_SET_CLIENT_CAP, &client_cap);*/

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

	prop = drmModeAtomicAlloc();
	ret = drmModeAtomicCommit(fd, prop, DRM_MODE_ATOMIC_TEST_ONLY, NULL);
	drmModeAtomicFree(prop);

	if (ret) {
		return 0; /* not supported */
	} else {
		return 1; /* supported */
	}
}

static int
render_buffer_compression_supported(struct ias_backend *backend)
{
	struct weston_compositor *compositor = backend->compositor;
	int *formats = NULL;
	uint64_t *modifiers = NULL;
	int num_formats, num_modifiers;
	int i, j;
	int rbc_supported = 0;

	/*
	 * Use EGL_EXT_image_dma_buf_import_modifiers to query and advertise
	 * format/modifier codes.
	 */
	compositor->renderer->query_dmabuf_formats(compositor, &formats,
						   &num_formats);

	for (i = 0; i < num_formats; i++) {
		compositor->renderer->query_dmabuf_modifiers(compositor,
							     formats[i],
							     &modifiers,
							     &num_modifiers);

		for (j = 0; j < num_modifiers; j++) {
			if (modifiers[j] == I915_FORMAT_MOD_Y_TILED_CCS ||
			    modifiers[j] == I915_FORMAT_MOD_Yf_TILED_CCS) {
				rbc_supported = 1;
				free(modifiers);
				goto out;
			}
		}
		free(modifiers);
	}

out:
	free(formats);

	return rbc_supported;
}

static void
session_notify(struct wl_listener *listener, void *data)
{
	struct weston_compositor *compositor = data;
	struct ias_crtc *ias_crtc, *next_crtc;
	struct ias_sprite *sprite, *next;
	struct ias_output *output;
	struct ias_backend *backend = container_of(compositor->backend,
			struct ias_backend, base);

	if (compositor->session_active) {
		weston_log("activating session\n");
		compositor->state = backend->prev_state;

		wl_list_for_each_safe(ias_crtc, next_crtc, &backend->crtc_list, link) {
			ias_crtc->request_set_mode = 1;
		}

		weston_compositor_damage_all(compositor);
		udev_input_enable(&backend->input);
	} else {
		weston_log("deactivating session\n");
		udev_input_disable(&backend->input);
		backend->prev_state = compositor->state;
		weston_compositor_offscreen(compositor);

		/* If we have a repaint scheduled (either from a
		 * pending pageflip or the idle handler), make sure we
		 * cancel that so we don't try to pageflip when we're
		 * vt switched away.  The OFFSCREEN state will prevent
		 * further attemps at repainting.  When we switch
		 * back, we schedule a repaint, which will process
		 * pending frame callbacks. */

		wl_list_for_each(output, &compositor->output_list, base.link) {
			output->base.repaint_needed = 0;
		}

		wl_list_for_each(ias_crtc, &backend->crtc_list, link) {
			drmModeSetCursor(backend->drm.fd, ias_crtc->crtc_id, 0, 0, 0);

			wl_list_for_each_safe(sprite, next, &ias_crtc->sprite_list, link) {
				/*  drmModeSetPlane(compositor->drm.fd, */
				DRM_SET_PLANE(backend, backend->drm.fd,
				sprite->plane_id,
				ias_crtc->crtc_id, 0, 0,
				0, 0, 0, 0, 0, 0, 0);
			}
		}
	}
}

static void
switch_vt_binding(struct weston_keyboard *keyboard,
		  const struct timespec * time,
		  uint32_t key, void *data)
{
	struct weston_compositor *compositor = data;

	weston_launcher_activate_vt(compositor->launcher, key - KEY_F1 + 1);
}

#ifdef HYPER_DMABUF
static int
init_hyper_dmabuf(struct ias_backend *backend)
{
	struct ioctl_hyper_dmabuf_tx_ch_setup msg;
	int ret;
	weston_log("Initializing hyper dmabuf\n");

	backend->hyper_dmabuf_fd = open(HYPER_DMABUF_PATH, O_RDWR);
	/* If opening failed, try old dev node used by hyper dmabuf */
	if (backend->hyper_dmabuf_fd < 0)
		backend->hyper_dmabuf_fd = open(HYPER_DMABUF_PATH_LEGACY, O_RDWR);

	if (backend->hyper_dmabuf_fd < 0) {
		weston_log("Cannot open hyper dmabuf device\n");
		return -1;
	}

	/* TODO: add config option to specify which domains should be used, for now we share always with dom0 */
        msg.remote_domain = 0;

	ret = ioctl(backend->hyper_dmabuf_fd, IOCTL_HYPER_DMABUF_TX_CH_SETUP, &msg);

        if (ret) {
		weston_log("%s: ioctl failed with error %d\n", __func__, ret);
		close(backend->hyper_dmabuf_fd);
		backend->hyper_dmabuf_fd = -1;
		return -1;
        }

	return 0;
}

static void
cleanup_hyper_dmabuf(struct ias_backend *backend)
{
	if (backend->hyper_dmabuf_fd >= 0) {
		close(backend->hyper_dmabuf_fd);
		backend->hyper_dmabuf_fd = -1;
	}
}
#endif

#ifdef BUILD_FRAME_CAPTURE
static void
capture_proxy_destroy_from_output(struct ias_output *output)
{
	if (output->cp) {
		weston_log("[WESTON] Destroying frame capture for output.\n");
		wl_list_remove(&output->capture_proxy_frame_listener.link);
		capture_proxy_destroy(output->cp);
		output->cp = NULL;
	} else {
		weston_log("ERROR: Trying to destroy capture proxy that doesn't exist for this output.\n");
	}

	output->base.disable_planes--;
}

static void
capture_proxy_destroy_from_surface(struct ias_backend *ias_backend, struct weston_surface *surface)
{
	struct ias_surface_capture *capture_item = NULL;
	struct ias_surface_capture *tmp_list_item = NULL;

	weston_log("[WESTON] Destroying frame capture for surface %p...\n", surface);
	wl_list_for_each_safe(capture_item, tmp_list_item, &ias_backend->capture_proxy_list, link) {
		if (capture_item->capture_surface == surface) {
			if (!capture_item->cp) {
				weston_log("ERROR: Trying to destroy capture proxy that doesn't exist for this surface.\n");
			} else {
				if (surface->output) {
					surface->output->disable_planes--;
				} else {
					weston_log("Warning - capture_proxy_destroy_from_surface - No output associated with surface %p.\n",
							surface);
				}
				capture_proxy_destroy(capture_item->cp);
				capture_item->cp = NULL;
			}

			weston_log("[WESTON] Removing callbacks...\n");
			wl_list_remove(&capture_item->capture_commit_listener.link);
			wl_list_remove(&capture_item->capture_vsync_listener.link);
			wl_list_remove(&capture_item->link);
			free(capture_item);
		}
	}
}


static struct ias_fb *
ias_capture_fb_get_from_bo(struct gbm_bo *bo, struct weston_buffer *buffer,
			   struct ias_backend *backend)

{
	struct ias_fb *fb = gbm_bo_get_user_data(bo);
	uint32_t width, height;
	uint32_t format, strides[4] = {0}, handles[4] = {0}, offsets[4] = {0};
	uint64_t modifiers[4] = {0};
	int ret;
	int flags = 0;
	struct linux_dmabuf_buffer *dmabuf = NULL;
	int i;

	if (fb) {
		if (fb->fb_id) {
			drmModeRmFB(backend->drm.fd, fb->fb_id);
		}
		/* TODO: need to find better way instead of creating fb again */
		free(fb);
	}

	fb = calloc(1, sizeof *fb);
	if (!fb) {
		IAS_ERROR("Failed to allocate fb: out of memory");
		exit(1);
	}

	fb->bo = bo;
	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	format = gbm_bo_get_format(bo);

	if (buffer)
		dmabuf = linux_dmabuf_buffer_get(buffer->resource);

	if (dmabuf) {
		for (i = 0; i < dmabuf->attributes.n_planes; i++) {
			handles[i] = gbm_bo_get_handle(bo).u32;
			strides[i] = dmabuf->attributes.stride[i];
			offsets[i] = dmabuf->attributes.offset[i];
			modifiers[i] = dmabuf->attributes.modifier[i];
			if (modifiers[i] > 0) {
				flags |= DRM_MODE_FB_MODIFIERS;
				if (modifiers[i] == I915_FORMAT_MOD_Y_TILED_CCS	||
				    modifiers[i] == I915_FORMAT_MOD_Yf_TILED_CCS)
					fb->is_compressed = 1;
			}
		}
	} else {
		if (format == GBM_FORMAT_NV12) {
			if (buffer != NULL) {
				gl_renderer->query_buffer(backend->compositor,
					buffer->legacy_buffer, EGL_STRIDE, (EGLint *)strides);
				gl_renderer->query_buffer(backend->compositor,
					buffer->legacy_buffer, EGL_OFFSET, (EGLint *)offsets);
			}
			handles[0] = gbm_bo_get_handle(bo).u32;
			handles[1] = gbm_bo_get_handle(bo).u32;
		} else if (format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888) {
			strides[0] = gbm_bo_get_stride(bo);
			handles[0] = gbm_bo_get_handle(bo).u32;
			offsets[0] = 0;
		}
	}

	ret = drmModeAddFB2WithModifiers(backend->drm.fd, width, height,
					format, handles, strides, offsets,
					modifiers, &fb->fb_id, flags);

	if (ret) {
		weston_log("Failed to create kms fb: %m.\n");
		free(fb);
		return NULL;
	}

	gbm_bo_set_user_data(bo, fb, ias_fb_destroy_callback);

	return fb;
}


/* We may well be sending the frame over RTP, so a timestamp is
 * needed. RFC6184 says that "A 90 kHz clock rate MUST be used." */
#define PIPELINE_CLOCK 90000

/* Callback that is called when a frame has been composited
 * and is ready to be displayed on the relevant output. */
static void
capture_frame_notify(struct wl_listener *listener, void *data)
{
	struct ias_output *output;
	struct ias_backend *c;
	struct ias_fb *fb;
	int fd, ret;
	uint32_t handle;
	int64_t start = 0;
	uint64_t frame_time; /* in microseconds */
	struct timespec start_spec;
	uint32_t timestamp = 0;
#ifdef PROFILE_REMOTE_DISPLAY
	struct timespec end_spec;
	int64_t duration, finish;
#endif

	/* The timestamp forms part of the RTP header and thus must be
	 * updated per frame. It is a 32-bit value that wraps. */
	clock_gettime(CLOCK_REALTIME, &start_spec);
	frame_time = start_spec.tv_sec * US_IN_SEC + start_spec.tv_nsec / NS_IN_US;
	timestamp = (PIPELINE_CLOCK * frame_time / US_IN_SEC) & 0xFFFFFFFF;

	output = container_of(listener, struct ias_output,
				capture_proxy_frame_listener);
	c = (struct ias_backend *) output->base.compositor->backend;

	if (!output->cp) {
		weston_log("Error: capture_frame_notify - output has no capture proxy!\n");
		return;
	}

	if (capture_proxy_profiling_is_enabled(output->cp)) {
		struct timespec start_spec;

		clock_gettime(CLOCK_REALTIME, &start_spec);
		start = timespec_to_nsec(&start_spec);
		weston_log("WESTON: Frame[%d] start: %ld ns for output.\n",
					capture_get_frame_count(output->cp), start);
	}

	/* get_next_fb() cannot fail in this case, since we have just been
	 * notified that a frame is available. */
	fb = output->ias_crtc->output_model->get_next_fb(output);
	handle = gbm_bo_get_handle(fb->bo).u32;

	ret = drmPrimeHandleToFD(c->drm.fd, handle,
				 DRM_CLOEXEC, &fd);
	if (ret) {
		weston_log("[capture proxy] "
			   "failed to create prime fd for front buffer\n");
		return;
	}

	ret = capture_proxy_handle_frame(output->cp, NULL, fd,
				gbm_bo_get_stride(fb->bo), CP_FORMAT_RGB, timestamp);
	if (ret < 0) {
		weston_log("[capture proxy] aborted: %m\n");
		capture_proxy_destroy_from_output(output);
	}

#ifdef PROFILE_REMOTE_DISPLAY
	clock_gettime(CLOCK_REALTIME, &end_spec);
	finish = timespec_to_nsec(&end_spec);
	duration = finish - start;
	weston_log("WESTON: capture_frame_notify - start: %ld ns, finish: %ld ns, duration: %ld ns.\n", start, finish, duration);
#endif
}


/* Callback that is called when any application commits a surface. */
static void
capture_commit_notify(struct wl_listener *listener, void *data)
{
	struct ias_backend *c;
	struct ias_fb *fb;
	int fd, ret;
	int abort = false;
	uint32_t handle;
	uint32_t format;
	struct weston_surface *surface = (struct weston_surface *)data;
	struct weston_buffer *buffer;
	struct linux_dmabuf_buffer *dmabuf;
	struct gbm_bo *bo;
	struct wl_shm_buffer *shm_buffer;
	struct ias_surface_capture *capture_item = NULL;
	struct ias_surface_capture *capture_tmp = NULL;
	struct ias_surface_capture *capture = NULL;

	static uint32_t extra_frames;

	int64_t start = 0;
	uint64_t frame_time; /* in microseconds */
	struct timespec start_spec;
	uint32_t timestamp = 0;
#ifdef PROFILE_REMOTE_DISPLAY
	struct timespec end_spec;
	int64_t duration, finish;
#endif

	/* The timestamp forms part of the RTP header and thus must be
	 * updated per frame. It is a 32-bit value that wraps. */
	clock_gettime(CLOCK_REALTIME, &start_spec);
	frame_time = start_spec.tv_sec * US_IN_SEC + start_spec.tv_nsec / NS_IN_US;
	timestamp = (PIPELINE_CLOCK * frame_time / US_IN_SEC) & 0xFFFFFFFF;

	capture_item = container_of(listener, struct ias_surface_capture,
			      capture_commit_listener);
	c = capture_item->backend;

	if (!c) {
		weston_log("Error: capture_commit_notify: no backend\n");
		return;
	}

	wl_list_for_each(capture_tmp, &c->capture_proxy_list, link) {
		if (capture_tmp->capture_surface == surface) {
			capture = capture_tmp;
			break;
		}
	}

	if (!capture) {
		/* Not a surface we are tracking. */
		return;
	}

	/* Allow a maximum of two frames to be encoded between composite
	 * events, to reduce load on the encoder. Only allowing a single frame
	 * is too aggressive. This could become a config option in future. */
	if (!vsync_received(capture->cp)) {
		extra_frames++;
		if (extra_frames > 1) {
			return;
		}
	}
	if (vsync_received(capture->cp)) {
		clear_vsyncs(capture->cp);
		if (extra_frames > 1) {
			weston_log("Dropped %d frames between composite events.\n",
					extra_frames - 1);
		}
		extra_frames = 0;
	}

	if (!capture->capture_surface->buffer_ref.buffer) {
		/* We believe this is the case when the GPU has been
		 * bottle-necked and the buffer isn't ready in time. */
		if (capture_proxy_verbose_is_enabled(capture->cp)) {
			weston_log("Warning - no buffer.\n");
		}
		return;
	}

	if (capture_proxy_profiling_is_enabled(capture->cp)) {
		start = timespec_to_nsec(&start_spec);
		weston_log("WESTON: Frame[%d] start: %ld ns for surface.\n",
					capture_get_frame_count(capture->cp), start);
	}

	buffer = capture->capture_surface->buffer_ref.buffer;
	shm_buffer = wl_shm_buffer_get(buffer->resource);

	if (shm_buffer) {
		ret = capture_proxy_handle_frame(capture->cp, shm_buffer, -1, 0, CP_FORMAT_RGB, timestamp);
		if (ret < 0) {
			weston_log("[capture proxy] shm buffer aborted: %m\n");
			/* This error is fatal. */
			capture_proxy_destroy_from_surface(c, capture->capture_surface);
		}
#ifdef PROFILE_REMOTE_DISPLAY
		clock_gettime(CLOCK_REALTIME, &end_spec);
		finish = timespec_to_nsec(&end_spec);
		duration = finish - start;
		weston_log("WESTON: capture_commit_notify (shm) - start: %ld ns, finish: %ld ns, duration: %ld ns.\n",
				start, finish, duration);
#endif
		return;
	} else {
		if ((dmabuf = linux_dmabuf_buffer_get(buffer->resource))) {
			struct gbm_import_fd_data gbm_dmabuf = {
				.fd = dmabuf->attributes.fd[0],
				.width = dmabuf->attributes.width,
				.height = dmabuf->attributes.height,
				.stride = dmabuf->attributes.stride[0],
				.format = dmabuf->attributes.format
			};

			bo = gbm_bo_import(c->gbm, GBM_BO_IMPORT_FD,
					   &gbm_dmabuf, GBM_BO_USE_SCANOUT);
		} else {
			bo = gbm_bo_import(c->gbm, GBM_BO_IMPORT_WL_BUFFER,
					   buffer->resource, GBM_BO_USE_SCANOUT);
		}

		if (!bo) {
			weston_log("[capture proxy]: Failed to import bo for wl_resource at %p - giving up.\n",
					buffer->resource);
			return;
		}
	}

	fb = ias_capture_fb_get_from_bo(bo, buffer, c);
	if (!fb) {
		weston_log("[capture proxy]: Failed to get fb from bo.\n");
		goto err_commit;
	}

	handle = gbm_bo_get_handle(fb->bo).u32;

	ret = drmPrimeHandleToFD(c->drm.fd, handle,
				 DRM_CLOEXEC, &fd);
	if (ret) {
		weston_log("[capture proxy]: Failed to create prime fd for front buffer.\n");
		goto err_commit;
	}

	format = gbm_bo_get_format(bo);
	if (format == GBM_FORMAT_XRGB8888 || format == GBM_FORMAT_ARGB8888) {
		ret = capture_proxy_handle_frame(capture->cp, NULL, fd,
				gbm_bo_get_stride(fb->bo), CP_FORMAT_RGB, timestamp);
	} else if (format == GBM_FORMAT_NV12) {
		ret = capture_proxy_handle_frame(capture->cp, NULL, fd,
				gbm_bo_get_stride(fb->bo), CP_FORMAT_NV12, timestamp);
	} else {
		weston_log("[capture proxy]: Unsupported surface format.\n");
		ret = -1;
	}
	if (ret < 0) {
		abort = true;
	}

err_commit:
	/* We've asked for the buffer to be kept alive long enough for us to
	 * encode it, so dereference it now that we're done with it. Note that
	 * the client keeps an open fd to the data until it is done. */
	if (fb != NULL) {
		gbm_bo_destroy(fb->bo);
	}
	weston_buffer_reference(&capture->capture_surface->buffer_ref, NULL);

	if (abort) {
		weston_log("[capture proxy]: Aborted: %m.\n");
		/* This error is fatal. */
		capture_proxy_destroy_from_surface(c, capture->capture_surface);
	}

#ifdef PROFILE_REMOTE_DISPLAY
	clock_gettime(CLOCK_REALTIME, &end_spec);
	finish = timespec_to_nsec(&end_spec);
	duration = finish - start;
	weston_log("WESTON: capture_commit_notify - start: %ld ns, finish: %ld ns, duration: %ld ns.\n",
			start, finish, duration);
#endif
}


/* Callback that is called when a vsync has occurred on the relevant
 * output. */
static void
capture_vsync_notify(struct wl_listener *listener, void *data)
{
	struct ias_surface_capture *capture_item = container_of(
		listener, struct ias_surface_capture, capture_vsync_listener);
	vsync_notify(capture_item->cp);
}


static void *
create_capture_proxy(struct ias_backend *c, struct wl_client *client)
{
	int fd;
	drm_magic_t magic;
	void *cp = NULL;

	fd = open(c->drm.filename, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}

	drmGetMagic(fd, &magic);
	drmAuthMagic(c->drm.fd, magic);

	cp = capture_proxy_create(fd, client);
	if (cp == NULL) {
		close(fd);
	}
	return cp;
}


static int
start_capture(struct wl_client *client,
		struct ias_backend *ias_backend, struct wl_resource *resource,
		struct weston_surface *surface, const uint32_t output_number,
		const int profile, const int verbose)
{
	struct ias_output *output = NULL;
	pid_t pid;

	/* Make sure that we're using the IAS backend... */
	if (ias_backend->magic != BACKEND_MAGIC) {
		IAS_ERROR("Backend does not match.");
		return IAS_HMI_FCAP_ERROR_INVALID;
	}
	wl_client_get_credentials(client, &pid, NULL, NULL);

	if (surface) {
		/* If a weston_surface was passed in, then we're capturing the
		 * buffers  of that surface. */
		struct ias_surface_capture *capture_proxy_item;
		struct ias_surface_capture *capture_check = NULL;

		if (surface->output) {
			output = container_of(surface->output, struct ias_output,
				base);
		} else {
			weston_log("Error - set_recording - No output associated with surface %p.\n",
					surface);
			return IAS_HMI_FCAP_ERROR_INVALID;
		}

		/* Check whether we're already recording this surface... */
		wl_list_for_each(capture_check, &ias_backend->capture_proxy_list, link) {
			if (capture_check->capture_surface == surface) {
				weston_log("Surface already has a capture client.\n");
				return IAS_HMI_FCAP_ERROR_DUPLICATE;
			}
		}

		surface->keep_buffer = 1;

		capture_proxy_item = calloc(1, sizeof(struct ias_surface_capture));
		capture_proxy_item->backend = ias_backend;
		capture_proxy_item->capture_surface = surface;
		capture_proxy_item->cp =
					create_capture_proxy(ias_backend, client);
		capture_proxy_set_size(capture_proxy_item->cp,
								output->width, output->height);
		if (!capture_proxy_item->cp) {
			weston_log("Failed to create capture proxy.\n");
			free(capture_proxy_item);
			return IAS_HMI_FCAP_ERROR_NO_CAPTURE_PROXY;
		}

		capture_proxy_item->capture_commit_listener.notify =
			capture_commit_notify;
		wl_signal_add(&output->base.commit_signal,
			  &capture_proxy_item->capture_commit_listener);

		wl_list_init(&capture_proxy_item->link);
		wl_list_insert(&ias_backend->capture_proxy_list, &capture_proxy_item->link);

		/* We need to know when a vsync has occurred, even if we are not
		 * capturing an output, in order to limit the number of frames
		 * that are handled from applications that commit at very
		 * high framerates. */
		capture_proxy_item->capture_vsync_listener.notify = capture_vsync_notify;
		wl_signal_add(&output->next_scanout_ready_signal,
			&capture_proxy_item->capture_vsync_listener);

		if (profile) {
			capture_proxy_enable_profiling(capture_proxy_item->cp, 1);
		}

		capture_proxy_set_verbose(capture_proxy_item->cp, verbose);
		capture_proxy_set_resource(capture_proxy_item->cp, resource);
	} else { /* We are capturing an output framebuffer... */
		struct wl_list *output_list = &ias_backend->compositor->output_list;
		uint32_t found_output = 0;

		wl_list_for_each(output, output_list, base.link) {
			if (output->base.id == output_number) {
				weston_log("Starting capture for output %d.\n", output_number);
				found_output = 1;
				break;
			}
		}
		if (!found_output) {
			weston_log("start_capture - Invalid output selection %u.\n",
				output_number);
			return IAS_HMI_FCAP_ERROR_INVALID;
		}

		if (ias_backend->format != GBM_FORMAT_XRGB8888) {
			weston_log("Failed to start capture proxy: output format not supported.\n");
			return IAS_HMI_FCAP_ERROR_NO_CAPTURE_PROXY;
		}

		if (output->cp) {
			weston_log("Output is already being captured.\n");
			return IAS_HMI_FCAP_ERROR_DUPLICATE;
		}

		output->cp = create_capture_proxy(ias_backend, client);
		capture_proxy_set_size(output->cp, output->width, output->height);
		if (!output->cp) {
			weston_log("Failed to create capture proxy.\n");
			return IAS_HMI_FCAP_ERROR_NO_CAPTURE_PROXY;
		}

		output->capture_proxy_frame_listener.notify = capture_frame_notify;
		wl_signal_add(&output->next_scanout_ready_signal,
			&output->capture_proxy_frame_listener);
		weston_output_schedule_repaint(&output->base);

		if (profile) {
			capture_proxy_enable_profiling(output->cp, 1);
		}

		capture_proxy_set_verbose(output->cp, verbose);
		capture_proxy_set_resource(output->cp, resource);
	}

	output->base.disable_planes++;
	weston_log("start_capture done.\n");
	return IAS_HMI_FCAP_ERROR_OK;
}



static int
stop_capture(struct wl_client *client,
		struct ias_backend *ias_backend, struct wl_resource *resource,
		struct weston_surface *surface, const uint32_t output_number)
{
	struct ias_output *output = NULL;
	pid_t pid;

	/* Make sure that we're using the IAS backend... */
	if (ias_backend->magic != BACKEND_MAGIC) {
		IAS_ERROR("Backend does not match.");
		return IAS_HMI_FCAP_ERROR_INVALID;
	}
	wl_client_get_credentials(client, &pid, NULL, NULL);

	if (surface) {
		/* If a weston_surface was passed in, then we were capturing the
		 * buffers  of that surface. */
		if (surface->output) {
			output = container_of(surface->output, struct ias_output,
				base);
		} else {
			weston_log("Error - stop_capture - No output associated with surface %p.\n",
					surface);
			return IAS_HMI_FCAP_ERROR_INVALID;
		}

		weston_log("Destroying capture proxy for surface %p.\n", surface);
		capture_proxy_destroy_from_surface(ias_backend, surface);
	} else { /* We were capturing an output framebuffer... */
		struct wl_list *output_list = &ias_backend->compositor->output_list;
		uint32_t found_output = 0;

		wl_list_for_each(output, output_list, base.link) {
			if (output->base.id == output_number) {
				weston_log("Stopping capture for output %d.\n", output_number);
				found_output = 1;
				break;
			}
		}
		if (!found_output) {
			weston_log("stop_capture - Invalid output selection %u.\n",
				output_number);
			return IAS_HMI_FCAP_ERROR_INVALID;
		}

		weston_log("Stopping capture for output %d.\n", output_number);
		capture_proxy_destroy_from_output(output);
	}

	weston_log("stop_capture done.\n");
	return IAS_HMI_FCAP_ERROR_OK;
}

static int
release_buffer_handle(struct ias_backend *ias_backend, uint32_t surfid,
						uint32_t bufid, uint32_t imageid,
						struct weston_surface *surface, uint32_t output_number)
{
	if (surface) {
		struct ias_surface_capture *capture_item = NULL;
		struct ias_surface_capture *tmp_item = NULL;

		wl_list_for_each_safe(capture_item, tmp_item, &ias_backend->capture_proxy_list, link) {
			if (capture_item->capture_surface == surface) {
				capture_proxy_release_buffer(capture_item->cp, surfid, bufid, imageid);
			}
		}
	} else {
		struct ias_output *output = NULL;
		struct wl_list *output_list = &ias_backend->compositor->output_list;
		uint32_t i = 0;

		wl_list_for_each(output, output_list, base.link) {
			if (output->base.id == output_number) {
				break;
			}
			i++;
		}
		if (i > output_number) {
			weston_log("release_buffer_handle - Invalid output selection %u.\n",
				output_number);
			return IAS_HMI_FCAP_ERROR_INVALID;
		}
		capture_proxy_release_buffer(output->cp, 0, 0, 0);
	}

	return IAS_HMI_FCAP_ERROR_OK;
}

#endif /*BUILD_FRAME_CAPTURE*/

static int
init_drm(struct ias_backend *backend, struct udev_device *device)
{
	const char *filename, *sysnum;
	uint64_t cap;
	int fd, ret;

	sysnum = udev_device_get_sysnum(device);
	if (sysnum)
		backend->drm.id = atoi(sysnum);
	if (!sysnum || backend->drm.id < 0) {
		weston_log("cannot get device sysnum\n");
		return -1;
	}

	filename = udev_device_get_devnode(device);
	fd = weston_launcher_open(backend->compositor->launcher, filename, O_RDWR);
	if (fd < 0) {
		/* Probably permissions error */
		weston_log("couldn't open %s, skipping\n",
			udev_device_get_devnode(device));
		return -1;
	}

	weston_log("using %s\n", filename);

	backend->drm.fd = fd;
	backend->drm.filename = strdup(filename);

	ret = drmGetCap(fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	if (ret == 0 && cap == 1)
		backend->clock = CLOCK_MONOTONIC;
	else
		backend->clock = CLOCK_REALTIME;

	return 0;
}

static struct gbm_device *
create_gbm_device(int fd)
{
	struct gbm_device *gbm;

	gl_renderer = weston_load_module("gl-renderer.so",
					 "gl_renderer_interface");
	if (!gl_renderer)
		return NULL;

	/* GBM will load a dri driver, but even though they need symbols from
	 * libglapi, in some version of Mesa they are not linked to it. Since
	 * only the gl-renderer module links to it, the call above won't make
	 * these symbols globally available, and loading the DRI driver fails.
	 * Workaround this by dlopen()'ing libglapi with RTLD_GLOBAL. */
	dlopen("libglapi.so.0", RTLD_LAZY | RTLD_GLOBAL);

	gbm = gbm_create_device(fd);

#ifdef USE_VM
	gl_renderer->vm_exec = vm_exec;
	gl_renderer->vm_dbg = vm_dbg;
#ifdef HYPER_DMABUF
	gl_renderer->vm_unexport_delay = vm_unexport_delay;
	gl_renderer->vm_plugin_path = vm_plugin_path;
	gl_renderer->vm_plugin_args = vm_plugin_args;
	gl_renderer->vm_share_only = vm_share_only;
#else
	weston_log("Hyper dmabuf support not enabled during compilation, "
		   "disabling surface sharing\n");
	gl_renderer->vm_exec = 0;
#endif // HYPER_DMABUF
#endif // USE_VM

	return gbm;
}

/* When initializing EGL, if the preferred buffer format isn't available
 * we may be able to susbstitute an ARGB format for an XRGB one.
 *
 * This returns 0 if substitution isn't possible, but 0 might be a
 * legitimate format for other EGL platforms, so the caller is
 * responsible for checking for 0 before calling gl_renderer->create().
 *
 * This works around https://bugs.freedesktop.org/show_bug.cgi?id=89689
 * but it's entirely possible we'll see this again on other implementations.
 */
static int
fallback_format_for(uint32_t format)
{
	switch (format) {
	case GBM_FORMAT_XRGB8888:
		return GBM_FORMAT_ARGB8888;
	case GBM_FORMAT_XRGB2101010:
		return GBM_FORMAT_ARGB2101010;
	default:
		return 0;
	}
}

static int
ias_compositor_create_gl_renderer(struct ias_backend *backend)
{
	EGLint format[2] = {
		backend->format,
		fallback_format_for(backend->format),
	};
	int n_formats = 1;
	int use_vm = 0;

#ifdef USE_VM
	if(gl_renderer->vm_exec) {
		backend->format = GBM_FORMAT_ARGB8888;
	}
	use_vm = gl_renderer->vm_exec;
#endif // USE_VM

	if (format[1])
		n_formats = 2;
	if (gl_renderer->display_create(backend->compositor,
					EGL_PLATFORM_GBM_KHR,
					(void *)backend->gbm,
					NULL,
					use_vm
					? gl_renderer->alpha_attribs
					: gl_renderer->opaque_attribs,
					format, n_formats) < 0) {
		return -1;
	}

	return 0;
}

static int
init_egl(struct ias_backend *backend, struct udev_device *device)
{
	backend->gbm = create_gbm_device(backend->drm.fd);

	if (!backend->gbm)
		return -1;

	if (ias_compositor_create_gl_renderer(backend) < 0) {
		gbm_device_destroy(backend->gbm);
		return -1;
	}

	TRACEPOINT(" - GLES2 renderer initialized");

	return 0;
}

static int
udev_event_is_hotplug(struct ias_backend *backend, struct udev_device *device)
{
	const char *sysnum;
	const char *val;

	sysnum = udev_device_get_sysnum(device);
	if (!sysnum || atoi(sysnum) != backend->drm.id)
		return 0;

	val = udev_device_get_property_value(device, "HOTPLUG");
	if (!val)
		return 0;

	return strcmp(val, "1") == 0;
}

static int
udev_ias_event(int fd, uint32_t mask, void *data)
{
	struct ias_backend *backend = data;
	struct udev_device *event;

	event = udev_monitor_receive_device(backend->udev_monitor);

	if (udev_event_is_hotplug(backend, event)) {
		ias_update_outputs(backend, event);
	}

	udev_device_unref(event);

	return 1;
}

static struct udev_device *find_drm_device(
		struct ias_backend *backend, struct udev_enumerate *e,
		const char *seat_id, const char **path)
{
	struct udev_list_entry *entry;
	struct udev_device *device, *drm_device;
	const char *device_seat;

	udev_enumerate_scan_devices(e);
	drm_device = NULL;

	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		*path = udev_list_entry_get_name(entry);
		device = udev_device_new_from_syspath(backend->udev, *path);
		device_seat =
			udev_device_get_property_value(device, "ID_SEAT");
		if (!device_seat)
			device_seat = default_seat;
		if (strcmp(device_seat, seat_id) == 0) {
			drm_device = device;
			break;
		}
		udev_device_unref(device);
	}

	return drm_device;
}

static struct ias_backend *
ias_compositor_create(struct weston_compositor *compositor,
		      struct weston_ias_backend_config *config)
{
	struct ias_backend *backend;
	struct udev_enumerate *e;
	struct udev_device *drm_device;
	const char *path;
	struct wl_event_loop *loop;
	uint32_t key;
	uint32_t counter = 0;
	const char *seat_id = default_seat;

	weston_log("initializing Intel Automotive Solutions backend\n");

	set_unset_env(&global_env_list);

	backend = malloc(sizeof *backend);
	if (backend == NULL)
		return NULL;
	memset(backend, 0, sizeof *backend);
	backend->compositor = compositor;

	/*
	 * Set compositor's keyboard/keymap setting to what we read from the
	 * config before initializing the base weston compositor.
	 */
	compositor->use_xkbcommon = use_xkbcommon;

	compositor->normalized_rotation = normalized_rotation;
	compositor->damage_outputs_on_init = damage_outputs_on_init;

	backend->print_fps = print_fps;
	backend->no_flip_event = no_flip_event;

	if (config->gbm_format) {
		if (strcmp(config->gbm_format, "xrgb8888") == 0)
			backend->format = GBM_FORMAT_XRGB8888;
		else if (strcmp(config->gbm_format, "argb8888") == 0)
			backend->format = GBM_FORMAT_ARGB8888;
		else if (strcmp(config->gbm_format, "rgb565") == 0)
			backend->format = GBM_FORMAT_RGB565;
		else if (strcmp(config->gbm_format, "xrgb2101010") == 0)
			backend->format = GBM_FORMAT_XRGB2101010;
		else {
			weston_log("fatal: unrecognized pixel format: %s\n",
				   config->gbm_format);
			goto err_base;
		}
	} else {
		backend->format = GBM_FORMAT_XRGB8888;
	}

	backend->use_pixman = config->use_pixman;

	if (config->seat_id)
		seat_id = config->seat_id;

	if (config->tty == 0)
		config->tty = 1;

	/* Check if we run ias-backend using weston-launch */
	compositor->launcher = weston_launcher_connect(compositor, config->tty,
						    seat_id, true);
	if (compositor->launcher == NULL) {
		weston_log("fatal: IAS backend should be run "
			   "using weston-launch binary or as root\n");
		goto err_compositor;
	}

	/*
	 * Set a magic number so the shell module can verify that it's running on
	 * the IAS backend.
	 */
	backend->magic = BACKEND_MAGIC;

	backend->udev = udev_new();
	if (backend->udev == NULL) {
		weston_log("failed to initialize udev context\n");
		goto err_compositor;
	}

	backend->session_listener.notify = session_notify;
	wl_signal_add(&compositor->session_signal, &backend->session_listener);

	TRACEPOINT(" - Before finding drm device");
	/* Worst case is that we wait for 2 seconds to find the drm device */
	drm_device = NULL;

	while(!drm_device && counter < 2000) {
		e = udev_enumerate_new(backend->udev);
		udev_enumerate_add_match_subsystem(e, "drm");
		udev_enumerate_add_match_sysname(e, "card[0-9]*");
		drm_device = find_drm_device(backend, e, seat_id, &path);

		if(!drm_device) {
		  udev_enumerate_unref(e);
			usleep(5000);
			counter++;
		}
	}
	TRACEPOINT(" - After finding drm device");

	if (drm_device == NULL) {
		weston_log("no drm device found\n");
		goto err_udev_enum;
	}

	TRACEPOINT(" - udev and tty setup complete");

	if (init_drm(backend, drm_device) < 0) {
		weston_log("failed to initialize kms\n");
		goto err_udev_dev;
	}

	if (init_egl(backend, drm_device) < 0) {
		weston_log("failed to initialize egl\n");
		goto err_udev_dev;
	}

#ifdef HYPER_DMABUF
	if (vm_exec && init_hyper_dmabuf(backend) < 0) {
		weston_log("Failed to initialize hyper dmabuf\n");
		goto err_udev_dev;
	}
#endif

	TRACEPOINT(" - EGL initialized");

	backend->base.destroy = ias_destroy;

	backend->prev_state = WESTON_COMPOSITOR_ACTIVE;

	for (key = KEY_F1; key < KEY_F9; key++)
		weston_compositor_add_key_binding(compositor, key,
				MODIFIER_CTRL | MODIFIER_ALT,
				switch_vt_binding, compositor);

#ifdef BUILD_FRAME_CAPTURE
	wl_list_init(&backend->capture_proxy_list);
#endif

	/*
	 * Query KMS to see if atomic/nuclear page fliping is supported.
	 */
	backend->has_nuclear_pageflip = atomic_ioctl_supported(backend->drm.fd);

	if (!use_nuclear_flip) {
		backend->has_nuclear_pageflip = 0;
	}

	backend->rbc_debug = rbc_debug;
	backend->use_cursor_as_uplane = use_cursor_as_uplane;

	/*
	 * Query KMS for the number of crtc's and create
	 * an ias_crtc for each and create a global object list of crtc's
	 * similar to how the list of outputs is created.
	 */
	wl_list_init(&output_list);
	wl_list_init(&backend->crtc_list);
	if (create_crtcs(backend) <= 0) {
		weston_log("failed to create crtcs for %s\n", path);
		goto err_sprite;
	}

	if (emgd_has_multiplane_drm(backend)) {
		backend->private_multiplane_drm = 0;
	} else {
		backend->private_multiplane_drm = 1;
	}

	TRACEPOINT(" - Sprites initialized");

	path = NULL;

	if (udev_input_init(&backend->input,
			    compositor, backend->udev, seat_id,
			    config->configure_device) < 0) {
		weston_log("failed to create input devices\n");
		goto err_sprite;
	}

	TRACEPOINT(" - Input initialized");

	loop = wl_display_get_event_loop(compositor->wl_display);
	backend->ias_source =
		wl_event_loop_add_fd(loop, backend->drm.fd,
				WL_EVENT_READABLE, on_ias_input, backend);

	backend->udev_monitor = udev_monitor_new_from_netlink(backend->udev, "udev");
	if (backend->udev_monitor == NULL) {
		weston_log("failed to intialize udev monitor\n");
		goto err_ias_source;
	}
	udev_monitor_filter_add_match_subsystem_devtype(backend->udev_monitor,
							"drm", NULL);
	backend->udev_ias_source =
		wl_event_loop_add_fd(loop,
				     udev_monitor_get_fd(backend->udev_monitor),
				     WL_EVENT_READABLE, udev_ias_event, backend);

	if (udev_monitor_enable_receiving(backend->udev_monitor) < 0) {
		weston_log("failed to enable udev-monitor receiving\n");
		goto err_udev_monitor;
	}

	udev_device_unref(drm_device);

	backend->get_sprite_list = get_sprite_list;
	backend->assign_view_to_sprite = assign_view_to_sprite;
	backend->assign_zorder_to_sprite = assign_zorder_to_sprite;
	backend->assign_blending_to_sprite = assign_blending_to_sprite;
	backend->attempt_scanout_for_view = ias_attempt_scanout_for_view;
	backend->get_tex_info = get_tex_info;
	backend->get_egl_image_info = get_egl_image_info;
	backend->set_viewport = set_viewport;
#ifdef BUILD_FRAME_CAPTURE
	backend->start_capture = start_capture;
	backend->stop_capture = stop_capture;
	backend->release_buffer_handle = release_buffer_handle;
#endif

	centre_pointer(backend);

	if (backend->has_nuclear_pageflip) {
		backend->rbc_supported = render_buffer_compression_supported(backend);
		backend->rbc_enabled = backend->rbc_supported && use_rbc && !has_overlapping_outputs(backend);
		if (backend->rbc_debug) {
			weston_log("[RBC] RBC support in DRM = %d\n", backend->rbc_supported);
			weston_log("[RBC] RBC enabled in compositor = %d\n", backend->rbc_enabled);
		}
	}


	compositor->backend = &backend->base;
	return backend;
err_udev_monitor:
	wl_event_source_remove(backend->udev_ias_source);
	udev_monitor_unref(backend->udev_monitor);
err_ias_source:
	wl_event_source_remove(backend->ias_source);
	udev_input_destroy(&backend->input);
err_sprite:
	compositor->renderer->destroy(compositor);
	compositor->renderer = NULL;
	gbm_device_destroy(backend->gbm);
	destroy_sprites(backend);
#ifdef HYPER_DMABUF
	if (vm_exec)
		cleanup_hyper_dmabuf(backend);
#endif
err_udev_dev:
	if(backend->drm.fd) {
		close(backend->drm.fd);
	}
	udev_device_unref(drm_device);
err_udev_enum:
	udev_enumerate_unref(e);
err_compositor:
	weston_compositor_shutdown(compositor);
err_base:
	free(backend);
	return NULL;
}

/***
 *** Worker functions for plugin manager helper funcs
 ***/

/*
 * get_sprite_list()
 */
static int
get_sprite_list(struct weston_output *output,
		struct ias_sprite ***sprite_list)
{
	struct ias_output *ias_output = (struct ias_output*)output;
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	struct ias_sprite *ias_sprite;
	int num_sprites = ias_crtc->num_sprites;
	int i;

	/* If sprites are broken (e.g., kernel too old), return none */
	if (ias_crtc->sprites_are_broken) {
		*sprite_list = NULL;
		return 0;
	}

	/* If we're in dualview or stereo mode, no sprites are available */
	if (!ias_crtc->output_model->sprites_are_usable) {
		*sprite_list = NULL;
		return 0;
	}

	/* Build a sprite list for the plugin */
	i = 0;
	*sprite_list = calloc(num_sprites, sizeof(struct ias_sprite*));
	if (!*sprite_list) {
		IAS_ERROR("Failed to allocate sprite list: out of memory");
		return 0;
	}

	wl_list_for_each(ias_sprite, &ias_crtc->sprite_list, link) {
		assert(i < num_sprites);
		if ((1<<ias_sprite->pipe_id) & ias_sprite->possible_crtcs)
			(*sprite_list)[i++] = ias_sprite;
	}

	assert(i == num_sprites);
	return num_sprites;
}

static int
assign_blending_to_sprite(struct weston_output *output,
		int sprite_id,
		int src_factor,
		int dst_factor,
		float blend_color,
		int enable)
{
	struct ias_output *ias_output = (struct ias_output*)output;
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	struct ias_sprite *s;
	int ret = -1;
	uint32_t format;

	/* Make sure sprites are actually functioning (e.g., new enough kernel) */
	if (ias_crtc->sprites_are_broken) {
		IAS_ERROR("Cannot use sprite; sprites are broken");
		return -1;
	}

	wl_list_for_each(s, &ias_crtc->sprite_list, link) {
		if (s->plane_id == sprite_id) {
			format = s->next->format;

			if ((src_factor == SPUG_BLEND_FACTOR_AUTO &&
					dst_factor == SPUG_BLEND_FACTOR_AUTO) ||
					(src_factor == SPUG_BLEND_FACTOR_ONE &&
							dst_factor == SPUG_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)) {
				s->constant_alpha = 1.0f;
				s->pixel_blend_mode = (format == GBM_FORMAT_ARGB8888) ?
						DRM_MODE_BLEND_PREMULTI :
						DRM_MODE_BLEND_PIXEL_NONE;
			} else if (src_factor == SPUG_BLEND_FACTOR_ONE &&
					dst_factor == SPUG_BLEND_FACTOR_ZERO) {
				s->constant_alpha = 1.0f;
				s->pixel_blend_mode = DRM_MODE_BLEND_PIXEL_NONE;
			} else if (src_factor == SPUG_BLEND_FACTOR_SRC_ALPHA &&
					dst_factor == SPUG_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
				s->constant_alpha = 1.0f;
				s->pixel_blend_mode = (format == GBM_FORMAT_ARGB8888) ?
						DRM_MODE_BLEND_COVERAGE :
						DRM_MODE_BLEND_PIXEL_NONE;
			} else if (src_factor == SPUG_BLEND_FACTOR_CONSTANT_ALPHA &&
					dst_factor == SPUG_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA) {
				s->constant_alpha = blend_color;
				s->pixel_blend_mode = DRM_MODE_BLEND_PIXEL_NONE;
			} else if (src_factor == SPUG_BLEND_FACTOR_CONSTANT_ALPHA &&
					dst_factor == SPUG_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA_TIMES_SRC_ALPHA) {
				s->constant_alpha = blend_color;
				s->pixel_blend_mode = DRM_MODE_BLEND_PREMULTI;
			} else if (src_factor == SPUG_BLEND_FACTOR_CONSTANT_ALPHA_TIMES_SRC_ALPHA &&
					dst_factor == SPUG_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA_TIMES_SRC_ALPHA) {
				s->constant_alpha = blend_color;
				s->pixel_blend_mode = DRM_MODE_BLEND_COVERAGE;
			} else {
				IAS_ERROR("Invalid blend factor combination");
				return -1;
			}

			s->blending_enabled = enable;
			s->sprite_dirty |= SPRITE_DIRTY_BLENDING;
			s->view->alpha = s->constant_alpha;
			ret = 0;
		}
	}

	return ret;
}

static int
assign_zorder_to_sprite(struct weston_output *output,
		int sprite_id,
		int position)
{
	struct ias_output *ias_output = (struct ias_output*)output;
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	struct ias_sprite *s;
	int ret = -1;

	/* Make sure sprites are actually functioning (e.g., new enough kernel) */
	if (ias_crtc->sprites_are_broken) {
		IAS_ERROR("Cannot use sprite; sprites are broken");
		return -1;
	}

	wl_list_for_each(s, &ias_crtc->sprite_list, link) {
		if (s->plane_id == sprite_id) {
			s->zorder = position;
			s->sprite_dirty |= SPRITE_DIRTY_ZORDER;
			ret = 0;
		}
	}

	return ret;
}

/*
 * Checks if compressed buffer meets all requrements to be resolved in display controller.
 * Assumes that buffer is Y or Yf tiled with CSS enabled.
 */
static uint32_t
is_rbc_resolve_possible_on_sprite(uint32_t rotation, uint32_t format) {
	if (rotation != 0 && rotation != 180) {
		return 0;
	}

	if (format != GBM_FORMAT_ARGB8888 && format != GBM_FORMAT_XRGB8888) {
		return 0;
	}

	return 1;
}

/* Check if a surface is flippable on a sprite in this output model */
static uint32_t
is_surface_flippable_on_sprite(struct weston_view *view,
		struct weston_output *output)
{
	struct ias_output *ias_output = (struct ias_output*)output;
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	struct weston_surface *surface = view->surface;

	/* Make sure sprites are actually functioning (e.g., new enough kernel) */
	if (ias_crtc->sprites_are_broken) {
		IAS_DEBUG("Cannot use sprite; sprites are broken");
		return 0;
	}

	 /* Don't use sprites on scaled outputs */
	if (ias_crtc->current_mode->base.width != ias_output->width ||
		ias_crtc->current_mode->base.height != ias_output->height) {
		IAS_DEBUG("Sprite usage is not supported on scaled outputs.");
		return 0;
	}

	/* If there's no buffer attached to this surface, bail out. */
	if (!surface->buffer_ref.buffer) {
		return 0;
	}

	/* SHM buffers can't be placed on a sprite plane */
	if (wl_shm_buffer_get(surface->buffer_ref.buffer->resource)) {
		IAS_DEBUG("SHM buffers can't be assigned to a sprite plane");
		return 0;
	}

	return 1;
}

/*
 * Crops src_rect by the same ratio that cropped_dest_rect
 * was cropped comparing to orig_dest_rect.
 */
static void
crop_rect_scaled(pixman_region32_t *src_rect,
		pixman_region32_t *orig_dest_rect,
		pixman_region32_t *cropped_dest_rect)
{
	pixman_box32_t *orig_extents;
	pixman_box32_t *cropped_extents;
	pixman_box32_t *src_extents;
	float x_ratio,y_ratio,w_ratio,h_ratio;
	int32_t cropped_w, cropped_h, orig_w, orig_h, src_w, src_h;

	orig_extents = pixman_region32_extents(orig_dest_rect);
	cropped_extents = pixman_region32_extents(cropped_dest_rect);

	orig_w = orig_extents->x2 - orig_extents->x1;
	orig_h = orig_extents->y2 - orig_extents->y1;
	cropped_w = cropped_extents->x2 - cropped_extents->x1;
	cropped_h = cropped_extents->y2 - cropped_extents->y1;

	/* Calculate by what ratio x,y,w,h of destination rectangle was cropped */
	x_ratio = ((float)(cropped_extents->x1 - orig_extents->x1)) / orig_w;
	y_ratio = ((float)(cropped_extents->y1 - orig_extents->y1)) / orig_h;
	w_ratio = ((float)(cropped_w - orig_w)) / orig_w;
	h_ratio = ((float)(cropped_h - orig_h)) / orig_h;

	src_extents = pixman_region32_extents(src_rect);

	src_w = src_extents->x2 - src_extents->x1;
	src_h = src_extents->y2 - src_extents->y1;

	/* Crop source rectangle using the same ratio like for destination rectangle */
	src_extents->x1 += x_ratio * src_w;
	src_extents->y1 += y_ratio * src_h;
	src_extents->x2 += x_ratio * src_w + w_ratio * src_w;
	src_extents->y2 += y_ratio * src_h + h_ratio * src_h;
}

/*
 * assign_surface_to_sprite()
 *
 * Assigns the specified surface (or subregion of the surface) to a sprite for
 * the frame currently being rendered and positions it at the specified
 * position on the screen.  This function should be called by a plugin's 'draw'
 * entrypoint each frame.  If the plugin does not call this function, the
 * sprite will automatically be turned off and not used for the current frame.
 */
static struct weston_plane *
assign_view_to_sprite(struct weston_view *view,
		/*
		struct ias_sprite *sprite,
		*/
		struct weston_output *output,
		int *sprite_id,
		int x,
		int y,
		int sprite_width,
		int sprite_height,
		pixman_region32_t *view_region)
{
	struct ias_output *ias_output = (struct ias_output*)output;
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_sprite *ias_sprite, *sprite;
	struct weston_surface *surface = view->surface;
	struct gbm_bo *bo;
	uint32_t format;
	uint32_t resolve_needed = 0;
	uint32_t downscaling = 0;
	struct linux_dmabuf_buffer *dmabuf;
	int i;

	wl_fixed_t sx1, sy1, sx2, sy2;
	pixman_box32_t *sprite_extents;
	pixman_region32_t src_rect, dest_rect, orig_dest_rect, crtc_rect;

	ias_sprite = NULL;
	sprite = NULL;

	if (!is_surface_flippable_on_sprite(view, output)) {
		return NULL;
	}

	if (x >= output->current_mode->width) {
		x = output->current_mode->width - 1;
	}

	if (y >= output->current_mode->height) {
		y = output->current_mode->height - 1;
	}

	/* Import the surface buffer as a GBM bo that we can flip */
	if ((dmabuf = linux_dmabuf_buffer_get(surface->buffer_ref.buffer->resource))) {
		struct gbm_import_fd_data gbm_dmabuf = {
			.fd = dmabuf->attributes.fd[0],
			.width = dmabuf->attributes.width,
			.height = dmabuf->attributes.height,
			.stride = dmabuf->attributes.stride[0],
			.format = dmabuf->attributes.format
		};

		for (i = 0; i < dmabuf->attributes.n_planes; i++) {
			if (dmabuf->attributes.modifier[i] == I915_FORMAT_MOD_Y_TILED_CCS ||
			    dmabuf->attributes.modifier[i] == I915_FORMAT_MOD_Yf_TILED_CCS) {
				resolve_needed = 1;
				break;
			}
		}

		bo = gbm_bo_import(backend->gbm, GBM_BO_IMPORT_FD,
				   &gbm_dmabuf, GBM_BO_USE_SCANOUT);
	} else {
		bo = gbm_bo_import(backend->gbm, GBM_BO_IMPORT_WL_BUFFER,
				   surface->buffer_ref.buffer->resource, GBM_BO_USE_SCANOUT);
	}

	if (!bo) {
		IAS_ERROR("Could not import surface buffer as GBM bo");
		return NULL;
	}

	format = gbm_bo_get_format(bo);

	/*
	 * Sprites should only get surfaces assigned once per frame.  We could
	 * handle this if we really wanted to (after releasing the GBM bo and
	 * DRM framebuffer), but most likely replacing a previous sprite
	 * assignment is likely a sign of programming error and it's better to
	 * reject it.
	 */
	wl_list_for_each(ias_sprite, &ias_crtc->sprite_list, link) {
		if (ias_sprite->type == DRM_PLANE_TYPE_OVERLAY ||
			(backend->use_cursor_as_uplane && ias_sprite->type == DRM_PLANE_TYPE_CURSOR)) {
			if (*sprite_id == 0) {
				if (ias_sprite->locked) continue;
				if (resolve_needed && !(backend->rbc_enabled &&
							ias_sprite->supports_rbc &&
							is_rbc_resolve_possible_on_sprite(0, format))) {
					continue;
				}
				sprite = ias_sprite;
				ias_sprite->locked = 1;
				break;
			} else if (ias_sprite->plane_id == *sprite_id) {
				if (resolve_needed && !(backend->rbc_enabled &&
							ias_sprite->supports_rbc &&
							is_rbc_resolve_possible_on_sprite(0, format))) {
					continue;
				}
				//if (ias_sprite->locked) continue;
				sprite = ias_sprite;
				ias_sprite->locked = 1;
				break;
			}
		}
	}

	if (!sprite) {
		IAS_ERROR("All sprites already has surface assigned");

		/*
		 * Sprite was not found, check if buffer was compressed as that could be 
		 * reason of not being able to use any of currently free sprites.
		 * Mark that DRI should resolve buffer for next frames
		 */
		if (resolve_needed) {
			weston_log("No RBC capable sprite available");
		}

		gbm_bo_destroy(bo);
		return NULL;
	}

	*sprite_id = sprite->plane_id;

	/* do not disable alpha channel for sprites,
	 * use scanout=false in ias_fb_get_from_bo()
	 */
	sprite->next = ias_fb_get_from_bo(bo, surface->buffer_ref.buffer,
					  ias_output, IAS_FB_OVERLAY);

	if (!sprite->next) {
		gbm_bo_destroy(bo);
		return NULL;
	}

	/* Get surface region into global coordinates */
	if (!view_region) {
		/*
		 * There was no surface region provided, so initialize src rectange
		 * to be of whole surface size.
		 */
		pixman_region32_init_rect(&src_rect, 0, 0,
				surface->width, surface->height);

		/*
		 * If custom sprite width and height was provided use it as size
		 * for dst rectangle, at the same time check if provided sprite
		 * width and height will require surface downscaling.
		 * If 0 was provided as sprite width or height use surface size
		 * as dst rectangle size instead.
		 */
		if (sprite_width != 0 && sprite_height != 0) {
			/* Check if downscaling */
			if (sprite_width < surface->width ||
			    sprite_height < surface->height) {
				downscaling = 1;
			}
			pixman_region32_init_rect(&dest_rect, x, y,
					sprite_width, sprite_height);
		} else {
			pixman_region32_init_rect(&dest_rect, x, y,
					surface->width, surface->height);
		}
	} else {
		/* There was surface subregion provided, so use it as src rectangle */
		pixman_region32_init(&src_rect);
		pixman_region32_copy(&src_rect, view_region);

		/*
		 * If custom sprite width and height was provided use it as size
		 * for dst rectangle, at the same time check if provided sprite
		 * width and heigh will require surface downscaling.
		 * If 0 was provided as sprite width or height use provided
		 * surface subregion size as dst rectangle size instead.
		 */
		if (sprite_width != 0 && sprite_height != 0) {
			sprite_extents = pixman_region32_extents(view_region);
			/* Check if downscaling */
			if (sprite_width < (sprite_extents->x2 - sprite_extents->x1) ||
			    sprite_height < (sprite_extents->y2 - sprite_extents->y1)) {
				downscaling = 1;
			}

			pixman_region32_init_rect(&dest_rect, x, y,
					sprite_width, sprite_height);
			pixman_region32_translate(&dest_rect,
					sprite_extents->x1, sprite_extents->y1);
		} else {
			pixman_region32_init(&dest_rect);
			pixman_region32_copy(&dest_rect, view_region);
			pixman_region32_translate(&dest_rect, x, y);
		}
	}

	/*
	 * Not supporting downscaling due to limited max downscale
	 * factor supported by HW scaler.
	 */
	if (downscaling) {
		IAS_ERROR("Sprite downscaling not supported");
		pixman_region32_fini(&src_rect);
		pixman_region32_fini(&dest_rect);
		sprite->next = NULL;
		gbm_bo_destroy(bo);
		return NULL;
	}

	/* Get the output region into global coordinates */
	pixman_region32_init(&crtc_rect);
	pixman_region32_copy(&crtc_rect, &output->region);
	pixman_region32_translate(&crtc_rect, -output->x, -output->y);

	if (ias_output->rotation) {
		crtc_rect.extents.x2 = output->current_mode->width;
		crtc_rect.extents.y2 = output->current_mode->height;
	}

	/*
	 * Make a copy of original dst rectange, so in case of scaling we
	 * will be able to check how it was cropped when doing intersect
	 * with crtc rectangle and then crop src rectangle by the same amount,
	 * otherwise surface on sprite will be stretched or squeezed instead
	 * of properly scaled.
	 */
	pixman_region32_init(&orig_dest_rect);
	pixman_region32_copy(&orig_dest_rect, &dest_rect);
	pixman_region32_intersect(&dest_rect, &crtc_rect,
					&dest_rect);

	sprite_extents = pixman_region32_extents(&dest_rect);

	sprite->plane.x = sprite_extents->x1;
	sprite->plane.y = sprite_extents->y1;
	sprite->dest_w = sprite_extents->x2 - sprite_extents->x1;
	sprite->dest_h = sprite_extents->y2 - sprite_extents->y1;

	/*
	 * Crop src rectangle by the same relative amount that dst
	 * rectange was cropped by crct rectangle.
	*/
	crop_rect_scaled(&src_rect, &orig_dest_rect, &dest_rect);

	sprite_extents = pixman_region32_extents(&src_rect);

	weston_view_from_global_fixed(view,
					wl_fixed_from_int(sprite_extents->x1),
					wl_fixed_from_int(sprite_extents->y1),
					&sx1, &sy1);

	weston_view_from_global_fixed(view,
					wl_fixed_from_int(sprite_extents->x2),
					wl_fixed_from_int(sprite_extents->y2),
					&sx2, &sy2);

	/* Make sure the sprite is fully visible on main display plane */

	if (sx1 < 0)
		sx1 = 0;
	if (sy1 < 0)
		sy1 = 0;
	if (sx2 > wl_fixed_from_int(output->current_mode->width))
		sx2 = wl_fixed_from_int(output->current_mode->width);
	if (sy2 > wl_fixed_from_int(output->current_mode->height))
		sy2 = wl_fixed_from_int(output->current_mode->height);

	/* The wl_fixed_from_int function converts the sprite plane extents into
	 * a 24.8 format; however, since the DRM driver expects the framebuffer
	 * and crtc coordinates to be in a 16.16 format, we convert the extents
	 * into a 16.16 format by shifting the values left by 8 bits.
	 */
	sprite->src_x = sx1 << 8;
	sprite->src_y = sy1 << 8;
	sprite->src_w = (sx2 - sx1) << 8;
	sprite->src_h = (sy2 - sy1) << 8;

	pixman_region32_fini(&src_rect);
	pixman_region32_fini(&dest_rect);
	pixman_region32_fini(&orig_dest_rect);
	pixman_region32_fini(&crtc_rect);

	/*
	 * HW scaler has limitation regarding minimal src/dst width and height
	 */
	if (sprite_width != 0 && sprite_height != 0 &&
		((sprite->src_w >> 16) < 10 || (sprite->src_h >> 16) < 10 ||
		 sprite->dest_w < 10 || sprite->dest_h < 10)) {
		IAS_ERROR("Sprite source or destination rectangle to small");
		sprite->next = NULL;
		gbm_bo_destroy(bo);
		return NULL;
	}

	/* Associate sprite with surface and save coordinates/region */
	sprite->view = view;
	view->plane = &sprite->plane;

	/*
	 * Reference buffer to prevent  weston_surface_attach() from
	 * releasing this buffer if a new buffer is attached to the client's
	 * surface. The buffer will be released by calling weston_buffer_reference()
	 * inside flip_handler_classic() once a new buffer is flipped.
	 */
	weston_buffer_reference(&sprite->next->buffer_ref, surface->buffer_ref.buffer);

	if (view->alpha != sprite->constant_alpha) {
		sprite->sprite_dirty |= SPRITE_DIRTY_BLENDING;
		sprite->constant_alpha = view->alpha;
	}

	return &ias_output->fb_plane;
}

static void
get_tex_info(struct weston_view *view,
		int *num,
		GLuint *names)
{
	int names_size, i;

	/* if they passed us num, get them the number of textures */
	if(num) {
		names_size = *num;
		*num = gl_renderer->get_num_textures(view->surface);

		/* if they also passed in names, give them either all of the
		 * texture names, or however many they asked for. Whichever
		 * is smaller. */
		if(names) {
			if(*num < names_size) {
				names_size = *num;
			}

			for(i = 0; i < names_size; i++)	{
				names[i] = gl_renderer->get_texture_name(view->surface, i);
			}
		}
	/* if they didn't pass in num, but they did pass in names, just
	 * give them the first name */
	} else if(names) {
		names[0] = gl_renderer->get_texture_name(view->surface, 0);
	} /* else nop */
}

static void
get_egl_image_info(struct weston_view *view,
		int *num,
		EGLImageKHR *names)
{
	int names_size, i;

	/* if they passed us num, get them the number of EGLImages */
	if(num) {
		names_size = *num;
		*num = gl_renderer->get_num_egl_images(view->surface);

		/* if they also passed in names, give them either all of the
		 * EGLImages, or however many they asked for. Whichever
		 * is smaller. */
		if(names) {
			if(*num < names_size) {
				names_size = *num;
			}

			for(i = 0; i < names_size; i++)	{
				names[i] = gl_renderer->get_egl_image_name(view->surface, i);
			}
		}
	/* if they didn't pass in num, but they did pass in names, just
	 * give them the first name */
	} else if(names) {
		names[0] = gl_renderer->get_egl_image_name(view->surface, 0);
	} /* else nop */
}

static void
set_viewport(int x, int y, int width, int height)
{
	gl_renderer->set_viewport(x, y, width, height);
}

/***
 *** Config parsing functions
 ***/

/*
 * backend_begin()
 *
 * Parses backend attributes from XML.
 */
void backend_begin(void *userdata, const char **attrs) {
	while (attrs[0]) {
		if (strcmp(attrs[0], "depth") == 0) {
			need_depth = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "stencil") == 0) {
			need_stencil = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "raw_keyboards") == 0) {
			use_xkbcommon = !(atoi(attrs[1]));
		} else if (strcmp(attrs[0], "normalized_rotation") == 0) {
			normalized_rotation = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "print_fps") == 0) {
			print_fps = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "use_nuclear_flip") == 0) {
			use_nuclear_flip = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "no_flip_event") == 0) {
			no_flip_event = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "no_color_correction") == 0 ) {
			no_color_correction = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "use_rbc") == 0) {
			use_rbc = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "rbc_debug") == 0) {
			rbc_debug = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "damage_outputs_on_init") == 0) {
			damage_outputs_on_init = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "vm") == 0) {
			vm_exec = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "vm_dbg") == 0) {
			vm_dbg = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "vm_unexport_delay") == 0) {
			vm_unexport_delay = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "vm_plugin_path") == 0) {
			strncpy(vm_plugin_path, attrs[1], 255);
		} else if (strcmp(attrs[0], "vm_plugin_args") == 0) {
			strncpy(vm_plugin_args, attrs[1], 255);
		} else if (strcmp(attrs[0], "use_cursor_as_uplane") == 0) {
			use_cursor_as_uplane = atoi(attrs[1]);
		} else if (strcmp(attrs[0], "vm_share_only") == 0) {
			vm_share_only = atoi(attrs[1]);
		}

		attrs += 2;
	}
}

/*
 * crtc_begin()
 *
 * Parses CRTC attributes from XML.
 */
void crtc_begin(void *userdata, const char **attrs)
{
	struct ias_configured_crtc *crtc;
	int x, y, refresh;

	cur_crtc = NULL;

	crtc = calloc(1, sizeof *crtc);
	if (!crtc) {
		IAS_ERROR("Failed to allocate crtc structure while parsing config");
		return;
	}

	while (attrs[0]) {
		if (strcmp(attrs[0], "name") == 0) {
			/* Avoid a klockworks memory leak warning */
			free(crtc->name);
			crtc->name = strdup(attrs[1]);
		} else if (strcmp(attrs[0], "mode") == 0) {
			/* How do we set the mode for this CRTC? */
			if (strcmp(attrs[1], "preferred") == 0) {
				crtc->config = CRTC_CONFIG_PREFERRED;
			} else if (strcmp(attrs[1], "current") == 0) {
				crtc->config = CRTC_CONFIG_CURRENT;
			} else if (sscanf(attrs[1], "%dx%d@%d", &x, &y, &refresh) == 3) {
				crtc->config = CRTC_CONFIG_MODE;
				crtc->width = x;
				crtc->height = y;
				crtc->refresh = refresh;
			} else if (sscanf(attrs[1], "%dx%d", &x, &y)) {
				crtc->config = CRTC_CONFIG_MODE;
				crtc->width = x;
				crtc->height = y;
				crtc->refresh = 0;
			} else {
				IAS_ERROR("Unknown CRTC mode config setting '%s'", attrs[1]);
			}
		} else if (strcmp(attrs[0], "model") == 0) {
			/* Is this CRTC a single display, a dualview display, or off? */
			free(crtc->model);
			crtc->model = strdup(attrs[1]);
		} else {
			IAS_ERROR("Unknown attribute '%s' to config element", attrs[0]);
		}

		attrs += 2;
	}

	if (!crtc->name) {
		IAS_ERROR("CRTC specified in config file with no name");
		free(crtc->model);
		free(crtc);
		return;
	}

	if (!crtc->model) {
		IAS_ERROR("No output model specified for CRTC");
		free(crtc->name);
		free(crtc);
		return;
	}

	cur_crtc = crtc;

	wl_list_insert(&configured_crtc_list, &crtc->link);
}

/*
 * output_begin()
 *
 * Parses output attributes from XML.
 */
void output_begin(void *userdata, const char **attrs)
{
	struct ias_configured_output *output;
	int x, y, r;
	int len;
	int extra = 0;

	static struct ias_configured_output *prev_output = NULL;

	if (cur_crtc == NULL)
		return;

	output = calloc(1, sizeof *output);
	if (!output) {
		IAS_ERROR("Failed to allocate output structure while parsing config");
		return;
	}

	if (cur_crtc->output_num >= MAX_OUTPUTS_PER_CRTC) {
		IAS_ERROR("Too many outputs defined for CRTC %s\n", cur_crtc->name);
		free(output);
		return;
	} else if (cur_crtc->output_num == MAX_OUTPUTS_PER_CRTC - 1 &&
			!use_cursor_as_uplane) {
		IAS_ERROR("Ignoring last configured output for CRTC %s\n", cur_crtc->name);
		free(output);
		return;
	}

	/* Associate output with CRTC */
	cur_crtc->output[cur_crtc->output_num] = output;

	while (attrs[0]) {
		if (strcmp(attrs[0], "name") == 0) {
			free(output->name);
			output->name = strdup(attrs[1]);
		} else if (strcmp(attrs[0], "size") == 0) {
			free(output->size);
			output->size = strdup(attrs[1]);
		} else if (strcmp(attrs[0], "position") == 0) {
			/* How do we position this output? */
			if (strcmp(attrs[1], "rightof") == 0) {
				output->position = OUTPUT_POSITION_RIGHTOF;
			} else if (strcmp(attrs[1], "below") == 0) {
				output->position = OUTPUT_POSITION_BELOW;
			} else if (strcmp(attrs[1], "origin") == 0) {
				output->position = OUTPUT_POSITION_ORIGIN;
				output->x = 0;
				output->y = 0;
			} else if (sscanf(attrs[1], "%d,%d", &x, &y)) {
				output->position = OUTPUT_POSITION_CUSTOM;
				output->x = x;
				output->y = y;
			} else {
				IAS_ERROR("Unknown output position setting '%s'", attrs[1]);
			}
		} else if (strcmp(attrs[0], "target") == 0) {
			free(output->position_target);
			output->position_target = strdup(attrs[1]);
		} else if (strcmp(attrs[0], "rotation") == 0) {
			if (sscanf(attrs[1], "%d", &r)) {
				output->rotation = r;
			}
		} else if (strcmp(attrs[0], "vm") == 0) {
			output->vm = atoi(attrs[1]);
		} else {
			/* Save any unknown output elements and pass to output model */
			extra += 2;
			output->attrs =
				realloc(output->attrs, ((extra + 1) * sizeof(char *)));

			if (output->attrs) {
				output->attrs[extra-2] = strdup(attrs[0]);
				output->attrs[extra-1] = strdup(attrs[1]);
				output->attrs[extra] = NULL;
			} else {
				IAS_ERROR("Allocation failed for extra output attributes.\n");
				exit(1);
			}
		}

		attrs += 2;
	}

	/*
	 * If no name was specified for this output, generate one for it.
	 */
	if (!output->name) {
		len = strlen(cur_crtc->name);

		output->name = calloc(1, len + 3);
		if (!output->name) {
			IAS_ERROR("Failed to allocate output name: out of memory");
			exit(1);
		}
		strcpy(output->name, cur_crtc->name);
		output->name[len] = '-';
		output->name[len+1] = cur_crtc->output_num;
	}

	cur_crtc->output_num++;

	/*
	 * If no position was set, assume that this output should be to the right
	 * of the previous output (or origin if this is the first output).
	 */
	if (output->position == OUTPUT_POSITION_UNDEFINED) {
		if (!prev_output) {
			output->position = OUTPUT_POSITION_ORIGIN;
		} else {
			output->position = OUTPUT_POSITION_RIGHTOF;
			output->position_target = strdup(prev_output->name);
		}
	}

	wl_list_insert(&configured_output_list, &output->link);
}

/*
 * input_begin()
 *
 * Parses input attributes from XML.
 */
void input_begin(void *userdata, const char **attrs)
{
	struct ias_configured_input *input;

	if (cur_crtc == NULL)
		return;

	input = calloc(1, sizeof *input);
	if (!input) {
		IAS_ERROR("Failed to allocate input structure while parsing config");
		return;
	}

	while (attrs[0]) {
		if (strcmp(attrs[0], "devnode") == 0) {
			/* Avoid a klockworks memory leak warning */
			free(input->devnode);
			input->devnode = strdup(attrs[1]);
		} else {
			IAS_ERROR("Unknown attribute '%s' to input element", attrs[0]);
		}

		attrs += 2;
	}

	/*
	 * If no name was specified for this input, ignore the tag
	 */
	if (!input->devnode) {
		IAS_ERROR("No input name specified");
		free(input);
		return;
	}

	free(input->devnode);
	free(input);
}

/*
 * env_begin()
 *
 * Parses env attributes from XML.
 */
void env_begin(void *userdata, const char **attrs)
{
	handle_env_common(attrs, &global_env_list);
}

/*
 * capture_begin()
 *
 * Parses env attributes from XML.
 */
void capture_begin(void *userdata, const char **attrs)
{
#ifdef BUILD_FRAME_CAPTURE
	/* Nothing to do here. */
#else
	weston_log("warning: frame capture options set in " CFG_FILENAME
			" but frame capture not compiled in\n");
#endif
}

/*
 * This function will determine and return the number of views on
 * this output excluding the cursor view.
 */
int num_views_on_output(struct weston_output *output)
{
	struct weston_view *ev, *next;
	struct weston_compositor *compositor = output->compositor;
	struct ias_output *ias_output = (struct ias_output *) output;
	int num_views = 0;

	wl_list_for_each_safe(ev, next, &compositor->view_list, link) {
		/* Make sure that the view is on this output */
		if (ev->output_mask == (1u << output->id) &&
				/* Make sure it's not a cursor */
				ias_output->ias_crtc->cursor_view != ev) {
			num_views++;
		}
	}
	return num_views;
}

/*
 * This function will determine if a surface's opaque region
 * covers the entire output. If it does, then the function
 * returns 1 else returns 0.
 */
int surface_covers_output(struct weston_surface *surface,
		struct weston_output *output)
{
	int ret;
	pixman_region32_t r;

	/* We can scanout an ARGB buffer if the surface's
	 * opaque region covers the whole output, but we have
	 * to use XRGB as the KMS format code. */
	pixman_region32_init_rect(&r, 0, 0,
				  output->width,
				  output->height);
	pixman_region32_subtract(&r, &r, &surface->opaque);

	ret = pixman_region32_not_empty(&r);
	pixman_region32_fini(&r);
	return !ret;
}

static void
config_init_to_defaults(struct weston_ias_backend_config *config)
{
}

/***
 *** Backend initial entrypoint
 ***/
WL_EXPORT int
weston_backend_init(struct weston_compositor *compositor,
		    struct weston_backend_config *config_base)
{
	int ret;
	struct ias_backend *b;
	struct weston_ias_backend_config config = {{0, }};

	TRACING_MODULE_INIT();

	if (config_base == NULL ||
	    config_base->struct_version != WESTON_IAS_BACKEND_CONFIG_VERSION ||
	    config_base->struct_size > sizeof(struct weston_ias_backend_config)) {
		weston_log("ias backend config structure is invalid\n");
		return -1;
	}

	config_init_to_defaults(&config);
	memcpy(&config, config_base, config_base->struct_size);

	wl_list_init(&configured_crtc_list);
	wl_list_init(&configured_output_list);
	wl_list_init(&global_env_list);

	ret = ias_read_configuration(CFG_FILENAME, backend_parse_data,
			sizeof(backend_parse_data) / sizeof(backend_parse_data[0]),
			NULL);
	if (ret) {
		IAS_ERROR("Failed to read configuration; bailing out");
		return 0;
	}

	b = ias_compositor_create(compositor, &config);
	if (b == NULL)
		return -1;

	return 0;
}

static int
components_list_create(struct ias_backend *backend,
		drmModeRes *resources,
		struct components_list *components_list)
{
	int i, stop_searching;
	char connector_name[32];
	const char *type_name;
	struct ias_configured_crtc *confcrtc;

	components_list->count_connectors = resources->count_connectors;
	components_list->num_conf_components = 0;
	wl_list_init(&components_list->list);

	/* Cache the connectors upfront to avoid calling drmModeGetConnector
	 * later.
	 */
	components_list->connector =
			(struct ias_connector **)calloc(resources->count_connectors,
					sizeof(struct ias_connector *));
	if (!components_list->connector) {
		IAS_ERROR("Cannot allocate memory for connector.");
		return -1;
	}

	for (i = 0; i < resources->count_connectors; i++) {
		components_list->connector[i] =
				calloc(1, sizeof *components_list->connector[i]);

		if (!components_list->connector[i]) {
			IAS_ERROR("Cannot allocate memory for connector.");

			for(; i >= 0; i--) {
				free(components_list->connector[i]);
			}

			free(components_list->connector);
			return -1;
		}

		TRACEPOINT("    * Before getting connector");
		components_list->connector[i]->connector =
				drmModeGetConnector(backend->drm.fd, resources->connectors[i]);
		TRACEPOINT("    * After getting connector");

		/* Do we recognize this connector type?  If not, it's unknown */
                if (components_list->connector[i]->connector->connector_type >=
                    ARRAY_LENGTH(connector_type_names)) {
                        type_name = "UNKNOWN";
                        IAS_DEBUG("Unrecognized KMS connector type %d",
                                        connector[i]->connector_type);
                } else {
                        type_name = connector_type_names[components_list->connector[i]->connector->connector_type];
                }

                /* Determine the name we should match against in the config file */
                snprintf(connector_name, sizeof(connector_name), "%s%d",
                                type_name,
                                components_list->connector[i]->connector->connector_type_id);

		/*
                 * Assuming we will find the connector this time around. The idea
                 * behind the following code is that the user knew what crtcs they
                 * wanted to be used with IAS as they provided the specific names
                 * via ias.conf. So why bother looking for extra connectors that are
                 * not going to be used by IAS anyways and will add extra time delay
                 * in its startup.
                 */
                stop_searching = 1;
                wl_list_for_each_reverse(confcrtc, &configured_crtc_list, link) {

                        if (strcmp(confcrtc->name, connector_name) == 0) {
                                confcrtc->found = 1;
                        }

                        stop_searching &= confcrtc->found;
                }

                if(stop_searching) {
                        resources->count_connectors = i+1;
                        break;
                }
	}

	return 0;
}

static void
components_list_destroy(struct components_list *components_list)
{
	struct connector_components *connector_components, *next;
	int i;

	for (i = 0; i < components_list->count_connectors; i++) {
		if (components_list->connector[i]) {
			drmModeFreeConnector(components_list->connector[i]->connector);
			free(components_list->connector[i]);
		}
	}

	free(components_list->connector);

	wl_list_for_each_safe(connector_components, next,
			&components_list->list, link) {
		wl_list_remove(&connector_components->link);
		free(connector_components);
	}

}

static int
components_list_add(struct components_list *components_list,
		char *connector_name,
		struct ias_configured_crtc *conf_crtc,
		struct ias_connector *connector)
{
	struct connector_components *components;
	components = malloc(sizeof(struct connector_components));

	if (components == NULL) {
		IAS_DEBUG("Cannot allocated memory to add components to list");
		return -1;
	}

	components->connector_name = connector_name;
	components->conf_crtc = conf_crtc;
	components->connector = connector;

	components_list->num_conf_components++;
	wl_list_insert(&components_list->list, &components->link);
	return 0;
}

static int
components_list_build(struct ias_backend *backend,
		drmModeRes *resources,
		struct components_list *components_list)
{
	struct ias_configured_crtc *confcrtc;
	struct ias_connector **connector;
	const char *type_name;
	char connector_name[32];
	int i;

	connector = components_list->connector;

	/*
	 * Loop over KMS connectors and create a CRTC for any that match CRTC's
	 * defined in the IAS config.
	 */
	backend->num_kms_crtcs = resources->count_crtcs;

	wl_list_for_each_reverse(confcrtc, &configured_crtc_list, link) {
		for(i = 0; i < resources->count_connectors; i++) {
			/* Skip the connector if NULL or currently in use */
			if (connector[i] == NULL || connector[i]->used) {
				continue;
			}

			/* Skip the connector if not connected to system */
			if (connector[i]->connector->connection != 1) {
				continue;
			}

			TRACEPOINT("    * Got connector");

			/* Do we recognize this connector type?  If not, it's unknown */
			if (connector[i]->connector->connector_type >= ARRAY_LENGTH(connector_type_names)) {
				type_name = "UNKNOWN";
				IAS_DEBUG("Unrecognized KMS connector type %d",
					connector[i]->connector->connector_type);
			} else {
				type_name = connector_type_names[connector[i]->connector->connector_type];
			}


			/* Determine the name we should match against in the config file */
			snprintf(connector_name, sizeof(connector_name), "%s%d",
					type_name,
					connector[i]->connector->connector_type_id);


			if (strcmp(confcrtc->name, connector_name) == 0) {
				IAS_DEBUG("Found match for connector %s", confcrtc->name);

				/* Check if CRTC already exists, no need to add copies */
				if (ias_crtc_find_by_name(backend, connector_name) == 0) {
					weston_log("CRTC matching connector %s exists \n",
							connector_name);
				} else if (components_list_add(components_list,
						connector_name,
						confcrtc,
						connector[i]) == 0) {
					connector[i]->used = 1;
					break;
				} else {
					return -1;
				}
			}
		}

		if (i == resources->count_connectors) {
			IAS_DEBUG("Could not find a connector for this CRTC.");
			continue;
		}
	}

	weston_log("Found %d matching sets of display components to build \n",
			components_list->num_conf_components);
	return 0;
}

static int
has_overlapping_outputs(struct ias_backend *backend)
{
	struct ias_crtc *ias_crtc,*ias_crtc2;
	pixman_region32_t overlap;
	int i,j,has_overlapping = 0;

	wl_list_for_each(ias_crtc, &backend->crtc_list, link) {
		wl_list_for_each(ias_crtc2, &backend->crtc_list, link) {
			if (ias_crtc == ias_crtc2)
				continue;
			for (i = 0; i < ias_crtc->num_outputs; i++) {
				for (j = 0; j < ias_crtc2->num_outputs; j++) {
					pixman_region32_init(&overlap);
					pixman_region32_intersect(&overlap,
							&ias_crtc->output[i]->base.region,
							&ias_crtc2->output[j]->base.region);
					if (pixman_region32_not_empty(&overlap)) {
						has_overlapping = 1;
					}
					pixman_region32_fini(&overlap);
					if (has_overlapping) {
						return 1;
					}
				}
			}
		}
	}
	return 0;
}

static int
ias_crtc_find_by_name(struct ias_backend *backend, char *requested_name)
{
	struct ias_crtc *ias_crtc = NULL;

	/* Compare elements in list with the string name value */
	wl_list_for_each(ias_crtc, &backend->crtc_list, link) {
		if (strcmp(ias_crtc->name, requested_name) == 0)
			return 0;
	}

	/* No element matching the sting name found */
	return -1;
}

static void
centre_pointer(struct ias_backend *backend)
{
	/*
	 * Position the mouse pointer in the middle of the first output.  We need
	 * to make sure that it's initially located in one of the outputs' bounding
	 * boxes, otherwise we'll crash in core weston as soon as it moves.
	 */
	struct weston_seat *w_seat;
	struct weston_output *w_output;
	struct ias_output *ias_output;

	/* Put pointers into the outputs they're bound to. */

	wl_list_for_each(w_seat, &backend->compositor->seat_list, link) {
		wl_list_for_each(w_output, &backend->compositor->output_list, link) {
			if (w_seat->output_mask & (1 << w_output->id)) {
				ias_output = (struct ias_output *)w_output;
				ias_move_pointer(backend->compositor, ias_output, ias_output->base.x, ias_output->base.y,
						ias_output->width, ias_output->height);
			}
		}
	}

	if(backend->input_present) {
		struct weston_pointer *pointer;
		wl_list_for_each(w_seat, &backend->compositor->seat_list, link) {
			wl_list_for_each(w_output, &backend->compositor->output_list, link) {
				if ((w_seat->output_mask & (1 << w_output->id))) {
					pointer = weston_seat_get_pointer(w_seat);
					if (pointer) {
						pointer->x = wl_fixed_from_int(w_output->x);
						pointer->y = wl_fixed_from_int(w_output->y);
					}
				}
			}
		}
	}

}

static void
ias_update_outputs(struct ias_backend *backend, struct udev_device *event)
{
	int ret;
	struct weston_view *curr_view, *next_view;

	/* Create any required CRTC's using the available resources */
	ret = create_crtcs(backend);

	if (ret <= 0) {
		weston_log("No new CRTCS created during hotplugging \n");
		return;
	} else {
		weston_log("Created %d new CRTCS during hotplugging \n", ret);
	}

	centre_pointer(backend);

	/* Dirty all of the compositor views to ensure that they get updated */
	wl_list_for_each_safe(curr_view, next_view,
			&backend->compositor->view_list, link) {
		weston_view_geometry_dirty(curr_view);
	}

}
