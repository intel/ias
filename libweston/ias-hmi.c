/*
 *-----------------------------------------------------------------------------
 * Filename: ias-hmi.c
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
 *   Intel Automotive Solutions hmi interface for the shell plugin of Weston
 *-----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <linux/input.h>

#include "ias-hmi.h"
#include "ias-shell.h"

static void
destroy_ias_hmi_resource(struct wl_resource *resource)
{
	struct hmi_callback *hmi, *next;
	struct ias_shell *shell = resource->data;

	wl_list_for_each_safe(hmi, next, &shell->sfc_change_callbacks, link) {
		if (resource == hmi->resource) {
			/* Remove ourselves from the surface's destructor list */
			wl_list_remove(&hmi->link);
			free(hmi);

			return;
		}
	}
}


static void
ias_hmi_set_behavior(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t id,
		uint32_t behavior)
{
	struct ias_shell *shell = shell_resource->data;
	struct ias_surface *shsurf;
	struct weston_seat *seat;
	struct weston_touch *touch = NULL;

	wl_list_for_each(seat, &shell->compositor->seat_list, link) {
		touch = weston_seat_get_touch(seat);
		break;
	}

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == id) {
			shsurf->next_behavior = (behavior & 0x00ffffff) |
				(shsurf->behavior & 0xff000000);
			if(touch && shsurf->next_behavior & IAS_HMI_INPUT_OWNER) {
				shell->compositor->input_view = shsurf->view;
				weston_touch_set_focus(touch, shsurf->view);
			} else {
				shell->compositor->input_view = NULL;
				ias_committed(shsurf->surface, 0, 0);
			}
			return;
		}
	}
}

static void
ias_hmi_set_constant_alpha(struct wl_client *client,
		   struct wl_resource *shell_resource,
		   uint32_t id,
		   uint32_t alpha)
{
	struct ias_shell *shell = shell_resource->data;
	struct ias_surface *shsurf;
	struct ias_surface *child_shsurf;
	int32_t rel_alpha;
	int32_t new_alpha;

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == id) {
			rel_alpha = alpha - (uint32_t)(shsurf->view->alpha * 0xFF);

			if (alpha <= 0xFF) {
				shsurf->view->alpha =
						(GLfloat)((GLfloat) alpha / (GLfloat) 0xFF);
				weston_surface_damage(shsurf->surface);

				/* Need to modify the alpha value for descendant surfaces */
				wl_list_for_each(child_shsurf, &shsurf->child_list, child_link) {
					new_alpha =
						(uint32_t)(child_shsurf->view->alpha * 0xFF) +
						rel_alpha;

					if (new_alpha < 0) {
						new_alpha = 0;
					} else if (new_alpha > 0xFF) {
						new_alpha = 0xFF;
					}

					ias_hmi_set_constant_alpha(client, shell_resource,
							SURFPTR2ID(child_shsurf), new_alpha);
				}

			} else {
				IAS_DEBUG("Invalid alpha value specified");
			}
			return;
		}
	}
}

static void
ias_hmi_move_surface(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t id,
		int32_t x, int32_t y)
{
	struct ias_shell *shell = shell_resource->data;
	struct ias_surface *shsurf;
	struct ias_surface *child_shsurf;
	int32_t relx, rely;

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == id) {
			/* Don't try to move fullscreen or background surfaces */
			if (shsurf->zorder == SHELL_SURFACE_ZORDER_BACKGROUND ||
					shsurf->zorder == SHELL_SURFACE_ZORDER_FULLSCREEN) {
				return;
			}

			/* Store the relative change in position so we know how much
			 * to move the child surfaces. When a surface is first created,
			 * shsurf->x still has the value of 0.
			 */
			relx = x - (int32_t)shsurf->view->geometry.x;
			rely = y - (int32_t)shsurf->view->geometry.y;
			shsurf->x = x;
			shsurf->y = y;
			shsurf->position_update = 1;
			ias_committed(shsurf->surface, 0, 0);

			wl_list_for_each(child_shsurf, &shsurf->child_list, child_link) {
				ias_hmi_move_surface(client, shell_resource,
						SURFPTR2ID(child_shsurf),
						child_shsurf->view->geometry.x + relx,
						child_shsurf->view->geometry.y + rely);
			}

			return;
		}
	}
}

static void
ias_hmi_resize_surface(struct wl_client *client,
		struct wl_resource *client_resource,
		uint32_t id,
		uint32_t width, uint32_t height)
{
	struct ias_shell *shell = client_resource->data;
	struct ias_surface *shsurf;
	struct weston_surface *es;
	struct weston_frame_callback *cb, *cnext;

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == id) {
			if (shsurf->zorder == SHELL_SURFACE_ZORDER_BACKGROUND ||
					shsurf->zorder == SHELL_SURFACE_ZORDER_FULLSCREEN ||
					(width <= 0 || height <= 0)) {
				ias_hmi_send_surface_info(client_resource, SURFPTR2ID(shsurf),
						shsurf->title,
						shsurf->zorder,
						(int32_t)shsurf->view->geometry.x,
						(int32_t)shsurf->view->geometry.y,
						shsurf->surface->width,
						shsurf->surface->height,
						(uint32_t) (shsurf->view->alpha * 0xFF),
						(uint32_t) (shsurf->behavior),
						shsurf->pid,
						shsurf->pname,
						shsurf->view->output ? shsurf->view->output->id : 0,
						ias_surface_is_flipped(shsurf));

				return;
			}

			shsurf->hmi_client->send_configure(shsurf->surface,
					width, height);

			/*
			 * Send callbacks for any outstanding 'frame' requests; it's
			 * possible that the changes we made here caused the surface
			 * to become visible even though it wasn't before.  If we
			 * don't send a frame event to get things moving again, the
			 * client will never send us a new buffer and the configure
			 * event above will have no effect.
			 */
			es = shsurf->surface;
			wl_list_for_each_safe(cb, cnext, &es->frame_callback_list, link) {
				wl_callback_send_done(cb->resource, 0);
				wl_resource_destroy(cb->resource);
			}

			return;
		}
	}
}

static void
ias_hmi_zorder_surface(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t id,
		uint32_t zorder)
{
	struct ias_shell *shell = shell_resource->data;
	struct ias_surface *shsurf;

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == id) {
			/*
			 * Don't allow changing the zorder of "special" surfaces
			 * (background, fullscreen, or popup).
			 */
			if (shsurf->zorder & 0xff000000) {
				return;
			}

			shsurf->next_zorder = (zorder & 0xffffff);
			ias_committed(shsurf->surface, 0, 0);
			return;
		}
	}
}

static void
ias_hmi_set_visible(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t id,
		uint32_t visibility)
{
	struct ias_shell *shell = shell_resource->data;
	struct ias_surface *shsurf;
	struct ias_surface *child_shsurf;

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == id) {
			/*
			 * If the client wants to make this surface visible and
			 * its already not visible, then we will make it visible
			 */
			if (visibility == IAS_HMI_VISIBLE_OPTIONS_VISIBLE &&
					shsurf->behavior & SHELL_SURFACE_BEHAVIOR_HIDDEN) {
				shsurf->next_behavior &= ~SHELL_SURFACE_BEHAVIOR_HIDDEN;
				ias_committed(shsurf->surface, 0, 0);
				weston_compositor_damage_all(shell->compositor);
			} else if (visibility == IAS_HMI_VISIBLE_OPTIONS_HIDDEN &&
					!(shsurf->behavior & SHELL_SURFACE_BEHAVIOR_HIDDEN)) {
				shsurf->next_behavior |= SHELL_SURFACE_BEHAVIOR_HIDDEN;
				ias_committed(shsurf->surface, 0, 0);
				weston_compositor_damage_all(shell->compositor);
			}

			/* Set the visibility for child and descendant surfaces. */
			wl_list_for_each(child_shsurf, &shsurf->child_list, child_link) {
				ias_hmi_set_visible(client, shell_resource,
						SURFPTR2ID(child_shsurf), visibility);
			}
			return;
		}
	}
}

static void
ias_hmi_start_capture(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t surfid,
		uint32_t output_number,
		int profile,
		int verbose)
{
#ifdef BUILD_REMOTE_DISPLAY
	struct ias_shell *ias_shell = shell_resource->data;
	struct ias_backend *ias_backend =
			(struct ias_backend *)ias_shell->compositor->backend;
	struct ias_surface *shsurf;
	struct weston_surface *surface = NULL;
	pid_t pid;
	uid_t uid;
	gid_t gid;
	int ret = 0;

	/* Make sure that we're using the IAS backend... */
	if (ias_backend->magic != BACKEND_MAGIC) {
		IAS_ERROR("Backend does not match.");
		return;
	}

	/* Only allow root to start the recorder... */
	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (gid != 0) {
		wl_resource_post_error(shell_resource,
			WL_SHELL_ERROR_ROLE, "Frame capture requires root access.");
		return;
	}

	if (surfid){
		wl_list_for_each(shsurf, &ias_shell->client_surfaces, surface_link) {
			if (SURFPTR2ID(shsurf) == surfid) {
				surface = shsurf->surface;
				printf("Starting capture for surface %p.\n", surface);
				break;
			}
		}
	} else {
		printf("Starting capture for output %u.\n", output_number);
	}

	ret = ias_backend->start_capture(client, ias_backend, shell_resource,
			surface, output_number, profile, verbose);
	if (ret) {
		IAS_ERROR("Failed to start capture.");
		ias_hmi_send_capture_error(shell_resource, (int32_t)pid, ret);
	}
#else
	wl_resource_post_error(shell_resource,
			WL_SHELL_ERROR_ROLE, "Frame capture not compiled in, to use frame capture configure with --enable-frame-capture");
#endif
}

static void
ias_hmi_stop_capture(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t surfid,
		uint32_t output_number)
{
#ifdef BUILD_REMOTE_DISPLAY
	pid_t pid;
	struct ias_shell *ias_shell = shell_resource->data;
	struct ias_backend *ias_backend =
			(struct ias_backend *)ias_shell->compositor->backend;
	struct ias_surface *shsurf;
	struct weston_surface *surface = NULL;
	uid_t uid;
	gid_t gid;
	int ret = 0;

	/* Make sure that we're using the IAS backend... */
	if (ias_backend->magic != BACKEND_MAGIC) {
		IAS_ERROR("Backend does not match.");
		return;
	}

	/* Only allow root to stop the recorder... */
	wl_client_get_credentials(client, &pid, &uid, &gid);
	if (gid != 0) {
		wl_resource_post_error(shell_resource,
			WL_SHELL_ERROR_ROLE, "Frame capture requires root access");
		return;
	}

	if (surfid){
		wl_list_for_each(shsurf, &ias_shell->client_surfaces, surface_link) {
			if (SURFPTR2ID(shsurf) == surfid) {
				surface = shsurf->surface;
				printf("Stopping capture for surface %p.\n", surface);
				break;
			}
		}
	} else {
		printf("Stopping capture for output %u.\n", output_number);
	}

	ret = ias_backend->stop_capture(client, ias_backend, shell_resource,
			surface, output_number);
	if (ret) {
		IAS_ERROR("Failed to stop capture.");
		ias_hmi_send_capture_error(shell_resource, (int32_t)pid, ret);
	}
#else
	wl_resource_post_error(shell_resource,
			WL_SHELL_ERROR_ROLE, "Frame capture not compiled in, to use frame capture configure with --enable-frame-capture");
#endif
}

static void
ias_hmi_release_buffer_handle(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t shm_surf_id, uint32_t bufid, uint32_t imageid,
		uint32_t surfid, uint32_t output_number)
{
#ifdef BUILD_REMOTE_DISPLAY
	struct ias_shell *ias_shell = (struct ias_shell *)shell_resource->data;
	struct ias_backend *ias_backend =
			(struct ias_backend *)ias_shell->compositor->backend;
	struct weston_surface *surface = NULL;
	int ret = 0;
	pid_t pid;
	uid_t uid;
	gid_t gid;

	wl_client_get_credentials(client, &pid, &uid, &gid);

	/* Make sure that we're using the IAS backend... */
	if (ias_backend->magic == BACKEND_MAGIC) {
		struct ias_surface *shsurf;

		if (surfid) {
			wl_list_for_each(shsurf, &ias_shell->client_surfaces, surface_link) {
				if (SURFPTR2ID(shsurf) == surfid) {
					surface = shsurf->surface;
					break;
				}
			}
		}
	}

	if (surface) {
		ret = ias_backend->release_buffer_handle(ias_backend, shm_surf_id,
							bufid, imageid, surface, 0);
	} else {
		ret = ias_backend->release_buffer_handle(ias_backend, 0, 0, 0, 0,
							output_number);
	}

	if (ret) {
		IAS_ERROR("Failed to release buffer handle.");
		ias_hmi_send_capture_error(shell_resource, (int32_t)pid, ret);
	}
#else
	pid_t pid;

	wl_client_get_credentials(client, &pid, NULL, NULL);

	wl_resource_post_error(shell_resource,
			WL_SHELL_ERROR_ROLE, "Frame capture not compiled in, to use frame capture configure with --enable-frame-capture");
#endif
}


static void
ias_hmi_set_shareable(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t id,
		uint32_t shareable)
{
	struct ias_shell *shell = shell_resource->data;
	struct ias_surface *shsurf;
	struct ias_surface *child_shsurf;
	struct hmi_callback *cb;

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == id) {
			 shsurf->shareable = shareable;

			/* Notify ias_hmi listeners of the surface sharing flag change  */
			wl_list_for_each(cb, &shell->sfc_change_callbacks, link) {
				ias_hmi_send_surface_sharing_info(cb->resource, SURFPTR2ID(shsurf),
					shsurf->title,
					shsurf->shareable,
					shsurf->pid,
					shsurf->pname);
			}

			 /* Set the shareable flag for child and descendant surfaces. */
			 wl_list_for_each(child_shsurf, &shsurf->child_list, child_link) {
				ias_hmi_set_shareable(client, shell_resource,
						SURFPTR2ID(child_shsurf), shareable);
			}
			return;
		}
	}
}

static void
ias_hmi_get_surface_sharing_info(struct wl_client *client,
		struct wl_resource *shell_resource, uint32_t id)
{
	struct ias_shell *shell = shell_resource->data;
	struct ias_surface *shsurf;
	struct hmi_callback *cb;

	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == id) {
			/* Notify ias_hmi listeners of the surface sharing flag status  */
			wl_list_for_each(cb, &shell->sfc_change_callbacks, link) {
				ias_hmi_send_surface_sharing_info(cb->resource, SURFPTR2ID(shsurf),
					shsurf->title,
					shsurf->shareable,
					shsurf->pid,
					shsurf->pname);
			}
			return;
		}
	}
}

static const struct ias_hmi_interface ias_hmi_implementation = {
	ias_hmi_set_constant_alpha,
	ias_hmi_move_surface,
	ias_hmi_resize_surface,
	ias_hmi_zorder_surface,
	ias_hmi_set_visible,
	ias_hmi_set_behavior,
	ias_hmi_set_shareable,
	ias_hmi_get_surface_sharing_info,
	ias_hmi_start_capture,
	ias_hmi_stop_capture,
	ias_hmi_release_buffer_handle,
};


/*
 * bind_ias_hmi()
 *
 * Handler executed when client binds to IAS hmi interface.
 */
void
bind_ias_hmi(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct ias_shell *shell = data;
	struct hmi_callback *cb;
	struct ias_surface *shsurf;

	/*
	 * Any client that binds to the hmi interface is interested in
	 * receiving callbacks related to hmi controlled surfaces.
	 */
	cb = calloc(1, sizeof *cb);
	if (!cb) {
		IAS_ERROR("Failed to allocate layout callback.");
		return;
	}

	cb->resource = wl_resource_create(client,
						&ias_hmi_interface, 1, id);
	wl_resource_set_implementation(cb->resource,
						&ias_hmi_implementation,
						shell, destroy_ias_hmi_resource);

	/* Add callback to shell's list */
	wl_list_insert(&shell->sfc_change_callbacks, &cb->link);

	/* Send the list of surfaces to this client only */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		ias_hmi_send_surface_info(cb->resource, SURFPTR2ID(shsurf),
							shsurf->title,
							shsurf->zorder,
							(int32_t)shsurf->view->geometry.x,
							(int32_t)shsurf->view->geometry.y,
							shsurf->surface->width,
							shsurf->surface->height,
							(uint32_t) (shsurf->view->alpha * 0xFF),
							(uint32_t) (shsurf->behavior),
							shsurf->pid,
							shsurf->pname,
							shsurf->view->output ? shsurf->view->output->id : 0,
							ias_surface_is_flipped(shsurf));
	}
}
