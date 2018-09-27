/*
 *-----------------------------------------------------------------------------
 * Filename: ias-plugin-framework.c
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
 *   Layout plugin framework.
 *-----------------------------------------------------------------------------
 */

#include <assert.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <linux/input.h>
#include <ctype.h>

#include "config.h"
#include "ias-common.h"
#include "ias-shell.h"
#include "ivi-shell.h"
#include "ias-backend.h"
#include "ias-layout-manager-server-protocol.h"
#include "ias-input-manager-server-protocol.h"
#include "ivi-shell.h"
#include "ivi-layout-export.h"
#include "ivi-layout-private.h"

#include <wayland-server.h>

#include "ias-plugin-framework-private.h"

/*
 * At the moment IAS can only handle four outputs (via dualview or stereo
 * mode)
 */
#define MAX_OUTPUTS 4

#define find_resource_for_client wl_resource_find_for_client

static void (*ias_config_fptr)(struct weston_surface *es, int32_t sx, int32_t sy);

static void handle_plugin(void *, const char **);
static void handle_input_plugin(void *, const char **);
static void set_up_seat_with_plugin_framework(struct weston_seat *seat);


void on_mouse_call(struct weston_pointer_grab *base);
void on_keyboard_call(struct weston_keyboard_grab *base);
void on_touch_call(struct weston_touch_grab *base);

/* this should be call-able directly from ias */
void plugin_on_draw(struct ias_output * output);

void plugin_on_pointer_motion(struct weston_pointer_grab *grab, const struct timespec *time,
		   struct weston_pointer_motion_event *event);

/* Element mapping for state machine */
static struct xml_element config_parse_data[] = {
	{ NONE,			NULL,			NULL,				IASCONFIG,					NONE },
	{ IASCONFIG,	"iasconfig",	NULL,				HMI | PLUGIN | INPUTPLUGIN,	NONE },
	{ PLUGIN,		"plugin",		handle_plugin,		NONE,						IASCONFIG },
	{ INPUTPLUGIN,	"input_plugin",	handle_input_plugin,NONE,						IASCONFIG },
};

/***
 *** Config parsing functions
 ***/

/*
 * handle_plugin()
 *
 * Handles a plugin element in the config.
 */
static void
handle_plugin(void *userdata, const char **attrs)
{
	struct ias_plugin *plugin;

	plugin = calloc(1, sizeof *plugin);
	if (!plugin) {
		IAS_ERROR("Failed to allocate plugin structure: out of memory");
		return;
	}

	while (attrs[0]) {
		if (!strcmp(attrs[0], "name")) {
			free(plugin->name);
			plugin->name = strdup(attrs[1]);
		} else if (!strcmp(attrs[0], "lib")) {
			free(plugin->libname);
			plugin->libname = strdup(attrs[1]);
		} else if (!strcmp(attrs[0], "activate_on")) {
			free(plugin->activate_on);
			plugin->activate_on = strdup(attrs[1]);
		} else if (!strcmp(attrs[0], "defer")) {
			plugin->init_mode = INIT_DEFERRED;
		}

		attrs += 2;
	}

	if (!plugin->name || !plugin->libname) {
		IAS_ERROR("Invalid plugin tag in config; ignoring.");
		framework->config_err = SPUG_TRUE;
		free(plugin->name);
		free(plugin->libname);
		free(plugin->activate_on);
		free(plugin);
		return;
	}

	/*
	 * It doesn't make sense to defer initialization of a plugin we are
	 * activating immediately.
	 */
	if (plugin->activate_on && plugin->init_mode == INIT_DEFERRED) {
		IAS_ERROR("Can not defer initialization of an auto-activated plugin!");
		plugin->init_mode = INIT_NORMAL;
	}

	wl_list_insert(&framework->plugin_list, &plugin->link);
	framework->num_plugins++;
}


/*
 * handle_input_plugin()
 *
 * Handles an input plugin element in the config.
 */
static void
handle_input_plugin(void *userdata, const char **attrs)
{
	struct ias_plugin *plugin;

	/*
	 * If we already have an input plugin, raise an error and ignore this one.
	 * There can be only one!
	 */
	if (framework->input_plugin) {
		IAS_ERROR("Too many input plugins; only one is allowed");
		framework->config_err = SPUG_TRUE;
		return;
	}

	plugin = calloc(1, sizeof *plugin);
	if (!plugin) {
		IAS_ERROR("Failed to allocate input plugin structure: out of memory");
		return;
	}

	while (attrs[0]) {
		if (!strcmp(attrs[0], "name")) {
			free(plugin->name);
			plugin->name = strdup(attrs[1]);
		} else if (!strcmp(attrs[0], "lib")) {
			free(plugin->libname);
			plugin->libname = strdup(attrs[1]);
		} else if (!strcmp(attrs[0], "defer")) {
			plugin->init_mode = INIT_DEFERRED;
		}

		attrs += 2;
	}

	if (!plugin->name || !plugin->libname) {
		IAS_ERROR("Invalid input plugin tag in config; ignoring.");
		free(plugin->name);
		free(plugin->libname);
		free(plugin);
		return;
	}

	framework->input_plugin = plugin;
}


/***
 *** Internal functions used by helpers
 ***/

/*
 * fetch_ivi_shell_surface()
 *
 * Get the IAS-specific surface data.  If we're not running on the IAS
 * shell, this will return NULL.
 */

WL_EXPORT struct ivi_shell_surface *
fetch_ivi_shell_surface(struct weston_surface *surface)
{
	if (!surface) {
		return NULL;
	}

	if (surface->committed == ias_config_fptr) {
		return (struct ivi_shell_surface *)surface->committed_private;
	} else {
		return NULL;
	}
}

/***
 *** Exported helper functions for use in plugins.
 ***/

/*
 * ias_get_surface_behavior()
 *
 * Returns the behavior bits for a surface.
 */
WL_EXPORT uint32_t ias_get_behavior_bits(struct weston_surface *surface)
{
	struct ivi_shell_surface *shsurf = fetch_ivi_shell_surface(surface);

	return shsurf ? shsurf->behavior : 0;
}

/*
 * ias_get_surface_zorder()
 *
 * Returns the zorder value for a surface.
 */
WL_EXPORT uint32_t ias_get_zorder(struct weston_surface *surface)
{
	IAS_ERROR("%s() unsupported with IVI shell", __func__);

	return 0;
}

/*
 * ias_has_surface_timedout()
 *
 * Indicates whether a surface has stopped responding to input.
 */
WL_EXPORT int ias_has_surface_timedout(struct weston_surface *surface)
{
	/* TODO */
	return 0;
}

/*
 * ias_toggle_frame_events()
 *
 * Temporarily disables/enables frame events sent to a surface.
 */
WL_EXPORT void ias_toggle_frame_events(struct weston_surface *surface)
{
	surface->suspend_frame_events ^= 1;
}

/*
 * ias_frame_events_enabled()
 *
 * Returns whether frame events are currently enabled for a surface.
 */
WL_EXPORT int ias_frame_events_enabled(struct weston_surface *surface)
{
	return !surface->suspend_frame_events;
}

/*
 * ias_get_owning_process_info()
 *
 * Returns the owning client's pid and name for a surface.
 */
WL_EXPORT void ias_get_owning_process_info(
		struct weston_surface *surface,
		uint32_t *pid,
		char **pname)
{
	struct ivi_shell_surface *shsurf = fetch_ivi_shell_surface(surface);
	struct wl_client *client = wl_resource_get_client(surface->resource);
	int surf_pid;

	wl_client_get_credentials(client, &surf_pid, NULL, NULL);

	if(shsurf) {
		if(pid) {
			/* TODO why is this 0? Using the wl_client_get_credentials() call
			 * above for now */
			/* *pid = shsurf->layout_surface->prop.creatorPid; */

			*pid = surf_pid;
		}
		if(pname) {
			char fname[128];
			char buff[256];
			int found_name = 0;
			FILE *fp;
			char *p;

			sprintf(fname, "/proc/%d/status",
									/* TODO This is 0, grab it from the weston
									 * surface instead */
									/* shsurf->layout_surface->prop.creatorPid); */
										surf_pid);
			fp = fopen(fname, "r");
			if (fp == NULL) {
				*pname = strdup("No_name");
				if (!(*pname)) {
					IAS_ERROR("Out of memory");
					exit(1);
				}
			} else {
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
						*pname = strdup(p);
						if (!(*pname)) {
							IAS_ERROR("Out of memory");
							exit(1);
						}
						(*pname)[strlen(p)-1] = '\0';
						found_name = 1;
						break;
					}
				}
				if (found_name == 0) {
					*pname = strdup("No_name");
					if (!(*pname)) {
						IAS_ERROR("Out of memory");
						exit(1);
					}
				}
				fclose(fp);
			}
		}
	}
}

/*
 * ias_get_weston_surface()
 *
 * Returns the weston surface behind the ivi_shell_surface.
 */
WL_EXPORT struct weston_surface * ias_get_weston_surface(
		uint32_t ivi_shell_surface_id)
{
	struct weston_surface *weston_surface = NULL;
	struct ivi_shell_surface *shsurf = (struct ivi_shell_surface *)(intptr_t)ivi_shell_surface_id;

	if(shsurf && shsurf->surface) {
		weston_surface = shsurf->surface;
	}
	return weston_surface;
}

/*
 * ias_is_surface_flippable()
 *
 * Returns 1 if a surface could potentially be flippable or 0 if it is not
 */
WL_EXPORT uint32_t ias_is_surface_flippable(
		struct weston_view *view,
		struct weston_output *output)
{
	struct ias_crtc *ias_crtc = ((struct ias_output *) output)->ias_crtc;

	if(ias_crtc->output_model->is_surface_flippable) {
		return ias_crtc->output_model->is_surface_flippable(view, output, 0);
	}
	return 0;
}

/*
 * ias_assign_surface_to_scanout()
 *
 * Returns 0 if a surface was successfully assigned to scanout or -1 in case of
 * error.
 */
WL_EXPORT uint32_t ias_assign_surface_to_scanout(
		struct weston_view *view,
		struct weston_output *output,
		uint32_t x,
		uint32_t y)
{
	struct ias_output *ias_output = (struct ias_output *)output;
	struct ias_backend *ias_backend = ((struct ias_backend *)
				(framework->compositor->backend));
	uint32_t ret = -1;

	if(ias_output && view &&
			ias_backend->attempt_scanout_for_view(output,
					view, 0)) {
		ias_output->scanout_surface = view->surface;
		ret = 0;
	}

	return ret;
}


/*
 * ias_get_sprite_list()
 *
 * Returns references to all sprite planes that are available for use
 * by a layout plugin.  Note that in some modes of operation (dualview,
 * stereo, etc.) sprites will be used internally or simply unavailable
 * so the list returned here will be empty.
 *
 * We could return the backend's wl_list of sprites directly to the
 * plugin here, but that would allow the customer's code to try to
 * add/remove items from the sprite list, which we don't want.  Instead
 * we just hand them back an array
 */
WL_EXPORT int ias_get_sprite_list(struct weston_output *output,
		struct ias_sprite ***sprite_list)
{
	/* Make sure they passed in a non-NULL pointer that we can update */
	if (!sprite_list) {
		return 0;
	}

	/* Call into backend */
	return ((struct ias_backend *)
			(framework->compositor->backend))->get_sprite_list(output, sprite_list);
}

/*
 * ias_assign_surface_to_sprite()
 *
 * Assigns a (sub-)surface to a sprite plane for the frame currently being
 * rendered and positions it at a specific location in relation on the output.
 * This function should be called by a plugin's 'draw' entrypoint each frame.
 * If the plugin does not call this function, the sprite will automatically
 * be turned off and not used for the current frame.
 *
 * Returns the weston_plane structure to which the surface was assigned.
 */
WL_EXPORT struct weston_plane *
ias_assign_surface_to_sprite(struct weston_view *view,
	/*
	struct ias_sprite *sprite,
	*/
	struct weston_output *output,
	int *sprite_id,
	int x,
	int y,
	int sprite_width,
	int sprite_height,
	pixman_region32_t *surface_region)
{
	struct weston_plane *ret;
	struct ias_backend* ias_comp;
	/*
	 * Make sure we were handed a non-NULL sprite.  This is just a sanity
	 * check in case we're running in dualview/stereo mode and the plugin
	 * forgot to pay attention to the return value of ias_get_sprite_list.
	 */
	if (!output) {
		return NULL;
	}
	ias_comp = ((struct ias_backend *)(framework->compositor->backend));

	/* Call into backend */
	ret = ias_comp->assign_view_to_sprite(
					view, output, sprite_id, x, y,
					sprite_width, sprite_height, surface_region);

	return ret;
}

/*
 * ias_assign_zorder_to_sprite()
 *
 * Given the sprite id, move the sprite to the top of bottom of all planes include
 * display plane or another sprite plane.  This function should be called by a
 * plugin's 'switch_to' or  'switch_from' entrypoint each frame.  If the plugin
 * want to dynamic move the plane between frame, it can be call in 'draw' entrypoint.
 *
 */
WL_EXPORT int
ias_assign_zorder_to_sprite(struct weston_output *output,
		int sprite_id,
		int position)

{
	int ret;

	/*
	 * Make sure we were handed a non-NULL sprite.  This is just a sanity
	 * check in case we're running in dualview/stereo mode and the plugin
	 * forgot to pay attention to the return value of ias_get_sprite_list.
	 */
	if (!output) {
		return -1;
	}

	ret = ((struct ias_backend *)
			(framework->compositor->backend))->assign_zorder_to_sprite(
					output, sprite_id, position);

	return ret;
}

/*
 * ias_assign_constant_alpha_to_sprite()
 * Given the sprite id, apply constant alpha to the sprite before it blends
 * in the pipe.   The constant alpha take values from 0-0xff.
 * 0 - full transparent
 * 0xff - opaque
 */
WL_EXPORT int
ias_assign_constant_alpha_to_sprite(struct weston_output *output,
		int sprite_id,
		float constant_value,
		int enable)
{
	int ret;

	/*
	 * Make sure we were handed a non-NULL sprite.  This is just a sanity
	 * check in case we're running in dualview/stereo mode and the plugin
	 * forgot to pay attention to the return value of ias_get_sprite_list.
	 */
	if (!output) {
		return -1;
	}

	if ((constant_value < 0.0) || (constant_value > 1.0)) {
		return -1;
	}

	ret = ((struct ias_backend *)
			(framework->compositor->backend))->assign_constant_alpha_to_sprite(
					output, sprite_id, constant_value, enable);

	return ret;

}

/*
 * ias_set_plugin_redraw_behavior()
 *
 * Changes the redraw strategy for a layout plugin.  By default, plugins will
 * have their draw() entrypoint called once per vblank, even if no application
 * surfaces have been updated.  This function may be used to change that
 * behavior and request that the plugin's draw entrypoint only be called when
 * surfaces are damaged.
 *
 * Important notes:
 *  - This function will have no effect when running in GPU-based dualview mode
 *  - When configured to redraw upon damage, the compositor will assume that
 *    surface geometry is a proper indication of which output a surface would
 *    show up on (which output(s) should be redrawn).
 */
WL_EXPORT void ias_set_plugin_redraw_behavior(struct weston_output *output,
		enum plugin_redraw_behavior behavior)
{
	struct ias_output *ias_output = (struct ias_output *)output;

	ias_output->plugin_redraw_always = (behavior != PLUGIN_REDRAW_DAMAGE);

	/*
	 * If we're switching back to redraw on every vblank, we need to
	 * manually trigger the first redraw to get everything rolling
	 * again.
	 */
	weston_output_schedule_repaint(output);
}

/***
 *** Plugin Manager implementation
 ***/

void *self;


/***
 *** Plugin input functions
 ***/

/*
 * We can better controll what happens with plugin input
 * with changes to this function*/


/*****plugin On functions****/
void on_mouse_call(struct weston_pointer_grab *base)
{
}
void on_keyboard_call(struct weston_keyboard_grab *base)
{
	/* if there is no active layout plugin updating the surface/view lists then
	 * we need to do it here in the input plugin. Is this the correct way of
	 * checking that there is no current layout plugin?*/
	if(!framework->last_actived_layout_plugin) {
		spug_update_all_lists();
	}
}
void on_touch_call(struct weston_touch_grab *base)
{

}

/*************pointer functions*******************/

/*TODO, do the same for all input functions!*/
static void
plugin_pointer_grab_focus(struct weston_pointer_grab *grab)
{
	on_mouse_call(grab);

/* TODO: use the correct focus */
#ifdef USING_FOCUS
	struct weston_surface* focus = NULL;

	if(grab->pointer->focus) {
		focus = grab->pointer->focus->surface;
	}
#endif


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_pointer_focus event_pointer_info;
		event_pointer_info.base.event_type = IPUG_POINTER_FOCUS;
		event_pointer_info.grab = grab;
		event_pointer_info.surface = NULL;
		event_pointer_info.x = grab->pointer->grab_x;
		event_pointer_info.y = grab->pointer->grab_y;
		framework->active_input_plugin->input_info.on_input(
								(struct ipug_event_info*)&event_pointer_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface->focus) {

		framework->last_actived_layout_plugin->info.mouse_grab.interface->focus(grab);
	} else {
		grab->pointer->default_grab.interface->focus(grab);
	}
}

static void
plugin_pointer_grab_button(struct weston_pointer_grab *grab,
	       const struct timespec *time, uint32_t button, uint32_t state)
{
	on_mouse_call(grab);

	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_pointer_button event_pointer_info;
		event_pointer_info.base.event_type = IPUG_POINTER_BUTTON;
		event_pointer_info.grab = grab;
		event_pointer_info.time = time;
		event_pointer_info.button = button;
		event_pointer_info.state = state;
		framework->active_input_plugin->input_info.on_input(
								(struct ipug_event_info*)&event_pointer_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface->button) {

		framework->last_actived_layout_plugin->info.mouse_grab.interface->button(
				grab,
				time,
				button,
				state);
	} else {
		grab->pointer->default_grab.interface->button(grab, time, button, state);
	}
}


static void
plugin_pointer_grab_motion(struct weston_pointer_grab *grab,
			const struct timespec *time,
			struct weston_pointer_motion_event *event)
{
	on_mouse_call(grab);

	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_pointer_motion event_pointer_info;
		event_pointer_info.base.event_type = IPUG_POINTER_MOTION;
		event_pointer_info.grab = grab;
		event_pointer_info.time = time;
		event_pointer_info.x = event->x;
		event_pointer_info.y = event->y;
		framework->active_input_plugin->input_info.on_input(
								(struct ipug_event_info*)&event_pointer_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface->motion) {

			/* we call this wrapper here instead of the plugin's grab because the
			 * cursor won't move if we just swallow the event */
			plugin_on_pointer_motion(grab, time, event);
	} else {
		grab->pointer->default_grab.interface->motion(grab, time, event);
	}
}

static void
plugin_pointer_grab_cancel(struct weston_pointer_grab *grab)
{
	on_mouse_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_pointer_cancel event_pointer_info;
		event_pointer_info.base.event_type = IPUG_POINTER_CANCEL;
		event_pointer_info.grab = grab;
		framework->active_input_plugin->input_info.on_input(
								(struct ipug_event_info*)&event_pointer_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface->cancel) {

		framework->last_actived_layout_plugin->info.mouse_grab.interface->cancel(grab);
	} else {
		grab->pointer->default_grab.interface->cancel(grab);
	}
}

static void
plugin_pointer_grid_axis(struct weston_pointer_grab *grab,
		  const struct timespec *time,
		  struct weston_pointer_axis_event *event)
{
}

static void
plugin_pointer_grid_axis_source(struct weston_pointer_grab *grab, uint32_t source)
{
}

static void
plugin_pointer_grid_frame(struct weston_pointer_grab *grab)
{
}

static const struct weston_pointer_grab_interface plugin_pointer_functions = {
	plugin_pointer_grab_focus,
	plugin_pointer_grab_motion,
	plugin_pointer_grab_button,
	plugin_pointer_grid_axis,
	plugin_pointer_grid_axis_source,
	plugin_pointer_grid_frame,
	plugin_pointer_grab_cancel
};

/********** keyboard functions *******************/

static void
plugin_keyboard_grab_key(struct weston_keyboard_grab *grab, const struct timespec *time,
		    uint32_t key, uint32_t state)
{
	on_keyboard_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_key_key event_key_info;
		event_key_info.base.event_type = IPUG_KEYBOARD_KEY;
		event_key_info.grab = grab;
		event_key_info.time = time;
		event_key_info.key = key;
		event_key_info.state = state;
		framework->active_input_plugin->input_info.on_input(
									(struct ipug_event_info*)&event_key_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.key_grab.interface &&
			framework->last_actived_layout_plugin->info.key_grab.interface->key) {

		framework->last_actived_layout_plugin->info.key_grab.interface->key(
				grab,
				time,
				key,
				state);
	} else {
		grab->keyboard->default_grab.interface->key(grab, time, key, state);
	}
}

static void
plugin_keyboard_grab_modifiers(struct weston_keyboard_grab *grab, uint32_t serial,
			  uint32_t mods_depressed, uint32_t mods_latched,
			  uint32_t mods_locked, uint32_t group)
{
	on_keyboard_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_key_mod event_key_info;
		event_key_info.base.event_type = IPUG_KEYBOARD_MOD;
		event_key_info.grab = grab;
		event_key_info.serial = serial;
		event_key_info.mods_depressed = mods_depressed;
		event_key_info.mods_latched = mods_latched;
		event_key_info.mods_locked = mods_locked;
		event_key_info.group = group;
		framework->active_input_plugin->input_info.on_input(
									(struct ipug_event_info*)&event_key_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.key_grab.interface &&
			framework->last_actived_layout_plugin->info.key_grab.interface->modifiers) {

		framework->last_actived_layout_plugin->info.key_grab.interface->modifiers(
				grab,
				serial,
				mods_depressed,
				mods_latched,
				mods_locked,
				group);
	} else {
		grab->keyboard->default_grab.interface->modifiers(grab, serial,
				mods_depressed,
				mods_latched,
				mods_locked,
				group);
	}
}

static void
plugin_keyboard_grab_cancel(struct weston_keyboard_grab *grab)
{
	on_keyboard_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_key_cancel event_key_info;
		event_key_info.base.event_type = IPUG_KEYBOARD_CANCEL;
		event_key_info.grab = grab;
		framework->active_input_plugin->input_info.on_input(
									(struct ipug_event_info*)&event_key_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.key_grab.interface &&
			framework->last_actived_layout_plugin->info.key_grab.interface->cancel) {

		framework->last_actived_layout_plugin->info.key_grab.interface->cancel(
				grab);
	} else {
		grab->keyboard->default_grab.interface->cancel(grab);
	}
}


static const struct weston_keyboard_grab_interface plugin_keyboard_functions = {
		plugin_keyboard_grab_key,
		plugin_keyboard_grab_modifiers,
		plugin_keyboard_grab_cancel,
};

/**********************Touch functions *********************/
static void plugin_touch_grab_down(struct weston_touch_grab *grab,
		const struct timespec *time,
		int touch_id,
		wl_fixed_t sx,
		wl_fixed_t sy)
{
	on_touch_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_touch_down event_touch_info;
		event_touch_info.base.event_type = IPUG_TOUCH_DOWN;
		event_touch_info.grab = grab;
		event_touch_info.time = time;
		event_touch_info.touch_id = touch_id;
		event_touch_info.sx = sx;
		event_touch_info.sy = sy;
		framework->active_input_plugin->input_info.on_input(
									(struct ipug_event_info*)&event_touch_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.touch_grab.interface &&
			framework->last_actived_layout_plugin->info.touch_grab.interface->down) {

		framework->last_actived_layout_plugin->info.touch_grab.interface->down(
				grab,
				time,
				touch_id,
				sx,	sy);
	} else {
		grab->touch->default_grab.interface->down(grab, time, touch_id, sx, sy);
	}
}

static void plugin_touch_grab_up(struct weston_touch_grab *grab,
		const struct timespec *time,
		int touch_id)
{
	on_touch_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_touch_up event_touch_info;
		event_touch_info.base.event_type = IPUG_TOUCH_UP;
		event_touch_info.grab = grab;
		event_touch_info.time = time;
		event_touch_info.touch_id = touch_id;
		framework->active_input_plugin->input_info.on_input(
									(struct ipug_event_info*)&event_touch_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.touch_grab.interface &&
			framework->last_actived_layout_plugin->info.touch_grab.interface->up) {

		framework->last_actived_layout_plugin->info.touch_grab.interface->up(
				grab,
				time,
				touch_id);
	} else {
		grab->touch->default_grab.interface->up(grab, time, touch_id);
	}
}

static void plugin_touch_grab_motion(struct weston_touch_grab *grab,
		const struct timespec *time,
		int touch_id,
		wl_fixed_t sx,
		wl_fixed_t sy)
{
	on_touch_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_touch_motion event_touch_info;
		event_touch_info.base.event_type = IPUG_TOUCH_MOTION;
		event_touch_info.grab = grab;
		event_touch_info.time = time;
		event_touch_info.touch_id = touch_id;
		event_touch_info.sx = sx;
		event_touch_info.sy = sy;
		framework->active_input_plugin->input_info.on_input(
									(struct ipug_event_info*)&event_touch_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.touch_grab.interface &&
			framework->last_actived_layout_plugin->info.touch_grab.interface->motion) {

		framework->last_actived_layout_plugin->info.touch_grab.interface->motion(
				grab,
				time,
				touch_id,
				sx, sy);
	} else {
		grab->touch->default_grab.interface->motion(grab, time, touch_id, sx, sy);
	}
}

static void plugin_touch_grab_frame(struct weston_touch_grab *grab)
{
	on_touch_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_touch_frame event_touch_info;
		event_touch_info.base.event_type = IPUG_TOUCH_FRAME;
		event_touch_info.grab = grab;
		framework->active_input_plugin->input_info.on_input(
									(struct ipug_event_info*)&event_touch_info);
	} else if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.touch_grab.interface &&
			framework->last_actived_layout_plugin->info.touch_grab.interface->cancel) {

		framework->last_actived_layout_plugin->info.touch_grab.interface->cancel(
				grab);
	} else {
		grab->touch->default_grab.interface->cancel(grab);
	}
}

static void plugin_touch_grab_cancel(struct weston_touch_grab *grab)
{
	on_touch_call(grab);


	if(framework->active_input_plugin &&
			framework->active_input_plugin->input_info.on_input) {
		struct ipug_event_info_touch_cancel event_touch_info;
		event_touch_info.base.event_type = IPUG_TOUCH_CANCEL;
		event_touch_info.grab = grab;
		framework->active_input_plugin->input_info.on_input(
									(struct ipug_event_info*)&event_touch_info);
	}
}

static const struct  weston_touch_grab_interface plugin_touch_functions = {
	plugin_touch_grab_down,
	plugin_touch_grab_up,
	plugin_touch_grab_motion,
	plugin_touch_grab_frame,
	plugin_touch_grab_cancel
};
/*****************EOF input functions****************/



/*weston event functions*/
static void
handle_seat_update(struct wl_listener *listener,
		   void *data)
{
	struct weston_seat *seat = data;
	if(framework->active_input_plugin) {
		set_up_seat_with_plugin_framework(seat);
	}
}

static void
handle_seat_created(struct wl_listener *listener,
		    void *data)
{
	struct weston_seat *seat = data;
	if(framework->active_input_plugin) {
		wl_signal_add(&seat->updated_caps_signal, &framework->seat_update_listener);
		set_up_seat_with_plugin_framework(seat);
	}
}

/* TODO: we need multiple grabs for multiple pointers etc */
static void set_up_seat_with_plugin_framework(struct weston_seat *seat)
{
	struct weston_pointer *pointer = weston_seat_get_pointer(seat);
	struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat);
	struct weston_touch *touch = weston_seat_get_touch(seat);

	if(pointer) {
		static struct weston_pointer_grab plugin_pointer_grab = {NULL, NULL};

		plugin_pointer_grab.interface = &plugin_pointer_functions;

	/* TODO: at the moment if there are 2 mice, the last mouse added would always
	 * get the "focus", we need a grab list, with a grab added for each seat */
		weston_pointer_start_grab(pointer, &plugin_pointer_grab);
	}

	if(keyboard) {
		static struct weston_keyboard_grab plugin_keyboard_grab = {NULL, NULL};

		/* if we have a layout plugin but no input plugin loaded
		 * then set the default grabs to the layout plugin's ones */
		if(!framework->input_plugin &&
				framework->last_actived_layout_plugin &&
				framework->last_actived_layout_plugin->info.key_grab.interface) {

			plugin_keyboard_grab.interface =
				framework->last_actived_layout_plugin->info.key_grab.interface;

		} else {
			plugin_keyboard_grab.interface = &plugin_keyboard_functions;
		}

		weston_keyboard_start_grab(keyboard, &plugin_keyboard_grab);
	}

	if(touch)	{
		static struct weston_touch_grab plugin_touch_grab = {NULL, NULL};

		/* if we have a layout plugin but no input plugin loaded
		 * then set the default grabs to the layout plugin's ones */
		if(!framework->input_plugin &&
				framework->last_actived_layout_plugin &&
				framework->last_actived_layout_plugin->info.touch_grab.interface) {

			plugin_touch_grab.interface =
				framework->last_actived_layout_plugin->info.touch_grab.interface;

		} else {
			plugin_touch_grab.interface = &plugin_touch_functions;
		}

		weston_touch_start_grab(touch, &plugin_touch_grab);
	}
}
/*
 * this function will set up the appropriate grabs to initize the deferred input
 */

static void initialize_input_plugin(void)
{
	struct ias_plugin *plugin;
	ias_input_plugin_init_fn input_plugin_init;
	void *handle;
	char *err;
	int ret;
	struct weston_seat *seat;

	plugin = framework->input_plugin;

	/* First we will see if there is an input plugin or not */
	if(!plugin) {
		IAS_DEBUG("No input plugin specified!");

		/* exit on failure */
		exit(1);
	}

	/* Make sure this was properly configured */
	if (!plugin->name || !plugin->libname) {
		IAS_ERROR("Input plugin name or library not specified!");
		free(plugin);
		framework->input_plugin = NULL;

		/* exit on failure */
		exit(1);
	}

	handle = dlopen(plugin->libname, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		IAS_ERROR("Failed to load plugin '%s' from '%s': %s", plugin->name,
				plugin->libname, dlerror());
		free(plugin);
		framework->input_plugin = NULL;

		/* exit on failure */
		exit(1);
	}

	/* Load the initialization function */
	dlerror();
	input_plugin_init = dlsym(handle, "ias_input_plugin_init");
	if ((err = dlerror()) != NULL) {
		IAS_ERROR("No initialization function in plugin '%s'",
				plugin->name);
		free(plugin);
		framework->input_plugin = NULL;

		/* exit on failure */
		exit(1);
	}

	/* Call the initialization function */
	ret = input_plugin_init(&plugin->input_info, PLUGIN_API_VERSION);
	if (ret) {
		IAS_ERROR("Failed to initialize plugin '%s'", plugin->name);
		free(plugin);
		framework->input_plugin = NULL;

		/* exit on failure */
		exit(1);
	}


	framework->input_plugin = plugin;
	framework->active_input_plugin = framework->input_plugin;

	/*
	 * Since there is no activate for input plugin, start grabbing input
	 * events immediately
	 */
	wl_list_for_each(seat, &framework->compositor->seat_list, link) {


		set_up_seat_with_plugin_framework(seat);

	}

	IAS_DEBUG("Loaded input plugin '%s'", plugin->name);
}

static void setvp(int x, int y, int width, int height)
{
	struct ias_backend *ias_backend =
					(struct ias_backend*)(framework->compositor->backend);

	ias_backend->set_viewport(x, y, width, height);
}

/*
 * plugin_on_draw()
 *
 * What happens on a "draw_plugin" call
 */
void plugin_on_draw(struct ias_output * output)
{
	struct weston_output* w_output = (struct weston_output*)output;

	setvp(0, 0, output->width, output->height);

	framework->base.repaint_output(w_output, NULL);
}

void plugin_on_pointer_motion(struct weston_pointer_grab *grab, const struct timespec *time,
		   struct weston_pointer_motion_event *event)
{
	if(framework->last_actived_layout_plugin &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface &&
			framework->last_actived_layout_plugin->info.mouse_grab.interface->motion) {

		/* clamp the pointer coords to an existing output before passing them to
		 * the plugin */
		wl_fixed_t x = wl_fixed_from_double(event->x);
		wl_fixed_t y = wl_fixed_from_double(event->y);
		weston_pointer_clamp(grab->pointer, &x, &y);

		framework->last_actived_layout_plugin->info.mouse_grab.interface->motion(
				grab,
				time,
				event);
		weston_pointer_move(grab->pointer, event);
	}
}

/*
 * initialize_layout_plugin()
 *
 * Load and initialize a layout plugin.  This is generally called for each
 * plugin at module init, but may be deferred via the config file if a plugin
 * won't be used immediately and shouldn't take time away from quickboot.
 */
static int
initialize_layout_plugin(struct ias_plugin *plugin)
{
	void *handle;
	char *err;
	int ret;
	ias_plugin_init_fn plugin_init;

	/* Make sure this was properly configured */
	if (!plugin->name || !plugin->libname) {
		wl_list_remove(&plugin->link);
		free(plugin);
		framework->num_plugins--;

		/* exit on failure */
		exit(1);
	}

	handle = dlopen(plugin->libname, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		IAS_ERROR("Failed to load plugin '%s' from '%s': %s", plugin->name,
				plugin->libname, dlerror());

		wl_list_remove(&plugin->link);
		free(plugin);
		framework->num_plugins--;

		/* exit on failure */
		exit(1);
	}

	/* Load the initialization function */
	dlerror();
	plugin_init = dlsym(handle, "ias_plugin_init");
	if ((err = dlerror()) != NULL) {
		IAS_ERROR("No initialization function in plugin '%s'",
				plugin->name);
		wl_list_remove(&plugin->link);
		free(plugin);
		framework->num_plugins--;

		/* exit on failure */
		exit(1);
	}

	/* Call the initialization function */
	ret = plugin_init(&plugin->info, plugin->info.id, PLUGIN_API_VERSION);
	if (ret) {
		IAS_ERROR("Failed to initialize plugin '%s'", plugin->name);
		wl_list_remove(&plugin->link);
		free(plugin);
		framework->num_plugins--;

		/* exit on failure */
		exit(1);
	}

	plugin->draw_plugin = &(plugin_on_draw);
	/* Mark plugin as initialized */
	plugin->init = 1;
	IAS_DEBUG("Loaded plugin #%d, '%s'", plugin->info.id, plugin->name);
	return 0;
}

/*
 * ias_activate_plugin()
 *
 * Activates a specific plugin by ID number.
 */
WL_EXPORT void
ias_activate_plugin(struct weston_output *output, ias_identifier id)
{
	struct ias_output *ias_output = (struct ias_output *)output;
	struct weston_compositor *compositor = output->compositor;
	struct ias_plugin *plugin;
	struct wl_resource *layout_callback_resource;
	struct weston_seat *seat = NULL;
	struct wl_resource *resource = NULL;
	int ret;

	/*
	 * If we're switching from core weston functionality to a plugin, save
	 * state that the plugin may clobber.
	 *
	 * TODO:  We probably need to add a lot more here, or else make it very
	 * clear to plugin writers that they need to clean up after themselves
	 * when deactivated.
	 */
	if (!ias_output->plugin) {
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING,
				&framework->saved_state.array_buffer_binding);
		glGetIntegerv(GL_CURRENT_PROGRAM, &framework->saved_state.glsl_prog);
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,
				&framework->saved_state.element_array_buffer_binding);
	}

	/* populate the spug lists */
	spug_init_all_lists();

	/* End any input grab already in effect from other plugins & insure default grabs are set up*/
	if(!framework->active_input_plugin) {
		wl_list_for_each(seat, &compositor->seat_list, link) {
			if (seat->pointer_device_count) {
				/* weston_pointer_set_default_grab(seat->pointer,
									seat->pointer->default_grab.interface);*/
				weston_pointer_end_grab(weston_seat_get_pointer(seat));
			}
			if (seat->keyboard_device_count) {
				/* weston_keyboard_set_default_grab(seat->keyboard_state,
									seat->keyboard_state->default_grab.interface); */
				weston_keyboard_end_grab(weston_seat_get_keyboard(seat));
			}
			if (seat->touch_device_count) {
				/* weston_touch_set_default_grab(seat->touch_state,
									seat->touch_state->default_grab.interface); */
				weston_touch_end_grab(weston_seat_get_touch(seat));
			}
		}
	}

	/* Walk the list, looking for this plugin */
	wl_list_for_each(plugin, &framework->plugin_list, link) {
		if (plugin->info.id != id) {
			continue;
		}

		/* Call "switch_from" plugin hook on current plugin (if any) */
		if (ias_output->plugin && ias_output->plugin->info.switch_from) {
			struct spug_output* soutput = get_output_wrapper(output);
			ias_output->plugin->info.switch_from(soutput->id);
		}

		/*
		 * Was initialization of this plugin deferred?  If so, initialize
		 * it now.
		 */
		if (!plugin->init) {
			ret = initialize_layout_plugin(plugin);

			if (ret) {
				/*
				 * We failed to initialize, so we'd better not continue with
				 * the activation.
				 */
				IAS_ERROR("Deferred initialization of layout plugin %d failed",
						id);
				break;
			}
			else
			{
				weston_log("previously deferred layout plugin, now active");
			}
		}

		IAS_DEBUG("Switching to layout '%s'", plugin->name);

		/* Update function pointers */
		ias_output->plugin = plugin;
		framework->last_actived_layout_plugin = plugin;

		/*
		 * If no input plugin is present, then set this plugin's grabs for all
		 * the seats and start grabbing input events
		 */
		if(!framework->active_input_plugin) {

			framework->active_input_plugin = plugin;

			wl_list_for_each(seat, &(compositor->seat_list), link) {
				/*
				 * Recall because seats might have changed?
				 * TODO: test with adding / removing seats, update accordingly
				 */
				set_up_seat_with_plugin_framework(seat);
			}
		}

		/* Call "switch_to" plugin hook */
		if (plugin->info.switch_to) {
			struct spug_output* soutput = get_output_wrapper(output);
			plugin->info.switch_to(soutput->id);
		}

		/*
		 * Broadcast layout change to interested clients
		 *
		 * TODO: Have plugin figure out which clients are relevant (i.e., part
		 * of this layout).
		 */

		wl_list_for_each(layout_callback_resource,
					&framework->layout_change_callbacks, link) {

			struct wl_client *client =
				wl_resource_get_client(layout_callback_resource);
			resource =
				find_resource_for_client(&output->resource_list,
						client);
			ias_layout_manager_send_layout_switched(layout_callback_resource,
																id, resource);
		}

		/*
		 * Also, notify the input plugin (if there is one) that we have a new
		 * layout plugin for this output
		 */
		if(framework->input_plugin &&
			framework->input_plugin->input_info.layout_switch_to) {
			struct spug_output* soutput = get_output_wrapper(output);

			framework->input_plugin->input_info.layout_switch_to(soutput->id,
					&plugin->info);
		}

		/* tell the framework that we're activating the layout plugin. */
		struct ipug_event_info_layout_switch_to info;
		info.base.event_type = IPUG_LAYOUT_SWITCH_TO;
		info.output = output;
		info.plugin = &plugin->info;
		ipug_send_event_to_default((struct ipug_event_info*)&info);

		/* Damage the output to ensure it is redrawn properly. */
		weston_output_damage(output);
		return;
	}

	IAS_ERROR("Failed to activate plugin %d (does not exist)", id);
}

/*
 * ias_deactivate_plugin()
 *
 * Deactivates the current plugin and returns to the default homescreen
 * layout.
 */
WL_EXPORT void
ias_deactivate_plugin(struct weston_output *output)
{
	struct ias_output *ias_output = (struct ias_output *)output;
	struct weston_compositor *compositor = output->compositor;
	struct weston_seat *seat = NULL;
	struct wl_resource *layout_callback_resource;
	struct wl_resource *resource = NULL;
	/* TODO: Use per-output seat? */
	wl_list_for_each(seat, &compositor->seat_list, link) {
		/* Use first seat for now */
		break;
	}

	if (!ias_output->plugin) {
		return;
	}

	IAS_DEBUG("Deactivating layout plugin '%s'", ias_output->plugin->name);

	/* Call "switch_from" plugin hook */
	if (ias_output->plugin->info.switch_from) {
		struct spug_output* soutput = get_output_wrapper(output);
		ias_output->plugin->info.switch_from(soutput->id);
	}

	/* tell the framework that we're deactivating the layout plugin. */
	{
		struct ipug_event_info_layout_switch_from info;
		info.base.event_type = IPUG_LAYOUT_SWITCH_FROM;
		info.output = output;
		info.plugin = &ias_output->plugin->info;
		ipug_send_event_to_default((struct ipug_event_info*)&info);
	}

	/*
	 * Also, notify the input plugin (if there is one) that we are removing
	 * this layout plugin for this output
	 */
	if(framework->input_plugin &&
		framework->input_plugin->input_info.layout_switch_from) {
		struct spug_output* soutput = get_output_wrapper(output);
		framework->input_plugin->input_info.layout_switch_from(soutput->id,
				&ias_output->plugin->info);
	}

	ias_output->plugin = NULL;

	/* Restore saved GL state */
	glBindBuffer(GL_ARRAY_BUFFER, framework->saved_state.array_buffer_binding);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
			framework->saved_state.element_array_buffer_binding);
	glUseProgram(framework->saved_state.glsl_prog);

	/* Release input grabs */
	if(!framework->input_plugin) {
		if (seat->pointer_device_count) {
			weston_pointer_end_grab(weston_seat_get_pointer(seat));
		}
		if (seat->keyboard_device_count) {
			weston_keyboard_end_grab(weston_seat_get_keyboard(seat));
		}
		if (seat->touch_device_count) {
			weston_touch_end_grab(weston_seat_get_touch(seat));
		}
	}

	/* Broadcast layout change to interested clients */
	wl_list_for_each(layout_callback_resource, &framework->layout_change_callbacks, link) {
		struct wl_client *client = wl_resource_get_client(layout_callback_resource);
		/* get new layout */
		resource = find_resource_for_client(&output->resource_list, client);
		/* tell layout mager it's switched */
		ias_layout_manager_send_layout_switched(layout_callback_resource, 0, resource);
	}
	/*
	 * Damage the entire compositor workspace to ensure everything is
	 * redrawn properly.
	 */
	weston_compositor_damage_all(compositor);
}



/***
 *** IAS Layout Manager protocol interface implementation
 ***
 *** The input manager interface allows a client to switch between input
 *** plugins and also raises events to notify clients about changes.
 ***/

/*
 * destroy_ias_input_resource
 *
 * Called when a client unbinds from the ias_input_manager interface.
 * We need to remove the client from the list of bound clients so that
 * we don't continue to try to send events to it.
 */
static void
destroy_ias_input_resource(struct wl_resource *resource)
{
	/* stub */
}
/*
 * input_manager_set_input()
 *
 * Implements the set_input request, allowing clients to request that the
 * compositor switch to a different input plugin.  Passing 0 for the
 * desired input restores the default homescreen input.
 */
static void
ias_input_manager_activate_input(struct wl_client *client,
		struct wl_resource *framework_resource,
		uint32_t input)
{
	struct weston_seat *seat;
	struct weston_keyboard *keyboard = NULL;
	struct weston_pointer *pointer = NULL;
	struct weston_touch *touch = NULL;
	assert(wl_resource_get_user_data(framework_resource) == framework);

	if(input == 0) {
		/* de-activate input plugin */
		framework->active_input_plugin = NULL;
		wl_list_for_each(seat, &framework->compositor->seat_list, link) {

			keyboard = weston_seat_get_keyboard(seat);
			pointer = weston_seat_get_pointer(seat);
			touch = weston_seat_get_touch(seat);

			if(framework->last_actived_layout_plugin) {
				/* if there's  an active layout plugin, send events there */
				if(keyboard) {
					keyboard->grab =
						&framework->last_actived_layout_plugin->info.key_grab;
				}
				if(pointer) {
					pointer->grab =
						&framework->last_actived_layout_plugin->info.mouse_grab;
				}
				if(touch) {
					touch->grab =
						&framework->last_actived_layout_plugin->info.touch_grab;
				}
			} else {
				/* if not, use the defaults */
				if(keyboard) {
					keyboard->grab = &keyboard->default_grab;
				}
				if(pointer) {
					pointer->grab = &pointer->default_grab;
				}
				if(touch) {
					touch->grab = &touch->default_grab;
				}
			}
		}
	} else {
		/* activate input plugin (currently we only support one pre-loaded
		 * input plugin...) */
		if(framework->input_plugin && !framework->input_plugin->init) {
			if(!framework->input_plugin->init) {
				weston_log("previously deferred input plugin, now active");
				initialize_input_plugin();
			} else {
				weston_log("input plugin re-actived");
				framework->active_input_plugin = framework->input_plugin;
			}
		}
	}
}


static const struct ias_input_manager_interface ias_input_manager_implementation = {
		ias_input_manager_activate_input,
};



/*
 * bind_ias_input_manager()
 *
 * Handler executed when client binds to IAS input manager.  Simply
 * binds the global object to a client-specific object ID.
 */
static void
bind_ias_input_manager(struct wl_client *client,
		void *data,
		uint32_t version,
		uint32_t id)
{
	struct wl_resource *input_callback_resource;

	assert(data == framework);

	/*
	 * Any client that binds to the input manager interface is interested in
	 * receiving callbacks when the input is changed.
	 */
	input_callback_resource = wl_resource_create(client,
			&ias_input_manager_interface, version, id );

	/* Fill out resource information */
	wl_resource_set_implementation(input_callback_resource,
			&ias_input_manager_implementation, framework,
			destroy_ias_input_resource);
}


/***
 *** IAS Layout Manager protocol interface implementation
 ***
 *** The layout manager interface allows a client to switch between layout
 *** plugins and also raises events to notify clients about changes.
 ***/

/*
 * destroy_ias_layout_resource
 *
 * Called when a client unbinds from the ias_layout_manager interface.
 * We need to remove the client from the list of bound clients so that
 * we don't continue to try to send events to it.
 */
static void
destroy_ias_layout_resource(struct wl_resource *resource)
{
	struct wl_resource *node, *next;

	wl_list_for_each_safe(node, next, &framework->layout_change_callbacks, link) {
		if (resource->client == node->client) {
			/* Remove ourselves from the layout's destructor list */
			wl_list_remove(&node->link);
			break;
		}
	}
}


/*
 * layout_manager_set_layout()
 *
 * Implements the set_layout request, allowing clients to request that the
 * compositor switch to a different layout plugin.  Passing 0 for the
 * desired layout restores the default homescreen layout.
 */
static void
ias_layout_manager_set_layout(struct wl_client *client,
		struct wl_resource *framework_resource,
		struct wl_resource *output_resource,
		uint32_t layout)
{
	struct weston_output *output = wl_resource_get_user_data(output_resource);

	assert(wl_resource_get_user_data(framework_resource) == framework);

	if (layout == 0) {
		ias_deactivate_plugin(output);


	} else {
		ias_activate_plugin(output, layout);
	}
}


static const struct ias_layout_manager_interface ias_layout_manager_implementation = {
	ias_layout_manager_set_layout,
};



/*
 * bind_ias_layout_manager()
 *
 * Handler executed when client binds to IAS layout manager.  Simply
 * binds the global object to a client-specific object ID.
 */
static void
bind_ias_layout_manager(struct wl_client *client,
		void *data,
		uint32_t version,
		uint32_t id)
{
	struct wl_resource *layout_callback_resource;
	struct ias_plugin *plugin;
	struct weston_output *output;
	struct ias_output *ias_output;
	struct wl_resource *resource = NULL;

	assert(data == framework);

	/*
	 * Any client that binds to the layout manager interface is interested in
	 * receiving callbacks when the layout is changed.
	 */
	layout_callback_resource = wl_resource_create(client,
			&ias_layout_manager_interface, version, id);

	/* Fill out resource information */
	wl_resource_set_implementation(layout_callback_resource,
			&ias_layout_manager_implementation, framework,
			destroy_ias_layout_resource);

	/* Add callback to shell's list */
	wl_list_insert(&(framework->layout_change_callbacks), &(layout_callback_resource->link));


	/* Send the list of layouts to this client only */
	wl_list_for_each(plugin, &framework->plugin_list, link) {
		ias_layout_manager_send_layout(layout_callback_resource,
				plugin->info.id, plugin->name);
	}

	/* Send a layout switched for any active plug-ins to this client only */
	wl_list_for_each(output, &framework->compositor->output_list, link) {
		ias_output = (struct ias_output *)output;
		if (ias_output->plugin) {

			struct wl_client *client = wl_resource_get_client(layout_callback_resource);
			resource =	find_resource_for_client(&output->resource_list,
									client);
			ias_layout_manager_send_layout_switched(layout_callback_resource,
					(uint32_t)ias_output->plugin->info.id, resource);
		}
	}
}




#if IASDEBUG
/*
 * layout1_binding()
 *
 * Activates the first plugin-provided layout on all outputs.
 */
static void
layout1_binding(struct wl_seat *seat,
		const struct timespec *time,
		uint32_t key,
		void *data)
{
	struct weston_compositor *compositor = data;
	struct weston_output *output;

	wl_list_for_each(output, &compositor->output_list, link) {
		ias_activate_plugin(output, 1);
		break;
	}
}

/*
 * layout2_binding()
 *
 * Activates the second plugin-provided layout on all outputs.
 */
static void
layout2_binding(struct wl_seat *seat,
		const struct timespec *time,
		uint32_t key,
		void *data)
{
	struct weston_compositor *compositor = data;
	struct weston_output *output;

	wl_list_for_each(output, &compositor->output_list, link) {
		ias_activate_plugin(output, 2);
		break;
	}
}
#endif

/*
 * module_init()
 *
 * Initialization function for the plugin framework module.  Loads and
 * initializes all plugins specified in the IAS config file.
 */
WL_EXPORT int wet_module_init(struct weston_compositor *compositor, int *argc, char *argv[]);
WL_EXPORT int wet_module_init(struct weston_compositor *compositor,int *argc, char *argv[])
{
	struct ias_backend *ias_backend = (struct ias_backend *)compositor->backend;
	struct ias_plugin *plugin, *next;
	void *shell_handle;
	ias_identifier id = 0;
	struct weston_output *output;
	struct ias_output *ias_output;
	char* output_names[MAX_OUTPUTS];
	int listlen, i;

	/* Allocate plugin framework object */
	framework = calloc(1, sizeof *framework);
	if (!framework) {
		IAS_ERROR("Failed to initialize plugin framework: %m");
		return -1;
	}
	framework->compositor = compositor;
	framework->config_err = SPUG_FALSE;

	/* Try to ensure that the backend we're using is really the IAS backend */
	if (ias_backend->magic != BACKEND_MAGIC) {
		IAS_ERROR("IAS plugin framework cannot be loaded when "
				  "not using the IAS backend");
		return -1;
	}
	/* Allow the plugins we load to access our own symbols */
	self = dlopen("ivi_plugin_framework.so", RTLD_LAZY | RTLD_GLOBAL);
	assert(self);

	/*
	 * We also need a function pointer to the ias_shell's committed function so
	 * that we can determine whether to enable IAS-specific helpers or not.
	 */
	shell_handle = dlopen("ivi-shell.so", RTLD_LAZY | RTLD_LOCAL);
	if (shell_handle) {
		ias_config_fptr = dlsym(shell_handle, "ivi_shell_surface_committed");
		dlclose(shell_handle);
	}

	/* Initialize plugin list */
	wl_list_init(&framework->plugin_list);

	/* Initialize list to track allocated lists */
	wl_list_init(&framework->allocated_lists);

	/* Initialize layout change callback list */
	wl_list_init(&framework->layout_change_callbacks);

	/* set up the renderer interface */
	spug_init_renderer();

	/*
	 * Create a dummy surface that can be used as the focus for plugin input
	 * grabs.  This surface will never be placed on a layer and will never
	 * have buffers attached to it.
	 */
	framework->grab_surface = weston_surface_create(compositor);

	if (!framework->grab_surface) {
		IAS_ERROR("Failed to create grab surface: out of memory");
		dlclose(self);
		free(framework);
		return -1;
	}

	weston_surface_set_size(framework->grab_surface,  16384, 16384);
	weston_surface_damage(framework->grab_surface);
	framework->grab_view = weston_view_create(framework->grab_surface);
	if (!framework->grab_view) {
		IAS_ERROR("Failed to create grab view: out of memory");
		dlclose(self);
		free(framework);
		return -1;
	}

	/* Read any plugins from the config file */
	ias_read_configuration(CFG_FILENAME, config_parse_data,
			sizeof(config_parse_data) / sizeof(config_parse_data[0]),
			framework);

	/* Check if plugin configuration is properly read */
	if (framework->config_err) {
		IAS_ERROR("Failed to read plugin configuration");
		dlclose(self);
		free(framework);
		return -1;
	}

	/* initialise lists so they're available to the plugins' init functions.
	 * There's no activate for the input plugin, so we need to set up the lists
	 * here in case it's initialised without an active layout plugin */
	spug_init_all_lists();

	/* First we will initialize the input plugin */
	if(framework->input_plugin)	{
		if(framework->input_plugin->init_mode != INIT_DEFERRED)	{
			initialize_input_plugin();
		}
	}

	/* Walk the plugin list and load the plugins */
	wl_list_for_each_reverse_safe(plugin, next, &framework->plugin_list, link) {
		/* Note: plugin ID's start from 1 (0 = standard layout) */
		plugin->info.id = ++id;

		if (plugin->init_mode != INIT_DEFERRED) {
			initialize_layout_plugin(plugin);

			if (!plugin->activate_on) {
				continue;
			}

			/*
			 * If this plugin is supposed to be activated immediately on any
			 * outputs, do that now.
			 *
			 * There's no logic here to make sure a customer doesn't write
			 * their config file such that multiple plugins try to activate
			 * immediately.  If they do so, the last plugin to try to activate
			 * will win.
			 *
			 * First, we need to split the comma-separated list into separate
			 * output names.
			 */
			listlen = 0;
			output_names[0] = strtok(plugin->activate_on, ",");
			while (output_names[listlen]) {
				/*
				 * We should never have a list of more outputs than the
				 * compositor can actually handle.
				 */
				if (listlen+1 == MAX_OUTPUTS) {
					break;
				}

				output_names[++listlen] = strtok(NULL, ",");
			}

			/* Now walk the outputs and activate on any that were named */
			wl_list_for_each(output, &framework->compositor->output_list, link) {
				ias_output = (struct ias_output *)output;

				for (i = 0; i < listlen; i++) {
					if (!strcmp(ias_output->name, output_names[i])) {
						ias_activate_plugin(output, plugin->info.id);
					}
				}
			}

			/* No longer need this list of output names */
			free(plugin->activate_on);
		}
	}

	/* Expose the ias_layout_manager interface to clients */
	if (!wl_global_create(compositor->wl_display,
				&ias_layout_manager_interface, 1, framework,
				bind_ias_layout_manager)) {
		return -1;
	}

	/* Expose the ias_input_manager interface to clients */
	if (!wl_global_create(compositor->wl_display,
				&ias_input_manager_interface, 1, framework,
				bind_ias_input_manager)) {
		return -1;
	}

	framework->seat_created_listener.notify = handle_seat_created;
	wl_signal_add(&compositor->seat_created_signal,&framework->seat_created_listener);

	framework->seat_update_listener.notify = handle_seat_update;

	/*
	 * In debug builds, add keybindings to quickly activate one of the first
	 * two plugins
	 */
#if IASDEBUG
	weston_compositor_add_key_binding(compositor, KEY_1,
			MODIFIER_SUPER, layout1_binding, compositor);
	weston_compositor_add_key_binding(compositor, KEY_2,
			MODIFIER_SUPER, layout2_binding, compositor);
#endif

	return 0;
}
