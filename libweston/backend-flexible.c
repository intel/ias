/*
 *-----------------------------------------------------------------------------
 * Filename: backend-flexible.c
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
 *   Backend functionality for handling custom output configurations. There
 *   are three planes, main, sprite 1, sprite 2.  Each plane is connected
 *   to it's own output and positioned indpendently of each other.
 *-----------------------------------------------------------------------------
 */

#include "config.h"
#include "ias-backend.h"
#include "ias-common.h"

#define PLANE_0     0

#define ALIGN(x, y) (((x + y) - 1) & ~(y - 1))
#undef WORKAROUND_UFO_STRIDE

extern struct gl_renderer_interface *gl_renderer;

struct flexible_scanout {
	int in_use;
	struct gbm_surface *surface;
	struct ias_fb *current;
	struct ias_fb *next;
};

struct flexible_plane_geometry {
	int x;
	int y;
	int width;
	int height;
};

struct ias_flexible_priv {
	struct flexible_scanout scanout[MAX_OUTPUTS_PER_CRTC];
	struct flexible_scanout scanout_save[MAX_OUTPUTS_PER_CRTC];
	struct flexible_plane_geometry plane_geometry[MAX_OUTPUTS_PER_CRTC];
	int drm_fd;
	int rp_count;    /* Number of output repaint calls processed */
	int rp_needed;   /* Number of outputs that need repainting */
	int in_handler;  /* currently handling a flip event */
	int pending;     /* Which planes are pending a commit */
	int commited;    /* Which planes have been commited, awaiting complete */
};

static void
init_flexible(struct ias_crtc *ias_crtc);
static void
init_output_flexible(struct ias_output *ias_output,
		struct ias_configured_output *cfg);
static int
generate_crtc_scanout_flexible(struct ias_crtc *ias_crtc,
	struct ias_output *output, pixman_region32_t *damage);
static int
pre_render_flexible(struct ias_output *output);
static void
post_render_flexible(struct ias_output *output);
static void
disable_output_flexible(struct ias_output *ias_output);
static void
enable_output_flexible(struct ias_output *ias_output);
static int
allocate_scanout_flexible(struct ias_crtc *ias_crtc, struct ias_mode *m);
static void
flip_handler_flexible(struct ias_crtc *ias_crtc, unsigned int sec,
		unsigned int usec, uint32_t old_fb_id, uint32_t obj_id);
static int
set_mode_flexible(struct ias_crtc *ias_crtc);
static void
set_next_fb_flexible(struct ias_output *output, struct ias_fb *ias_fb);
static struct ias_fb*
get_next_fb_flexible(struct ias_output *output);
static void
flip_flexible(int drm_fd, struct ias_crtc *ias_crtc, int output_id);
static void
flip_flexible_legacy(int drm_fd, struct ias_crtc *ias_crtc, int output_id);
static uint32_t is_surface_flippable_flexible(
			struct weston_view *view,
			struct weston_output *output,
			uint32_t check_xy);

extern void
ias_get_object_properties(int fd,
		struct ias_properties *drm_props,
		uint32_t obj_id, uint32_t obj_type,
		struct ias_sprite *sprite);

struct ias_output_model output_model_flexible = {
	.name = "flexible",
	.can_client_flip = 1,
	.render_flipped = 0,
	.hw_cursor = 0,
	.sprites_are_usable = 0,
	.stereoscopic = 0,
	.init = init_flexible,
	.init_output = init_output_flexible,
	.generate_crtc_scanout = generate_crtc_scanout_flexible,
	.pre_render = pre_render_flexible,
	.post_render = post_render_flexible,
	.disable_output = disable_output_flexible,
	.enable_output = enable_output_flexible,
	.allocate_scanout = allocate_scanout_flexible,
	.flip_handler = flip_handler_flexible,
	.set_mode = set_mode_flexible,
	.set_next_fb = set_next_fb_flexible,
	.get_next_fb = get_next_fb_flexible,
	.flip = flip_flexible,
	.update_sprites = NULL,
	.is_surface_flippable = is_surface_flippable_flexible,
};

static int next_scanout = PLANE_0;
static uint32_t mode_id = 0;

static void
create_sprites_for_crtc(struct ias_crtc *ias_crtc)
{
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_sprite *sprite;
	drmModePlaneRes *plane_res;
	drmModePlane *plane;
	uint32_t i;
	int sprite_count = 0, ret;
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

		if ((sprite->type == DRM_PLANE_TYPE_CURSOR && !backend->use_cursor_as_uplane) ||
			!((1 << ias_crtc->index) & plane->possible_crtcs)) {
			DRM_FREE_PLANE(backend, plane);
			free(sprite);

			continue;
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

		sprite->output_id = ias_crtc->num_outputs;
		ias_crtc->num_outputs++;

		if (ias_crtc->num_outputs == ias_crtc->configuration->output_num) {
			break;
		}
	}

	free(plane_res->planes);
	free(plane_res);
}

static void
init_flexible(struct ias_crtc *ias_crtc)
{
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_flexible_priv *priv;

	if ((priv = calloc(1, sizeof(struct ias_flexible_priv))) == NULL) {
		IAS_ERROR("Failed to allocate memory for output private data.");
		return;
	}
	ias_crtc->output_model_priv = priv;

	priv->rp_count = 0;
	priv->rp_needed = 0;
	priv->drm_fd = backend->drm.fd;

	create_sprites_for_crtc(ias_crtc);

	/* If atomic pageflip isn't supported, fall back to legacy code */
	if (!backend->has_nuclear_pageflip) {
		output_model_flexible.flip = flip_flexible_legacy;
	}
}


/*
 * init_output_flexible()
 *
 * Initializes an output for a CRTC.  Since this is a flexible setup, the
 * output height and height should match those of the CRTC.
 */
static void
init_output_flexible(struct ias_output *ias_output,
		struct ias_configured_output *cfg)
{
	struct ias_crtc *ias_crtc = ias_output->ias_crtc;
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	char **attrs = cfg->attrs;
	int width, height;

	/* Check for flexibile model-specific output attributes */
	if (attrs) {
		while (attrs[0]) {
			if (strcmp(attrs[0], "plane_position") == 0) {
				if ((sscanf(attrs[1], "%d,%d",
							&priv->plane_geometry[next_scanout].x,
							&priv->plane_geometry[next_scanout].y)) != 2){
					IAS_ERROR("Badly formed plane position element: %s\n",
							attrs[1]);
				}
			} else if (strcmp(attrs[0], "plane_size") == 0) {
				if ((sscanf(attrs[1], "%dx%d",
							&priv->plane_geometry[next_scanout].width,
							&priv->plane_geometry[next_scanout].height)) != 2){
					IAS_ERROR("Badly formed plane size element: %s\n",
							attrs[1]);
				}
			} else if (strcmp(attrs[0], "transparent") == 0) {
				if ((sscanf(attrs[1], "%d",
							&ias_crtc->transparency_enabled)) != 1) {
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

	/*
	 * Flexible model supports "inherit" and "scale."  For the main display
	 * plane, "inherit" will use the current mode.  For sprite planes,
	 * inherit will use the "plane_size" values parsed above.
	 */
	if (!cfg->size || !strcmp(cfg->size, "inherit")) {
		if ((next_scanout == PLANE_0) &&
				(priv->plane_geometry[next_scanout].width == 0) &&
				(priv->plane_geometry[next_scanout].height == 0)) {
			ias_output->width = ias_crtc->current_mode->mode_info.hdisplay;
			ias_output->height = ias_crtc->current_mode->mode_info.vdisplay;

			priv->plane_geometry[next_scanout].width = ias_output->width;
			priv->plane_geometry[next_scanout].height = ias_output->height;
		} else {
			ias_output->width = priv->plane_geometry[next_scanout].width;
			ias_output->height = priv->plane_geometry[next_scanout].height;
		}
	} else if (sscanf(cfg->size, "scale:%dx%d", &width, &height)) {
		ias_output->width = width;
		ias_output->height = height;
	} else {
		IAS_ERROR("Unknown size setting '%s' for classic model", cfg->size);
	}
	free(cfg->size);

	ias_output->scanout = next_scanout++;
}

/*
 * generate_crtc_scanout_flexible()
 *
 * The goal is to get the output's surface attached to the proper
 * scanout buffer.
 *
 * TODO: Figure out what needs to be done here.
 *
 * Is this going to need to know the output so that it can access the
 * correct scanout buffer?
 */
static int
generate_crtc_scanout_flexible(struct ias_crtc *ias_crtc,
	struct ias_output *output,
	pixman_region32_t *damage)
{
	struct gbm_bo *bo;
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = &priv->scanout[output->scanout];
	struct weston_plane *plane = &ias_crtc->backend->compositor->primary_plane;
	struct ias_backend *backend = ias_crtc->backend;
	struct ias_sprite *ias_sprite;

	if (!scanout->next) {
		ias_output_render(output, damage);
		pixman_region32_subtract(&plane->damage, &plane->damage, damage);

		if(!output->scanout_surface) {
			/* Find sprite that will be used for that scanout */
			wl_list_for_each(ias_sprite, &ias_crtc->sprite_list, link) {
				if ((backend->use_cursor_as_uplane || ias_sprite->type != DRM_PLANE_TYPE_CURSOR) &&
						(uint32_t)ias_sprite->output_id == output->scanout) {
					break;
				}
			}

			bo = gbm_surface_lock_front_buffer(scanout->surface);
			if (!bo) {
				IAS_ERROR("Failed to lock hardware buffer: %m");
				return 0;
			}

			scanout->next = ias_fb_get_from_bo(bo, NULL, output, IAS_FB_SCANOUT);

			if (!scanout->next) {
				IAS_ERROR("Failed to get ias_fb for bo.");
				gbm_surface_release_buffer(scanout->surface, bo);
				return 0;
			}

#if defined(BUILD_VAAPI_RECORDER) || defined(BUILD_FRAME_CAPTURE)
			wl_signal_emit(&output->next_scanout_ready_signal, output);
#endif
		}
	}

	if (!scanout->next) {
		IAS_ERROR("Failed to generate scanout contents.");
		return 0;
	}

	return 1;
}

/*
 * pre_render_flexible()
 *
 * Prepare to render a flexible output.  We need to make current with the
 * proper EGL surface.
 */
static int
pre_render_flexible(struct ias_output *output)
{
	if (gl_renderer->use_output(&output->base) < 0) {
		IAS_ERROR("failed to make current");
		return -1;
	}

	return 0;
}

/*
 * post_render_flexible()
 *
 * Complete rendering to a flexible output.  Classic outputs are EGL window
 * system surfaces, we just need to call eglSwapBuffers().
 */
static void
post_render_flexible(struct ias_output *output)
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
 * disable_output_flexible()
 *
 * Disables a flexible output.  However, since we don't have a 1:1
 * mapping ov outputs to CRTC's don't use use DPMS.
 */
static void
disable_output_flexible(struct ias_output *ias_output)
{
	ias_output->disabled = 1;
}

/*
 * enable_output_flexible()
 *
 * Enables a flexible output.
 */
static void
enable_output_flexible(struct ias_output *ias_output)
{
	ias_output->disabled = 0;
	weston_output_damage(&ias_output->base);
}

/*
 * allocate_scanout_flexible()
 *
 * Allocate the scanout buffers for the flexible display mode. There are
 * three scanout buffers, one for the main display, one for the left eye,
 * and one for the right eye.
 */
static int
allocate_scanout_flexible(struct ias_crtc *ias_crtc, struct ias_mode *m)
{
	int i;
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = priv->scanout;
	struct flexible_scanout *scanout_save = priv->scanout_save;
	EGLint format = ias_crtc->backend->format;
	int use_vm = 0;

	for (i = 0; i < ias_crtc->num_outputs; i++) {
		/*
		 * If the old buffers haven't been released, then a previous mode set
		 * was never complete and the current buffers can be released. Otherwise
		 * save the current buffers until the mode set completes.
		 */
		if (scanout_save[i].in_use) {
			if (scanout[i].in_use) {
				gl_renderer->output_destroy(&ias_crtc->output[i]->base);
				gbm_surface_destroy(scanout[i].surface);
			}
		} else {
			scanout_save[i].in_use = scanout[i].in_use;
			scanout_save[i].surface = scanout[i].surface;
			scanout_save[i].current = scanout[i].current;
			scanout_save[i].next = scanout[i].next;
		}

		/*
		 * For a mode set to succeed, the main plane must be equal to
		 * or larger than the timing horizontal/vertical dimensions.
		 *
		 * There may be some issues with corner cases involving a scaled
		 * main plane.
		 */
		/*if (i == PLANE_0) {
			ias_crtc->output[i]->width = m->base.width;
			ias_crtc->output[i]->height = m->base.height;
		}*/

		/* The width needs to be 64 byte aligned because an X-tiled surface
		 * (assuming 32 bpp) needs a 256 byte aligned stride. Usually, the
		 * DRI driver makes the alignment adjustments but currently UFO is
		 * not doing it; therefore this temporary workaround is needed.
		 */
		scanout[i].surface = gbm_surface_create(ias_crtc->backend->gbm,
#ifdef WORKAROUND_UFO_STRIDE
				ALIGN(ias_crtc->output[i]->width, 512),
#else
				ias_crtc->output[i]->width,
#endif
				ias_crtc->output[i]->height,
				GBM_FORMAT_ARGB8888,
				GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
		if (!scanout[i].surface) {
			IAS_ERROR("Failed to create scanout for CRTC");
			return -1;
		}

#ifdef USE_VM
		use_vm = gl_renderer->vm_exec;
#endif // USE_VM

		if (gl_renderer->output_window_create(&ias_crtc->output[i]->base,
						      (EGLNativeWindowType)scanout[i].surface,
						      scanout[i].surface,
						      use_vm
						      ? gl_renderer->alpha_attribs
						      : gl_renderer->opaque_attribs,
						      &format, 1)) {
			weston_log("failed to create gl renderer output state\n");
			gbm_surface_destroy(scanout->surface);
			return -1;
		}

		scanout[i].current = NULL;
		scanout[i].next = NULL;
		scanout[i].in_use = 1;
	}

	for (i = 0; i < ias_crtc->num_outputs; i++) {
		if (ias_crtc->output[i]) {
			/*
			 * Mark CRTC's output as dirty, update output mode, and associate
			 * EGL surface with CRTC scanout.
			 */
			if ((i == PLANE_0) &&
					(priv->plane_geometry[i].width == 0) &&
					(priv->plane_geometry[i].height == 0)) {
				ias_crtc->output[i]->base.current_mode = &m->base;
			}
			ias_crtc->output[i]->base.dirty = 1;
			weston_output_move(&ias_crtc->output[i]->base,
					ias_crtc->output[i]->base.x,
					ias_crtc->output[i]->base.y);
		}
	}

	/*
	 * After allocating scanouts for this CRTC, reset scanout counter,
	 * so that next CRTC can initialize flexible output model correctly.
	 */
	next_scanout = PLANE_0;
	return 0;
}


static void
sprite_zorder(int drm_fd, uint32_t plane_id, uint32_t crtc_id)
{
	struct drm_intel_sprite_pipeblend pipeblend;

	memset(&pipeblend, 0, sizeof(pipeblend));
	pipeblend.plane_id = plane_id;
	pipeblend.crtc_id = crtc_id;
	int ret;

	ret = drmIoctl(drm_fd, DRM_IOCTL_IGD_GET_PIPEBLEND, &pipeblend);

	if (ret != 0) {
		IAS_ERROR("Failed to get sprite %d properties", plane_id);
		return;
	}
	pipeblend.enable_flags |= I915_SPRITEFLAG_PIPEBLEND_FBBLENDOVL;
	pipeblend.fb_blend_ovl = 0;

	//pipeblend.zorder_value = ?
	//pipeblend.enable_flags |= I915_SPRITEFLAG_PIPEBLEND_ZORDER;

	ret = drmIoctl(drm_fd, DRM_IOCTL_IGD_SET_PIPEBLEND, &pipeblend);

	if (ret != 0) {
		IAS_ERROR("Failed to change sprite %d properties", plane_id);
	}
}


/*
 * This code is used only if atomic page-flip is not available in the
 * kernel drm driver.
 *
 * See which planes have a pending page flip operation and mark
 * them as complete.  When used with sprite planes, this will have
 * flicker/tearing because this event will only be sent for the
 * main display plane.
 */
static void
legacy_flip_handler_flexible(struct ias_crtc *ias_crtc, unsigned int sec, unsigned int usec)
{
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = priv->scanout;
	int p;
	struct timespec ts;
	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
			WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION |
			WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

	/*
	 * For each plane, check to see if it has it's flip pending bit set
	 * and if it does, clear the bit and notify the compositor that the
	 * flip is complete.
	 */
	for (p = PLANE_0; p <= ias_crtc->num_outputs; p++) {
		if (priv->pending & (1<<p)) {
			if (scanout[p].current) {
				if (scanout[p].current->is_client_buffer) {
					gbm_bo_destroy(scanout[p].current->bo);
				} else {
					gbm_surface_release_buffer(
							scanout[p].surface,
							scanout[p].current->bo);
				}
			}

			scanout[p].current = scanout[p].next;
			scanout[p].next = NULL;
			priv->pending &= ~(1<<p);

			ts.tv_sec = sec;
			ts.tv_nsec = usec * 1000;
			weston_output_finish_frame(&ias_crtc->output[p]->base, &ts, flags);
		}
	}
}

/*
 * The following is the fallback method to flip content to the
 * various planes. This is only used if atomic page flip support
 * is not available.
 */
static void
flip_flexible_legacy(int drm_fd, struct ias_crtc *ias_crtc, int output_id)
{
	struct ias_output *output;
	struct ias_sprite *ias_sprite;
	int sprites[2] = { 0 };
	int s = 0;
	int ret;
	uint32_t w, h;
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = priv->scanout;

	if (scanout[PLANE_0].next &&
			!(priv->pending & 0x01)) {
		if (ias_crtc->request_set_mode) {
			set_mode_flexible(ias_crtc);
		}

		if (drmModePageFlip(drm_fd, ias_crtc->crtc_id,
					scanout[PLANE_0].next->fb_id,
					DRM_MODE_PAGE_FLIP_EVENT, ias_crtc) < 0) {
			IAS_ERROR("Queueing main plane pageflip of buffer %d failed: %m",
					scanout[PLANE_0].next->fb_id);
		}

		/* Set a bit to indicate main plane was flipped */
		priv->pending |= 0x01;
	}

	/*
	 * FIXME: Sprite planes need to be better assigned to each
	 * output.  Right now we just pick the first for left and the
	 * the second for right.
	 */
	wl_list_for_each(ias_sprite, &ias_crtc->sprite_list, link) {
		sprites[s] = ias_sprite->plane_id;
		s++;
	}

	if (scanout[1].next && !(priv->pending & 0x02)) {
		assert(sprites[0]);

		sprite_zorder(drm_fd, sprites[0], ias_crtc->crtc_id);

		/* Locate output and get position based on it */
		output = ias_crtc->output[1];
		w = output->width << 16;
		h = output->height << 16;

		ret = drmModeSetPlane(drm_fd, sprites[0], ias_crtc->crtc_id,
					scanout[1].next->fb_id, 0,
					priv->plane_geometry[1].x,
					priv->plane_geometry[1].y,
					/* output->base.x, output->base.y, */
					output->width, output->height, /* Dest cord */
					0, 0,
					/* output->base.x, output->base.y, */
					w, h  /* Src cord */
					);

		if (ret) {
			IAS_ERROR("Queueing Left Eye pageflip failed %d: %m", ret);
		}

		/* Set a bit to indicate the sprite plane was flipped */
		priv->pending |= 0x02;
	}


	if (scanout[2].next && !(priv->pending & 0x04)) {
		assert(sprites[1]);

		sprite_zorder(drm_fd, sprites[1], ias_crtc->crtc_id);

		/* Locate output and get position based on it */
		output = ias_crtc->output[2];
		w = output->width << 16;
		h = output->height << 16;

		if (drmModeSetPlane(drm_fd, sprites[1], ias_crtc->crtc_id,
					scanout[2].next->fb_id, 0,
					/* output->base.x, output->base.y, */
					priv->plane_geometry[2].x,
					priv->plane_geometry[2].y,
					/* 0, 0, */
					output->width, output->height, /* Dest cord */
					/* output->base.x, output->base.y, */
					0, 0,
					w, h  /* Src cord */
					)) {

			IAS_ERROR("Queueing Right Eye pageflip failed: %m");
		}

		/* Set a bit to indicate the sprite plane was flipped */
		priv->pending |= 0x04;
	}
}

/*
 * When atomic page flip support is present, the page flips for the
 * various planes can be combined. There are also a couple of places
 * in the code where the plane properties can be sent to the kernel
 * driver and committed. This function is where the call to the
 * kernel driver is made.
 */
static int
commit(struct ias_crtc *ias_crtc)
{
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	int ret = 0;
	struct timespec ts;
	uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;

	if (ias_crtc->backend->no_flip_event) {
		flags = 0;
	}

	if (mode_id){
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	}

	/* Clear counters and move planes from pending to committed. */
	priv->rp_count = 0;
	priv->rp_needed = 0;
	priv->commited = priv->pending;
	priv->pending = 0;
	ret = drmModeAtomicCommit(priv->drm_fd, ias_crtc->prop_set, flags, ias_crtc);
	if (ret) {
		IAS_ERROR("Queueing atomic pageflip failed: %m (%d)", ret);
		IAS_ERROR("This failure will prevent clients from updating.");
	}

	mode_id = 0;

	/* Free and re-allocate the property set so it's always clean */
	drmModeAtomicFree(ias_crtc->prop_set);
	ias_crtc->prop_set = drmModeAtomicAlloc();

	if (ias_crtc->backend->no_flip_event) {
		clock_gettime(ias_crtc->backend->clock, &ts);
		flip_handler_flexible(ias_crtc, ts.tv_sec, ts.tv_nsec/1000, 0, 0);
	}

	return ret;
}


/*
 * flip_handler_flexible
 *
 * With atomic page flip support, this will get called once to indicate
 * the completion of a commit. Wit non-atomic page flip support, this
 * will get called only for the main plane's flip complete event.
 */
static void
flip_handler_flexible(struct ias_crtc *ias_crtc,
		unsigned int sec,
		unsigned int usec,
		uint32_t old_fb_id,
		uint32_t obj_id)
{
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = priv->scanout;
	int s;
	struct timespec ts;
	uint32_t flags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
			WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION |
			WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK;

	/*
	 * When the atomic page flip support is not available, this
	 * function will get called with obj_id set to zero.  Call the legacy
	 * handler to deal with it.  When atomic page flip support is present,
	 * obj_id will be the crtc object id.
	 */
	if (!ias_crtc->backend->has_nuclear_pageflip) {
		legacy_flip_handler_flexible(ias_crtc, sec, usec);
		return;
	}

	/*
	 * priv->commited holds the information about which planes were updated in
	 * the last drmModePropertySetCommit(). Use this to notify the output that
	 * the buffer has been flipped and the old buffer is now availabe for
	 * rendering.
	 */
	//if (obj_id == ias_crtc->crtc_id) {
	priv->in_handler = 1;
	for (s = PLANE_0; s <= ias_crtc->num_outputs; s++) {
		if (priv->commited & (1<<s)) {
			if (scanout[s].current) {
				if (scanout[s].current->is_client_buffer) {
					gbm_bo_destroy(scanout[s].current->bo);
				} else {
					gbm_surface_release_buffer(
							scanout[s].surface,
							scanout[s].current->bo);
				}
			}

			scanout[s].current = scanout[s].next;
			scanout[s].next = NULL;
			priv->commited &= ~(1<<s);

			wl_signal_emit(&ias_crtc->output[s]->printfps_signal, ias_crtc->output[s]);


			if (ias_crtc->backend->no_flip_event) {
				ias_crtc->output[s]->base.repaint_status = REPAINT_NOT_SCHEDULED;
				clock_gettime(ias_crtc->backend->compositor->presentation_clock, &ts);
				weston_output_finish_frame(&ias_crtc->output[s]->base, &ts, WP_PRESENTATION_FEEDBACK_INVALID);
			} else {
				ts.tv_sec = sec;
				ts.tv_nsec = usec * 1000;
				weston_output_finish_frame(&ias_crtc->output[s]->base, &ts, flags);
			}
		} /*else if (ias_crtc->output[s]->base.repaint_needed) {
			weston_output_finish_frame(&ias_crtc->output[s]->base, msecs);
		}*/
	}

	/*
	 * priv->page_flip_pending holds the infomation about which planes
	 * have been updated and are ready to flip now.  If any are ready
	 * commit them.
	 */
	if (priv->pending) {
		commit(ias_crtc);
	}
	//}
	priv->in_handler = 0;
}

/*
 * flip
 *
 * If there's a scanout buffer ready to be flipped, update the property
 * set with the plane's info. This may or may not actually commit the
 * the change. It won't be committed if there are other planes that
 * are also ready but the properties have not been updated or if we're
 * in the middle of handling a previous commit.
 */
static void
flip_flexible(int drm_fd, struct ias_crtc *ias_crtc, int output_id)
{
	struct ias_output *output;
	struct ias_sprite *ias_sprite;
	int s = 0;
	uint32_t w, h;
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = priv->scanout;
	struct ias_backend *backend = ias_crtc->backend;
	int i;

	if (ias_crtc->prop_set) {
		wl_list_for_each(ias_sprite, &ias_crtc->sprite_list, link) {
			if ((backend->use_cursor_as_uplane || ias_sprite->type != DRM_PLANE_TYPE_CURSOR) &&
				ias_sprite->output_id == output_id) {
				break;
			}
		}

		if (ias_crtc->request_set_mode) {
			set_mode_flexible(ias_crtc);
		}

		s = output_id;

		if (scanout[s].next) {
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.fb_id,
					scanout[s].next->fb_id);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.crtc_id,
					ias_crtc->crtc_id);

			output = ias_crtc->output[s];
			w = output->width << 16;
			h = output->height << 16;

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.crtc_x, priv->plane_geometry[s].x);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.crtc_y, priv->plane_geometry[s].y);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.crtc_w, priv->plane_geometry[s].width);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.crtc_h, priv->plane_geometry[s].height);

			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.src_x, 0);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.src_y, 0);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.src_w, w);
			drmModeAtomicAddProperty(ias_crtc->prop_set,
					ias_sprite->plane_id,
					ias_sprite->prop.src_h, h);

			if (backend->rbc_supported && backend->rbc_debug) {
				weston_log("[RBC] Commiting scanout %d, compression enabled = %d\n",
					   s, scanout[s].next->is_compressed);
			}

			priv->pending |= (1 << s);

			/*
			 * TODO: set properties for sprite plane z-order when
			 * they've been implemented in the driver.
			 *
			 * sprite_zorder(drm_fd, ias_sprite->plane_id, ias_crtc->crtc_id);
			 */
		}

		/* Calculate how many outputs are ready to flip */
		if (priv->rp_count == 0) {
			for (i = 0; i < ias_crtc->num_outputs; i++) {
				if (ias_crtc->output[i]->base.repaint_needed) {
					priv->rp_needed++;
				}
			}
		}
		priv->rp_count++;

		/*
		 * If this is the last output needing a repaint and we're not
		 * executing in the handler and we're not waiting for the
		 * previous commit to complete, commit all pending planes.
		 */
		if ((priv->rp_count >= priv->rp_needed) &&
				!priv->in_handler && !priv->commited) {
			if (commit(ias_crtc)) {
				return;
			}
		}

		return;
	}
}

/*
 * is_surface_flippable_flexible()
 *
 * Returns 1 if a surface could potentially be flippable or 0 if it is not
 */
static uint32_t is_surface_flippable_flexible(
			struct weston_view *view,
			struct weston_output *output,
			uint32_t check_xy)
{
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
set_mode_flexible(struct ias_crtc *ias_crtc)
{
	int fb = 0;
	int ret;
	int i;
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = priv->scanout;
	struct flexible_scanout *scanout_save = priv->scanout_save;
	struct ias_backend *backend = ias_crtc->backend;
	drmModeCrtcPtr current_crtc_mode;
	drmModeModeInfo *mode = &ias_crtc->current_mode->mode_info;

	/* Make sure there's a valid framebuffer ready to be set */
	if (!scanout[PLANE_0].next) {
		return 0;
	}

	fb = scanout[PLANE_0].next->fb_id;

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

		/*
		 * Check for old saved buffers.  If this call is made when the current
		 * ias_fb pointer is NULL, then there's a good chance a the mode was
		 * just changed and we have old scanout buffer information still around.
		 */
		for (i = 0; i < ias_crtc->num_outputs; i++) {
			if (!scanout[i].current) {
				if (scanout_save[i].in_use) {
					if (scanout_save[i].current) {
						if (scanout_save[i].current->is_client_buffer) {
							gbm_bo_destroy(scanout_save[i].current->bo);
						} else {
							gbm_surface_release_buffer(
									scanout_save[i].surface,
									scanout_save[i].current->bo);
						}
					}

					if (scanout_save[i].next) {
						if (scanout_save[i].next->is_client_buffer) {
							gbm_bo_destroy(scanout_save[i].next->bo);
						} else {
							gbm_surface_release_buffer(
									scanout_save[i].surface,
									scanout_save[i].next->bo);
						}
					}

					gl_renderer->output_destroy(&ias_crtc->output[i]->base);
					gbm_surface_destroy(scanout_save[i].surface);
				}
				scanout_save[i].current = NULL;
				scanout_save[i].next = NULL;
				scanout_save[i].in_use = 0;
			}
		}

		ias_crtc->request_set_mode = 0;
	}
	return 0;
}


/*
 * set_next_fb assigns a ias_fb structure that was derrived from a
 * client's surface as the next scanout buffer to flip to.  For the
 * flexible model, this simply sets the next scanout to the incoming
 * ias_fb.
 */
static void
set_next_fb_flexible(struct ias_output *output, struct ias_fb *ias_fb)
{
	struct ias_crtc *ias_crtc = output->ias_crtc;
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = &priv->scanout[output->scanout];

	scanout->next = ias_fb;
}

static struct ias_fb*
get_next_fb_flexible(struct ias_output *output)
{
	struct ias_crtc *ias_crtc = output->ias_crtc;
	struct ias_flexible_priv *priv =
		(struct ias_flexible_priv *)ias_crtc->output_model_priv;
	struct flexible_scanout *scanout = &priv->scanout[output->scanout];

	return scanout->next;
}
