/*
 *-----------------------------------------------------------------------------
 * Filename: ias-relay-input.c
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
 *   Intel Automotive Solutions relay input interface for the shell plugin of
 *   Weston
 *-----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <linux/input.h>

#include "ias-relay-input.h"
#include "ias-shell.h"


static void
ias_relay_input_send_touch(struct wl_client *client,
			struct wl_resource *resource,
			uint32_t touch_event_type,
			uint32_t surfid,
			uint32_t touch_id,
			uint32_t x,
			uint32_t y,
			uint32_t time)
{
	struct ias_shell *shell = wl_resource_get_user_data(resource);
	struct ias_surface *shsurf;
	struct wl_resource *surf_resource = NULL;
	struct wl_resource *ws_resource = NULL;
	struct wl_resource *target_resource = NULL;
	struct wl_resource *t_resource = NULL;
	struct weston_seat *seat = NULL;

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == surfid) {
			surf_resource = shsurf->resource;
			ws_resource = shsurf->surface->resource;
			break;
		}
	}

	if (surf_resource == NULL) {
		printf("No surface to match surfid.\n");
		return;
	}



	wl_list_for_each(seat, &shell->compositor->seat_list, link) {
		wl_list_for_each(t_resource, &seat->touch_state->resource_list, link) {
			if (wl_resource_get_client(t_resource) == wl_resource_get_client(surf_resource)) {
				target_resource = t_resource;
				break;
			}
		}
		if (target_resource) {
			break;
		} else {
			printf("Target resource not found for seat %p.\n", seat);
		}
	}

	if (target_resource == NULL) {
		printf("No target resource found.\n");
		return;
	}

	switch(touch_event_type) {
		case IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_DOWN:
		{
			uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
			wl_touch_send_down(target_resource,
					serial, time,
					ws_resource,
					touch_id, x, y);
			break;
		}
		case IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_UP:
		{
			uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
			wl_touch_send_up(target_resource, serial, time, touch_id);
			break;
		}
		case IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_MOTION:
			wl_touch_send_motion(target_resource, time, touch_id, x, y);
			break;
		case IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_FRAME:
			 wl_touch_send_frame(target_resource);
			break;
		case IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_CANCEL:
			wl_touch_send_cancel(target_resource);
			break;
	}
}

static void
ias_relay_input_send_key(struct wl_client *client,
			struct wl_resource *resource,
			uint32_t key_event_type,
			uint32_t surfid,
			uint32_t time,
			uint32_t key,
			uint32_t state,
			uint32_t mods_depressed,
			uint32_t mods_latched,
			uint32_t mods_locked,
			uint32_t group)
{
	struct ias_shell *shell = wl_resource_get_user_data(resource);
	struct ias_surface *shsurf;
	struct wl_resource *surf_resource = NULL;
	struct wl_resource *t_resource = NULL;
	struct wl_resource *target_resource = NULL;
	struct weston_seat *seat = NULL;

	/* Walk the surface list looking for the requested surface.  */
	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (SURFPTR2ID(shsurf) == surfid) {
			surf_resource = shsurf->resource;
			break;
		}
	}

	if (surf_resource == NULL) {
		printf("No surface to match surfid.\n");
		return;
	}

	wl_list_for_each(seat, &shell->compositor->seat_list, link) {
		wl_list_for_each(t_resource, &seat->keyboard_state->resource_list, link) {
			if (wl_resource_get_client(t_resource) == wl_resource_get_client(surf_resource)) {
				target_resource = t_resource;
				break;
			}
		}
		if (target_resource) {
			break;
		} else {
			wl_list_for_each(t_resource, &seat->keyboard_state->focus_resource_list, link) {
				if (wl_resource_get_client(t_resource) == wl_resource_get_client(surf_resource)) {
					target_resource = t_resource;
					break;
				}
			}
			if (target_resource) {
				break;
			} else {
				printf("Target resource not found for seat %p.\n", seat);
			}
		}
	}

	if (target_resource == NULL) {
		printf("No target resource found.\n");
		return;
	}

	uint32_t serial = wl_display_next_serial(wl_client_get_display(client));
	switch(key_event_type) {
	case IAS_RELAY_INPUT_KEY_EVENT_TYPE_KEY:
		wl_keyboard_send_key(target_resource, serial, time, key, state);
		break;
	}
}

static const struct ias_relay_input_interface ias_relay_input_implementation = {
	ias_relay_input_send_touch,
	ias_relay_input_send_key,
};


/*
 * bind_ias_relay_input()
 *
 * Handler executed when client binds to IAS relay input interface.
 */
void
bind_ias_relay_input(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &ias_relay_input_interface, 1, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &ias_relay_input_implementation,
			data, NULL);
}

