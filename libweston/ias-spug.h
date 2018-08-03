/*
 *-----------------------------------------------------------------------------
 * Filename: ias-spug.h
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
 *-----------------------------------------------------------------------------
 */

#ifndef IAS_SPUG_H
#define IAS_SPUG_H

struct ias_output;
struct ias_sprite;
struct spug_view_draw_info;
struct spug_draw_info;

/* spug types */
typedef unsigned int* spug_id;
typedef unsigned int* spug_view_id;
typedef unsigned int* spug_surface_id;
typedef unsigned int* spug_seat_id;
typedef unsigned int* spug_output_id;
typedef unsigned int* spug_plane_id;
typedef unsigned int* spug_global_id;
typedef unsigned int* spug_client_id;
typedef spug_view_id * spug_view_list;
typedef spug_surface_id * spug_surface_list;
typedef spug_seat_id * spug_seat_list;
typedef spug_output_id * spug_output_list;
typedef spug_plane_id * spug_plane_list;
typedef unsigned int spug_is_mask;
typedef int spug_bool;
typedef unsigned int ipug_event_mask;
typedef struct weston_matrix spug_matrix;
/* this seems to be unused. I can't find the definition anywhere and I've only
 * come across it in the layout plugin samples */
typedef struct wl_surface spug_wl_surface;
typedef wl_fixed_t spug_fixed_t;
typedef struct weston_touch_grab spug_touch_grab;
typedef struct weston_touch_grab_interface spug_touch_grab_interface;
typedef struct weston_keyboard_grab spug_keyboard_grab;
typedef struct weston_keyboard_grab_interface spug_keyboard_grab_interface;
typedef struct weston_pointer_grab spug_pointer_grab;
typedef struct weston_pointer_grab_interface spug_pointer_grab_interface;

/* wl_list wrappers */
typedef struct wl_list spug_list;
#define spug_list_for_each(pos, head, member) wl_list_for_each(pos, head, member)
#define spug_list_for_each_safe(pos, tmp, head, member) wl_list_for_each_safe(pos, tmp, head, member)
#define spug_list_for_each_reverse(pos, head, member) spug_list_for_each_reverse(pos, head, member)
#define spug_list_for_each_reverse_safe(pos, tmp, head, member) wl_list_for_each_reverse_safe(pos, tmp, head, member)

WL_EXPORT void spug_list_init(spug_list *list);
WL_EXPORT void spug_list_insert(spug_list *list, spug_list *elm);
WL_EXPORT void spug_list_remove(spug_list *elm);
WL_EXPORT int spug_list_length(const spug_list *list);
WL_EXPORT int spug_list_empty(const spug_list *list);
WL_EXPORT void spug_list_insert_list(spug_list *list, spug_list *other);

struct spug_interface;
/* wl_message replacement */
struct spug_message {
	const char *name;
	const char *signature;
	const struct spug_interface **types;
};

/* wl_interface replacement */
struct spug_interface {
	const char *name;
	int version;
	int method_count;
	const struct spug_message *methods;
	int event_count;
	const struct spug_message *events;
};

/* wl_object replacement */
struct spug_object {
	const struct spug_interface *interface;
	const void *implementation;
	uint32_t id;
};

typedef wl_global_bind_func_t spug_global_bind_func_t;

struct spug_extension_data {
	void* data;
	spug_global_bind_func_t bind;
};

struct spug_global {
	struct wl_signal destroy_signal;
	struct wl_listener global_destroy_listener;

	spug_global_id id;
	struct wl_global *global;
	struct spug_extension_data *ext_data;
};

struct spug_client {
	struct wl_list link;
	spug_client_id id;
	struct wl_client *client;
	struct wl_signal destroy_signal;
	struct wl_listener client_destroy_listener;
};

typedef struct wl_resource spug_resource;

/* wrapper for wl_global_create. Used for extending wayland */
WL_EXPORT spug_global_id spug_global_create(const struct spug_interface *interface,
									void *data, spug_global_bind_func_t bind);

/* wrapper for wl_global_destroy() */
WL_EXPORT void spug_global_destroy(spug_global_id global);

/* wrapper for wl_client_add_resource */
WL_EXPORT uint32_t
spug_client_add_resource(spug_client_id client_id, spug_resource *resource);

/* typedef of the function pointer passed to glib for destroying any elements
 * remaining in a hash table when that table is destroyed */
typedef void
(spug_glib_destructor)(void*);

typedef void*
(spug_wrapper_constructor)(void*);

/* plugin callbacks */

/* spug_filter_view_list */
typedef spug_bool
(*spug_view_filter_fn)(const spug_view_id, spug_view_list);

/* spug_draw */
typedef	void
(*spug_view_draw_fn)(struct spug_view_draw_info *, struct spug_draw_info *);

typedef void
(*spug_view_draw_setup_fn)(void);

typedef void
(*spug_view_draw_clean_state_fn)(void);


struct spug_view {
	struct wl_signal destroy_signal;
	struct wl_listener view_destroy_listener;

	spug_view_id id;
	struct weston_view *view;
	struct spug_view_draw_info *view_draw_info;
	struct spug_draw_info *draw_info;
};

struct spug_surface {
	struct wl_signal destroy_signal;
	struct wl_listener surface_destroy_listener;

	spug_surface_id id;
	struct weston_surface *surface;
	/* there may be multiple views pointing to this surface. parent_view is the
	 * one that was passed to create_spug_surface */
	struct spug_view *parent_view;
	struct spug_surface_draw_info *surface_draw_info;
	struct spug_draw_info *draw_info;
};

struct spug_seat {
	struct weston_seat *seat;
	spug_seat_id id;
};

struct spug_output {
	struct weston_output *output;
	spug_output_id id;
};

struct spug_plane {
	struct weston_plane *plane;
	spug_plane_id id;
};

struct spug_view_draw_info {
	GLuint texture;
	EGLImageKHR egl_image;
	spug_view_id id;
};

/* same as spug_view_draw_info now, but it'd be good to keep it separate in case
 * we want to add more to either one in the future */
struct spug_surface_draw_info {
	GLuint texture;
	EGLImageKHR egl_image;
	spug_surface_id id;
};

struct spug_draw_info {
};

/* spug_bool */
#define SPUG_FALSE 0
#define SPUG_TRUE  1

/* spug_is_mask */
#define SPUG_IS_COMP_SOLID 	(1 << 1)
#define SPUG_IS_CURSOR		(1 << 2)

/* a node for a wl_list to track list allocations */
struct spug_allocated_list {
	spug_view_list allocated_list;
	struct wl_list link;
};

/* wrapper lists */
enum spug_wrapper_type {
	SPUG_WRAPPER_VIEW = 0,
	SPUG_WRAPPER_SURFACE,
	SPUG_WRAPPER_SEAT,
	SPUG_WRAPPER_OUTPUT,
	SPUG_WRAPPER_PLANE,
	SPUG_WRAPPER_CLIENT,
	SPUG_WRAPPER_GLOBAL,

	/* this has to stay at the bottom, it's used for tracking the number of
	 * e.g hashtables we need */
	SPUG_WRAPPER_SIZE
};

/* input events */
enum ipug_event_type {
	IPUG_POINTER_FOCUS,
	IPUG_POINTER_MOTION,
	IPUG_POINTER_BUTTON,
	IPUG_POINTER_CANCEL,
	IPUG_KEYBOARD_KEY,
	IPUG_KEYBOARD_MOD,
	IPUG_KEYBOARD_CANCEL,
	IPUG_TOUCH_DOWN,
	IPUG_TOUCH_UP,
	IPUG_TOUCH_MOTION,
	IPUG_TOUCH_FRAME,
	IPUG_TOUCH_CANCEL,
	IPUG_LAYOUT_SWITCH_TO,
	IPUG_LAYOUT_SWITCH_FROM,

	/* this has to stay at the bottom */
	IPUG_NUM_EVENT_TYPES
};

/* These are passed to ipug_set_input_focus to select the event types the plugin
 * is focusing */
#define IPUG_POINTER_FOCUS_BIT		(1 << 0)
#define IPUG_POINTER_MOTION_BIT		(1 << 1)
#define IPUG_POINTER_BUTTON_BIT		(1 << 2)
#define IPUG_POINTER_CANCEL_BIT		(1 << 3)
#define IPUG_KEYBOARD_KEY_BIT		(1 << 4)
#define IPUG_KEYBOARD_MOD_BIT		(1 << 5)
#define IPUG_KEYBOARD_CANCEL_BIT	(1 << 6)
#define IPUG_TOUCH_DOWN_BIT			(1 << 7)
#define IPUG_TOUCH_UP_BIT			(1 << 8)
#define IPUG_TOUCH_MOTION_BIT		(1 << 9)
#define IPUG_TOUCH_FRAME_BIT		(1 << 10)
#define IPUG_TOUCH_CANCEL_BIT		(1 << 11)
#define IPUG_LAYOUT_SWITCH_TO_BIT	(1 << 12)
#define IPUG_LAYOUT_SWITCH_FROM_BIT	(1 << 13)

#define IPUG_POINTER_ALL_BIT (IPUG_POINTER_FOCUS_BIT | \
						IPUG_POINTER_MOTION_BIT | \
						IPUG_POINTER_BUTTON_BIT | \
						IPUG_POINTER_CANCEL_BIT)

#define IPUG_KEYBOARD_ALL_BIT (IPUG_KEYBOARD_KEY_BIT | \
                           IPUG_KEYBOARD_MOD_BIT | \
                           IPUG_KEYBOARD_CANCEL_BIT)

#define IPUG_TOUCH_ALL_BIT (IPUG_TOUCH_DOWN_BIT | \
						IPUG_TOUCH_UP_BIT | \
						IPUG_TOUCH_MOTION_BIT | \
						IPUG_TOUCH_FRAME_BIT | \
						IPUG_TOUCH_CANCEL_BIT)

#define IPUG_LAYOUT_SWITCH_ALL_BIT (IPUG_LAYOUT_SWITCH_TO_BIT | \
								IPUG_LAYOUT_SWITCH_FROM_BIT)

#define IPUG_EVENTS_ALL_BIT (IPUG_POINTER_ALL_BIT | \
						IPUG_KEYBOARD_ALL_BIT | \
						IPUG_TOUCH_ALL_BIT | \
						IPUG_LAYOUT_SWITCH_ALL_BIT)

/* moved these enums here from ias-plugin-framework-definitions.h
 * to avoid a circular dependency */
enum plugin_redraw_behavior {
	PLUGIN_REDRAW_ALWAYS,
	PLUGIN_REDRAW_DAMAGE
};
/*
 * Surface zorder assignment.  zorder is basically a way of exposing weston's
 * internal layer concept to applications.  Multiple surfaces may be placed at
 * the same z-order.  The actual term "layer" was avoided to avoid confusion
 * with the GenIVI layer manager.  Although apps can specify their own z-order
 * for surfaces (with most surfaces going to the default z-order of 0), some
 * z-order values (above 0x00ffffff) do have special shell-specific meaning
 * and will always be rendered above or below any user-defined z-order.  These
 * values are used for special purposes (background, popup, fullscreen, etc.).
 */
enum shell_surface_zorder {
	SHELL_SURFACE_ZORDER_DEFAULT    = 0,
	SHELL_SURFACE_ZORDER_BACKGROUND = 0x01000000,
	SHELL_SURFACE_ZORDER_FULLSCREEN,
	SHELL_SURFACE_ZORDER_POPUP,
};

/* equivalent to wl_output_transform */
enum spug_output_transform {
	SPUG_OUTPUT_TRANSFORM_NORMAL = 0,
	SPUG_OUTPUT_TRANSFORM_90 = 1,
	SPUG_OUTPUT_TRANSFORM_180 = 2,
	SPUG_OUTPUT_TRANSFORM_270 = 3,
	SPUG_OUTPUT_TRANSFORM_FLIPPED = 4,
	SPUG_OUTPUT_TRANSFORM_FLIPPED_90 = 5,
	SPUG_OUTPUT_TRANSFORM_FLIPPED_180 = 6,
	SPUG_OUTPUT_TRANSFORM_FLIPPED_270 = 7,
};

enum spug_blend_factor {
	SPUG_BLEND_FACTOR_AUTO,
	SPUG_BLEND_FACTOR_ZERO,
	SPUG_BLEND_FACTOR_ONE,
	SPUG_BLEND_FACTOR_SRC_ALPHA,
	SPUG_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	SPUG_BLEND_FACTOR_CONSTANT_ALPHA,
	SPUG_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
	SPUG_BLEND_FACTOR_CONSTANT_ALPHA_TIMES_SRC_ALPHA,
	SPUG_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA_TIMES_SRC_ALPHA,
};

struct ipug_event_listener {
	struct ias_plugin *plugin;
};

struct ipug_event_info {
	enum ipug_event_type event_type;
};

struct ipug_event_info_pointer_focus {
	struct ipug_event_info base;
	struct weston_pointer_grab *grab;
	struct wl_surface *surface;
	int32_t x;
	int32_t y;
};

struct ipug_event_info_pointer_motion {
	struct ipug_event_info base;
	struct weston_pointer_grab *grab;
	const struct timespec *time;
	wl_fixed_t x;
	wl_fixed_t y;
	uint32_t mask;
};

struct ipug_event_info_pointer_button {
	struct ipug_event_info base;
	struct weston_pointer_grab *grab;
	const struct timespec *time;
	uint32_t button;
	uint32_t state;
};

struct ipug_event_info_pointer_cancel {
	struct ipug_event_info base;
	struct weston_pointer_grab *grab;
};

struct ipug_event_info_touch_down {
	struct ipug_event_info base;
	struct weston_touch_grab *grab;
	const struct timespec *time;
	int touch_id;
	wl_fixed_t sx;
	wl_fixed_t sy;
};

struct ipug_event_info_touch_motion {
	struct ipug_event_info base;
	struct weston_touch_grab *grab;
	const struct timespec *time;
	int touch_id;
	int sx;
	int sy;
};

struct ipug_event_info_touch_up {
	struct ipug_event_info base;
	struct weston_touch_grab *grab;
	const struct timespec *time;
	int touch_id;
};

struct ipug_event_info_touch_frame {
	struct ipug_event_info base;
	struct weston_touch_grab *grab;
};

struct ipug_event_info_touch_cancel {
	struct ipug_event_info base;
	struct weston_touch_grab *grab;
};

struct ipug_event_info_key_key {
	struct ipug_event_info base;
	struct weston_keyboard_grab *grab;
	const struct timespec *time;
	uint32_t key;
	uint32_t state;
};

struct ipug_event_info_key_mod {
	struct ipug_event_info base;
	struct weston_keyboard_grab *grab;
	uint32_t serial;
	uint32_t mods_depressed;
	uint32_t mods_latched;
	uint32_t mods_locked;
	uint32_t group;
};

struct ipug_event_info_key_cancel {
	struct ipug_event_info base;
	struct weston_keyboard_grab *grab;
};

struct ipug_event_info_layout_switch_to {
	struct ipug_event_info base;
	struct weston_output *output;
	struct ias_plugin_info *plugin;
};

struct ipug_event_info_layout_switch_from {
	struct ipug_event_info base;
	struct weston_output *output;
	struct ias_plugin_info *plugin;
};

/* wrappers around ias_* functions */
WL_EXPORT uint32_t spug_assign_surface_to_scanout(	const spug_view_id view,
													int x, int y);
WL_EXPORT void spug_toggle_frame_events(const spug_view_id view);
WL_EXPORT int spug_frame_events_enabled(const spug_view_id id);
WL_EXPORT uint32_t spug_is_surface_flippable(const spug_view_id view);
WL_EXPORT int spug_has_surface_timedout(const spug_view_id view);
WL_EXPORT uint32_t spug_get_behavior_bits(const spug_view_id id);
WL_EXPORT enum shell_surface_zorder spug_get_zorder(const spug_view_id view_id);
WL_EXPORT void spug_activate_plugin(const spug_output_id output,
									uint32_t ias_id);
WL_EXPORT void spug_set_plugin_redraw_behavior(	const spug_output_id id,
										enum plugin_redraw_behavior behavior);
WL_EXPORT void spug_deactivate_plugin(const spug_output_id id);
WL_EXPORT void spug_get_owning_process_info(const spug_view_id id,
											uint32_t *pid,
											char **pname);
WL_EXPORT int spug_get_sprite_list( const spug_output_id id,
									struct ias_sprite ***sprites);
WL_EXPORT struct spug_plane* spug_assign_surface_to_sprite(const spug_view_id view_id,
								const spug_output_id output_id,
								int* sprite_id,
								int x, int y,
								pixman_region32_t *surface_region);
WL_EXPORT struct spug_plane* spug_assign_surface_to_sprite_scaled(const spug_view_id view_id,
								const spug_output_id output_id,
								int* sprite_id,
								int x, int y, int sprite_w, int sprite_h,
								pixman_region32_t *surface_region);

WL_EXPORT int spug_assign_zorder_to_sprite(	const spug_output_id output_id,
											int sprite_id,
											int position);
WL_EXPORT int spug_assign_constant_alpha_to_sprite(	const spug_output_id output_id,
													int sprite_id,
													float constant_value,
													int enable);
WL_EXPORT int spug_assign_blending_to_sprite(const spug_output_id output_id,
											int sprite_id,
											int src_factor,
											int dst_factor,
											float blend_color,
											int enable);

/* return true if the view is a cursor */
WL_EXPORT spug_bool spug_view_is_cursor(const spug_view_id view_id);

/* return true if the view is the sprite belonging to the specified pointer  */
WL_EXPORT spug_bool spug_view_is_sprite(const spug_view_id view_id,
										const spug_seat_id seat_id);

/* get the number of GL textures belonging to this view */
WL_EXPORT int spug_view_num_textures(const spug_view_id view_id);

/* I like this one better than the above two */
WL_EXPORT spug_bool spug_view_is(const spug_view_id view, const spug_is_mask mask);

/* view getters */
WL_EXPORT pixman_region32_t* spug_view_get_trans_boundingbox(const spug_view_id view_id);

/* we'll need some sort of mechanism for dealing with plugins that want to
 * maintain multiple view lists. There are a few options:
 *		1) allow the plugin to copy lists
 *		2) have spug_filter_view_list return a new list, leaving the one
 *		it is passed intact.
 * I'd rather go for 2), as it would have the filter building a
 * list rather than stripping one down, which makes the max_views param to
 * spug_filter_view_list more useful.
 */
WL_EXPORT void spug_copy_view_list(spug_view_list *dst, spug_view_list src);
WL_EXPORT void spug_release_view_list(spug_view_list *view_list);
WL_EXPORT spug_bool spug_view_list_is_empty(spug_view_list *view_list);


/* iterates over a view list, passing each view to view_draw. returns a list
 * of the views that weren't rejected by view_draw. will stop once max_views
 * is reached
 */
WL_EXPORT spug_view_list
spug_filter_view_list(spug_view_list view_list,
					spug_view_filter_fn view_filter,
					int max_views);

/* call all the spug_init_*_list() functions below */
WL_EXPORT void spug_init_all_lists(void);

/* call all the spug_update_*_list() functions below */
WL_EXPORT void spug_update_all_lists(void);

/* spug_view list */
WL_EXPORT void spug_update_view_list(void);
WL_EXPORT void spug_init_view_list(void);
WL_EXPORT spug_view_list spug_get_view_list(void);
WL_EXPORT int spug_view_list_length(spug_view_list view_list);

/* spug_surface list */
WL_EXPORT void spug_update_surface_list(void);
WL_EXPORT void spug_init_surface_list(void);
WL_EXPORT spug_surface_list spug_get_surface_list(void);
WL_EXPORT int spug_surface_list_length(spug_surface_list surface_list);

/* spug_seat list */
WL_EXPORT void spug_update_seat_list(void);
WL_EXPORT void spug_init_seat_list(void);
WL_EXPORT spug_seat_list spug_get_seat_list(void);
WL_EXPORT int spug_seat_list_length(spug_seat_list seat_list);

/* spug_output list */
WL_EXPORT void spug_update_output_list(void);
WL_EXPORT void spug_init_output_list(void);
WL_EXPORT spug_output_list spug_get_output_list(void);
WL_EXPORT int spug_output_list_length(spug_output_list output_list);

/* spug_plane list */
WL_EXPORT void spug_update_plane_list(void);
WL_EXPORT void spug_init_plane_list(void);

/* return true if the seat has a pointer with a sprite */
WL_EXPORT spug_bool spug_seat_has_sprite(const spug_seat_id seat_id);

/* get the spug_view_id of the seat's sprite, if it has one */
WL_EXPORT spug_view_id spug_get_seat_sprite(const spug_seat_id seat_id);

/* get the number of pointer, keyboard or touch devices belonging to the
 * specified seat */
WL_EXPORT int spug_get_seat_pointer_count(spug_seat_id seat_id);
WL_EXPORT int spug_get_seat_keyboard_count(spug_seat_id seat_id);
WL_EXPORT int spug_get_seat_touch_count(spug_seat_id seat_id);

/* get the name of a seat */
WL_EXPORT const char* spug_get_seat_name(spug_seat_id seat_id);

/* get the current output of a seat */
WL_EXPORT spug_output_id spug_get_seat_output(spug_seat_id seat_id);

/* calls view_draw on each view in view_list. the loop will be bookended by
 * view_draw_setup() and view_draw_clean_state() if they are non-NULL
 */
WL_EXPORT void spug_draw(spug_view_list view_list,
						spug_view_draw_fn view_draw,
						spug_view_draw_setup_fn view_draw_setup,
						spug_view_draw_clean_state_fn view_draw_clean_state);

/* draw the default (weston's?) cursor. */
WL_EXPORT void spug_draw_default_cursor(void);

/* get the mouse coordinates. Presently uses the first seat with a pointer in
 * compositor->seat_list. returns SPUG_TRUE on success, SPUG_FALSE on failure */
WL_EXPORT spug_bool spug_mouse_xy(int *x, int *y, spug_seat_id seat_id);

/* return id of output containing  the given point */
WL_EXPORT spug_output_id spug_get_output_id(int x, int y);

/* is this function a bit ...silly? */
/* return the given point's coordinates relative to the origin of the output
 * it falls in */
WL_EXPORT void spug_point_output_relative_xy(	int x,
												int y,
												int *rel_x,
												int *rel_y);

/* return output extents */
WL_EXPORT void spug_get_output_region(	spug_output_id id,
										int *x,
										int *y,
										int *width,
										int *height);

/* wl_fixed_* wrappers */
WL_EXPORT double spug_fixed_to_double(spug_fixed_t f);
WL_EXPORT int spug_fixed_to_int(spug_fixed_t f);
WL_EXPORT spug_fixed_t spug_fixed_from_double(double d);
WL_EXPORT spug_fixed_t spug_fixed_from_int(int i);


/* get an output's matrix */
WL_EXPORT spug_matrix* spug_get_matrix(spug_output_id id);

/* get an output's transformation */
WL_EXPORT enum spug_output_transform spug_get_output_transform(spug_output_id id);

/* weston_matrix_* wrappers. */
WL_EXPORT void spug_matrix_init(spug_matrix *matrix);
WL_EXPORT void spug_matrix_multiply(spug_matrix *m, const spug_matrix *n);
WL_EXPORT void spug_matrix_translate(spug_matrix *matrix, float x, float y, float z);
WL_EXPORT void spug_matrix_scale(spug_matrix *matrix, float x, float y,float z);
WL_EXPORT void spug_matrix_rotate_xy(spug_matrix *matrix, float cos, float sin);
WL_EXPORT void spug_matrix_transform(spug_matrix *matrix, struct weston_vector *v);

/* wrapper around weston_seat_set_keyboard_focus */
WL_EXPORT void spug_surface_activate(spug_view_id view, spug_seat_id seat);

/* send input event to surface belonging to process with given pid */
WL_EXPORT spug_bool ipug_send_event_to_pid(struct ipug_event_info *info, uint32_t pid);

/* send input event to surface belonging to process with given pname */
WL_EXPORT spug_bool ipug_send_event_to_pname(struct ipug_event_info *info, const char *pname);

/* send input event to currently focused listener */
WL_EXPORT spug_bool ipug_send_event_to_focus(struct ipug_event_info *event_info);

/* send input event to a specific view */
WL_EXPORT spug_bool ipug_send_event_to_view(struct ipug_event_info *event_info,
											const spug_view_id view_id);

/* set the focused surface for the specified event types in ipug_send_event_to_focus */
WL_EXPORT void ipug_set_input_focus(ipug_event_mask event_mask, const spug_view_id);

/* return a bitfield containing the event types that are currently focused on a view */
WL_EXPORT ipug_event_mask ipug_get_input_focus(const spug_view_id view_id);

/* send input event to the default listener */
WL_EXPORT spug_bool ipug_send_event_to_default(struct ipug_event_info *event_info);

/* initialise the renderer */
WL_EXPORT void spug_init_renderer(void);
#endif
