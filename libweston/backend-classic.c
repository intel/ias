/*
 *-----------------------------------------------------------------------------
 * Filename: backend-classic.c
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
 *   Backend functionality for handling 'classic' output functionality
 *   (i.e., one output per CRTC, sprite and cursor planes usable).
 *-----------------------------------------------------------------------------
 */

#include "ias-backend.h"
#include "ias-common.h"

#define DRM_ROTATE_0 (1 << 0)
#define DRM_ROTATE_90 (1 << 1)
#define DRM_ROTATE_180 (1 << 2)
#define DRM_ROTATE_270 (1 << 3)

extern struct gl_renderer_interface *gl_renderer;

struct classic_scanout {
	int in_use;
	struct gbm_surface *surface;
	struct ias_fb *current;
	struct ias_fb *next;
};

struct ias_classic_priv {
	struct classic_scanout scanout;
	struct classic_scanout scanout_bak;
};

enum plane_flip_state {
	FLIP_STATE_FLIPPED = 0,
	FLIP_STATE_UPDATED = 1,
	FLIP_STATE_PENDING = 2,
};


static void
init_classic(struct ias_crtc *ias_crtc);
static void
init_output_classic(struct ias_output *ias_output,
		struct ias_configured_output *cfg);
static int
generate_crtc_scanout_classic(struct ias_crtc *ias_crtc,
		struct ias_output *output, pixman_region32_t *damage);
static int
pre_render_classic(struct ias_output *output);
static void
post_render_classic(struct ias_output *output);
static void
disable_output_classic(struct ias_output *ias_output);
static void
enable_output_classic(struct ias_output *ias_output);
static int
allocate_scanout_classic(struct ias_crtc *ias_crtc, struct ias_mode *m);
static void
flip_handler_classic(struct ias_crtc *ias_crtc, unsigned int sec,
		unsigned int usec, uint32_t old_fb_id, uint32_t obj_id);
static int
set_mode_classic(struct ias_crtc *ias_crtc);
static void
set_next_fb_classic(struct ias_output *output, struct ias_fb *ias_fb);
static struct ias_fb*
get_next_fb_classic(struct ias_output *output);
static void
flip_classic(int drm_fd, struct ias_crtc *ias_crtc, int output_num);
static void
update_sprites_classic(struct ias_crtc *ias_crtc);
static uint32_t is_surface_flippable_classic(
			struct weston_view *view,
			struct weston_output *output,
			uint32_t check_xy);
static void
switch_mode_classic(struct ias_crtc *ias_crtc, struct ias_mode *m);
static void
update_primary_plane(struct ias_crtc *ias_crtc, struct classic_scanout *scanout);
extern void
ias_get_object_properties(int fd,
		struct ias_properties *drm_props,
		uint32_t obj_id, uint32_t obj_type,
		struct ias_sprite *sprite);

const struct ias_output_model output_model_classic = {
	.name = "classic",
	.outputs_per_crtc = 1,
	.can_client_flip = 1,
	.scanout_count = 1,
	.render_flipped = 0,
	.hw_cursor = 1,
	.sprites_are_usable = 1,
	.stereoscopic = 0,
	.init = init_classic,
	.init_output = init_output_classic,
	.generate_crtc_scanout = generate_crtc_scanout_classic,
	.pre_render = pre_render_classic,
	.post_render = post_render_classic,
	.disable_output = disable_output_classic,
	.enable_output = enable_output_classic,
	.allocate_scanout = allocate_scanout_classic,
	.flip_handler = flip_handler_classic,
	.set_mode = set_mode_classic,
	.set_next_fb = set_next_fb_classic,
	.get_next_fb = get_next_fb_classic,
	.flip = flip_classic,
	.update_sprites = update_sprites_classic,
	.is_surface_flippable = is_surface_flippable_classic,
	.switch_mode = switch_mode_classic,
};

static void
create_sprites_for_crtc(struct ias_crtc *ias_crtc)
{
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_sprite *sprite;
	drmModePlaneRes *plane_res;
	drmModePlane *plane;
	uint32_t i;
	int sprite_count = 0, ret, has_cursor = 0;
	uint32_t current_crtcs = 0;
	struct drm_i915_get_pipe_from_crtc_id pipeinfo;
	uint32_t have_first_overlay_plane = 0;

	/* plane_res = drmModeGetPlaneResources(backend->drm.fd); */
	plane_res = DRM_GET_PLANE_RESOURCES(backend, backend->drm.fd);
	if (!plane_res) {
		weston_log("failed to get plane resources: %s\n",
			strerror(errno));
		return;
	}

	memset(&pipeinfo, 0, sizeof(pipeinfo));
	pipeinfo.crtc_id = ias_crtc->crtc_id;

	ret = drmIoctl(backend->drm.fd, DRM_IOCTL_I915_GET_PIPE_FROM_CRTC_ID, &pipeinfo);
	if (ret != 0) {
		IAS_ERROR("Failed to get pipe id for crtc %d", ias_crtc->crtc_id);
		return;
	}

	for (i = 0; i < plane_res->count_planes; i++) {
		/* plane = drmModeGetPlane(backend->drm.fd, plane_res->planes[i]); */
		plane = DRM_GET_PLANE(backend, backend->drm.fd, plane_res->planes[i]);
		if (!plane)
			continue;

		sprite = malloc(sizeof(*sprite) + ((sizeof(uint32_t)) *
					plane->count_formats));
		if (!sprite) {
			weston_log("%s: out of memory\n",
				__func__);
			free(plane);
			continue;
		}

		memset(sprite, 0, sizeof *sprite);

		ias_get_object_properties(backend->drm.fd, &sprite->prop, plane->plane_id,
				DRM_MODE_OBJECT_PLANE, sprite);

		if (!(plane->possible_crtcs & (1 << ias_crtc->index))) {
			DRM_FREE_PLANE(backend, plane);
			free(sprite);

			continue;
		}

		if(sprite->type == DRM_PLANE_TYPE_CURSOR) {
			has_cursor = 1;
		}

		if (current_crtcs != plane->possible_crtcs)
		{
			sprite_count = 0;
			current_crtcs = plane->possible_crtcs;
		}

		weston_plane_init(&sprite->plane, backend->compositor, 0, 0);
		sprite->possible_crtcs = plane->possible_crtcs;
		sprite->plane_id = plane->plane_id;
		sprite->index = ++sprite_count;
		sprite->current = NULL;
		sprite->next = NULL;
		sprite->locked = 0;

		/*
		 * RBC is supported only on pipes A and B
		 */
		if (pipeinfo.pipe == 0 || pipeinfo.pipe == 1) {
			/*
			 * Only primary plane and first overlay plane are able to resolve color
			 */
			if (sprite->type == DRM_PLANE_TYPE_PRIMARY ||
					(sprite->type == DRM_PLANE_TYPE_OVERLAY && !have_first_overlay_plane)) {
				sprite->supports_rbc = 1;
				if (sprite->type == DRM_PLANE_TYPE_OVERLAY) {
					have_first_overlay_plane = 1;
				}
			}
		}

		/*
		 * Mark that sprite 1 will be used as main overlay plane for output
		 */
		if (sprite->index == 1) {
			sprite->output_id = CRTC_PLANE_MAIN + 1;
		}

		sprite->compositor = backend;
		sprite->count_formats = plane->count_formats;
		memcpy(sprite->formats, plane->formats,
				plane->count_formats * sizeof(plane->formats[0]));
		/*
		drmModeFreePlane(plane);
		*/
		DRM_FREE_PLANE(backend, plane);

		/*
		 * On VLV, sprites are fixed to CRTC's, so save a reference to the
		 * actual CRTC into the sprite.
		 */
		sprite->pipe_id = pipeinfo.pipe;

		/* possible_crtcs is a bit mask which is calculated by 1 << pipe */
		sprite->ias_crtc = ias_crtc;

		wl_list_insert(&ias_crtc->sprite_list, &sprite->link);
		ias_crtc->num_sprites++;
	}

	/*
	 * If we couldn't find any cursor planes, then we will consider cursors
	 * as broken
	 */
	if(!has_cursor) {
		ias_crtc->cursors_are_broken = 1;
	}

	free(plane_res->planes);
	free(plane_res);
}

static void
init_classic(struct ias_crtc *ias_crtc)
{
	struct ias_classic_priv *priv;

	if ((priv = calloc(1, sizeof(struct ias_classic_priv))) == NULL) {
		IAS_ERROR("Failed to allocate memory for output private data.");
		return;
	}
	ias_crtc->output_model_priv = priv;

	create_sprites_for_crtc(ias_crtc);
	ias_crtc->num_outputs = 1;
}


/*
 * init_output_classic()
 *
 * Initializes an output for a CRTC.  Since this is a classic setup, the
 * output height and height should match those of the CRTC.
 */
static void
init_output_classic(struct ias_output *ias_output,
		struct ias_configured_output *cfg)
{
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	char **attrs = cfg->attrs;
	int width, height;

	/* Check for classic model-specific output attributes */
	if (attrs) {
		while (attrs[0]) {
			if (strcmp(attrs[0], "transparent") == 0) {
				if ((sscanf(attrs[1], "%d",
							&ias_output->transparency_enabled)) != 1) {
					IAS_ERROR("Badly formed transparent element: %s\n",
							attrs[1]);
				}
			} else {
				IAS_ERROR("Unknown attribute '%s' in output element", attrs[0]);
			}
			attrs += 2;
		}
	}
	free(cfg->attrs);

	/* Only sizing options for classic outputs are 'inherit' and 'scale' */
	if (!cfg->size || !strcmp(cfg->size, "inherit")) {
		ias_output->width = ias_crtc->current_mode->mode_info.hdisplay;
		ias_output->height = ias_crtc->current_mode->mode_info.vdisplay;
	} else if (sscanf(cfg->size, "scale:%dx%d", &width, &height)) {
		ias_output->width = width;
		ias_output->height = height;
	} else {
		IAS_ERROR("Unknown size setting '%s' for classic model", cfg->size);
	}
	free(cfg->size);

	ias_output->scanout = CRTC_PLANE_MAIN + 1;
}

/*
 * generate_crtc_scanout_classic()
 *
 * Generate the scanout buffer for a single output per crtc setup.  Since
 * this is the "simple" case, we actually do more than just generate a
 * scanout here; the cursor plane is usable in this mode, so we'll also
 * stick a surface that looks like a cursor onto the cursor plane if
 * appropriate.  If we decide to re-add the heuristics for flipping
 * surfaces onto sprite planes, that would also happen in this function.
 * Also note that this is the one configuration where we can flip directly
 * to full-screen client buffers, so there's a chance to completely bypass
 * having to render a scanout.
 */
static int
generate_crtc_scanout_classic(struct ias_crtc *ias_crtc,
		struct ias_output *output,
		pixman_region32_t *damage)
{
	struct gbm_bo *bo;
	struct weston_plane *primary_plane = &ias_crtc->backend->compositor->primary_plane;
	struct ias_classic_priv *priv =
		(struct ias_classic_priv *)ias_crtc->output_model_priv;
	struct classic_scanout *scanout = &priv->scanout;
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_sprite *ias_sprite;

	/*
	 * If we don't have a client buffer already queued up for flipping,
	 * generate the composited scanout.
	 */
	if (!scanout->next) {
		ias_output_render(ias_crtc->output[0], damage);
		pixman_region32_subtract(&primary_plane->damage,
				&primary_plane->damage, damage);

		if(!ias_crtc->output[0]->scanout_surface) {
			/* Find sprite that will be used for that scanout */
			wl_list_for_each(ias_sprite, &ias_crtc->sprite_list, link) {
				if ((backend->use_cursor_as_uplane || ias_sprite->type != DRM_PLANE_TYPE_CURSOR) &&
						(uint32_t)ias_sprite->output_id == output->scanout) {
					break;
				}
			}

			bo = gbm_surface_lock_front_buffer(scanout->surface);
			if (!bo) {
				IAS_ERROR("failed to lock front buffer: %m");
				return 0;
			}

			scanout->next = ias_fb_get_from_bo(bo, NULL, ias_crtc->output[0],
							   IAS_FB_SCANOUT);

			if (!scanout->next) {
				IAS_ERROR("failed to get ias_fb for bo");
				gbm_surface_release_buffer(scanout->surface, bo);
				return 0;
			}

#if defined(BUILD_VAAPI_RECORDER) || defined(BUILD_FRAME_CAPTURE)
			wl_signal_emit(&output->next_scanout_ready_signal, output);
#endif
		}
	}

	/* If there isn't a buffer ready to be flipped to at this point, abort. */
	if (!scanout->next) {
		IAS_ERROR("Failed to generate scanout contents");
		return 0;
	}


	return 1;
}

/*
 * pre_render_classic()
 *
 * Prepare to render a classic output.  We need to make current with the
 * proper EGL surface.
 */
static int
pre_render_classic(struct ias_output *output)
{
	if (gl_renderer->use_output(&output->base) < 0) {
		IAS_ERROR("failed to make current");
		return -1;
	}

	return 0;
}

/*
 * post_render_classic()
 *
 * Complete rendering to a classic output.  Classic outputs are EGL window
 * system surfaces, we just need to call eglSwapBuffers().
 */
static void
post_render_classic(struct ias_output *output)
{
	/*
	 * output->scanout_surface will not be NULL in the only circumstance where
	 * a plugin decided that this surface is flippable. In such a circumstance
	 * we should avoid eglSwapBuffers which this post_render will call because
	 * not just is it redundant if we have already selected a client's buffer
	 * to flip directly on a HW plane, but due to recent mesa changes, which
	 * expect eglSwapBuffers to only be called when the back buffer info has
	 * been filled which will only happen with a call to GL function like
	 * glClear. This function will not be called in the plugin if it decides
	 * to flip a surface and calling eglSwapBuffers in such a scenario will
	 * result in a seg fault.
	 */
	if(!output->scanout_surface) {
		gl_renderer->swap_output_buffers(&output->base);
	}
}

/*
 * disable_output_classic()
 *
 * Disables a classic output.  Since we have a 1:1 mapping out outputs to
 * CRTC's, we can just use DPMS to turn off the display.
 */
static void
disable_output_classic(struct ias_output *ias_output)
{
	ias_output->disabled = 1;
	ias_set_dpms(ias_output->ias_crtc, WESTON_DPMS_OFF);
}

/*
 * enable_output_classic()
 *
 * Enables a classic output.  We just need to turn DPMS back on.
 */
static void
enable_output_classic(struct ias_output *ias_output)
{
	ias_output->disabled = 0;
	weston_output_damage(&ias_output->base);
	ias_set_dpms(ias_output->ias_crtc, WESTON_DPMS_ON);
}

/*
 * allocate_scanout_classic()
 *
 * Allocate the scanout buffer for the classic display mode. Only
 * one scanout buffer per crtc.
 */
static int
allocate_scanout_classic(struct ias_crtc *ias_crtc, struct ias_mode *m)
{
	struct ias_classic_priv *priv =
		(struct ias_classic_priv *)ias_crtc->output_model_priv;
	struct classic_scanout *scanout = &priv->scanout;
	EGLint format = ias_crtc->backend->format;
	struct gbm_surface *old_surf = scanout->surface;
	int32_t old_width = ias_crtc->output[0]->width;
	int32_t old_height = ias_crtc->output[0]->height;
	uint32_t surface_flags;
	int use_vm = 0;

	/*
	 * The right sequence should have the old output destroyed first, then
	 * create the new output. The previous code deosn't guarantee the right
	 * sequence. When the sequence is reversed, the newly created output is
	 * destroyed and it causes blank screen.
	 *
	 * The new code changes the sequence to destroy the old output first,
	 * then create the new output. It also adds the logic to revert to the
	 * old display mode if it fails to create the new output.
	 */
	if (scanout->in_use) {
		gl_renderer->output_destroy(&ias_crtc->output[0]->base);

		if (scanout->current) {
			if (scanout->current->is_client_buffer) {
				gbm_bo_destroy(scanout->current->bo);
			} else {
				gbm_surface_release_buffer(
					scanout->surface,
					scanout->current->bo);
			}

			scanout->current = NULL;
		}

		if (scanout->next) {
			if (scanout->next->is_client_buffer) {
				gbm_bo_destroy(scanout->next->bo);
			} else {
				gbm_surface_release_buffer(
					scanout->surface,
					scanout->next->bo);
			}

			scanout->next = NULL;
		}
	}


	/*
	 * For a mode set to succeed, the main plane must be equal to
	 * or larger than the timing horizontal/vertical dimensions.
	 *
	 * There may be some issues with corner cases involving a scaled
	 * main plane.
	 */
	ias_crtc->output[0]->width = m->base.width;
	ias_crtc->output[0]->height = m->base.height;

	surface_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;

	if (ias_crtc->output[0]->vm)
		surface_flags |= GBM_BO_USE_LINEAR;

	scanout->surface = gbm_surface_create(ias_crtc->backend->gbm,
			m->base.width, m->base.height,
			GBM_FORMAT_ARGB8888,
			surface_flags);

	if (!scanout->surface) {
		IAS_ERROR("Failed to create scanout for CRTC");
		goto try_revert_mode;
	}
#ifdef USE_VM
	use_vm = gl_renderer->vm_exec;
#endif

	if (gl_renderer->output_window_create(&ias_crtc->output[0]->base,
					      (EGLNativeWindowType)scanout->surface,
					      scanout->surface,
					      use_vm
					      ? gl_renderer->alpha_attribs
					      : gl_renderer->opaque_attribs,
					      &format, 1) < 0) {
		weston_log("failed to create gl renderer output state\n");
		gbm_surface_destroy(scanout->surface);
		goto try_revert_mode;
	}

	if (old_surf) {
		gbm_surface_destroy(old_surf);
	}

	scanout->current = NULL;
	scanout->next = NULL;
	scanout->in_use = 1;

	if (ias_crtc->output[0]) {
		/*
		 * Mark CRTC's output as dirty, update output mode, and associate
		 * EGL surface with CRTC scanout.
		 */
		ias_crtc->output[0]->base.current_mode = &m->base;
		ias_crtc->output[0]->base.dirty = 1;
		weston_output_move(&ias_crtc->output[0]->base,
				ias_crtc->output[0]->base.x,
				ias_crtc->output[0]->base.y);
	}

	return 0;

try_revert_mode:
	if (old_surf) {
		scanout->surface = old_surf;
		ias_crtc->output[0]->width = old_width;
		ias_crtc->output[0]->height = old_height;

		if (gl_renderer->output_window_create(&ias_crtc->output[0]->base,
						      (EGLNativeWindowType)scanout->surface,
						      scanout->surface,
						      use_vm
						      ? gl_renderer->alpha_attribs
						      : gl_renderer->opaque_attribs,
						      &format, 1) == 0) {

			ias_crtc->output[0]->base.dirty = 1;
			weston_output_move(&ias_crtc->output[0]->base,
					ias_crtc->output[0]->base.x,
					ias_crtc->output[0]->base.y);

			ias_crtc->current_mode->base.flags |= WL_OUTPUT_MODE_CURRENT;
			ias_crtc->request_set_mode = 1;

			weston_compositor_damage_all(ias_crtc->backend->compositor);
			scanout->in_use = 1;
		}
		else {
			weston_log("failed to create gl renderer output state for previous mode\n");
			gbm_surface_destroy(old_surf);
			scanout->surface = NULL;
			scanout->in_use = 0;
		}
	}

	return -1;
}


static void
flip_handler_classic(struct ias_crtc *ias_crtc,
		unsigned int sec,
		unsigned int usec,
		uint32_t old_fb_id,
		uint32_t obj_id)
{
	struct ias_classic_priv *priv =
		(struct ias_classic_priv *)ias_crtc->output_model_priv;
	struct classic_scanout *scanout = &priv->scanout;
	struct ias_sprite *s;
	struct timespec ts;
	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
			WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION |
			WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

	/* If obj_id == 0, then legacy pageflip code is being used */
	if ((obj_id == ias_crtc->crtc_id) || (obj_id == 0)) {
		if (scanout->current) {
			if (scanout->current->is_client_buffer) {
				gbm_bo_destroy(scanout->current->bo);
			} else {
				gbm_surface_release_buffer(
						scanout->surface,
						scanout->current->bo);
			}
		}

		scanout->current = scanout->next;
		scanout->next = NULL;
		ias_crtc->page_flip_pending = FLIP_STATE_FLIPPED;

		wl_list_for_each(s, &ias_crtc->sprite_list, link) {
			if (s->page_flip_pending == FLIP_STATE_PENDING || s->current) {
				if(s->current) {
					gbm_bo_destroy(s->current->bo);
				}

				s->current = s->next;
				s->next = NULL;

				s->page_flip_pending = FLIP_STATE_FLIPPED;
			}
			s->locked = 0;
		}

		/* If color correction was updated, free blob with LUT table */
		if (ias_crtc->color_correction_blob_id) {
			drmModeDestroyPropertyBlob(ias_crtc->backend->drm.fd, ias_crtc->color_correction_blob_id);
			ias_crtc->color_correction_blob_id = 0;
		}

		wl_signal_emit(&ias_crtc->output[0]->printfps_signal, ias_crtc->output[0]);

		if (ias_crtc->backend->no_flip_event && ias_crtc->backend->has_nuclear_pageflip) {
			ias_crtc->output[0]->base.repaint_status = REPAINT_NOT_SCHEDULED;
		} else {
			ts.tv_sec = sec;
			ts.tv_nsec = usec * 1000;
			weston_output_finish_frame(&ias_crtc->output[0]->base, &ts, flags);
		}
	}

	/*
	 * Right now, this is ignoring the sprite plane events. If we try
	 * to acknowledge them sperate from the main plane events, there
	 * is a race condition that leads to a dead lock.
	 *
	 * The above code assumes that the sprites have (or will have) flipped
	 * and the old framebuffer can be removed.
	 */
}

/* For sprite rotation to be permitted we must make sure that the surface
 * that is being rotated is Y-Tiled, otherwise we cannot do this in hardware
 */
static bool
check_rotation_permitted(struct ias_backend *backend,
				struct ias_sprite *sprite)
{
	struct drm_i915_gem_get_tiling param;
	struct gbm_bo *bo = NULL;
	struct weston_buffer *buffer = NULL;
	uint32_t handle;

	if (sprite->view) {
		buffer = sprite->view->surface->buffer_ref.buffer;
	} else {
		//We have no view yet so lets ignore this request
		return 0;
	}

	if (buffer) {
		bo = gbm_bo_import(backend->gbm, GBM_BO_IMPORT_WL_BUFFER,
					buffer->resource, GBM_BO_USE_SCANOUT);
	} else {
		IAS_ERROR("Cannot verify tiling mode\n");
		return 0;
	}

	if (!bo) {
		IAS_ERROR("Cannot verify tiling mode\n");
		return 0;
	}

	handle = gbm_bo_get_handle(bo).u32;

	memset(&param, 0, sizeof(param));
	param.handle = handle;
	int ret = drmIoctl(backend->drm.fd, DRM_IOCTL_I915_GEM_GET_TILING,
								&param);
	if (ret != 0) {
		IAS_ERROR("Cannot get tiling of bo(%u) error %d/%s\n",
					handle, ret, strerror(errno));
		return 0;
	}

	gbm_bo_destroy(bo);

	if (param.tiling_mode == I915_TILING_Y) {
		return 1;
	} else {
		return 0;
	}
}

/*
 * flip
 *
 * If there's a scanout buffer ready to be flipped, schedule it.
 */
static void
flip_classic(int drm_fd, struct ias_crtc *ias_crtc, int output_num)
{
	struct ias_classic_priv *priv =
		(struct ias_classic_priv *)ias_crtc->output_model_priv;
	struct classic_scanout *scanout = &priv->scanout;
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_sprite *s;
	uint32_t flags = 0;
	struct timespec ts;
	int ret;

	if (scanout->next) {
		if (ias_crtc->request_set_mode) {
			flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
			set_mode_classic(ias_crtc);
		}

		if (backend->has_nuclear_pageflip && ias_crtc->prop_set) {
			if (!ias_crtc->backend->no_flip_event) {
				flags |= (DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT);
			}

			/* Check if any sprite planes are ready */
			wl_list_for_each(s, &ias_crtc->sprite_list, link) {
				if ((s->type == DRM_PLANE_TYPE_OVERLAY ||
					(s->type == DRM_PLANE_TYPE_CURSOR && backend->use_cursor_as_uplane)) &&
						s->page_flip_pending == FLIP_STATE_UPDATED) {
					s->page_flip_pending = FLIP_STATE_PENDING;
				}
			}

			update_primary_plane(ias_crtc, scanout);
			ret = drmModeAtomicCommit(drm_fd, ias_crtc->prop_set, flags, ias_crtc);
			if (ret) {
				IAS_ERROR("Queueing atomic pageflip failed: %m");
				return;
			}

			/* Free and re-allocate the property set so it's always clean */
			drmModeAtomicFree(ias_crtc->prop_set);
			ias_crtc->prop_set = drmModeAtomicAlloc();

			if (ias_crtc->backend->no_flip_event) {
				clock_gettime(ias_crtc->backend->clock, &ts);
				flip_handler_classic(ias_crtc, ts.tv_sec, ts.tv_nsec/1000, 0, 0);
			}

		} else {

			if (drmModePageFlip(drm_fd, ias_crtc->crtc_id,
						scanout->next->fb_id,
						DRM_MODE_PAGE_FLIP_EVENT, ias_crtc) < 0) {
				IAS_ERROR("Queueing pageflip failed: %m");
				IAS_ERROR("Clients on this output will stop updating.");
				return;
			}
		}

		ias_crtc->page_flip_pending = FLIP_STATE_PENDING;
	}
}

/*
 * is_surface_flippable_classic()
 *
 * Returns 1 if a surface could potentially be flippable or 0 if it is not
 */
static uint32_t is_surface_flippable_classic(
			struct weston_view *view,
			struct weston_output *output,
			uint32_t check_xy)
{
	struct ias_crtc *ias_crtc = ((struct ias_output *) output)->ias_crtc;
	struct weston_surface *surface = view->surface;

	/*
	 * Make sure the surface matches the output dimensions exactly and isn't
	 * transformed in some way.  We also can't scanout of SHM buffers directly
	 * so reject those as well.
	 *
	 * Technically this could be improved to scanout of buffers that are
	 * bigger than the output dimension; we may want to revisit that case in
	 * the future if it seems important.
	 */
	if (
			/* Surface does not share upper left coord with output */
			(check_xy &&
			((int)view->geometry.x != output->x ||
			(int)view->geometry.y != output->y)) ||

			/* Surface is not the same size as the output */
			surface->width != output->current_mode->width ||
			surface->height != output->current_mode->height ||

			/* The output is scaled from CRTC size */
			ias_crtc->current_mode->base.width != output->width ||
			ias_crtc->current_mode->base.height != output->height ||

			surface->buffer_ref.buffer == NULL ||

			/* SHM buffers are unflippable */
			wl_shm_buffer_get(surface->buffer_ref.buffer->resource) ||

			/* The surface has transformations or non-standard shaders */
			view->transform.enabled ||
			(!surface->compositor->renderer->is_shader_of_type(
					surface, WL_SHM_FORMAT_XRGB8888) &&
			 !surface->compositor->renderer->is_shader_of_type(
										surface, WL_SHM_FORMAT_ARGB8888)) ||
			/*
			 * If it is ARGB surface, then in order for it to be flipped,
			 * it should either be the only one on this output OR its opaque
			 * region is equal to the output which will be the case if the
			 * application set it using wl_surface_set_opaque_region
			 */
			(surface->compositor->renderer->is_shader_of_type(
										surface, WL_SHM_FORMAT_ARGB8888) &&
			!surface_covers_output(surface, output) &&
			num_views_on_output(output) != 1)) {
		return 0;
	}

	return 1;
}

static int
set_mode_classic(struct ias_crtc *ias_crtc)
{
	int fb = 0;
	int ret;
	struct ias_classic_priv *priv =
		(struct ias_classic_priv *)ias_crtc->output_model_priv;
	struct classic_scanout *scanout = &priv->scanout;
	struct ias_backend *backend = ias_crtc->backend;
	drmModeCrtcPtr current_crtc_mode;
	drmModeModeInfo *mode = &ias_crtc->current_mode->mode_info;
	uint32_t mode_id;

	/* Make sure a valid framebuffer is ready to be set */
	if (!scanout->next) {
		return 0;
	}

	fb = scanout->next->fb_id;

	/*
	 * If mode was already set (eg. by i915 due to splash screen)
	 * and it is the same as requested one, just skip mode set call
	 */
	current_crtc_mode = drmModeGetCrtc(backend->drm.fd, ias_crtc->crtc_id);
	if (!current_crtc_mode) {
		IAS_ERROR("Cannot check mode of crtc\n");
		return -1;
	}

	if (current_crtc_mode->mode_valid &&
		(uint32_t)current_crtc_mode->width == (uint32_t)ias_crtc->current_mode->base.width &&
		(uint32_t)current_crtc_mode->height == (uint32_t)ias_crtc->current_mode->base.height &&
		current_crtc_mode->mode.vrefresh == ias_crtc->current_mode->mode_info.vrefresh) {
		ias_crtc->request_set_mode = 0;
	}

	drmModeFreeCrtc(current_crtc_mode);

	if (ias_crtc->request_set_mode) {
		if (backend->has_nuclear_pageflip && ias_crtc->prop_set) {
			assert (drmModeCreatePropertyBlob(backend->drm.fd, mode,
				sizeof(*mode), &mode_id) == 0);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
						ias_crtc->crtc_id,
						ias_crtc->prop.mode_id,
						mode_id);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
						ias_crtc->crtc_id,
						ias_crtc->prop.active,
						1);

			add_connector_id(ias_crtc);
		} else {
			ret = drmModeSetCrtc(backend->drm.fd, ias_crtc->crtc_id, fb, 0, 0,
				       &ias_crtc->connector_id, 1, &ias_crtc->current_mode->mode_info);
		        if (ret) {
			        IAS_ERROR("Set mode of %dx%d @ %uHz failed: %m",
					        ias_crtc->current_mode->base.width,
					        ias_crtc->current_mode->base.height,
					        ias_crtc->current_mode->base.refresh / 1000);
			        return -1;
		        }
		}

		ias_crtc->request_set_mode = 0;
	}

	return 0;
}

/*
 * set_next_fb assigns a ias_fb structure that was derrived from a
 * client's surface as the next scanout buffer to flip to.  For the
 * classic model, this simply sets the next scanout to the incoming
 * ias_fb.
 */
static void
set_next_fb_classic(struct ias_output *output, struct ias_fb *ias_fb)
{
	struct ias_classic_priv *priv =
		(struct ias_classic_priv *)ias_fb->output->ias_crtc->output_model_priv;
	struct classic_scanout *scanout = &priv->scanout;

	scanout->next = ias_fb;
}


static struct ias_fb*
get_next_fb_classic(struct ias_output *output)
{
	struct ias_crtc *ias_crtc = output->ias_crtc;
	struct ias_classic_priv *priv =
		(struct ias_classic_priv *)ias_crtc->output_model_priv;
	struct classic_scanout *scanout = &priv->scanout;

	return scanout->next;
}

static void
update_primary_plane(struct ias_crtc *ias_crtc, struct classic_scanout *scanout)
{
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_sprite *s;

	wl_list_for_each(s, &ias_crtc->sprite_list, link) {
		if (s->type == DRM_PLANE_TYPE_PRIMARY) {
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.crtc_x, 0);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.crtc_y, 0);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.crtc_w, ias_crtc->output[0]->width);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.crtc_h, ias_crtc->output[0]->height);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.src_x, 0);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.src_y, 0);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.src_w, ias_crtc->output[0]->width << 16);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.src_h, ias_crtc->output[0]->height << 16);


			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id,
					s->prop.crtc_id,
					ias_crtc->crtc_id);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id,
					s->prop.fb_id,
					scanout->next->fb_id);


			if (backend->rbc_supported && backend->rbc_debug) {
				weston_log("[RBC] commiting scanout, compression enabled = %d\n",
					   scanout->next->is_compressed);
			}

			if (ias_crtc->color_correction_blob_id) {
				drmModeAtomicAddProperty(ias_crtc->prop_set,
							ias_crtc->crtc_id,
							ias_crtc->prop.gamma_lut,
							ias_crtc->color_correction_blob_id);
			}
			break;
		}
	}
}

static void
update_sprites_classic(struct ias_crtc *ias_crtc)
{
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_sprite *s;
	struct ias_output *ias_output = ias_crtc->output[0];

	wl_list_for_each(s, &ias_crtc->sprite_list, link) {

		/* Make sure this is a sprite for the current CRTC */
		if (s->type == DRM_PLANE_TYPE_PRIMARY ||
			(s->type == DRM_PLANE_TYPE_CURSOR && !backend->use_cursor_as_uplane)) {
			continue;
		}

		/* Does this sprite have a surface assigned for this frame? */
		if (s->next && s->next->fb_id && !s->page_flip_pending) {
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.crtc_x, s->plane.x);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.crtc_y, s->plane.y);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.crtc_w, s->dest_w);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.crtc_h, s->dest_h);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.src_x, s->src_x);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.src_y, s->src_y);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.src_w, s->src_w);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id, s->prop.src_h, s->src_h);


			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id,
					s->prop.crtc_id,
					ias_crtc->crtc_id);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id,
					s->prop.fb_id,
					s->next->fb_id);

			if (backend->rbc_supported && backend->rbc_debug) {
				weston_log("Flipping sprite, compressed = %d\n",
					   s->next->is_compressed);
			}

			/* Is there a race condition here? */
			s->page_flip_pending = FLIP_STATE_UPDATED;

			/*
			 * Based on zorder, if it is above the plane, set it
			 * If below plane, check any
			 */
			if (s->sprite_dirty) {

				if (s->sprite_dirty & SPRITE_DIRTY_ZORDER) {
					IAS_ERROR("Failed to change sprite zorder property - not supported");
				}

				if (s->sprite_dirty & SPRITE_DIRTY_BLENDING) {
					if (s->blending_enabled) {
						drmModeAtomicAddProperty(ias_crtc->prop_set,
								s->plane_id,
								s->prop.pixel_blend_mode,
								s->pixel_blend_mode);

						drmModeAtomicAddProperty(ias_crtc->prop_set,
								s->plane_id,
								s->prop.alpha,
								s->constant_alpha * 0xFFFF);
					} else {
						drmModeAtomicAddProperty(ias_crtc->prop_set,
								s->plane_id,
								s->prop.pixel_blend_mode,
								DRM_MODE_BLEND_PIXEL_NONE);

						drmModeAtomicAddProperty(ias_crtc->prop_set,
								s->plane_id,
								s->prop.alpha,
								0xFFFF);
					}
				}

				s->sprite_dirty = 0;
			}

			if (ias_output->rotation &&
				check_rotation_permitted(backend, s)) {
				uint64_t rotation = ias_output->rotation;

				/* We need to switch rotation for 90/270 */
				switch (rotation) {
				case WL_OUTPUT_TRANSFORM_NORMAL:
				case WL_OUTPUT_TRANSFORM_FLIPPED_180:
					rotation = DRM_ROTATE_0;
					break;
				case WL_OUTPUT_TRANSFORM_90:
				case WL_OUTPUT_TRANSFORM_FLIPPED_270:
					rotation = DRM_ROTATE_270;
					break;
				case WL_OUTPUT_TRANSFORM_180:
				case WL_OUTPUT_TRANSFORM_FLIPPED:
					rotation = DRM_ROTATE_180;
					break;
				case WL_OUTPUT_TRANSFORM_270:
				case WL_OUTPUT_TRANSFORM_FLIPPED_90:
					rotation = DRM_ROTATE_90;
					break;
				}
				drmModeAtomicAddProperty(ias_crtc->prop_set,
						s->plane_id,
						s->prop.rotation,
						rotation);
			}
		} else if (!s->locked) {
			/*
			 * If a surface was not assigned to this sprite, disable it.
			 */
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id,
					s->prop.fb_id,
					0);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					s->plane_id,
					s->prop.crtc_id,
					0);

			s->locked = 0;
		}
	}
}

static void
switch_mode_classic(struct ias_crtc *ias_crtc, struct ias_mode *m)
{
	struct ias_output *ias_output = ias_crtc->output[0];

	ias_output_scale(ias_output, m->base.width, m->base.height);
}
