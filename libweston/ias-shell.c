/*
 *-----------------------------------------------------------------------------
 * Filename: ias-shell.c
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
 *   Intel Automotive Solutions shell module for Weston
 *-----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <linux/input.h>

#include "ias-hmi.h"
#include "ias-relay-input.h"
#include "ias-shell.h"
#include "shared/helpers.h"


static struct ias_shell *self;

static void (*renderer_attach)(struct weston_surface *es, struct weston_buffer *buffer);

static struct timeval curr_time;
static uint32_t metrics_timing = 0;
static uint32_t do_print_fps = 0;
/*
 * Custom zorder (layer).
 */
struct custom_zorder {
	uint32_t id;
	struct weston_layer layer;
	struct wl_list link;
};

/*
 * Track which shell interface clients are bound to.
 */
struct bound_client {
	struct wl_client *client_id;
	struct wl_list link;
};

/*
 * Handy linked list function not provided by wayland utilities.
 */
#define wl_list_first(head, type, member)             \
	wl_list_empty(head) ? NULL : container_of((head)->next, type, member)


/***
 *** Function Prototypes
 ***/

static void
destroy_ias_surface_resource(struct wl_resource *);

static struct ias_surface *
ias_surface_constructor(void *, struct weston_surface *,
		const struct weston_shell_client *);

static void
ias_shell_output_change_notify(struct wl_listener *listener, void *data);

static void
send_configure(struct weston_surface *surface,
		int32_t width,
		int32_t height);

static void
set_transient(struct shell_surface *child,
		struct weston_surface *parent,
		int x, int y, uint32_t flags);

static void
set_toplevel(struct shell_surface *shsurf);


static bool
surface_exists(struct weston_surface *surface,
			struct ias_shell *shell);

static void
surface_revert_keyboard_focus(struct ias_surface *shsurf,
				struct ias_shell *shell);

static void
surface_remove_prev_focus_list(struct ias_surface *shsurf,
				struct ias_shell *shell);

int ias_surface_is_flipped(struct ias_surface *shsurf)
{
	int ret = 0;
	struct weston_plane *primary_plane = &shsurf->surface->compositor->primary_plane;
	struct ias_output *output = (struct ias_output*)shsurf->output;

	if (shsurf->view->plane) {
		if (shsurf->view->plane != primary_plane) {
			ret = 1;
		}
	} else if (output && output->scanout_surface) {
		if (shsurf->surface == output->scanout_surface) {
			ret = 1;
		}
	}
	return ret;
}
/***
 *** IAS Shell Surface Implementation
 ***
 *** The functions below implement all of the standard entrypoints for a
 *** Wayland ias_surface as defined in protocol/ias-shell.xml.
 *** Some of this functionality can be implemented as noop's for IVI usage
 *** models, but we need to keep in mind that standard toolkits (QT, GTK,
 *** etc.) may call some of these API's and behave strangely if they
 *** don't behave in a similar manner to the desktop shell's implementation.
 ***/

/*
 * ias_surface_pong()
 *
 * Handles a client -> server pong request.  Clients send these in response
 * to ping events to let the server know they're still alive.  When we
 * receive a pong, we want to remove the currently outstanding ping timer.
 */
static void
ias_surface_pong(struct wl_client *client,
	struct wl_resource *resource,
	uint32_t serial)
{
	struct ias_surface *shsurf = resource->data;

	/* Make sure pong includes same serial number sent with ping */
	if (shsurf->ping_info.serial != serial) {
		return;
	}

	/* If this surface had previously timed out, clear the bit */
	if (shsurf->ping_info.timedout) {
		shsurf->ping_info.timedout = 0;
	}

	/* Remove ping timeout from main event loop listening */
	if (shsurf->ping_info.source) {
		wl_event_source_remove(shsurf->ping_info.source);
		shsurf->ping_info.source = NULL;
	}

	/* No longer waiting for a pong */
	shsurf->ping_info.active = 0;
}

/*
 * ias_surface_set_title()
 *
 * Sets the title for a surface.  This doesn't have any specific semantics
 * explained in the Wayland protocol.  Presumably the shell could present the
 * title in an "application unresponsive" type popup, or print it out in a task
 * switcher if desired.  In IVI we save the title away in the shell surface in
 * case a layout plugin wants to use the title in its own logic.
 */
static void
ias_surface_set_title(struct wl_client *client,
		struct wl_resource *resource,
		const char *title)
{
	struct ias_surface *shsurf = resource->data;

	/* Replace any existing title */
	free(shsurf->title);
	shsurf->title = strdup(title);
}

static struct weston_output *
get_default_output(struct weston_compositor *compositor)
{
	return container_of(compositor->output_list.next,
			    struct weston_output, link);
}

static void
ias_surface_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource)
{
	struct ias_surface *shsurf = resource->data;
	struct weston_frame_callback *cb, *cnext;

	if (output_resource) {
		shsurf->output =
			weston_head_from_resource(output_resource)->output;
	} else {
		shsurf->output = get_default_output(shsurf->shell->compositor);
	}
	shsurf->next_zorder = SHELL_SURFACE_ZORDER_FULLSCREEN;

	if(shsurf->output) {
		weston_view_set_position(shsurf->view,
				shsurf->output->x,
				shsurf->output->y);
		send_configure(shsurf->surface,
				shsurf->output->width,
				shsurf->output->height);
	}

	/*
	 * Finish any outstanding frame callback events; this needs to be done
	 * immediately here as the client would be awaiting this event.
	 */
	wl_list_for_each_safe(cb, cnext, &shsurf->surface->frame_callback_list, link) {
		wl_callback_send_done(cb->resource, 0);
		wl_resource_destroy(cb->resource);
	}
}


static void ias_surface_unset_fullscreen(struct wl_client *client,
			 struct wl_resource *resource,
			 uint32_t width,
			 uint32_t height)
{
	struct ias_surface *shsurf = resource->data;

	wl_list_remove(&shsurf->fullscreen_transform.link);
	wl_list_init(&shsurf->fullscreen_transform.link);
	shsurf->next_zorder = SHELL_SURFACE_ZORDER_DEFAULT;
	send_configure(shsurf->surface, width, height);
}

static const struct ias_surface_interface ias_surface_implementation = {
	ias_surface_pong,
	ias_surface_set_title,
	ias_surface_set_fullscreen,
	ias_surface_unset_fullscreen,
};



/*
 * shell_surface_set_class
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
shell_surface_set_class(struct wl_client *client,
		struct wl_resource *resource, const char *class)
{
	/* Does ias_surface have a class field? if so set it here */
}

/*
 * shell_surface_move
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
shell_surface_move(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *seat_resource, uint32_t serial)
{
	/* Not implemented in IAS shell */
}

/*
 * shell_surface_resize
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
shell_surface_resize(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *seat_resource, uint32_t serial,
		uint32_t edges)
{
	/* Not implemented in IAS shell */
}

/*
 * shell_surface_set_toplevel
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
shell_surface_set_toplevel(struct wl_client *client,
		struct wl_resource *resource)
{
	struct ias_surface *shsurf = resource->data;

	set_toplevel((struct shell_surface *)shsurf);
}

/*
 * shell_surface_set_transient
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
shell_surface_set_transient(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *parent_resource,
		int x, int y, uint32_t flags)
{
	struct ias_surface *parent_surface = parent_resource->data;

	set_transient((struct shell_surface *)resource->data,
			parent_surface->surface, x, y, flags);
}

/*
 * shell_surface_set_popup
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
shell_surface_set_popup(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *seat_resource,
		uint32_t serial,
		struct wl_resource *parent_resource,
		int32_t x, int32_t y, uint32_t flags)
{
	/* Not implemented in IAS shell */
}


/*
 * shell_surface_set_maximized()
 *
 * "Maximized" and "fullscreen" are basically the same thing for IVI
 * settings.  They differ for desktop-style compositors in that maximized
 * applications don't cover panel surfaces, whereas fullscreen surfaces do,
 * but IVI has no built-in panel functionality.  The set_maximized() request
 * also doesn't specify a "method" or "framerate" to use, so we implement
 * this with basically the same logic as fullscreen, except that we use
 * "centered" as the fullscreen method and ignore the framerate.  We also
 * turn on all "edges" bits for resizing, even though we don't expect
 * desktop-style grab-based resizes.
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
shell_surface_set_maximized(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *output_resource )
{
	struct ias_surface *shsurf = resource->data;
	struct weston_output *output;

	/* Output is optional; if unset, the default (primary) output is used */
	if (output_resource) {
		output = weston_head_from_resource(output_resource)->output;
	} else {
		output = get_default_output(shsurf->surface->compositor);
	}

	shsurf->next_zorder = SHELL_SURFACE_ZORDER_FULLSCREEN;

	/* Let the client know about its new dimensions */
	shsurf->hmi_client->send_configure(shsurf->surface,
			output->current_mode->width,
			output->current_mode->height);
}

/*
 * shell_surface_set_fullscreen
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
shell_surface_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource,
		uint32_t method,
		uint32_t framerate,
		struct wl_resource *output_resource)
{
	ias_surface_set_fullscreen(client, resource, output_resource);
}


/*
 * WL_SHELL
 *
 * Interface for wl_shell_surface.  This provides minimal suppport for
 * wl_shell surface control.  In some cases, these may be wrappers around
 * ias_surface functions.
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static const struct wl_shell_surface_interface shell_surface_implementation = {
	ias_surface_pong,
	shell_surface_move,
	shell_surface_resize,
	shell_surface_set_toplevel,
	shell_surface_set_transient,
	shell_surface_set_fullscreen,
	shell_surface_set_popup,
	shell_surface_set_maximized,
	ias_surface_set_title,
	shell_surface_set_class,
};


/***
 *** Shell client interface.
 ***
 *** These functions are used to send events to the shell client.  For
 *** the IAS compositor, the customer supplies the shell client (this is their
 *** HMI); this differs from tablet/desktop usage where there is a standard
 *** shell client provided.
 ***/

static void
send_configure(struct weston_surface *surface,
		int32_t width,
		int32_t height)
{
	struct ias_surface *shsurf = get_ias_surface(surface);

	assert(shsurf);
	/* As ias_shell and wl_shell configure even have different
	 * parameters, correct one need to be send, so that client
	 * application can interpret it properly
	 */
	if (!shsurf->wl_shell_interface) {
		ias_surface_send_configure(shsurf->resource, width, height);
	} else {
		wl_shell_surface_send_configure(shsurf->resource, 0, width, height);
	}
}

static const struct weston_shell_client hmi_client = {
	send_configure
};


/***
 *** IAS Shell Implementation
 ***
 *** The functions below implement the IAS-specific shell functionality.
 *** There should be a function here for every request defined for the
 *** ias_shell interface in protocol/ias-shell.xml.
 ***/

/*
 * ias_shell_set_background()
 *
 * Handles the "set_background" request against the IAS shell.  The surface
 * provided will be removed from any zorders that it is already a
 * member of and moved to the background zorder the next time a buffer
 * is attached to it.
 */
static void
ias_shell_set_background(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *output_resource,
		struct wl_resource *surface_resource)
{
	struct ias_surface *shsurf = surface_resource->data;

	/* Background surfaces have no parent */
	if (shsurf->parent) {
		wl_list_remove(&shsurf->child_link);
		shsurf->parent = NULL;
	}

	/* Surface will move to background layer on next buffer attach */
	shsurf->next_zorder = SHELL_SURFACE_ZORDER_BACKGROUND;

	/* Add to list of background surfaces */
	wl_list_remove(&shsurf->special_link);
	wl_list_insert(&shsurf->shell->background_surfaces, &shsurf->special_link);

	/* Assign the surface to the output that it will be the background for */
	shsurf->output = weston_head_from_resource(output_resource)->output;

	/*
	 * Reposition background surface at upper left of output in case the
	 * output has moved.
	 */
	weston_view_set_position(shsurf->view,
			shsurf->output->x, shsurf->output->y);

	/*
	 * Raise a "configure" event to notify the client about the appropriate
	 * surface dimensions (i.e., the size of the output it's the background
	 * for).
	 */
	send_configure(shsurf->surface,
			shsurf->output->width,
			shsurf->output->height);
}

/*
 * ias_shell_set_parent()
 *
 * Sets the parent of a surface.  Child surfaces move with their parent in
 * the default layout (customer layouts loaded by plugins may choose to
 * ignore parent/child links).
 */
static void
ias_shell_set_parent(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *surface_resource,
		struct wl_resource *parent_resource)
{
	struct ias_surface *shsurf = surface_resource->data;
	struct ias_surface *parent_surface = parent_resource->data;
	struct ias_surface *iter;

	if (parent_surface == shsurf->parent) {
		/* Already the parent */
		return;
	}

	/*
	 * If this surface is the parent of the parent surface or any of it's
	 * ancestors, then we cannot allow it to be a child because this will
	 * introduce a recursive loop.
	 */
	for(iter = parent_surface; iter; iter = iter->parent) {
		if(iter == shsurf) {
			/* Can't allow this */
			return;
		}
	}

	/* Unlink from old parent */
	if (shsurf->parent) {
		wl_list_remove(&shsurf->child_link);
	}

	/* Link to new parent */
	wl_list_insert(&parent_surface->child_list, &shsurf->child_link);
	shsurf->parent = parent_surface;
}

/*
 * ias_shell_set_parent_with_position()
 *
 * Sets the parent of a surface, along with the relative position.
 * Child surfaces move with their parent in the default layout
 * (customer layouts loaded by plugins may choose to ignore parent/child links).
 */
static void
ias_shell_set_parent_with_position(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *surface_resource,
		struct wl_resource *parent_resource,
		int32_t x, int32_t y)
{
	struct ias_surface *shsurf = surface_resource->data;
	struct ias_surface *parent_surface = parent_resource->data;

	ias_shell_set_parent(client, resource, surface_resource, parent_resource);

	shsurf->view->geometry.x =
		(int32_t)parent_surface->view->geometry.x + x;
	shsurf->view->geometry.y =
		(int32_t)parent_surface->view->geometry.y + y;

	shsurf->view->transform.dirty = 1;
}

/*
 * ias_shell_popup()
 *
 * Shows the specified surface as a popup with the specified priority.  If
 * a higher priority popup already exists, this popup will be either queued
 * or dropped, according to the config.  If a lower priority popup is already
 * present, it will be replaced, but a "popup_interrupted" event will be
 * raised to notify the app that it may re-raise it, if desired.
 */
static void
ias_shell_popup(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *output_resource,
		struct wl_resource *surface_resource,
		uint32_t priority)
{
	struct ias_shell *shell = resource->data;
	struct ias_surface *shsurf = surface_resource->data;
	struct ias_surface *current_popup;

	shsurf->next_zorder = SHELL_SURFACE_ZORDER_POPUP;
	shsurf->popup.priority = priority;

	/* Assign the surface to the output that the popup is for */
	shsurf->output = weston_head_from_resource(output_resource)->output;

	/*
	 * Figure out whether this surface is high enough priority to set as
	 * the visible popup or whether we should queue it.
	 *
	 * TODO:  The semantics of what we should do here need to be double
	 * checked to see if they actually match what is desired.  At the moment:
	 *   * Popups on different outputs still preempt each other (i.e., the
	 *     constraint is one visible popup globally rather than one visible
	 *     popup per display).
	 *   * Preempted popups (or lower priority popups) just get stuck in
	 *     a queue and get (re-)shown whenever all higher priority popups
	 *     are done.  Clients get no notification that their popup hasn't
	 *     been successfully displayed yet.
	 */
	current_popup = wl_list_first(&shell->popup_surfaces,
		struct ias_surface,
		special_link);
	if (!current_popup) {
		/* No popups currently being shown; add as first and only */
		wl_list_insert(&shell->popup_surfaces, &shsurf->special_link);
	} else if (current_popup->popup.priority < shsurf->popup.priority) {
		/*
		 * This popup is higher priority than the currently active popup.
		 * We should preempt it.  Calling ias_committed() here will
		 * cause the behavior changes to take effect.
		 */
		wl_list_insert(&shell->popup_surfaces, &shsurf->special_link);
		current_popup->next_behavior |= SHELL_SURFACE_BEHAVIOR_HIDDEN;
		ias_committed(current_popup->surface, 0, 0);
	} else {
		/*
		 * There are already higher priority popups up.  Keep it hidden for
		 * now and stick it in the queue.
		 */
		shsurf->next_behavior |= SHELL_SURFACE_BEHAVIOR_HIDDEN;
		wl_list_for_each(current_popup,
			&shell->popup_surfaces,
			special_link)
		{
			if (current_popup->popup.priority < shsurf->popup.priority) {
				wl_list_insert(current_popup->special_link.prev,
					&shsurf->special_link);
				return;
			}
		}

		/* Insert surface at very end of popup list */
		wl_list_insert(shell->popup_surfaces.prev, &shsurf->special_link);
	}
}

/*
 * ias_shell_set_zorder()
 *
 * Assigns a surface to a specified zorder; clients may use zorder
 * values to enforce z-ordering of their surfaces.  Collection values may be in
 * the range 0 - 0x00ffffff.  Collection assignment takes effect the next time
 * a buffer is attached to the surface.
 */
static void
ias_shell_set_zorder(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *surface_resource,
		uint32_t zorder)
{
	struct ias_surface *shsurf = surface_resource->data;

	/*
	 * Make sure the client app doesn't try to use a zorder value that
	 * we reserve for internal use.
	 */
	shsurf->next_zorder = (zorder & 0x00ffffff);
}

/*
 * ias_shell_set_behavior()
 *
 * Sets behavior bits on a surface.  The lower 24-bits will be cleared and
 * set to whatever value is provided.  The upper 8 bits (reserved for
 * internal shell use) will not be changed.
 */
static void
ias_shell_set_behavior(struct wl_client *client,
		struct wl_resource *resource,
		struct wl_resource *surface_resource,
		uint32_t behavior)
{
	struct ias_surface *shsurf = surface_resource->data;

	/* Set lower 24-bits, but don't touch upper 8 bits. */
	shsurf->next_behavior = (behavior & 0x00ffffff) |
		                    (shsurf->behavior & 0xff000000);
}

static void
get_process_info(struct ias_surface *shsurf, const char *name)
{
	char fname[128];
	FILE *fp;
	pid_t pid;
	uid_t uid;
	gid_t gid;
	char *title = NULL;

	/* If we do not have a title for this surface, look up the process
	 * name for the owning wayland client.
	 */
	wl_client_get_credentials(shsurf->client, &pid, &uid, &gid);
	sprintf(fname, "/proc/%d/status", pid);
	fp = fopen(fname, "r");
	if (fp == NULL) {
		title = strdup("No_name");
		if (!title) {
			IAS_ERROR("Out of memory");
			exit(1);
		}
	} else {
		int found_name = 0;
		char *p;
		char buff[256];
		while (fgets(buff, 256, fp)) {
			if (!strncmp(buff, "Name:", 5)) {
				/* Account for the 'Name:' label and any preceding spaces
				 * or tabs.  Also need to remove the trailing carriage
				 * return.
				 */
				p = &buff[5];
				while(isblank(*p)) {
					p++;
				}
				title = strdup(p);
				if (!title) {
					IAS_ERROR("Out of memory");
					exit(1);
				}
				title[strlen(p)-1] = '\0';
				found_name = 1;
				break;
			}
		}
		if (found_name == 0) {
			title = strdup("No_name");
			if (!title) {
				IAS_ERROR("Out of memory");
				exit(1);
			}
		}
		fclose(fp);
	}

	/* first let's see if the title is already set */
	if (!shsurf->title || (shsurf->title[0] == '\0')) {
		/* If title was set, free it */
		if (shsurf->title) {
			free(shsurf->title);
		}

		/* now let's see if the name was specified */
		if(name) {
			shsurf->title = strdup(name);
		} else  {
			shsurf->title = strdup(title);
		}
		if (!shsurf->title) {
			IAS_ERROR("Out of memory");
			exit(1);
		}
	} else {
		IAS_DEBUG("Title already set: %s\n", shsurf->title);
	}
	shsurf->pid = pid;
	if(title) {
		/* free any previously set pname */
		if (shsurf->pname) {
			free(shsurf->pname);
		}
		shsurf->pname = strdup(title);
	}
	/* free the memory used by this local variable after we are done with it */
	free(title);
}

static void
ias_shell_get_ias_surface_internal(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t id,
		struct wl_resource *surface_resource,
		const char *name,
		bool wl_surface)
{
	struct weston_surface *surface = surface_resource->data;
	struct ias_shell *shell = shell_resource->data;
	struct ias_surface *shsurf;
	struct hmi_callback *cb;

	shsurf = get_ias_surface(surface);
	if (shsurf) {
		/*
		 * The purpose of this request is to create a new shell surface for an
		 * existing wl_surface; if a shell surface has already been created for
		 * this wl_surface it's an error.  The Weston desktop shell returns an
		 * WL_DISPLAY_ERROR_INVALID_OBJECT event in this case, which seems like
		 * a strange choice (that is supposed to be used for when the server
		 * can't find a specified object), but we'll return the same error code
		 * for consistency.
		 */
		wl_resource_post_error(surface_resource,
				WL_DISPLAY_ERROR_INVALID_OBJECT,
				"ias_shell :: Shell surface already created");
		return;
	}

	/* Create the shell_surface for this wl_surface */
	shsurf = ias_surface_constructor(shell, surface, &hmi_client);
	if (!shsurf) {
		/*
		 * Again, this choice of error code is a bit odd, but it's consistent
		 * with Weston'd desktop shell behavior.
		 */
		wl_resource_post_error(surface_resource,
				WL_DISPLAY_ERROR_INVALID_OBJECT,
				"Failed to create shell surface");
		return;
	}

	/* Setup the pointer back to the client that created the surface */
	shsurf->client = client;

	/*
	 * Make sure we keep a reference back to the resource for the shell
	 * (so we can raise configure events from it in the future).
	 */
	shsurf->shell_resource = shell_resource;

	/* Surface sharing is disabled by default for the new surface */
	shsurf->shareable = 0;

	/* get pid and pname for this surface and store them into shsurf */
	get_process_info(shsurf, name);

	if (!wl_surface) {
		/* Setup resource data from shell surface */
		shsurf->resource = wl_resource_create(client,
				&ias_surface_interface, 1, id);
		wl_resource_set_implementation(shsurf->resource,
				&ias_surface_implementation,
				shsurf, destroy_ias_surface_resource);
	} else {
		shsurf->resource = wl_resource_create(client,
				&wl_shell_surface_interface, 1, id);
		wl_resource_set_implementation(shsurf->resource,
				&shell_surface_implementation,
				shsurf, destroy_ias_surface_resource);
		shsurf->wl_shell_interface = 1;
	}

	/*
	 * Put the shell surface on the surface list.  This list is used
	 * by the set position api to find the correct shell surface. Logically,
	 * this should send the surface object ID to the HMI client and the HMI
	 * client would then just send the surface object ID back. However,
	 * the object is a application client ID, not HMI client ID so that
	 * doesn't work.  Instead, we'll send the address of the shell surface
	 * object. To the HMI client, the address is nothing but an opaque
	 * identifier.
	 */
	wl_list_insert(&shell->client_surfaces, &shsurf->surface_link);

	wl_list_for_each(cb, &shell->sfc_change_callbacks, link) {
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


static void
ias_shell_get_ias_surface(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t id,
		struct wl_resource *surface_resource,
		const char *name)
{
	ias_shell_get_ias_surface_internal(
		client, shell_resource, id, surface_resource, name, false);
}

static const struct ias_shell_interface ias_shell_implementation = {
	ias_shell_set_background,
	ias_shell_set_parent,
	ias_shell_set_parent_with_position,
	ias_shell_popup,
	ias_shell_set_zorder,
	ias_shell_set_behavior,
	ias_shell_get_ias_surface,
};


/*
 * WL_SHELL
 * wl_shell_get_shell_surface
 *
 * If the wl_shell interface is being used by a client, provide a
 * wl_shell_surface and the expected interfaces similar to what the desktop
 * shell provides.
 *
 * This exist to aid with client development by supporting existing code
 * while it is ported to the IAS shell interfaces. It is not expected that
 * this will be used in a production IVI environment.
 */
static void
wl_shell_get_shell_surface(struct wl_client *client,
		struct wl_resource *shell_resource,
		uint32_t id,
		struct wl_resource *surface_resource)
{
	ias_shell_get_ias_surface_internal(
		client, shell_resource, id, surface_resource, "", true);
}

/* WL_SHELL */
static const struct wl_shell_interface wl_shell_implementation = {
	wl_shell_get_shell_surface,
};


/***
 *** ias-shell-config.c functions
 ***/

void
ias_shell_configuration(struct ias_shell *shell);


/***
 *** Debug/development-only helpers.
 ***
 *** These helper functions are used internally by the shell, and are only
 *** built in when doing debug builds.
 ***/
#if IASDEBUG

/*
 * terminate_binding()
 *
 * Handles ctrl+alt+backspace by shutting down the compositor.
 */
static void
terminate_binding(struct wl_seat *seat,
		const struct timespec *time,
		uint32_t key,
		void *data)
{
	struct weston_compositor *compositor = data;

	wl_display_terminate(compositor->wl_display);
}

#endif


/***
 *** Helper functions
 ***/

/*
 * ias_shell_destructor()
 *
 * Shell destructor function.  This is hooked to the compositor's
 * "destroy" signal.
 */
static void
ias_shell_destructor(struct wl_listener *listener, void *data)
{
	struct ias_shell *shell =
		container_of(listener, struct ias_shell, destroy_listener);

	/* Kill the HMI client */
	if (shell->hmi.client) {
		wl_client_destroy(shell->hmi.client);
	}
	free(shell->hmi.execname);

	free(shell);
}


/*
 * WL_SHELL
 * unbind_ias_shell()
 *
 * Called when the client is exiting.  Remove client from the ias_shell
 * clients list.
 */
static void
unbind_ias_shell(struct wl_resource *resource)
{
	struct bound_client *bound, *next;
	struct ias_shell *shell = resource->data;

	wl_list_for_each_safe(bound, next, &shell->ias_shell_clients, link) {
		if (resource->client == bound->client_id) {
			wl_list_remove(&bound->link);
			free(bound);
			return;
		}
	}
}

/*
 * bind_ias_shell()
 *
 * Handler executed when client binds to IAS-specific shell interface.  Simply
 * binds the global object to a client-specific object ID.
 */
static void
bind_ias_shell(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct ias_shell *shell = data;
	struct wl_resource *hmi_client;
	struct wl_resource *resource;
	struct bound_client *bound;

	resource = wl_resource_create(client, &ias_shell_interface, 1, id);
	if (resource) {
		wl_resource_set_implementation(resource, &ias_shell_implementation,
				shell, unbind_ias_shell);
	}

	/* Check if client is bound to wl_shell interface */
	wl_list_for_each(bound, &shell->wl_shell_clients, link) {
		if (bound->client_id == client) {
			/* Already bound to wl_shell */
			IAS_ERROR("Client previously bound to wl_shell.");
			wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
					"Permission to bind IAS shell denied.");
			wl_resource_destroy(resource);
			return;
		}
	}

	/* Add client to ias shell client list */
	bound = calloc(1, sizeof(*bound));
	if(!bound) {
		IAS_ERROR("Failed to allocate shell client list resource.");
		exit(1);
	}
	bound->client_id = client;
	wl_list_insert(&self->ias_shell_clients, &bound->link);

	if (client == self->hmi.client) {
		hmi_client = wl_resource_create(client, &ias_shell_interface, 1, id);
		if (hmi_client) {
			wl_resource_set_implementation(hmi_client, &ias_shell_implementation,
					shell, NULL);
		}
		self->hmi.ias_shell = hmi_client;
	}
}

/*
 * WL_SHELL
 * unbind_wl_shell()
 *
 * Called when the client is exiting.  Remove client from the wl_shell
 * clients list.
 */
static void
unbind_wl_shell(struct wl_resource *resource)
{
	struct bound_client *bound, *next;
	struct ias_shell *shell = resource->data;

	wl_list_for_each_safe(bound, next, &shell->wl_shell_clients, link) {
		if (resource->client == bound->client_id) {
			wl_list_remove(&bound->link);
			free(bound);
			return;
		}
	}
}

/*
 * WL_SHELL
 * bind_wl_shell()
 *
 * Handler executed when client binds to the non-IAS WL Shell interface.
 * Simply binds the global object to a client-specific object ID.
 */
static void
bind_wl_shell(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct ias_shell *shell = data;
	struct wl_resource *resource;
	struct bound_client *bound;

	resource = wl_resource_create(client, &wl_shell_interface, 1, id);
	if (resource) {
		wl_resource_set_implementation(resource, &wl_shell_implementation,
						shell, unbind_wl_shell);
	}

	/* Check if client is bound to ias_shell interface */
	wl_list_for_each(bound, &shell->ias_shell_clients, link) {
		if (bound->client_id == client) {
			/* Already bound to ias_shell */
			IAS_ERROR("Client previously bound to ias_shell.");
			wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
					"Permission to bind wl shell denied.");
			wl_resource_destroy(resource);
			return;
		}
	}

	/* Add client to wl shell client list */
	bound = calloc(1, sizeof(*bound));
	if(!bound) {
		IAS_ERROR("Failed to allocate shell client list resource.");
		exit(1);
	}
	bound->client_id = client;
	wl_list_insert(&self->wl_shell_clients, &bound->link);
}

/*
 *  surface_exists()
 *
 * Checks if the surface exists
 */
static bool
surface_exists(struct weston_surface *surface,
			struct ias_shell *shell)
{

	struct ias_surface *shsurf;
	bool ret = false;

	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		if (shsurf->surface == surface) {
			ret = true;
			break;
		}
	}

	return ret;
}

/*
 * surface_revert_keyboard_focus()
 *
 * Change back the keyboard focus to the surface that had it previously
 */
static void
surface_revert_keyboard_focus(struct ias_surface *shsurf,
				struct ias_shell *shell)
{
	struct weston_surface *surface = shsurf->surface;
	struct weston_compositor *compositor = surface->compositor;
	struct weston_seat *seat;
	struct weston_surface *focus;
	struct custom_surface *prev_focus;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		if (!keyboard)
			continue;

		/* get information about keyboard focus state after last focus change */
		focus = keyboard->last_state.focus;

		/* check if current surface has keyboard focus assigned*/
		if (focus == surface) {
			wl_list_for_each(prev_focus, &keyboard->last_state.prev_focus_list, link) {

				if (surface_exists(prev_focus->surf, shell)) {
					weston_keyboard_set_focus(keyboard, prev_focus->surf);
					wl_list_remove(&prev_focus->link);
					free(prev_focus);
					break;
				}
			}
		}
		break;
	}
}


/*
 * surface_remove_prev_focus_list()
 *
 * Removes surface from prev_focus_list
 */
static void
surface_remove_prev_focus_list(struct ias_surface *shsurf,
				struct ias_shell *shell)
{
	struct weston_surface *surface = shsurf->surface;
	struct weston_compositor *compositor = surface->compositor;
	struct weston_seat *seat;
	struct custom_surface *prev_focus;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		struct weston_keyboard *keyboard =
			weston_seat_get_keyboard(seat);

		if (!keyboard)
			continue;

		/* find surface to remove from surface list */
		wl_list_for_each(prev_focus, &keyboard->last_state.prev_focus_list, link) {
			if (prev_focus->surf == surface) {
				wl_list_remove(&prev_focus->link);
				free(prev_focus);
				break;
			}
		}
		break;
	}
}




/*
 * ias_surface_destructor()
 *
 * Destroys a IAS surface.
 */
static void
ias_surface_destructor(struct ias_surface *shsurf)
{
	struct ias_shell *shell = shsurf->shell;
	struct ias_surface *child;
	struct hmi_callback *cb;
	struct frame_data *fd, *fd_tmp;

	/* remove destroyed surface from prev_focus_list */
	surface_remove_prev_focus_list(shsurf, shell);
	/* try to set back the keyboard focus to the previous surface */
	surface_revert_keyboard_focus(shsurf, shell);

	/* Notify ias_hmi listeners of the surface destruction */
	wl_list_for_each(cb, &shell->sfc_change_callbacks, link) {
		ias_hmi_send_surface_destroyed(cb->resource, SURFPTR2ID(shsurf),
				shsurf->title,
				shsurf->pid,
				shsurf->pname);
	}

	/* Remove ourselves from the surface's destructor list */
	wl_list_remove(&shsurf->surface_destroy_listener.link);

	/* There is no longer a shell surface associated with this wl_surface */
	shsurf->surface->committed = NULL;

	/* Destroy the ping timeout timer. */
	if (shsurf->ping_info.source) {
		wl_event_source_remove(shsurf->ping_info.source);
		shsurf->ping_info.source = NULL;
	}

	/* Remove surface from surface lists */
	wl_list_remove(&shsurf->surface_link);

	/* Remove surface from popup/background special surface lists */
	wl_list_remove(&shsurf->special_link);

	/* Break parent/child connections */
	if (shsurf->parent) {
		wl_list_remove(&shsurf->child_link);
	}
	wl_list_for_each(child, &shsurf->child_list, child_link) {
		child->parent = NULL;
	}

	/*
	 * Was this a popup surface?  If so, unhide the top priority popup.
	 * (note that if we weren't actually the highest priority popup
	 * it's still okay to "unhide" the already visible popup.
	 */
	if (shsurf->next_zorder == SHELL_SURFACE_ZORDER_POPUP) {
		struct ias_surface *top_popup;
		top_popup = wl_list_first(&shell->popup_surfaces,
				struct ias_surface,
				special_link);

		if (top_popup) {
			top_popup->next_behavior &= ~SHELL_SURFACE_BEHAVIOR_HIDDEN;

			/* Call configure to make the behavior change take effect. */
			ias_committed(top_popup->surface, 0, 0);
		}
	}

	wl_list_for_each_safe(fd, fd_tmp, &shsurf->output_list, output_link) {
		wl_list_remove(&fd->output_link);
		free(fd);
	}

	/*
	 * No need for NULL checks here, as pname and title
	 * are initialized to empty strings in ias_surface_constructor
	 */
	free(shsurf->pname);
	free(shsurf->title);

	/* Remove surface from surface lists and deallocate it */
	free(shsurf);
}

/*
 * handle_surface_destroy()
 *
 * Handler for surface destruction that will destroy the ias shell surface
 * when the underlying surface is destroyed.
 */
static void
handle_surface_destroy(struct wl_listener *listener, void *data)
{
	struct ias_surface *shsurf = container_of(listener,
			struct ias_surface,
			surface_destroy_listener);

	/*
	 * If a resource has been created for client-side tracking, destroy
	 * the resource (which will trigger the destructor).  Otherwise just
	 * call the destructor directly.
	 */
	if (shsurf->resource) {
		wl_resource_destroy(shsurf->resource);
	} else {
		wl_signal_emit(&shsurf->destroy_signal,
				shsurf->resource);
		ias_surface_destructor(shsurf);
	}
}

/*
 * ias_surface_constructor()
 *
 * IAS shell surface constructor.
 */
static struct ias_surface *
ias_surface_constructor(void *shellptr,
		struct weston_surface *surface,
		const struct weston_shell_client *hmi_client)
{
	struct ias_shell *shell = shellptr;
	struct ias_surface *shsurf;
	struct weston_output *output;
	struct frame_data *fd;

	/*
	 * Make sure no shell surface has already been created for this
	 * wl_surface.
	 */
	if (surface->committed) {
		IAS_ERROR("Surface %p already has shell surface", surface);
		return NULL;
	}

	shsurf = calloc(1, sizeof *shsurf);
	if (!shsurf) {
		IAS_ERROR("Failed to allocate shell surface");
		return NULL;
	}

	shsurf->view = weston_view_create(surface);
	if (!shsurf->view) {
		IAS_ERROR("no memory to allocate shell surface\n");
		free(shsurf);
		return NULL;
	}

	/* Hook our shell's configure handler to the base surface */
	surface->committed = ias_committed;
	surface->committed_private = shsurf;

	/* Setup IAS surface data */
	shsurf->shell = shell;
	shsurf->surface = surface;
	shsurf->hmi_client = hmi_client;
	wl_list_init(&shsurf->child_list);
	wl_list_init(&shsurf->fullscreen_transform.link);

	/* Hook up handler for client-side destruction  */
	wl_signal_init(&shsurf->destroy_signal);
	shsurf->surface_destroy_listener.notify = handle_surface_destroy;
	wl_signal_add(&surface->destroy_signal,
			&shsurf->surface_destroy_listener);

	/* Initialize rotation */
	wl_list_init(&shsurf->rotation.transform.link);
	weston_matrix_init(&shsurf->rotation.rotation);

	/*
	 * Initialize special surface list node so that we don't segfault if
	 * we try to remove the surface from a list it was never part of.
	 */
	wl_list_init(&shsurf->special_link);

	wl_list_init(&shsurf->surface_link);
	wl_list_init(&shsurf->output_list);

	wl_list_for_each(output, &surface->compositor->output_list, link) {
		fd = calloc(1, sizeof *fd);
		fd->output_id = output->id;
		wl_list_init(&fd->output_link);
		wl_list_insert(&shsurf->output_list, &fd->output_link);
	}

	/*
	 * Initialize process id and name for the client app associated with this
	 * surface
	 */
	shsurf->pid = 0;

	/*
	 * The surface title and pname can't be NULL or demarshalling will fail
	 * in some cases. Thus set the title and pname to something here.
	 */
	shsurf->title = strdup("\0");
	shsurf->pname = strdup("\0");

	return shsurf;
}

/*
 * destroy_ias_surface_resource()
 *
 * Callback for ias_surface resource destruction.  Simply calls the ias_surface
 * destructor to destroy the underlying ias_surface.
 */
static void
destroy_ias_surface_resource(struct wl_resource *resource)
{
	struct ias_surface *shsurf = resource->data;

	ias_surface_destructor(shsurf);
}

/*
 * get_ias_surface()
 *
 * Fetches the shell surface associated with a base weston_surface.
 */
WL_EXPORT struct ias_surface *
get_ias_surface(struct weston_surface *surface)
{
	if (!surface) {
		return NULL;
	}

	if (surface->committed == ias_committed) {
		return surface->committed_private;
	} else {
		return NULL;
	}
}

/*
 * Range from SHELL_SURFACE_ZORDER_DEFAULT to
 * SHELL_SURFACE_ZORDER_BACKGROUND - 1,
 * can be used for regular surfaces by ias shell,
 * weston allows for values between WESTON_LAYER_POSITION_NORMAL
 * and WESTON_LAYER_POSITION_UI - 1 to be used for that purpose.
 * Currently range defined by weston layer positions is greater than
 * range defined by ias shell, but if that will change in future 
 * it won't be possible to simply map ias shell range to wayland layer range.
 */
static_assert(SHELL_SURFACE_ZORDER_BACKGROUND - SHELL_SURFACE_ZORDER_DEFAULT < WESTON_LAYER_POSITION_UI - WESTON_LAYER_POSITION_NORMAL,
	      "IAS Shell normal zorder range not fitting in weston normal layer position range!!!");

/*
 * Transforms zorder values used by ias shell to values used by weston
 */
static int
ias_zorder_to_weston_layer_position(uint32_t zorder)
{
	switch(zorder) {
		case SHELL_SURFACE_ZORDER_BACKGROUND:
			return WESTON_LAYER_POSITION_BACKGROUND;
		case SHELL_SURFACE_ZORDER_FULLSCREEN:
			return WESTON_LAYER_POSITION_FULLSCREEN;
		case SHELL_SURFACE_ZORDER_POPUP:
			return WESTON_LAYER_POSITION_TOP_UI;
		default:
			return WESTON_LAYER_POSITION_NORMAL + zorder;
	}
}

/*
 * add_to_layer()
 *
 * Adds a surface to the appropriate weston layer.
 */
static bool
add_to_layer(struct ias_surface *shsurf)
{
	struct ias_shell *shell = shsurf->shell;
	struct weston_layer *layer = NULL;
	struct custom_zorder *zorder, *new_zorder;
	bool surf_focus = true;

	/*
	 * If surface is currently hidden (e.g., lower priority popup), don't
	 * add it to any layer.
	 */
	if (shsurf->behavior & SHELL_SURFACE_BEHAVIOR_HIDDEN) {
		return false;
	}

	switch (shsurf->zorder) {
	case SHELL_SURFACE_ZORDER_BACKGROUND:
		layer = &shell->background_layer;
		break;
	case SHELL_SURFACE_ZORDER_FULLSCREEN:
		layer = &shell->fullscreen_layer;
		break;
	case SHELL_SURFACE_ZORDER_POPUP:
		layer = &shell->popup_layer;
		break;
	case SHELL_SURFACE_ZORDER_DEFAULT:
		layer = &shell->default_app_layer;
		__attribute__((fallthrough));
	default:
		/*
		 * Not a built-in zorder, must be user-defined.  Walk
		 * the zorder list looking for it.
		 */
		wl_list_for_each(zorder, &shell->custom_layers, link) {
			/*
			 * If the new surface has z-order value == SHELL_SURFACE_ZORDER_DEFAULT
			 * check if there are surfaces with greater z-order value
			 */
			if (shsurf->zorder == SHELL_SURFACE_ZORDER_DEFAULT) {
				if (zorder->id > SHELL_SURFACE_ZORDER_DEFAULT &&
					zorder->id < SHELL_SURFACE_ZORDER_BACKGROUND) {
					surf_focus = false;
					break;
				}
				continue;
			}

			/*
			 * The zorder list is sorted; walk it until we hit a
			 * greater zorder value.
			 */
			if (zorder->id < shsurf->zorder) {
				continue;
			}

			/*
			 * If we iterate to a higher zorder value, then the value we
			 * wanted is missing and we should create it now.
			 */
			if (zorder->id > shsurf->zorder) {

				/* Create new zorder layer */
				new_zorder = calloc(1, sizeof *new_zorder);
				new_zorder->id = shsurf->zorder;
				weston_layer_init(&new_zorder->layer, shell->compositor);
				weston_layer_set_position(&new_zorder->layer,
							  ias_zorder_to_weston_layer_position(new_zorder->id));
				wl_list_insert(zorder->link.prev, &new_zorder->link);

				/* Point at new zorder for surface insertion below */
				zorder = new_zorder;

				/*
				 * There is existing surface with greater z-order value, so
				 * the new existing surface will not have keyboard input attached
				 */
				if (zorder->id < SHELL_SURFACE_ZORDER_BACKGROUND) {
					surf_focus = false;
				}
			}


			/*
			 * zorder now points to the custom zorder we want to place
			 * this surface at; insert it into the surface list.
			 */
			layer = &zorder->layer;
			break;
		}

		if (layer == NULL) {
			/* The list was probably empty so create an entry now.  */
			new_zorder = calloc(1, sizeof *new_zorder);
			new_zorder->id = shsurf->zorder;

			/* Position the new layer below the full screen layer */
			weston_layer_init(&new_zorder->layer,
					  shell->compositor);
			weston_layer_set_position(&new_zorder->layer,
						  ias_zorder_to_weston_layer_position(new_zorder->id));
			wl_list_insert(zorder->link.prev, &new_zorder->link);

			/* Point at new zorder for surface insertion below */
			zorder = new_zorder;
			layer = &zorder->layer;
		}
	}

	weston_layer_entry_insert(&layer->view_list, &shsurf->view->layer_link);
	shsurf->layer = layer;

	return surf_focus;
}

/*
 * map()
 *
 * Maps a surface, making it visible.  This places the surface in the
 * appropriate surface list, assigns it to an output, and in the future may
 * kick off any desired transitional animations.
 *
 * Note that we re-map a surface if its zorder value changes so that it
 * will be moved to the appropriate layer surface list.
 */
static void
map(struct ias_shell *shell,
		struct weston_surface *surface,
		int32_t width,
		int32_t height,
		int32_t sx,
		int32_t sy)
{
	struct ias_surface *shsurf = get_ias_surface(surface);
	struct weston_seat *seat;
	bool surf_focus = false;

	/* We can't map non-IAS surfaces */
	if (!shsurf) {
		return;
	}

	/*
	 * Since we might re-map an already-mapped surface (if the zorder
	 * changes), we need to remove it from any surface list that it's
	 * already on.
	 */
	if (shsurf->layer) {
		weston_layer_entry_remove(&shsurf->view->layer_link);
		shsurf->layer = NULL;
	}

	/* Zorder change takes effect now */
	shsurf->zorder = shsurf->next_zorder;

	/* Perform extra handling for special zorders */
	switch (shsurf->zorder) {
		case SHELL_SURFACE_ZORDER_BACKGROUND:
#ifdef IASDEBUG
			/*
			 * If the HMI is properly setting a background of its own, we no
			 * longer have any need for the default background, so destroy
			 * it.
			 */
			if (shell->default_background) {
				weston_surface_destroy(shell->default_background);
				shell->default_background = NULL;
			}
#endif
			/* Background should always match output's position */
			weston_view_set_position(shsurf->view,
					shsurf->output->x, shsurf->output->y);
			break;
		case SHELL_SURFACE_ZORDER_POPUP:
			/* Popups should be centered */
			weston_view_set_position(shsurf->view,
					shsurf->output->x + (shsurf->output->current_mode->width - width) / 2,
					shsurf->output->y + (shsurf->output->current_mode->height - height) / 2);
			break;

		case SHELL_SURFACE_ZORDER_DEFAULT:
		case SHELL_SURFACE_ZORDER_FULLSCREEN:
		default:
			/* Noop */
			;
	}

	/* Add the surface to the appropriate layer surface list */
	surf_focus = add_to_layer(shsurf);

	/* Activate surface so that it receives input events */
	wl_list_for_each(seat, &surface->compositor->seat_list, link) {
		/* Don't activate popups that are hidden */
		if (!(shsurf->behavior & SHELL_SURFACE_BEHAVIOR_HIDDEN)) {
			/* if surface is allowed to have keyboard input */
			if (surf_focus) {
				weston_seat_set_keyboard_focus(seat, surface);
			} else {
				surface_revert_keyboard_focus(shsurf, shell);
			}
		}
	}

	/* Damage surface */
	weston_surface_damage(surface);

	/*
	 * Ideally, we should be calling weston_surface_assign_output(surface) here
	 * to assign an output to this surface. However, since we cannot call
	 * weston_surface_assign_output(surface) directly as it is not exposed, we
	 * set the geometry to dirty so that weston_surface_assign_output will be
	 * called by weston_surface_update_transform which will be called after map()
	 * inside ias_committed.
	 */
	shsurf->view->transform.dirty = 1;

	/*
	 * Note for future:  if we wanted to trigger any surface transitional
	 * animations, this would be the place to initialize and kick them off.
	 */
}


static void
update_frame_count_and_attach(struct weston_surface *es, struct weston_buffer *buffer)
{
	struct ias_surface *shsurf = get_ias_surface(es);
	struct frame_data *fd;

	(*renderer_attach)(es, buffer);

	if (shsurf) {
		wl_list_for_each(fd, &shsurf->output_list, output_link) {
			if (es->output_mask & (1u << fd->output_id)) {
				fd->frame_count++;
			}
		}
	}
}

static void fill_surf_name(struct weston_surface *surface, int len,
		char *surf_name)
{
	struct ias_surface *shsurf = get_ias_surface(surface);

	assert(shsurf);
	if(shsurf->title) {
		strncpy(surf_name, shsurf->title, len);
	}
}

static uint64_t get_surf_id(struct weston_surface *surface)
{
	struct ias_surface *shsurf = get_ias_surface(surface);

	assert(shsurf);
	return (uint64_t)shsurf;
}

static int get_shareable_flag(struct weston_surface *surface)
{
	struct ias_surface *shsurf = get_ias_surface(surface);

	if (shsurf) {
		return shsurf->shareable;
	}

	return 0;
}

static void
print_fps(struct wl_listener *listener, void *data)
{
	struct ias_shell *shell = self;
	float time_diff_secs;
	uint32_t curr_time_ms;
	uint32_t diff_time_ms;
	struct ias_surface *shsurf;
	struct ias_output *ias_output = data;
	struct frame_data *fd;
	int    timehit = 0;
	int    do_print = 0;
	struct hmi_callback *cb;

	gettimeofday(&curr_time, NULL);
	curr_time_ms = (curr_time.tv_sec * 1000 + curr_time.tv_usec / 1000);
	diff_time_ms = curr_time_ms - ias_output->prev_time_ms;

	time_diff_secs = (diff_time_ms) / 1000;

	if (time_diff_secs >= metrics_timing) {
		timehit = 1;
		if (do_print_fps) {
			do_print = 1;
			fprintf(stdout, "--------------------------------------------------------\n");
			fprintf(stdout, "%s[%d]: output %d flips\n", ias_output->name, ias_output->base.id,
				ias_output->flip_count-ias_output->last_flip_count);
		}

		wl_list_for_each(cb, &shell->ias_metrics_callbacks, link) {
			ias_metrics_send_output_info(cb->resource, diff_time_ms, ias_output->name, ias_output->base.id, ias_output->flip_count-ias_output->last_flip_count);
		}
		ias_output->last_flip_count = ias_output->flip_count;
	}

	wl_list_for_each(shsurf, &shell->client_surfaces, surface_link) {
		/* We only show those surfaces that are on this particular output */
		if (!(shsurf->view->output_mask & (1 << ias_output->base.id)))
			continue;

		/*
		 * Since a surface could be on multiple outputs, we now need to show
		 * the frame count for a particular output only
		 */
		wl_list_for_each(fd, &shsurf->output_list, output_link) {
			if (ias_output->base.id == fd->output_id) {
				fd->flip_count++;
				if (timehit) {
					if (do_print) {
						fprintf(stdout, "%s:%u: %d frames, %d flips in %6.3f seconds = %6.3f FPS\n",
							shsurf->pname, SURFPTR2ID(shsurf), fd->frame_count, fd->flip_count, time_diff_secs,
							fd->frame_count / time_diff_secs);
						fflush(stdout);
					}

					wl_list_for_each(cb, &shell->ias_metrics_callbacks, link) {
						ias_metrics_send_process_info(cb->resource, SURFPTR2ID(shsurf), shsurf->title, 
								shsurf->pid, shsurf->pname, ias_output->base.id, 
								diff_time_ms, fd->frame_count, fd->flip_count);
					}

					fd->frame_count = 0;
					fd->flip_count = 0;

					ias_output->prev_time_ms = curr_time_ms;
				}
			}
		}
	}
	if (timehit) {
		ias_output->prev_time_ms = curr_time_ms;
	}

	if (do_print) {
		fprintf(stdout, "--------------------------------------------------------\n");
		fflush(stdout);
	}
}

static void scale_surface_if_fullscreen(struct ias_surface *shsurf)
{
	struct weston_surface *surface = shsurf->surface;

	/*
	 * Remove existing transform. We will recalculate it below if still
	 * relevant.
	 */
	wl_list_remove(&shsurf->fullscreen_transform.link);
	wl_list_init(&shsurf->fullscreen_transform.link);

	/*
	 * If the client requested its buffers to be fullscreen but didn't
	 * actually provide fullscreen buffers, then the compositor is free
	 * to do whatever it wants. The code below scales the buffer to
	 * fullscreen
	 */
	if (shsurf->zorder == SHELL_SURFACE_ZORDER_FULLSCREEN &&
			shsurf->output &&
			(shsurf->output->width != surface->width ||
			 shsurf->output->height != surface->height)) {
		struct weston_matrix *matrix;
		float xscale, yscale;

		matrix = &shsurf->fullscreen_transform.matrix;
		weston_matrix_init(matrix);

		xscale = (float) shsurf->output->width /
			(float) surface->width;
		yscale = (float) shsurf->output->height /
			(float) surface->height;

		weston_matrix_scale(matrix, xscale, yscale, 1);
		wl_list_insert(&shsurf->view->geometry.transformation_list,
				&shsurf->fullscreen_transform.link);
		weston_view_set_position(shsurf->view, shsurf->output->x, shsurf->output->y);
	}
}

/*
 * ias_committed()
 *
 * Called to size/position a surface, typically after a new buffer is attached.
 * This is where most of the state that we've been tracking, such as zorder and
 * behavior, should actually take effect.
 */
WL_EXPORT void
ias_committed(struct weston_surface *surface, int32_t relx, int32_t rely)
{
	struct ias_surface *shsurf = get_ias_surface(surface);
	struct ias_shell *shell;
	int mapping_change = 0, geometry_change = 0;
	GLfloat oldx, oldy;
	GLfloat newx, newy;
	int32_t sx = 0, sy = 0;
	uint32_t old_hidden;
	uint32_t new_hidden;
	struct hmi_callback *cb;
	struct weston_output *old_output;
#ifdef BUILD_REMOTE_DISPLAY
	struct ias_backend *ias_backend =
			(struct ias_backend *)surface->compositor->backend;
#endif

	/* Shouldn't be possible to get here with non-IAS surfaces */
	assert(shsurf);

	shell = shsurf->shell;
	old_hidden = shsurf->behavior & SHELL_SURFACE_BEHAVIOR_HIDDEN;
	new_hidden = shsurf->next_behavior & SHELL_SURFACE_BEHAVIOR_HIDDEN;

	/*
	 * If racing apps submit popups at nearly the same time, it's possible
	 * that a popup will get re-configured before its first buffer is
	 * attached.  If we get here without a buffer, just bail out since
	 * we know we'll be called again as soon as the client does send its
	 * first buffer.
	 */
	if (!surface->width_from_buffer) {
		return;
	}

	/* Make sure the behavior bits are updated. */
	if (shsurf->behavior != shsurf->next_behavior) {
		shsurf->behavior = shsurf->next_behavior;
		mapping_change = 1;
	}

	/*
	 * If a surface becomes hidden, damage the area below it and remove it from
	 * its layer.  No further processing will be needed until it becomes
	 * visible again.
	 */
	if (new_hidden && !old_hidden) {
		weston_view_damage_below(shsurf->view);
		weston_layer_entry_remove(&shsurf->view->layer_link);
		return;
	}

	/*
	 * If a previously hidden surface becomes visible, then it needs to be
	 * remapped into a layer.
	 */
	if (old_hidden && !new_hidden) {
		mapping_change = 1;
	}

	/*
	 * If a surface changes zorder, then it needs to be remapped into the
	 * appropriate layer.
	 */
	if (shsurf->zorder != shsurf->next_zorder) {
		mapping_change = 1;
		weston_view_damage_below(shsurf->view);
	}

	/*
	 * If the ias_hmi interface has been used to reposition the surface, take
	 * the absolute position that was set.
	 *
	 * TODO: Could we use a surface behavior bit instead of a shell
	 * surface field?
	 */
	if (shsurf->position_update) {
		/* Absolute position (via set_position()) */
		sx = shsurf->x;
		sy = shsurf->y;
		geometry_change = 1;
		shsurf->position_update = 0;
	} else {
		/* Relative position */
		weston_view_to_global_float(shsurf->view, 0, 0, &oldx, &oldy);
		weston_view_to_global_float(shsurf->view, relx, rely, &newx, &newy);
		sx = (uint32_t)(shsurf->view->geometry.x + newx - oldx);
		sy = (uint32_t)(shsurf->view->geometry.y + newy - oldy);

		/* Did relative surface position change? */
		if (relx || rely) {
			geometry_change = 1;
		}
	}

	/*
	 * shsurf->last_width/last_height hold the current width and height of
	 * this surface whereas surface->width/height hold the new width and
	 * height.
	 */
	if (surface->width != shsurf->last_width ||
			surface->height != shsurf->last_height) {
		/* surface size changed */
		geometry_change = 1;
	}

	/*
	 * If the surface hasn't been mapped to an output yet, or if there was a
	 * change that requires re-mapping, do that first.
	 */
	if (!weston_surface_is_mapped(surface) || mapping_change) {
		if (!(shsurf->next_behavior & SHELL_SURFACE_BEHAVIOR_HIDDEN) &&
				!(shsurf->next_behavior & IAS_HMI_INPUT_OWNER)) {
			map(shell, surface, surface->width, surface->height,
					sx, sy);
			surface->is_mapped = true;
			shsurf->view->is_mapped = true;
		}
	}

	/*
	 * Set the surface size and position, and mark geometry dirty so that
	 * transforms are updated.  Maximized windows should always start at
	 * (0,0) for now (TODO: we can modify this to handle centering-based
	 * fullscreen later, if necessary).
	 */
	if(geometry_change) {
		if (shsurf->zorder == SHELL_SURFACE_ZORDER_FULLSCREEN &&
				shsurf->output) {
			sx = shsurf->output->region.extents.x1;
			sy = shsurf->output->region.extents.y1;
		}

		weston_view_damage_below(shsurf->view);

		weston_view_set_position(shsurf->view, sx, sy);
		shsurf->last_width = surface->width;
		shsurf->last_height = surface->height;

		weston_surface_damage(surface);

		scale_surface_if_fullscreen(shsurf);
	}

	old_output = surface->output;
	weston_view_update_transform(shsurf->view);

	/*
	 * If the output for this surface has changed, then let's inform the backend
	 */
#ifdef BUILD_REMOTE_DISPLAY
	if(old_output != surface->output) {
		ias_backend->change_capture_output(ias_backend, surface);
	}
#else
	/* To get rid of a set but not used warning */
	old_output = old_output;
#endif

	/*
	 * Notify listeners on ias_hmi interface that this surface has
	 * been updated.
	 */
	if (geometry_change || mapping_change) {

		/* We must damage the surface after updating its transform */
		weston_surface_damage(surface);

		wl_list_for_each(cb, &shell->sfc_change_callbacks, link) {
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
}

/*
 * activate_surface()
 *
 * Activates surface clicked or touched by the user.
 */
static void
activate_surface(struct weston_surface *surface,
		 struct weston_seat *seat)
{
	struct ias_surface *shsurf;
	if (!surface) {
		return;
	}

	shsurf = get_ias_surface(surface);
	assert(shsurf);

	/* Activate and raise surface */
	weston_seat_set_keyboard_focus(seat, surface);

	weston_layer_entry_remove(&shsurf->view->layer_link);
	weston_layer_entry_insert(&shsurf->layer->view_list, &shsurf->view->layer_link);
	weston_view_damage_below(shsurf->view);
	weston_surface_damage(surface);
}

/*
 * activate_binding()
 *
 * Activates a surface when clicked by the user.
 */
static void
activate_binding(struct weston_pointer *pointer,
		const struct timespec *time,
		uint32_t button,
		void *data)
{
	struct weston_surface *surface = NULL;
	struct ias_surface *shsurf;

	if(pointer && pointer->focus)
	{
		/* Which surface received the mouse click? */
		surface = (struct weston_surface *)pointer->focus->surface;
	}

	if (!surface) {
		return;
	}

	/* Don't "activate" the desktop background */
	shsurf = get_ias_surface(surface);
	assert(shsurf);
	if (shsurf->zorder == SHELL_SURFACE_ZORDER_BACKGROUND) {
		return;
	}

	/*
	 * Make sure we aren't in a special grab right now which assigns special
	 * meanings to input events.  At the moment we don't actually use grabs
	 * for anything, but we may in the future (e.g., a popup grab).
	 */
	if (pointer->grab == &pointer->default_grab) {
		activate_surface(surface, pointer->seat);
	}
}

/*
 * activate_touch_binding()
 *
 * Activates a surface when touched by the user.
 */
static void
activate_touch_binding(struct weston_touch *touch,
		       const struct timespec *time,
		       void *data)
{
	struct weston_surface *surface = NULL;
	struct ias_surface *shsurf;


	if(touch && touch->focus)
	{
		/* Which surface received the touch? */
		surface = (struct weston_surface *)touch->focus->surface;
	}

	if (!surface) {
		return;
	}

	/* Don't "activate" the desktop background */
	shsurf = get_ias_surface(surface);
	assert(shsurf);
	if (shsurf->zorder == SHELL_SURFACE_ZORDER_BACKGROUND) {
		return;
	}

	/*
	 * Make sure we aren't in a special grab right now which assigns special
	 * meanings to input events.  At the moment we don't actually use grabs
	 * for anything, but we may in the future (e.g., a popup grab).
	 */
	if (touch->grab == &touch->default_grab) {
		activate_surface(surface, touch->seat);
	}
}

/*
 * ias_add_bindings()
 *
 * Adds input event bindings for various mouse/keyboard events.
 */
static void
ias_add_bindings(struct weston_compositor *compositor,
		struct ias_shell *shell)
{
	/* Allow mouse clicks to activate applications */
	weston_compositor_add_button_binding(compositor, BTN_LEFT, 0,
			activate_binding, shell);

	/* Allow touches to activate applications */
	weston_compositor_add_touch_binding(compositor, 0,
			activate_touch_binding, shell);

#if IASDEBUG
	/*
	 * Add a termination keybinding for development use (ctrl-alt-backspace).
	 * This won't be present on release builds of the compositor, but may be
	 * useful to developers.
	 */
	weston_compositor_add_key_binding(compositor, KEY_BACKSPACE,
			MODIFIER_CTRL | MODIFIER_ALT,
			terminate_binding, compositor);
#endif

}

/*
 * sigchld_handler()
 *
 * Handles SIGCHLD signals when a child process (i.e., the HMI dies or is
 * terminated).
 */
static void
sigchld_handler(struct weston_process *process, int status)
{
	IAS_ERROR("HMI client has crashed or terminated");
	self->hmi.client = NULL;
}


/***
 *** xwayland support
 ***/

/*
 * This is a wrapper around our surface contructor that returns an
 * ias_surface that has been cast to a shell_surface.  This is done
 * to satisfy the xwayland function table prototype for the surfce
 * create function.
 */
static struct shell_surface *
shell_surface_constructor(void *shellptr,
		struct weston_surface *surface,
		const struct weston_shell_client *hmi_client)
{
	return (struct shell_surface *)ias_surface_constructor(shellptr,
			surface, hmi_client);
}


/*
 * Requests that a surface be mapped transient to an existing surface.
 * Typically the surface will be locked to the existing surface so that
 * they activate and move together; the exception is if the flags
 * parameter here is set to WL_SHELL_SURFACE_TRANSIENT_INACTIVE, in which
 * case this serves only to set the initial position of the surface.
 */
static void
set_transient(struct shell_surface *child,
		struct weston_surface *parent,
		int x, int y, uint32_t flags)
{
	struct ias_surface *child_surface = (struct ias_surface *)child;
	struct ias_surface *parent_surface = get_ias_surface(parent);

	/* Make sure the parent surface is an IAS surface */
	if (!parent_surface) {
		IAS_ERROR("Tried to make non-IAS surface a parent surface");
		return;
	}

	/*
	 * Should this set the surface position data?
	 *
	 * child_surface->transient.x = x;
	 * child_surface->transient.y = y;
	 * child_surface->transient.flags = flags;
	 */

	/*
	 * If this surface is already transient to the parent, no further work
	 * is required.
	 */
	if (child_surface->parent == parent_surface) {
		return;
	}

	/*
	 * If this surface was previously transient to a different surface, unlink
	 * it from the old parent.  This isn't a situation that makes much sense
	 * logic-wise, but it covers all bases.
	 */
	if (child_surface->parent) {
		wl_list_remove(&child_surface->child_link);
	}

	/* Link surface to new parent */
	child_surface->parent = parent_surface;
	wl_list_insert(&parent_surface->child_list, &child_surface->child_link);

	child_surface->next_behavior |= SHELL_SURFACE_BEHAVIOR_TRANSIENT;
}

/*
 * Marks a surface as the top level surface. This is a special case
 * z-order adjustment.
 */
static void
set_toplevel(struct shell_surface *shsurf)
{
	struct ias_surface *ias_surface = (struct ias_surface *)shsurf;
	/*
	 * Prevent background surfaces or popup surfaces from becoming top-level
	 * (i.e., normal surfaces).  There's no real use case to do this, and
	 * this hack works around some poor logic in the toy toolkit (i.e., that
	 * "custom" windows don't need a shell surface).  This means that we
	 * must either use toy toolkit custom windows and live without shell
	 * surfaces on the compositor side (thus losing a bunch of functionality),
	 * or use regular surfaces and just prevent the toy toolkit from trying
	 * to automatically set them as toplevels when our app has specifically
	 * set them as something else.
	 *
	 * The proper fix would really be to fix the toy toolkit, but we'd prefer
	 * to leave that code as-is.  This approach works, although it's ugly
	 * code-wise.
	 */
	if (ias_surface->next_zorder == SHELL_SURFACE_ZORDER_BACKGROUND ||
			ias_surface->zorder == SHELL_SURFACE_ZORDER_BACKGROUND ||
			ias_surface->next_zorder == SHELL_SURFACE_ZORDER_POPUP ||
			ias_surface->zorder == SHELL_SURFACE_ZORDER_POPUP)
	{
		return;
	}

	ias_surface->next_zorder = SHELL_SURFACE_ZORDER_DEFAULT;
}



/*
 * surface_move
 *
 * Stubbed to provide non-null entry point for xwayland
 */
static int
surface_move(struct shell_surface *shsurf,
		struct weston_pointer *pointer)
{
	return 0;
}

/*
 * surface_resize
 *
 * Stubbed to provide non-null entry point for xwayland
 */
static int
surface_resize(struct shell_surface *shsurf,
		struct weston_pointer *pointer, uint32_t edges)
{
	return 0;
}

/*
 * ias_shell_output_change_notify()
 *
 * When an output is scaled or moved, this gets called to allow the background
 * surface (if any) to be re-configured to the new output size and position.
 * Popup surfaces are also repositioned (but do not need to be resized).
 */
static void
ias_shell_output_change_notify(struct wl_listener *listener, void *data)
{
	struct ias_output *ias_output = data;
	struct ias_surface *shsurf;
	unsigned int width, height;
	struct bound_client *bound;
	struct ias_shell *shell;
	struct wl_resource *resource;

	/* Walk list of background surfaces and resize/reposition them */
	wl_list_for_each(shsurf, &self->background_surfaces, special_link) {
		/* Make sure this surface is on the output that changed */
		if (shsurf->output == (struct weston_output *)ias_output) {
			if (ias_output->base.current_mode && ias_output->is_resized) {
				/*
				 * Send a reconfigure event to the client so that it's aware of
				 * the new output size.
				 */
				send_configure(shsurf->surface,
						shsurf->output->current_mode->width,
						shsurf->output->current_mode->height);

				shell = shsurf->shell;

				wl_list_for_each(bound, &shell->ias_shell_clients, link) {
					resource = wl_resource_find_for_client(
							&ias_output->head.resource_list,
							bound->client_id);

					wl_output_send_geometry(resource,
							shsurf->output->x,
							shsurf->output->y,
							ias_output->width,
							ias_output->height,
							ias_output->head.subpixel,
							ias_output->head.make,
							ias_output->head.model,
							shsurf->output->transform);
				}

				/* Update internal weston state */
				weston_view_set_position(shsurf->view, shsurf->output->x,
						shsurf->output->y);

				shsurf->view->surface->width = shsurf->output->current_mode->width;
				shsurf->view->surface->height = shsurf->output->current_mode->height;

				scale_surface_if_fullscreen(shsurf);
			} else {
				/*
				 * Just a reposition; no need to notify client.  Just
				 * reposition the background surface so that it stays bound
				 * to the upper left corner of the output.
				 */
				weston_view_set_position(shsurf->view,
						shsurf->output->x, shsurf->output->y);
			}
		}
	}

	/* Walk list of popup surfaces and make sure they stay centered */
	wl_list_for_each(shsurf, &self->popup_surfaces, special_link) {
		if (shsurf->output == (struct weston_output *)ias_output) {
			/* Ignore hidden popups */
			if (shsurf->behavior & SHELL_SURFACE_BEHAVIOR_HIDDEN) {
				continue;
			}

			/* Get current surface geometry */
			width = shsurf->surface->width;
			height = shsurf->surface->height;

			/* Center the popup in the middle of the output */
			weston_view_set_position(shsurf->view,
					shsurf->output->x + (shsurf->output->current_mode->width - width) / 2,
					shsurf->output->y + (shsurf->output->current_mode->height - height) / 2);

		}
	}

	/* Make sure whole output gets repainted */
	weston_output_damage(&ias_output->base);

	/* Done processing updates.  Mark output as not resized. */
	ias_output->is_resized = 0;
}

/***
 *** Public entrypoints
 ***/

/*
 * module_init()
 *
 * Initializes the IAS IVI shell.  This will be called from the
 * main Weston compositor after it dlopen()'s our shell plugin.
 */
WL_EXPORT int wet_shell_init(struct weston_compositor *compositor,
			int *argc, char *argv[])
{
	struct ias_shell *shell;
	struct ias_backend *ias_compositor;
	struct weston_output *output;
	struct ias_output *ias_output;

	/* Allocate shell object */
	shell = calloc(1, sizeof *shell);
	if (!shell) {
		IAS_ERROR("Failed to initialize shell: %m");
		return -1;
	}
	self = shell;

	shell->compositor = compositor;

	/*
	 * Hook the shell destructor to the compositor's destruction signal.  We
	 * don't need to hook anything to the lock/unlock signals since those
	 * aren't (currently) used for IVI.
	 */
	shell->destroy_listener.notify = ias_shell_destructor;
	wl_signal_add(&compositor->destroy_signal, &shell->destroy_listener);

	/* Prevent automatic screen blanking on idle */
	compositor->idle_inhibit = 1;

	/*
	 * Hookup IAS implementations of weston -> shell interface as defined in
	 * compositor.h.
	 *
	 * This interface exists primarily for use by xwayland at the moment so
	 * they may not even wind up getting used in IVI settings right now.
	 */
	compositor->shell_interface.shell = shell;
	compositor->shell_interface.create_shell_surface = shell_surface_constructor;
	compositor->shell_interface.set_toplevel = set_toplevel;
	compositor->shell_interface.set_transient = set_transient;
	compositor->shell_interface.move = surface_move;
	compositor->shell_interface.resize = surface_resize;

	renderer_attach = compositor->renderer->attach;
	compositor->renderer->attach = update_frame_count_and_attach;
	compositor->renderer->fill_surf_name = fill_surf_name;
	compositor->renderer->get_surf_id = get_surf_id;
	compositor->renderer->get_shareable_flag = get_shareable_flag;

	/*
	 * Initialize custom surface layer list for custom zorders (empty
	 * for now).
	 */
	wl_list_init(&shell->custom_layers);

	/* Initialize special surface lists */
	wl_list_init(&shell->background_surfaces);
	wl_list_init(&shell->popup_surfaces);
	wl_list_init(&shell->client_surfaces);

	/* Initialize hmi callback list */
	wl_list_init(&shell->sfc_change_callbacks);

	/* Initialize shell client lists */
	wl_list_init(&shell->ias_shell_clients);
	wl_list_init(&shell->wl_shell_clients);
	wl_list_init(&shell->ias_metrics_callbacks);

	/*
	 * Initialize standard layers.  The organization for our shell is:
	 *     Cursor Layer (provided automatically by Weston)
	 *     Popup Layer
	 *     Fullscreen Layer
	 *     << user-defined custom zorder layers >>
	 *     Default App Layer
	 *     Background Layer
	 */
	weston_layer_init(&shell->popup_layer, compositor);
	weston_layer_set_position(&shell->popup_layer, WESTON_LAYER_POSITION_TOP_UI);

	weston_layer_init(&shell->fullscreen_layer, compositor);
	weston_layer_set_position(&shell->fullscreen_layer, WESTON_LAYER_POSITION_FULLSCREEN);

	weston_layer_init(&shell->default_app_layer, compositor);
	weston_layer_set_position(&shell->default_app_layer, WESTON_LAYER_POSITION_NORMAL);

	weston_layer_init(&shell->background_layer, compositor);
	weston_layer_set_position(&shell->background_layer, WESTON_LAYER_POSITION_BACKGROUND);

	/*
	 * If we're running with the IAS backend, setup a listener for each output
	 * so that we can get notifications about output moves or resizes.  This
	 * will allow us to reconfigure or reposition background and popup
	 * surfaces.
	 */
	ias_compositor = (struct ias_backend *)compositor->backend;
	if (ias_compositor->magic == BACKEND_MAGIC) {
		/* Walk the output list and hook up each listener. */
		wl_list_for_each(output, &compositor->output_list, link) {
			ias_output = (struct ias_output *)output;
			ias_output->update_listener.notify =
				ias_shell_output_change_notify;
			wl_signal_add(&ias_output->update_signal,
					&ias_output->update_listener);

			if (ias_compositor->metrics_timing) {
				do_print_fps = ias_compositor->print_fps;
				metrics_timing = ias_compositor->metrics_timing;
				ias_output->printfps_listener.notify = print_fps;
				wl_signal_add(&ias_output->printfps_signal,
						&ias_output->printfps_listener);
			}
		}
	}

#ifdef IASDEBUG
	/*
	 * Add a default background surface in debug builds.  This default
	 * background is an obnoxious yellow color to help us recognize that
	 * the HMI application has failed to provide its own background surface
	 * on this output.
	 */
	shell->default_background = weston_surface_create(compositor);
	shell->default_view = weston_view_create(shell->default_background);

	weston_view_set_position(shell->default_view, 0, 0);
	shell->default_background->width = 8192;
	shell->default_background->height = 8192;

	weston_surface_set_color(shell->default_background, 1.0, 1.0, 0.0, 0.0);
	weston_layer_entry_insert(&shell->background_layer.surface_list,
			&shell->default_background->layer_link);
	pixman_region32_init(&shell->default_background->input);
#endif

	/* Load configuration file */
	ias_shell_configuration(shell);

	/*
	 * Create global objects for ias_shell, ias_hmi, and layout manager.
	 * Note that the layout manager will only be advertised if we're actually
	 * running the IAS backend.
	 */
	if (!wl_global_create(compositor->wl_display,
				&ias_shell_interface, 1, shell, bind_ias_shell))
	{
		return -1;
	}
	if (!wl_global_create(compositor->wl_display,
				&ias_hmi_interface, 1, shell, bind_ias_hmi))
	{
		return -1;
	}
	if (!wl_global_create(compositor->wl_display,
				&ias_relay_input_interface, 1, shell, bind_ias_relay_input))
	{
		return -1;
	}
	if (!wl_global_create(compositor->wl_display,
				&ias_metrics_interface, 1, shell, bind_ias_metrics))
	{
		return -1;
	}

	ias_add_bindings(compositor, shell);

	/* WL_SHELL */
	if (!wl_global_create(compositor->wl_display,
				&wl_shell_interface, 1, shell, bind_wl_shell)) {
		return -1;
	}

	screenshooter_create(compositor);

	/* Launch the HMI client */
	if (shell->hmi.execname) {
		IAS_DEBUG("Launching HMI (%s)", shell->hmi.execname);

		shell->hmi.client = weston_client_launch_with_env(shell->compositor,
				&shell->hmi.process,
				shell->hmi.execname,
				&shell->hmi.environment,
				sigchld_handler);
		if (!shell->hmi.client) {
			IAS_ERROR("Failed to launch HMI client: %m");
		}
	}

	return 0;
}


