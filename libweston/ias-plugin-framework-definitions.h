/*
 *-----------------------------------------------------------------------------
 * Filename: ias-plugin-framework-definitions.h
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
 *   This public header provides the interface by which customer layout
 *   and input plugins may interact with internal data structures of the Intel
 *   Automotive Solutions shell.
 *
 *  split from ias-plugin-framework.h so we can do specific client things to the original .h
 *-----------------------------------------------------------------------------
 */

#ifndef IAS_PLUGIN_DEFINITINS_H
#define IAS_PLUGIN_DEFINITINS_H

#include "ias-spug.h"
/*
 * Function signatures for entry points by which the IAS shell will
 * call into a plugin.
 */

/* Currently supported plugin API version */
#define PLUGIN_API_VERSION 2

typedef uint32_t ias_identifier;
struct ias_sprite;
struct ias_plugin_info;
struct ipug_event_info;

typedef void
(*ias_draw_fn)(spug_view_list);

typedef void
(*ias_switchto_fn)(const spug_output_id);

typedef void
(*ias_switchfrom_fn)(const spug_output_id);

typedef void
(*ias_layout_switchto_fn)(const spug_output_id, struct ias_plugin_info *);

typedef void
(*ias_layout_switchfrom_fn)(const spug_output_id, struct ias_plugin_info *);


/*
 * IAS plugin information structure.  The IAS shell will
 * allocate this structure and pass it the plugin's initialization function to
 * be filled in.  Note that future releases of the IAS compositor may add
 * additional fields to this structure (but may not change or remove the
 * existing fields after an official release), so the inforec_version field
 * should be used to allow forward/backward compatibility.
 */
struct ias_plugin_info {

	ias_switchto_fn      switch_to;
	ias_switchfrom_fn    switch_from;

	/* Input grabs */
	struct weston_pointer_grab mouse_grab;
	struct weston_keyboard_grab key_grab;
	struct weston_touch_grab touch_grab;

	/*
	 * Forward-compatibility support: version of ias_plugin_info that the
	 * plugin has filled this structure according to.
	 */
	uint32_t inforec_version;

	/* Runtime identifier of plugin */
	unsigned int id;

	ias_draw_fn          draw;
};


struct ias_input_plugin_info {

	ias_layout_switchto_fn   layout_switch_to;
	ias_layout_switchfrom_fn layout_switch_from;

	/* Input grabs */
	struct weston_pointer_grab_interface *mouse_grab;
	struct weston_keyboard_grab_interface *key_grab;
	struct weston_touch_grab_interface *touch_grab;

	/* API plugin entry point callback */
	void(*on_input)(struct ipug_event_info *info);
};

/* Plugin initialization function pointer type */
typedef int
(*ias_plugin_init_fn)(struct ias_plugin_info *,
		ias_identifier,
		uint32_t);

/* Input plugin initialization function pointer type */
typedef int
(*ias_input_plugin_init_fn)(struct ias_input_plugin_info *,
		uint32_t);

/***
 *** Helper functions that plugin may use to call back into the compositor.
 ***/
WL_EXPORT struct ias_surface *fetch_ias_surface(struct weston_surface *surface);
WL_EXPORT uint32_t ias_get_behavior_bits(struct weston_surface *surface);
WL_EXPORT uint32_t ias_get_zorder(struct weston_surface *surface);
WL_EXPORT void ias_activate_plugin(struct weston_output *, ias_identifier);
WL_EXPORT void ias_deactivate_plugin(struct weston_output *);
WL_EXPORT int ias_has_surface_timedout(struct weston_surface *);
WL_EXPORT void ias_toggle_frame_events(struct weston_surface *);
WL_EXPORT int ias_frame_events_enabled(struct weston_surface *);
WL_EXPORT void ias_get_owning_process_info(
		struct weston_surface *surface,
		uint32_t *pid,
		char **pname);
WL_EXPORT struct weston_surface * ias_get_weston_surface(
		uint32_t ias_surface_id);
WL_EXPORT uint32_t ias_is_surface_flippable(
		struct weston_view *view,
		struct weston_output *output);
WL_EXPORT uint32_t ias_assign_surface_to_scanout(
		struct weston_view *view,
		struct weston_output *output,
		uint32_t x,
		uint32_t y);
/*
 * ias_get_sprite_list()
 *
 * Returns an array of opaque ias_sprite structures representing the sprites
 * available for use on a given output.  These ias_sprite structures are
 * passed to other helper functions.  Note that sprites may be completely
 * unavailable in some modes of operation (interleaved dualview, stereo HDMI,
 * etc.) so the return value should be checked to see how big the sprite list
 * actually is.
 *
 * The caller should free() the sprite list (but not the sprite objects
 * themselves) when done to avoid memory leaks.
 */
WL_EXPORT int ias_get_sprite_list(struct weston_output *,
		struct ias_sprite ***);

/*
 * ias_assign_zorder_to_sprite()
 *
 * Given the sprite id, move the sprite to the top of bottom of all planes include
 * display plane or another sprite plane.  The sprite_id can obtains after
 * assign surface to sprite.
 *
 */
WL_EXPORT int
ias_assign_zorder_to_sprite(struct weston_output *output,
		int sprite_id,
		int position);

enum sprite_surface_zorder {
	SPRITE_SURFACE_ZORDER_TOP,
	SPRITE_SURFACE_ZORDER_BOTTOM
};

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
		int enable);

/*
 * ias_assign_surface_to_sprite()
 *
 * Assigns the specified surface (or subregion of the surface) to a sprite for
 * the frame currently being rendered and positions it at the specified
 * position on the screen.
 * If sprite_width and sprite_height parameters will be 0, surface size
 * (or size of surface subregion) will be used as size of sprite, if non 0
 * values will be provided for both, then surface (or its subregion) will
 * be scaled (in hardware) to match provided sprite size.
 * Downscaling is not supported and minimal required visible sprite
 * width/height when scaling is enabled is 10 pixels, if these condition
 * won't be met surface won't be assigned to sprite.
 *
 * This function should be called by a plugin's 'draw'
 * entrypoint each frame.  If the plugin does not call this function, the
 * sprite will automatically be turned off and not used for the current frame.
 *
 * The final parameter (region) specifies the subrectangle of the surface that
 * should actually be shown onscreen.  Passing NULL will cause the entire
 * surface to be shown.
 *
 * This function returns the weston_plane structure associated with the sprite
 * that the surface was assigned to.
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
	pixman_region32_t *surface_region);

/*
 * ias_set_plugin_redraw_behavior()
 *
 * Changes the redraw strategy for an output.  By default, an output will be
 * redrawn each vblank if a plugin is active, otherwise it will only be drawn
 * when there is damage that the compositor is aware of.  This helper function
 * allows a plugin to change the redraw behavior of an output when a plugin
 * is active (so that redraws only happen on compositor-known damage).
 *
 * Important notes:
 *  - This function will have no effect when running in GPU-based dualview mode
 *  - When configured to redraw upon damage, the compositor will assume that
 *    surface geometry is a proper indication of which output a surface would
 *    show up on (which output(s) should be redrawn).
 */
WL_EXPORT void ias_set_plugin_redraw_behavior(struct weston_output *,
		enum plugin_redraw_behavior);

/*
 * Surface behaviors.  Behaviors are a set of bits associated with a surface.
 * The upper 8 bits are reserved for use by the shell itself .  The lower 24
 * bits may be used for customer-specific purposes (i.e., apps can turn on
 * or off specific bits and then the custom layout plugins can use those bits
 * to control their behavior).
 */
enum shell_surface_behavior {
	SHELL_SURFACE_BEHAVIOR_REGULAR    = 0,
	SHELL_SURFACE_BEHAVIOR_TRANSIENT  = 0x01000000,
	SHELL_SURFACE_BEHAVIOR_BACKGROUND = 0x02000000,
	SHELL_SURFACE_BEHAVIOR_HIDDEN     = 0x04000000,
};

#endif
