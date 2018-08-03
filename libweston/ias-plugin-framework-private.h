/*
 *-----------------------------------------------------------------------------
 * Filename: ias-plugin-framework-private.h
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
 *   This private header provides the plugin framework info and spug render
 *   interface
 *-----------------------------------------------------------------------------
 */

#ifndef IAS_PLUGIN_FRAMEWORK_PRIVATE_H
#define IAS_PLUGIN_FRAMEWORK_PRIVATE_H

#include <glib.h>

/*
 * Plugin framework information (singleton)
 */
struct {
	struct weston_renderer base;
	struct weston_compositor *compositor;

	/* Number of layout plugins loaded */
	uint32_t num_plugins;

	/* List of layout plugins */
	struct wl_list plugin_list;

	/* One and only input plugin */
	struct ias_plugin *input_plugin;

	/* currently active input plugin (this can also be a layout plugin)*/
	struct ias_plugin *active_input_plugin;

	/* main layout plugin (if input plugin is deactivated we fall back to the last layout plugin activated)*/
	struct ias_plugin *last_actived_layout_plugin;

	/* Compositor saved state to restore after plugin finishes */
	struct {
		GLint array_buffer_binding;
		GLint glsl_prog;
		GLint element_array_buffer_binding;
	} saved_state;

	/*
	 * Dummy surface that all plugin input grabs will use for x,y
	 * positioning.
	 */
	struct weston_surface *grab_surface;
	struct weston_view * grab_view;

	/*listeners for seat hot plugs*/
	struct wl_listener seat_created_listener;
	struct wl_listener seat_update_listener;
	/* Callback list to notify when layout changes */
	struct wl_list layout_change_callbacks;

	/* treat input focus separately for each event type. This allows for
	 * example the layout plugin to have keypresses sent to a focused app,
	 * but continue to receive pointer motion events itself. */
	struct spug_view* input_focus[IPUG_NUM_EVENT_TYPES];

	spug_view_list spug_view_ids;
	spug_surface_list spug_surface_ids;
	spug_seat_list spug_seat_ids;
	spug_output_list spug_output_ids;
	spug_plane_list spug_plane_ids;
	spug_bool lists_initialised;
	spug_bool config_err;
	void *spug_ids[SPUG_WRAPPER_SIZE];
	GHashTable *spug_hashtables[SPUG_WRAPPER_SIZE];
	struct wl_list output_node_list;

	/* keep track of lists allocated to a plugin using spug_filter_view_list(),
	these will be freed at the end of spug_draw() */
	struct wl_list allocated_lists;
} *framework;

struct spug_renderer_interface {

	/* update the specified id list */
	void (*update_spug_ids)(enum spug_wrapper_type table, spug_id **ids);

	/* check that key is already in the specified table. If not, wrap and add it */
	spug_bool (*confirm_hash)(enum spug_wrapper_type table,	void* key);

	/* get a spug wrapper from it's ID */
	struct spug_view* (*get_view_from_id)(const spug_view_id id);
	struct spug_surface* (*get_surface_from_id)(const spug_surface_id id);
	struct spug_output* (*get_output_from_id)(const spug_output_id id);
	struct spug_seat* (*get_seat_from_id)(const spug_seat_id id);
	struct spug_client* (*get_client_from_id)(const spug_client_id id);

	/* get a GL texture name or EGLImage from gl-renderer */
	GLuint (*get_view_texture)(struct weston_view * view);
	EGLImageKHR (*get_view_egl_image)(struct weston_view * view);

	/* destroy spug wrappers */
	void (*destroy_spug_view_common)(struct spug_view *sview);
	void (*destroy_spug_surface_common)(struct spug_surface *ssurface);
	void (*destroy_spug_client_common)(struct spug_client *sclient);
	void (*destroy_spug_seat)(gpointer sseat);
	void (*destroy_spug_output)(gpointer soutput);
	void (*destroy_spug_plane)(gpointer splane);

	/* create info structs passed to the plugin's view_draw function */
	struct spug_view_draw_info* (*create_spug_view_draw_info)(struct spug_view *sview);
	struct spug_surface_draw_info* (*create_spug_surface_draw_info)(struct spug_surface *ssurface);
	struct spug_draw_info* (*create_spug_draw_info)(struct weston_view *view);
	void (*update_spug_draw_infos)(struct spug_view *sview);

	/* create a spug wrapper for the given weston struct */
	struct spug_view* (*create_spug_view)(struct weston_view *view);
	struct spug_surface* (*create_spug_surface)(struct weston_view *view);
	struct spug_seat* (*create_spug_seat)(struct weston_seat *seat);
	struct spug_plane* (*create_spug_plane)(struct weston_plane *plane);
	struct spug_client* (*create_spug_client)(struct wl_client *client);
	struct spug_output* (*create_spug_output)(struct weston_output *output);

	/* call a plugin's bind extension function */
	void (*call_bind_ext)(struct wl_client *client,
				void *data,
				uint32_t version,
				uint32_t id);

	/* input grabs */
	void (*input_grab_focus)(struct weston_pointer_grab *grab,
				struct wl_surface *surface,
				int32_t x,
				int32_t y);

	void (*input_grab_motion)(struct weston_pointer_grab *grab,
				const struct timespec *time,
				wl_fixed_t x,
				wl_fixed_t y);

	void (*input_grab_button)(struct weston_pointer_grab *grab,
				const struct timespec *time,
				uint32_t button,
				uint32_t state);

	void (*input_grab_pointer_cancel)(struct weston_pointer_grab *grab);

	void (*input_grab_touch_down)(struct weston_touch_grab *grab,
			const struct timespec *time,
			int touch_id,
			wl_fixed_t sx,
			wl_fixed_t sy);

	void (*input_grab_touch_up)(struct weston_touch_grab *grab,
			const struct timespec *time,
			int touch_id);

	void (*input_grab_touch_motion)(struct weston_touch_grab *grab,
			const struct timespec *time,
			int touch_id,
			int sx,
			int sy);

	void (*input_grab_touch_frame)(struct weston_touch_grab *grab);

	void (*input_grab_touch_cancel)(struct weston_touch_grab *grab);

	void (*input_grab_key)(struct weston_keyboard_grab *grab,
			const struct timespec *time,
			uint32_t key,
			uint32_t state);

	void (*input_grab_modifiers)(struct weston_keyboard_grab *grab,
			uint32_t serial,
			uint32_t mods_depressed,
			uint32_t mods_latched,
			uint32_t mods_locked,
			uint32_t group);

	void (*input_grab_key_cancel)(struct weston_keyboard_grab *grab);

	void (*layout_switch_to)(struct weston_output *output,
			struct ias_plugin_info *plugin);

	void (*layout_switch_from)(struct weston_output *output,
			struct ias_plugin_info *plugin);

	/* get a client's surface wrapper from it's owning process's pid or name */
	struct spug_surface* (*get_surface_from_pid)(uint32_t pid);
	struct spug_surface* (*get_surface_from_pname)(const char *pname);

	/* send an event to the specified view */
	spug_bool (*send_event_to_view)(struct ipug_event_info *event_info,
			struct spug_view *sview);
};

/* prototypes needed by both ias-plugin-framework.c and ias-spug.c,
 * but not plugins */

struct spug_output*
get_output_wrapper(struct weston_output *output);

struct spug_plane*
get_plane_wrapper(struct weston_plane *plane);

struct spug_client*
get_client_wrapper(struct wl_client *client);

#endif
