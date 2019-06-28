/*
 *-----------------------------------------------------------------------------
 * Filename: ias-shell.h
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
 *   Intel Automotive Solutions shell module for Weston.  This is a private
 *   header used internally by the IAS shell.  Customer plugins may only
 *   interact with the types defined here via the interface laid out in the
 *   public header ias-plugin-manager.h.
 *-----------------------------------------------------------------------------
 */

#ifndef __IAS_SHELL_H__
#define __IAS_SHELL_H__

#include "config.h"
#include "ias-common.h"
#include "ias-shell-server-protocol.h"

#define CFG_FILENAME "ias.conf"

struct ias_shell;

WL_EXPORT struct ias_surface*
get_ias_surface(struct weston_surface *);

void
ias_committed(struct weston_surface *surface, int32_t relx, int32_t rely);


struct frame_data {
	int frame_count;
	int flip_count;
	uint32_t output_id;
	struct wl_list output_link;
};

/*
 * Singleton data structure to hold all shell-specific data.
 */
struct ias_shell {
	/* Compositor reference */
	struct weston_compositor *compositor;

	/* Rendering layer surface lists (exposed to clients as "zorder") */
	struct weston_layer popup_layer;        /* Used for popup notifications */
	struct weston_layer fullscreen_layer;   /* Used by apps in "fullscreen"
											 * mode; there should be only one */
	struct weston_layer default_app_layer;  /* Default layer for app surfaces */
	struct weston_layer background_layer;   /* Wallpaper layer */
	struct wl_list custom_layers;           /* List of custom layers */

	/* HMI application bookkeeping */
	struct {
		char *execname;                     /* HMI executable name */
		struct weston_process process;
		struct wl_client *client;
		struct wl_resource *ias_shell;
		struct wl_list environment;
	} hmi;

	/*
	 * Special HMI surface lists.
	 *
	 * Backgrounds are typically used for wallpaper, although it would be
	 * possible to have a "live" background if desired.  There will be only one
	 * background surface per display.  Display layouts may either enable or
	 * disable layouts.
	 *
	 * Other general layers may be added here in the future.
	 */
	struct wl_list background_surfaces;
	struct wl_list popup_surfaces;
	struct wl_list client_surfaces;

#ifdef IASDEBUG
	/*
	 * Special 'default' background surface.  An HMI should really set the
	 * background itself, but if it fails to do so you'll see artifacts that
	 * are never cleaned up on the part of the screen that has no other
	 * surfaces visible (mouse trails, remnants of moved windows, etc.).
	 * For debug builds, we'll stick a default surface in the background
	 * surface list with an obnoxious yellow color to help us recognize
	 * if/when there is no background surface set on a display.
	 */
	struct weston_surface *default_background;
	struct weston_surface *default_view;
#endif

	/* Listeners for various client requests. */
	struct wl_listener destroy_listener;

	/* Callback list to notify when surface changes */
	struct wl_list sfc_change_callbacks;

	/* Keep track of which clients are bound to which shell interface */
	struct wl_list wl_shell_clients;
	struct wl_list ias_shell_clients;

	/*
	 * Note for future expansion:  At the moment we assume that lockscreens,
	 * screensavers, and built-in panels aren't a feature that makes sense for
	 * IVI.  If a customer desires this kind of functionality, additional
	 * layers should be defined above, as well as a list of "saver/lock" or
	 * "panel" surfaces.
	 */
};

/*
 * Shell surface data.  This structure defines the iasshell-specific data
 * tracked for each wl_surface.  Note that some of the fields here
 * (behavior, zorder, etc.) have both a "real" value and a "next"
 * value.  "next" values become real when a buffer is attached to the
 * surface; this allows a client to set all the surface state it wants
 * and then attach a buffer without worrying about race conditions with
 * the compositor's own frame events.  Actually moving surfaces between
 * layers and such all happens at attach time, when real and next values
 * differ.
 */
struct ias_surface {
	struct wl_resource *resource;
	struct wl_signal destroy_signal;

	/* Reference to generic surface structure */
	struct weston_surface *surface;
	struct weston_view *view;

	int32_t last_width, last_height;

	/* Parent surface */
	struct ias_surface *parent;

	/* List of child surfaces (move together with parent) */
	struct wl_list child_list;

	/* Pointer back to ias_shell singleton */
	struct ias_shell *shell;

	/* Pointer back to wl_client */
	struct wl_client *client;

	/* Client-specific resource for the shell used to create this ias_surface */
	struct wl_resource *shell_resource;

	/* vtable for sending events to the shell client (hmi) */
	const struct weston_shell_client *hmi_client;

	/* Last activity timestamp and source (for ping/pong handling) */
	struct {
		struct wl_event_source *source;
		uint32_t serial;

		/* A ping has been sent to the client; we're waiting for a pong */
		unsigned active   : 1;

		/* No pong received for this surface before timeout */
		unsigned timedout : 1;
	} ping_info;

	/* Surface zorder */
	uint32_t zorder, next_zorder;

	/*
	 * Surface behavior and title; requests to set these are part of
	 * ias_shell and ias_surface protocols, but no specific semantics are
	 * defined.  We don't actually use these ourselves, but we save them
	 * away in case a custom layout plugin wants to use them for its own logic.
	 * See set_title and set_bahavior request handlers for more information.
	 */
	uint32_t behavior, next_behavior;
	char *title;

	/*
	 * Layer that this surface is currently on.  This can be determined from
	 * the zorder field, but this provides a faster way to get at it directly
	 * when restacking surfaces.
	 */
	struct weston_layer *layer;

	/* Per-surface rotation */
	struct {
		struct weston_transform transform;
		struct weston_matrix rotation;
	} rotation;

	struct weston_transform fullscreen_transform; /* matrix from x, y */


	/* Popup information */
	struct {
		uint32_t priority;
	} popup;

	struct weston_output *output;

	/* Listener object for destruction of underlying wl_surface */
	struct wl_listener surface_destroy_listener;

	/* Node in parent's child list */
	struct wl_list child_link;

	/* Node in surface list, for all clent surfaces exclude
	 * special surface list like popup list */
	struct wl_list surface_link;

	/* Node in special surface list (popup list, background list, etc.) */
	struct wl_list special_link;

	/* HMI client requested positional data */
	uint32_t position_update;
	int32_t x;
	int32_t y;

	/*
	 * The process id and name of client application that created this
	 * surface
	 */
	uint32_t pid;
	char *pname;
	/*
	 * List of output that this surface is on. Need this for frame count
	 * purpose
	 */
	struct wl_list output_list;

	/* Was that surface created using wl_shell interface */
	int wl_shell_interface;
	/* flag that indicates if surface could be shared */
	int shareable;
};

// whether surface is directly flipped or composited
int ias_surface_is_flipped(struct ias_surface *shsurf);

#endif
