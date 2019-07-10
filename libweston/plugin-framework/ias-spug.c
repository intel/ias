/*
 *-----------------------------------------------------------------------------
 * Filename: ias-spug.c
 *-----------------------------------------------------------------------------
 * Copyright 2014-2018 Intel Corporation
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
 *   Intel Automotive Solutions "spug" API implementated functions used by
 *   layout and input plugins.
 *-----------------------------------------------------------------------------
 */
#include <ias-common.h>
#include <string.h>
#include <glib.h>
#include <inttypes.h>
#include <libweston-internal.h>

#include "ias-plugin-framework-private.h"

struct plugin_output_node {

	struct ias_plugin_info *plugin;
	struct weston_output *output;

	/* Link in plugin list */
	struct wl_list link;
};

struct spug_renderer_interface renderer_interface;

static void
spug_init_hashtable(enum spug_wrapper_type table, spug_glib_destructor destructor);

static void
spug_destroy_hashtable(enum spug_wrapper_type table);

static void
_spug_global_destroy(gpointer data);

static void
update_spug_ids(enum spug_wrapper_type table, spug_id** ids)
{
	GList *cur, *head;
	int num_values, i = 0;
	spug_id *id_arr;

	/* regenerate list of view ids */
	if(*ids) {
		free(*ids);
	}

	head = g_hash_table_get_keys(framework->spug_hashtables[table]);
	num_values = g_list_length(head);

	(*ids) = calloc(num_values + 1, sizeof(spug_id));
        if ((*ids) == NULL) {
		IAS_ERROR("Memory Allocation failure \n");
		return;
	}

	cur = head;
	id_arr = *ids;
	while(i < num_values) {
		id_arr[i++] = (spug_id)cur->data;
		cur = cur->next;
	}
	g_list_free(head);
}

static gpointer
wrap(enum spug_wrapper_type table, void *key)
{
	switch(table) {
		case SPUG_WRAPPER_VIEW:
			return (gpointer)renderer_interface.create_spug_view((struct weston_view*)key);
		case SPUG_WRAPPER_SURFACE:
			return (gpointer)renderer_interface.create_spug_surface((struct weston_view*)key);
		case SPUG_WRAPPER_SEAT:
			return (gpointer)renderer_interface.create_spug_seat((struct weston_seat*)key);
		case SPUG_WRAPPER_OUTPUT:
			return (gpointer)renderer_interface.create_spug_output((struct weston_output*)key);
		case SPUG_WRAPPER_PLANE:
			return (gpointer)renderer_interface.create_spug_plane((struct weston_plane*)key);
		default:
			return 0;
	}
}

/* returns SPUG_TRUE if the table did not already contain a wrapper for the
 * provided key and one was created. Returns SPUG_FALSE if one already existed
 * */
static spug_bool
confirm_hash(enum spug_wrapper_type table, void *key)
{
	gpointer wrapper;
	/* if value has no corresponding spug wrapper in the table
	 * then we will create one. */
	wrapper = g_hash_table_lookup(framework->spug_hashtables[table],
			table != SPUG_WRAPPER_SURFACE ?
			key : ((struct weston_view*)key)->surface);
	if(!wrapper) {
		wrapper = wrap(table, key);
		g_hash_table_insert(framework->spug_hashtables[table],
			table != SPUG_WRAPPER_SURFACE ?
			key : ((struct weston_view*)key)->surface,
			wrapper);
		return SPUG_TRUE;
	}

	return SPUG_FALSE;
}

static struct spug_view *
get_view_from_id(const spug_view_id id)
{
	struct spug_view *sview;

	sview =
		g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_VIEW], id);

	if(!sview) {
		IAS_ERROR("Invalid id 0x%016"PRIXPTR", unable to find matching view\n",
				(uintptr_t)id);
	}

	return sview;
}

static struct spug_surface *
get_surface_from_id(const spug_surface_id id)
{
	struct spug_surface *ssurface;

	ssurface =
		g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_SURFACE], id);

	if(!ssurface) {
		IAS_ERROR("Invalid id 0x%016"PRIXPTR", unable to find matching surface\n",
				(uintptr_t)id);
	}

	return NULL;
}

static struct spug_output *
get_output_from_id(const spug_output_id id)
{
	struct spug_output *soutput;

	soutput =
		g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_OUTPUT], id);

	if(!soutput) {
		IAS_ERROR("Invalid id 0x%016"PRIXPTR", unable to find matching output\n",
				(uintptr_t)id);
	}

	return soutput;
}

static struct spug_seat *
get_seat_from_id(const spug_seat_id id)
{
	struct spug_seat *sseat;

	sseat =
		g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_SEAT], id);

	if(!sseat) {
		IAS_ERROR("Invalid id 0x%016"PRIXPTR", unable to find matching seat\n",
				(uintptr_t)id);
	}

	return sseat;
}

static struct spug_client *
get_client_from_id(const spug_client_id id)
{
	struct spug_client *sclient;

	sclient =
		g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_CLIENT], id);

	if(!sclient) {
		IAS_ERROR("Invalid id 0x%016"PRIXPTR", unable to find matching client\n",
				(uintptr_t)id);
	}

	return sclient;
}

WL_EXPORT uint32_t
spug_assign_surface_to_scanout(const spug_view_id view, int x, int y)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view);

	if(sview) {
		return ias_assign_surface_to_scanout(sview->view,
												sview->view->output, x, y);
	}

	/* ias_assign_surface_to_scanout returns -1 on error, so that's what our
	 * caller will be expecting */
	IAS_ERROR("Unable to assign view with id 0x%016"PRIXPTR" for scanout, (view is NULL)\n",
			(uintptr_t)view);
	return -1;
}

WL_EXPORT void
spug_toggle_frame_events(const spug_view_id view)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view);

	if(!sview || !sview->view) {
		IAS_ERROR("Invalid view\n");
		return;
	}

	ias_toggle_frame_events(sview->view->surface);
}

WL_EXPORT int
spug_frame_events_enabled(const spug_view_id id)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(id);

	if(sview && sview->view && sview->view->surface) {
		return ias_frame_events_enabled(sview->view->surface);
	}

	return 0;
}

WL_EXPORT uint32_t
spug_is_surface_flippable(const spug_view_id view)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view);
	struct weston_output *output;

	if(sview && sview->view) {
		output = sview->view->output;
		return ias_is_surface_flippable(sview->view, output);
	}

	/* ias_is_surface_flippable returns 0 if the surface can't be flipped,
	 * so we could return that, but we should also acknowledge that there's
	 * something wrong with the spug_view_id we were passed */

	IAS_ERROR("Invalid view (id 0x%016"PRIXPTR"), unable to test flippability\n",
			(uintptr_t)view);
	return 0;
}

WL_EXPORT int
spug_has_surface_timedout(const spug_view_id view)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view);

	if(sview && sview->view) {
		return ias_has_surface_timedout(sview->view->surface);
	}

	IAS_ERROR("Invalid view (id 0x%016"PRIXPTR", sview %p, view %p), unable to test "
		"surface timeout\n", (uintptr_t)view, sview, sview ? sview->view : 0);

	return 0;
}

WL_EXPORT int
spug_view_num_textures(const spug_view_id view)
{
	int num = 0;
	struct spug_view *sview = renderer_interface.get_view_from_id(view);
	struct ias_backend *ias_compositor =
					(struct ias_backend *)(framework->compositor->backend);

	if(sview) {
		ias_compositor->get_tex_info(sview->view, &num, NULL);
	} else {
		IAS_ERROR("invalid view (id 0x%016"PRIXPTR"), unable to get number of textures."
			" Returning 0\n", (uintptr_t)view);
	}

	return num;
}

static GLuint
get_view_texture(struct weston_view * view)
{
	int num = 1;
	GLuint tex_name;
	struct ias_backend *ias_compositor =
					(struct ias_backend *)(framework->compositor->backend);

	ias_compositor->get_tex_info(view, &num, &tex_name);

	return tex_name;
}

static EGLImageKHR
get_view_egl_image(struct weston_view * view)
{
	int num = 1;
	EGLImageKHR tex_name;
	struct ias_backend *ias_compositor =
					(struct ias_backend *)(framework->compositor->backend);

	ias_compositor->get_egl_image_info(view, &num, &tex_name);

	return tex_name;
}

/* destroy a spug_view */
static void
destroy_spug_view_common(struct spug_view *sview)
{
	ipug_event_mask event_mask;

	if(sview->view_draw_info) {
		free(sview->view_draw_info);
	}

	if(sview->draw_info) {
		free(sview->draw_info);
	}

	/* if this view is the focus for any input events, they need to be
	 * unfocused */
	event_mask = ipug_get_input_focus(sview->id);
	if(event_mask) {
		ipug_set_input_focus(event_mask, 0);
	}

	wl_list_remove(&sview->view_destroy_listener.link);

	/* use g_hash_table_steal and not g_hash_table_remove here. the remove
	 * function calls the destructor, which is what we're doing right now.
	 * The steal function removes the item from the table without calling the
	 * destructor */
	g_hash_table_steal(framework->spug_hashtables[SPUG_WRAPPER_VIEW], sview->id);

	renderer_interface.update_spug_ids(SPUG_WRAPPER_VIEW,
			(spug_id**)&framework->spug_view_ids);

	free(sview);
}

/* destroy notifier for weston. This is called when the underlying
 * weston_view is destroyed */
static void
destroy_spug_view_wl(struct wl_listener *listener, void *data)
{
	struct spug_view *sview = container_of(listener,
									struct spug_view,
									view_destroy_listener);

	renderer_interface.destroy_spug_view_common(sview);
}

/* destroy notifier for glib. This is called when the hash table containing all
 * of our spug_views is destroyed */
static void
destroy_spug_view_g(gpointer data)
{
	renderer_interface.destroy_spug_view_common((struct spug_view*) data);
}

static void
destroy_spug_surface_common(struct spug_surface *ssurface)
{
	if(ssurface->surface_draw_info) {
		free(ssurface->surface_draw_info);
	}

	if(ssurface->draw_info) {
		free(ssurface->draw_info);
	}

	wl_list_remove(&ssurface->surface_destroy_listener.link);

	/* use g_hash_table_steal and not g_hash_table_remove here. the remove
	 * function calls the destructor, which is what we're doing right now.
	 * The steal function removes the item from the table without calling the
	 * destructor */
	g_hash_table_steal(framework->spug_hashtables[SPUG_WRAPPER_SURFACE],
		ssurface->id);

	free(ssurface);

	renderer_interface.update_spug_ids(SPUG_WRAPPER_SURFACE,
				(spug_id**)&framework->spug_surface_ids);
}

static void
destroy_spug_surface_wl(struct wl_listener *listener, void *data)
{
	struct spug_surface *ssurface = container_of(listener,
												struct spug_surface,
												surface_destroy_listener);

	renderer_interface.destroy_spug_surface_common(ssurface);
}

static void
destroy_spug_surface_g(gpointer data)
{
	renderer_interface.destroy_spug_surface_common((struct spug_surface*) data);
}

static void
destroy_spug_client_common(struct spug_client *sclient)
{
	wl_list_remove(&sclient->link);
	free(sclient);
}

static void
destroy_spug_client_g(gpointer data)
{
	renderer_interface.destroy_spug_client_common((struct spug_client*) data);
}


static void
destroy_spug_client_wl(struct wl_listener *listener, void *data)
{
	struct spug_client *sclient = container_of(listener,
									struct spug_client,
									client_destroy_listener);

	renderer_interface.destroy_spug_client_common(sclient);
}

static struct spug_view_draw_info *
create_spug_view_draw_info(struct spug_view *sview)
{
	struct spug_view_draw_info *info;

	info = calloc(1, sizeof(struct spug_view_draw_info));

	if(info) {

		info->texture = renderer_interface.get_view_texture(sview->view);
		info->egl_image = renderer_interface.get_view_egl_image(sview->view);
		info->id = sview->id;

		return info;
	}

	/* calloc failed */
	IAS_ERROR("calloc failed, exiting\n");
	exit(1);
}

static struct spug_surface_draw_info *
create_spug_surface_draw_info(struct spug_surface *ssurface)
{
	struct spug_surface_draw_info *info;

	info = calloc(1, sizeof(struct spug_surface_draw_info));

	if(info) {

		info->texture =
				renderer_interface.get_view_texture(ssurface->parent_view->view);
		info->egl_image =
				renderer_interface.get_view_egl_image(ssurface->parent_view->view);
		info->id = ssurface->id;

		return info;
	}

	/* calloc failed */
	IAS_ERROR("calloc failed, exiting\n");
	exit(1);
}

static struct spug_draw_info *
create_spug_draw_info(struct weston_view *view)
{
	struct spug_draw_info *info;

	info = calloc(1, sizeof(struct spug_draw_info));

	return info;
}

static void
update_spug_draw_infos(struct spug_view *sview)
{
	sview->view_draw_info->texture = renderer_interface.get_view_texture(sview->view);
}

static struct spug_view *
create_spug_view(struct weston_view *view)
{
	struct spug_view *sview;

	sview = calloc(1, sizeof(struct spug_view));
	if(!sview) {
		IAS_ERROR("calloc failed, exiting\n");
		exit(1);
	}

	sview->id = (spug_view_id)view;
	sview->view = view;
	sview->view_draw_info = renderer_interface.create_spug_view_draw_info(sview);
	sview->draw_info = renderer_interface.create_spug_draw_info(view);

	/* destroy the spug_view when the weston_view is destroyed */
	wl_signal_init(&sview->destroy_signal);
	sview->view_destroy_listener.notify = destroy_spug_view_wl;
	wl_signal_add(&view->destroy_signal, &sview->view_destroy_listener);

	return sview;
}

static struct spug_surface *
create_spug_surface(struct weston_view *view)
{
	struct spug_surface *ssurface;

	ssurface = calloc(1, sizeof(struct spug_surface));
	if(!ssurface) {
		IAS_ERROR("calloc failed, exiting\n");
		exit(1);
	}

	ssurface->id = (spug_surface_id)view->surface;
	ssurface->surface = view->surface;
	ssurface->parent_view = renderer_interface.get_view_from_id((spug_view_id)view);
	ssurface->surface_draw_info = renderer_interface.create_spug_surface_draw_info(ssurface);
	ssurface->draw_info = renderer_interface.create_spug_draw_info(view);

	/* destroy the spug_surface when the weston_surface is destroyed */
	wl_signal_init(&ssurface->destroy_signal);
	ssurface->surface_destroy_listener.notify = destroy_spug_surface_wl;
	wl_signal_add(&ssurface->surface->destroy_signal,
			&ssurface->surface_destroy_listener);

	return ssurface;
}

static struct spug_seat *
create_spug_seat(struct weston_seat *seat)
{
	struct spug_seat *sseat;

	sseat = calloc(1, sizeof(struct spug_seat));
	if(!sseat) {
		IAS_ERROR("calloc failed, exiting\n");
		exit(1);
	}

	sseat->seat = seat;
	sseat->id = (spug_seat_id)seat;

	return sseat;
}

static struct spug_plane *
create_spug_plane(struct weston_plane *plane)
{
	struct spug_plane *splane;

	splane = calloc(1, sizeof(struct spug_plane));
	if(!splane) {
		IAS_ERROR("calloc failed, exiting\n");
		exit(1);
	}

	splane->plane = plane;
	splane->id = (spug_plane_id)plane;

	return splane;
}

static struct spug_client *
create_spug_client(struct wl_client *client)
{
	struct spug_client *sclient;

	sclient = calloc(1, sizeof(struct spug_client));
	if(!sclient) {
		IAS_ERROR("calloc failed, exiting\n");
		exit(1);
	}

	sclient->client = client;
	sclient->id = (spug_client_id)client;

	/* destroy the spug_client when the weston_client is destroyed */
	wl_signal_init(&sclient->destroy_signal);
	sclient->client_destroy_listener.notify = destroy_spug_client_wl;
	wl_client_add_destroy_listener(client, &sclient->client_destroy_listener);

	return sclient;
}

static struct spug_output *
create_spug_output(struct weston_output *output)
{
	struct spug_output *soutput;

	soutput = calloc(1, sizeof(struct spug_output));
	if(!soutput) {
		IAS_ERROR("calloc failed, exiting\n");
		exit(1);
	}

	soutput->output = output;
	soutput->id = (spug_output_id)output;

	return soutput;
}

struct spug_output*
get_output_wrapper(struct weston_output *output)
{
	struct spug_output *soutput;

	soutput =
		g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_PLANE], output);

	if(soutput) {
		return soutput;
	}

	/* we don't already have our own wrapper for this output, so make one and
	 * insert it into our table */
	soutput = renderer_interface.create_spug_output(output);
	g_hash_table_insert(framework->spug_hashtables[SPUG_WRAPPER_PLANE],
						output, soutput);

	return soutput;
}

struct spug_plane*
get_plane_wrapper(struct weston_plane *plane)
{
	struct spug_plane *splane;

	splane =
		g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_PLANE], plane);

	if(splane) {
		return splane;
	}

	/* we don't already have our own wrapper for this plane, so make one and
	 * insert it into our table */
	splane = renderer_interface.create_spug_plane(plane);
	g_hash_table_insert(framework->spug_hashtables[SPUG_WRAPPER_PLANE],
						plane, splane);

	return splane;
}

struct spug_client*
get_client_wrapper(struct wl_client *client)
{
	struct spug_client *sclient;

	sclient =
		g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_CLIENT], client);

	/* we don't already have our own wrapper for this client, so make one and
	 * insert it into our list */
	sclient = renderer_interface.create_spug_client(client);
	g_hash_table_insert(framework->spug_hashtables[SPUG_WRAPPER_CLIENT],
						client, sclient);

	return sclient;
}

static void
destroy_spug_seat(gpointer sseat)
{
	free((struct spug_seat *)sseat);
}

static void
destroy_spug_output(gpointer soutput)
{
	free((struct spug_output *)soutput);
}

static void
destroy_spug_plane(gpointer splane)
{
	free((struct spug_plane*)splane);
}

WL_EXPORT void
spug_init_view_list(void)
{
	spug_init_hashtable(SPUG_WRAPPER_VIEW, destroy_spug_view_g);
	spug_update_view_list();
}

WL_EXPORT void
spug_init_surface_list(void)
{
	spug_init_hashtable(SPUG_WRAPPER_SURFACE, destroy_spug_surface_g);
	spug_update_surface_list();
}

WL_EXPORT void
spug_init_seat_list(void)
{
	spug_init_hashtable(SPUG_WRAPPER_SEAT, renderer_interface.destroy_spug_seat);
	spug_update_seat_list();
}

WL_EXPORT void
spug_init_output_list(void)
{
	spug_init_hashtable(SPUG_WRAPPER_OUTPUT, renderer_interface.destroy_spug_output);
	spug_update_output_list();
}

/* the below init_*_list functions don't update their lists, because they are
 * populated differently to the above lists. */
WL_EXPORT void
spug_init_plane_list(void)
{
	spug_init_hashtable(SPUG_WRAPPER_PLANE, renderer_interface.destroy_spug_plane);
}

static void
spug_init_client_list(void)
{
	spug_init_hashtable(SPUG_WRAPPER_CLIENT, destroy_spug_client_g);
}

static void
spug_init_global_list(void)
{
	spug_init_hashtable(SPUG_WRAPPER_GLOBAL, _spug_global_destroy);
}

typedef void (*spug_init_list_fn)(void);
spug_init_list_fn spug_init_list_fns[SPUG_WRAPPER_SIZE] =
{
	spug_init_view_list,
	spug_init_surface_list,
	spug_init_seat_list,
	spug_init_output_list,
	spug_init_plane_list,
	spug_init_client_list,
	spug_init_global_list,
};

WL_EXPORT void
spug_init_all_lists(void)
{
	if(!framework->lists_initialised) {
		int i;

		for(i = 0; i < SPUG_WRAPPER_SIZE; i++) {
			spug_init_list_fns[i]();
		}

		if(framework->output_node_list.next == NULL) {
			wl_list_init(&framework->output_node_list);
		}

		framework->lists_initialised = SPUG_TRUE;
	}
}

static void
spug_destroy_all_lists(void)
{
	if(framework->lists_initialised) {
		int i;

		for(i = 0; i < SPUG_WRAPPER_SIZE; i++) {
			spug_destroy_hashtable(i);
		}

		framework->lists_initialised = SPUG_FALSE;
	}
}

WL_EXPORT void
spug_update_all_lists(void)
{
	spug_update_view_list();
	spug_update_surface_list();
	spug_update_seat_list();
	spug_update_output_list();
}

WL_EXPORT void
spug_update_view_list()
{
	struct weston_view *view;
	spug_bool need_update_ids = SPUG_FALSE;

	/* we store spug_views in a hashtable, and use the address of the
	 * corresponding weston_view as the key. */
	wl_list_for_each(view, &framework->compositor->view_list, link) {
		if(renderer_interface.confirm_hash(SPUG_WRAPPER_VIEW, view)) {
			need_update_ids = SPUG_TRUE;
		}
	}

	if(need_update_ids) {
		renderer_interface.update_spug_ids(SPUG_WRAPPER_VIEW,
										(spug_id**)&framework->spug_view_ids);
	}
}

WL_EXPORT void
spug_update_surface_list(void)
{
	struct weston_view *view;
	spug_bool need_update_ids = SPUG_FALSE;

	/* we use the view list here because the compositor doesn't maintain a
	 * surface list. */
	wl_list_for_each(view, &framework->compositor->view_list, link) {
		if(renderer_interface.confirm_hash(SPUG_WRAPPER_SURFACE, view)) {
			need_update_ids = SPUG_TRUE;
		}
	}

	if(need_update_ids) {
		renderer_interface.update_spug_ids(SPUG_WRAPPER_SURFACE,
										(spug_id**)&framework->spug_surface_ids);
	}
}

WL_EXPORT void
spug_update_seat_list(void)
{
	struct weston_seat *seat;
	spug_bool need_update_ids = SPUG_FALSE;

	wl_list_for_each(seat, &framework->compositor->seat_list, link) {
		if(renderer_interface.confirm_hash(SPUG_WRAPPER_SEAT, seat)) {
			need_update_ids = SPUG_TRUE;
		}
	}

	if(need_update_ids) {
		renderer_interface.update_spug_ids(SPUG_WRAPPER_SEAT,
										(spug_id**)&framework->spug_seat_ids);
	}
}

WL_EXPORT void
spug_update_output_list(void)
{
	struct weston_output *output;
	spug_bool need_update_ids = SPUG_FALSE;

	wl_list_for_each(output, &framework->compositor->output_list, link) {
		if(renderer_interface.confirm_hash(SPUG_WRAPPER_OUTPUT, output)) {
			need_update_ids = SPUG_TRUE;
		}
	}

	if(need_update_ids) {
		renderer_interface.update_spug_ids(SPUG_WRAPPER_OUTPUT,
										(spug_id**)&framework->spug_output_ids);
	}
}

WL_EXPORT void
spug_update_plane_list(void)
{
	struct weston_plane *plane;
	spug_bool need_update_ids = SPUG_FALSE;

	wl_list_for_each(plane, &framework->compositor->plane_list, link) {
		if(renderer_interface.confirm_hash(SPUG_WRAPPER_PLANE, plane)) {
			need_update_ids = SPUG_TRUE;
		}
	}

	if(need_update_ids) {
		renderer_interface.update_spug_ids(SPUG_WRAPPER_PLANE,
										(spug_id**)&framework->spug_plane_ids);
	}
}

static void
spug_init_hashtable(enum spug_wrapper_type table, spug_glib_destructor destructor)
{
	framework->spug_hashtables[table] = g_hash_table_new_full(
														g_direct_hash,
														g_direct_equal,
														NULL,
														destructor);
	spug_update_view_list();
}

static void
spug_destroy_hashtable(enum spug_wrapper_type table)
{
	g_hash_table_destroy(framework->spug_hashtables[table]);
}

WL_EXPORT spug_view_list
spug_get_view_list(void)
{
	return framework->spug_view_ids;
}

WL_EXPORT spug_surface_list
spug_get_surface_list(void)
{
	return framework->spug_surface_ids;
}

WL_EXPORT spug_seat_list
spug_get_seat_list(void)
{
	return framework->spug_seat_ids;
}

WL_EXPORT spug_output_list
spug_get_output_list(void)
{
	return framework->spug_output_ids;
}

WL_EXPORT int
spug_view_list_length(spug_view_list view_list)
{
	int length = 0;

	if(view_list) {
		while(view_list[length]) {
			length++;
		}
	}

	return length;
}

WL_EXPORT int
spug_surface_list_length(spug_surface_list surface_list)
{
	int length = 0;

	if(surface_list) {
		while(surface_list[length]) {
			length++;
		}
	}

	return length;
}

WL_EXPORT int
spug_seat_list_length(spug_seat_list seat_list)
{
	int length = 0;

	if(seat_list) {
		while(seat_list[length]) {
			length++;
		}
	}

	return length;
}

WL_EXPORT int
spug_output_list_length(spug_output_list output_list)
{
	int length = 0;

	if(output_list) {
		while(output_list[length]) {
			length++;
		}
	}

	return length;
}

WL_EXPORT spug_bool
spug_seat_has_sprite(const spug_seat_id seat_id)
{
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat_id);
	struct weston_pointer *pointer = NULL;

	if(!sseat || !sseat->seat) {
		IAS_ERROR("invalid seat\n");
		return SPUG_FALSE;
	}

	pointer = weston_seat_get_pointer(sseat->seat);
	if(pointer && pointer->sprite) {
		return SPUG_TRUE;
	}
	return SPUG_FALSE;
}

WL_EXPORT spug_view_id
spug_get_seat_sprite(const spug_seat_id id)
{
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(id);
	struct spug_view *sview;
	struct weston_pointer *pointer = NULL;

	if(!spug_seat_has_sprite(id)) {
		IAS_ERROR("no sprite to get\n");
		return NULL;
	}

	pointer = weston_seat_get_pointer(sseat->seat);

	/* find the spug_view for this seat's sprite */
	sview = g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_VIEW],
					pointer->sprite);

	if(sview) {
		return sview->id;
	}

	IAS_ERROR("seat with id %d has a sprite, but its view doesn't appear "
		"in the compositor's view list\n", SURFPTR2ID(id));

	return NULL;
}

WL_EXPORT int
spug_get_seat_pointer_count(spug_seat_id seat_id)
{
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat_id);

	if(sseat && sseat->seat) {
		return sseat->seat->pointer_device_count;
	}

	IAS_ERROR("%s: Invalid seat id %d\n", __func__, SURFPTR2ID(seat_id));

	/* returning 0 for invalid id */
	return 0;
}

WL_EXPORT int
spug_get_seat_keyboard_count(spug_seat_id seat_id)
{
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat_id);

	if(sseat && sseat->seat) {
		return sseat->seat->keyboard_device_count;
	}

	IAS_ERROR("%s: Invalid seat id %d\n", __func__, SURFPTR2ID(seat_id));

	/* returning 0 for invalid id */
	return 0;
}

WL_EXPORT int
spug_get_seat_touch_count(spug_seat_id seat_id)
{
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat_id);

	if(sseat && sseat->seat) {
		return sseat->seat->touch_device_count;
	}

	IAS_ERROR("%s: Invalid seat id %d\n", __func__, SURFPTR2ID(seat_id));

	/* returning 0 for invalid id */
	return 0;
}

WL_EXPORT const char*
spug_get_seat_name(spug_seat_id seat_id)
{
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat_id);

	if(sseat && sseat->seat) {
		return (const char*)sseat->seat->seat_name;
	}

	IAS_ERROR("%s: Invalid seat id %d\n", __func__, SURFPTR2ID(seat_id));

	/* returning 0 for invalid id */
	return 0;
}

WL_EXPORT spug_output_id
spug_get_seat_output(spug_seat_id seat_id)
{
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat_id);
	struct spug_output *soutput;
	spug_output_id output_id = 0;

	if(sseat && sseat->seat) {
		/* get the the spug_output that points to the weston_output that
		 * this seat points to */
		soutput = get_output_wrapper(sseat->seat->output);
		if(soutput) {
			output_id = soutput->id;
		}
	}

	return output_id;
}

WL_EXPORT uint32_t
spug_get_behavior_bits(const spug_view_id id)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(id);

	if(sview && sview->view && sview->view->surface) {
		return ias_get_behavior_bits(sview->view->surface);
	}

	/* just return 0? */
	return 0;
}

WL_EXPORT enum
shell_surface_zorder spug_get_zorder(const spug_view_id view_id)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view_id);

	/* is this too paranoid? */
	if(sview && sview->view && sview->view->surface) {
		return ias_get_zorder(sview->view->surface);
	}

	/* ERROR, invalid view_id */
	return SHELL_SURFACE_ZORDER_DEFAULT;
}

WL_EXPORT void
spug_activate_plugin(const spug_output_id output_id, uint32_t ias_id)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(output_id);

	if(soutput) {
		ias_activate_plugin(soutput->output, ias_id);
	}
}

WL_EXPORT void
spug_set_plugin_redraw_behavior(spug_output_id id,
								enum plugin_redraw_behavior behavior)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(id);

	if(soutput) {
		ias_set_plugin_redraw_behavior(soutput->output, behavior);
	}
}

WL_EXPORT void
spug_deactivate_plugin(const spug_output_id id)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(id);

	if(!soutput) {
		IAS_ERROR("invalid output\n");
		return;
	}

	ias_deactivate_plugin(soutput->output);
}

WL_EXPORT void
spug_get_owning_process_info(const spug_view_id id,	uint32_t *pid, char **pname)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(id);

	if(sview && sview->view && sview->view->surface) {
		ias_get_owning_process_info(sview->view->surface, pid, pname);
	}
}

WL_EXPORT int
spug_get_sprite_list(const spug_output_id id, struct ias_sprite ***sprites)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(id);

	if(soutput) {
		return ias_get_sprite_list(soutput->output, sprites);
	}

	return 0;
}

WL_EXPORT struct spug_plane*
spug_assign_surface_to_sprite(const spug_view_id view_id,
								const spug_output_id output_id,
								int* sprite_id,
								int x, int y,
								pixman_region32_t *surface_region)
{
	/* Assign surface to sprite without any scaling by providng 0 as sprite width and height */
	return spug_assign_surface_to_sprite_scaled(view_id, output_id,
						 sprite_id, x, y,
						 0, 0, surface_region);
}

WL_EXPORT struct spug_plane*
spug_assign_surface_to_sprite_scaled(const spug_view_id view_id,
				const spug_output_id output_id,
				int* sprite_id,
				int x, int y, int sprite_w, int sprite_h,
				pixman_region32_t *surface_region)
{
	struct weston_plane *plane;
	struct spug_plane *splane;
	struct spug_view *view = renderer_interface.get_view_from_id(view_id);
	struct spug_output *output = renderer_interface.get_output_from_id(output_id);

	plane = ias_assign_surface_to_sprite(view->view,
					output->output,
					sprite_id,
					x, y, sprite_w, sprite_h,
					surface_region);

	splane = get_plane_wrapper(plane);
	return splane;
}

WL_EXPORT int
spug_assign_zorder_to_sprite(const spug_output_id output_id,
							int sprite_id,
							int position)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(output_id);

	if(soutput) {
		return ias_assign_zorder_to_sprite(soutput->output, sprite_id, position);
	}

	return -1;
}

WL_EXPORT int
spug_assign_constant_alpha_to_sprite(	const spug_output_id output_id,
										int sprite_id,
										float constant_value,
										int enable)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(output_id);

	if(soutput) {
		return ias_assign_constant_alpha_to_sprite(soutput->output, sprite_id,
											constant_value, enable);
	}

	return -1;
}

WL_EXPORT int
spug_assign_blending_to_sprite(const spug_output_id output_id,
		int sprite_id,
		int src_factor,
		int dst_factor,
		float blend_color,
		int enable)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(output_id);

	if(soutput) {
		return ias_assign_blending_to_sprite(soutput->output, sprite_id, src_factor,
				dst_factor, blend_color, enable);
	}

	return -1;
}

WL_EXPORT spug_bool
spug_view_is_cursor(const spug_view_id view_id)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view_id);
	struct spug_seat *sseat;
	GList *cur, *head;
	spug_bool ret = SPUG_FALSE;

	if(!sview || !sview->view) {
		IAS_ERROR("%s: invalid view id\n", __func__);
		return SPUG_FALSE;
	}


	head =
		g_hash_table_get_values(framework->spug_hashtables[SPUG_WRAPPER_SEAT]);

	cur = head;
	while(cur) {
		sseat = (struct spug_seat*)cur->data;
		if(sseat && spug_view_is_sprite(view_id, sseat->id)) {
			ret = SPUG_TRUE;
			break;
		}
		cur = cur->next;
	}
	g_list_free(head);

	return ret;
}

WL_EXPORT spug_bool
spug_view_is_sprite(const spug_view_id view_id, const spug_seat_id seat_id)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view_id);
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat_id);
	struct weston_pointer *pointer = NULL;

	if(!sview || !sview->view) {
		IAS_ERROR("%s: invalid view\n", __func__);
		return SPUG_FALSE;
	}

	if(!sseat || !sseat->seat) {
		IAS_ERROR("%s: invalid seat\n", __func__);
		return SPUG_FALSE;
	}

	pointer = weston_seat_get_pointer(sseat->seat);
	if(!pointer) {
		return SPUG_FALSE;
	}

	if(sview->view == pointer->sprite) {
		return SPUG_TRUE;
	}

	return SPUG_FALSE;
}

WL_EXPORT spug_bool
spug_view_is(const spug_view_id view_id, const spug_is_mask mask)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view_id);

	if(!sview || !sview->view || !sview->view->surface) {
		/* ERROR invalid view */
		return SPUG_FALSE;
	}

	if(mask & SPUG_IS_CURSOR && spug_view_is_cursor(view_id)) {
		return SPUG_TRUE;
	}

	return SPUG_FALSE;
}

WL_EXPORT pixman_region32_t*
spug_view_get_trans_boundingbox(const spug_view_id view_id)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view_id);

	if(!sview) {
		/* ERROR invalid view */
		return NULL;
	}

	return &sview->view->transform.boundingbox;
}

WL_EXPORT void
spug_copy_view_list(spug_view_list *dst, spug_view_list src)
{
	int i, length;
	if(src && dst) {
		if(*dst) {
			free(*dst);
		}

		length = spug_view_list_length(src);

		*dst = calloc(1, sizeof(spug_view_id) * length + 1);

		for(i = 0; i < length; i++) {
			(*dst)[i] = src[i];
		}
	}
}

WL_EXPORT void
spug_release_view_list(spug_view_list *view_list)
{
	if(*view_list) {
		free(*view_list);
	}
}

WL_EXPORT spug_bool
spug_view_list_is_empty(spug_view_list *view_list)
{
	if((spug_view_id*)(view_list)[0] == 0) {
		return SPUG_TRUE;
	}
	return SPUG_FALSE;
}

WL_EXPORT spug_view_list
spug_filter_view_list(	spug_view_list view_list,
						spug_view_filter_fn view_filter,
						int max_views)
{
	spug_view_list filtered_list;
	spug_view_list old_list = (spug_view_id *)view_list;
	int i=0, filtered_i=0;
	struct spug_allocated_list *alloc_list;

	if(!view_list || !view_filter || max_views < 1) {
		return NULL;
	}

	filtered_list = calloc(1, sizeof(spug_view_id) *
					spug_view_list_length(view_list) + 1);

	if (filtered_list == NULL) {
		IAS_ERROR("Failed to create list: out of memory \n");
		return NULL;
	}

	/* keep track of lists being allocated above to ensure we do not leak this memory. */
	alloc_list = calloc(1, sizeof(struct spug_allocated_list));
	if (alloc_list == NULL) {
		IAS_ERROR("Failed to create list: out of memory \n");
		free(filtered_list);
		return NULL;
	}

	alloc_list->allocated_list = filtered_list;
	wl_list_init(&alloc_list->link);
	wl_list_insert(&framework->allocated_lists, &alloc_list->link);

	while(old_list[i]) {
		/* the plugin's filter function should return true if it wants
		 * to keep the view, so add it to the filtered list */
		if(view_filter(old_list[i], view_list)) {
			filtered_list[filtered_i++] = old_list[i];
		}
		if(filtered_i == max_views)	{
			break;
		}
		i++;
	}

	return filtered_list;
}

WL_EXPORT void
spug_list_init(spug_list *list)
{
	wl_list_init((struct wl_list*)list);
}

WL_EXPORT void
spug_list_insert(spug_list *list, spug_list *elm)
{
	wl_list_insert((struct wl_list*)list, (struct wl_list*)elm);
}

WL_EXPORT void
spug_list_remove(spug_list *elm)
{
	wl_list_remove((struct wl_list*)elm);
}

WL_EXPORT int
spug_list_length(const spug_list *list)
{
	return wl_list_length((struct wl_list*)list);
}

WL_EXPORT int
spug_list_empty(const spug_list *list)
{
	return wl_list_empty((struct wl_list*)list);
}

WL_EXPORT void
spug_list_insert_list(spug_list *list, spug_list *other)
{
	wl_list_insert_list((struct wl_list*)list, (struct wl_list*)other);
}

static void
call_bind_ext(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct spug_extension_data *ext_data = data;
	struct spug_client *sclient = get_client_wrapper(client);

	ext_data->bind(sclient->client, ext_data->data, version, id);
}

WL_EXPORT spug_global_id
spug_global_create(const struct spug_interface *interface,
					void *data, spug_global_bind_func_t bind)
{
	struct spug_global *sglobal;
	struct spug_extension_data *ext_data;

	sglobal = calloc(1, sizeof(struct spug_global));
	ext_data = calloc(1, sizeof(struct spug_extension_data));
	if(!sglobal || !ext_data) {
		IAS_ERROR("calloc failed\n");
		return NULL;
	}

	ext_data->data = data;
	ext_data->bind = bind;

	sglobal->ext_data = ext_data;
	sglobal->global = wl_global_create(framework->compositor->wl_display,
					(struct wl_interface*)interface,
					interface->version,
					ext_data,
					(wl_global_bind_func_t)renderer_interface.call_bind_ext);
	sglobal->id = (spug_global_id)sglobal->global;
	g_hash_table_insert(framework->spug_hashtables[SPUG_WRAPPER_GLOBAL],
						sglobal->id, sglobal);

	return sglobal->id;
}

static void
_spug_global_destroy(gpointer data)
{
	struct spug_global *sglobal;
	spug_global_id global_id = (spug_global_id)data;

	sglobal = g_hash_table_lookup(framework->spug_hashtables[SPUG_WRAPPER_GLOBAL],
					(unsigned int*)global_id);

	if(sglobal) {
		wl_global_destroy(sglobal->global);
		free(sglobal->ext_data);
		free(sglobal);
	}
}

WL_EXPORT void
spug_global_destroy(spug_global_id global)
{
	_spug_global_destroy((gpointer)global);
}

WL_EXPORT void
spug_draw(	spug_view_list view_list,
			spug_view_draw_fn view_draw,
			spug_view_draw_setup_fn view_draw_setup,
			spug_view_draw_clean_state_fn view_draw_clean_state)
{
	if(view_draw_setup)	{
		view_draw_setup();
	}

	/* the plugin might want to call one of the other two functions without
	 * drawing anything. I can't think of a reason why they would want that
	 * but I also can't think of a reason to deny them the ability */
	if(view_list && view_draw) {
		int i;
		for(i = 0; view_list[i]; i++) {
			struct spug_view *sview =
						renderer_interface.get_view_from_id((spug_view_id)view_list[i]);
			if(sview) { /* shall we just ignore dodgy IDs? */

				/* the built in client apps don't work in grid_layout. The
				 * texture we get from them is just a solid black. This fixes
				 * that, but is it the right way to go about it? */
				if (sview->view->surface->touched) {
					framework->compositor->renderer->flush_damage(sview->view->surface);
				}
				weston_view_move_to_plane(sview->view,
							&framework->compositor->primary_plane);

				view_draw(sview->view_draw_info, sview->draw_info);
			}
		}
	}

	if(view_draw_clean_state) {
		view_draw_clean_state();
	}

	/* Cleanup the lists allocated by spug_filter_view_list() */
	struct spug_allocated_list *alloc_list, *tmp;
	wl_list_for_each_safe(alloc_list, tmp, &framework->allocated_lists, link) {
		spug_release_view_list(&alloc_list->allocated_list);
		wl_list_remove(&alloc_list->link);
		free(alloc_list);
	}
}

WL_EXPORT void
spug_draw_default_cursor(void)
{
	/* are we going to offer a function to draw a mouse cursor? if not we can
	 * scrap this altogether */
}

/* default input grabs. Moved here from the input plugin, they're just
 * boilerplate that would otherwise be implemented the same way by everyone
 */

static void
input_grab_focus(struct weston_pointer_grab *grab,
		struct wl_surface *surface,
		int32_t x,
		int32_t y)
{
	struct plugin_output_node *node, *tmp;
	int32_t local_x, local_y;
	int handled = 0;

	local_x = wl_fixed_to_int(x);
	local_y = wl_fixed_to_int(y);

	wl_list_for_each_safe(node, tmp, &framework->output_node_list, link) {
		/*
		 * Figure out which output contains the click coordinates and if we the
		 * plugin supports this handler
		 */
		if(node->plugin->mouse_grab.interface &&
				node->plugin->mouse_grab.interface->focus &&
				local_x >= node->output->region.extents.x1 &&
				local_x < node->output->region.extents.x2 &&
				local_y >= node->output->region.extents.y1 &&
				local_y < node->output->region.extents.y2) {
			handled = 1;
			node->plugin->mouse_grab.interface->focus(grab);
			break;
		}
	}

	/*
	 * If no plugin handled the event, we will send it to the default
	 * interface
	 */
	if(!handled) {
		grab->pointer->default_grab.interface->focus(grab);
	}
}

static void
input_grab_motion(struct weston_pointer_grab *grab,
			const struct timespec *time, wl_fixed_t x, wl_fixed_t y)
{
	struct plugin_output_node *node, *tmp;
	int handled = 0;
	struct weston_pointer_motion_event event = {
		.mask = WESTON_POINTER_MOTION_REL,
		.x = wl_fixed_to_double(x),
		.y = wl_fixed_to_double(y),
	};

	wl_list_for_each_safe(node, tmp, &framework->output_node_list, link) {
		/*
		 * Figure out which output contains the click coordinates and if we the
		 * plugin supports this handler
		 */
		if(node->plugin->mouse_grab.interface &&
				node->plugin->mouse_grab.interface->motion &&
				wl_fixed_to_int(x) >= node->output->region.extents.x1 &&
				wl_fixed_to_int(x) < node->output->region.extents.x2 &&
				wl_fixed_to_int(y) >= node->output->region.extents.y1 &&
				wl_fixed_to_int(y) < node->output->region.extents.y2) {
			handled = 1;
			node->plugin->mouse_grab.interface->motion(grab, time, &event);
			break;
		}
	}

	/*
	 * If no plugin handled the event, we will send it to the default
	 * interface
	 */
	if(!handled) {
		grab->pointer->default_grab.interface->motion(grab, time, &event);
	}
}

static void
input_grab_button(struct weston_pointer_grab *grab,
		const struct timespec *time,
		uint32_t button,
		uint32_t state)
{
	struct plugin_output_node *node, *tmp;
	int handled = 0;

	/*
	 * TODO: What if you move the mouse from one output to another while
	 * dragging
	 */
	wl_list_for_each_safe(node, tmp, &framework->output_node_list, link) {
		/*
		 * Figure out which output contains the click coordinates and if we the
		 * plugin supports this handler
		 */
		if(node->plugin->mouse_grab.interface &&
				node->plugin->mouse_grab.interface->button &&
				wl_fixed_to_int(grab->pointer->x) >= node->output->region.extents.x1 &&
				wl_fixed_to_int(grab->pointer->x) < node->output->region.extents.x2 &&
				wl_fixed_to_int(grab->pointer->y) >= node->output->region.extents.y1 &&
				wl_fixed_to_int(grab->pointer->y) < node->output->region.extents.y2) {
			handled = 1;
			node->plugin->mouse_grab.interface->button(grab, time, button, state);
			break;
		}
	}

	/*
	 * If no plugin handled the event, we will send it to the default
	 * interface
	 */
	if(!handled) {
		grab->pointer->default_grab.interface->button(grab, time, button,
				state);
	}
}

static void
input_grab_pointer_cancel(struct weston_pointer_grab *grab)
{
	/* TODO */
}

static void
input_grab_touch_down(struct weston_touch_grab *grab,
		const struct timespec *time,
		int touch_id,
		wl_fixed_t sx,
		wl_fixed_t sy)
{
	struct plugin_output_node *node, *tmp;
	int x = wl_fixed_to_int(grab->touch->grab_x);
	int y = wl_fixed_to_int(grab->touch->grab_y);
	int handled = 0;

	wl_list_for_each_safe(node, tmp, &framework->output_node_list, link) {
		/*
		 * Figure out which output contains the click coordinates and if we the
		 * plugin supports this handler
		 */
		if(node->plugin->touch_grab.interface &&
				node->plugin->touch_grab.interface->down &&
				x >= node->output->region.extents.x1 &&
				x < node->output->region.extents.x2 &&
				y >= node->output->region.extents.y1 &&
				y < node->output->region.extents.y2) {
			handled = 1;
			node->plugin->touch_grab.interface->down(grab, time, touch_id, sx, sy);
			break;
		}
	}

	/*
	 * If no plugin handled the event, we will send it to the default
	 * interface
	 */
	if(!handled) {
		grab->touch->default_grab.interface->down(grab, time, touch_id,
				sx, sy);
	}
}

static void
input_grab_touch_up(struct weston_touch_grab *grab,
		const struct timespec *time,
		int touch_id)
{
	struct plugin_output_node *node, *tmp;
	int x = wl_fixed_to_int(grab->touch->grab_x);
	int y = wl_fixed_to_int(grab->touch->grab_y);
	int handled = 0;

	wl_list_for_each_safe(node, tmp, &framework->output_node_list, link) {
		/*
		 * Figure out which output contains the click coordinates and if we the
		 * plugin supports this handler
		 */
		if(node->plugin->touch_grab.interface &&
				node->plugin->touch_grab.interface->up &&
				x >= node->output->region.extents.x1 &&
				x < node->output->region.extents.x2 &&
				y >= node->output->region.extents.y1 &&
				y < node->output->region.extents.y2) {
			handled = 1;
			node->plugin->touch_grab.interface->up(grab, time, touch_id);
			break;
		}
	}

	/*
	 * If no plugin handled the event, we will send it to the default
	 * interface
	 */
	if(!handled) {
		grab->touch->default_grab.interface->up(grab, time, touch_id);
	}
}

static void
input_grab_touch_motion(struct weston_touch_grab *grab,
		const struct timespec *time,
		int touch_id,
		int sx,
		int sy)
{
	struct plugin_output_node *node, *tmp;
	int x = wl_fixed_to_int(grab->touch->grab_x);
	int y = wl_fixed_to_int(grab->touch->grab_y);
	int handled = 0;

	wl_list_for_each_safe(node, tmp, &framework->output_node_list, link) {
		/*
		 * Figure out which output contains the click coordinates and if we the
		 * plugin supports this handler
		 */
		if(node->plugin->touch_grab.interface &&
				node->plugin->touch_grab.interface->motion &&
				x >= node->output->region.extents.x1 &&
				x < node->output->region.extents.x2 &&
				y >= node->output->region.extents.y1 &&
				y < node->output->region.extents.y2) {
			handled = 1;
			node->plugin->touch_grab.interface->motion(grab, time, touch_id, sx, sy);
			break;
		}
	}

	/*
	 * If no plugin handled the event, we will send it to the default
	 * interface
	 */
	if(!handled) {
		grab->touch->default_grab.interface->motion(grab, time, touch_id,
				sx, sy);
	}
}

static void
input_grab_touch_frame(struct weston_touch_grab *grab)
{
	/* TODO */
}

static void
input_grab_touch_cancel(struct weston_touch_grab *grab)
{
	/* TODO */
}

static void
input_grab_key(struct weston_keyboard_grab *grab,
		const struct timespec *time,
		uint32_t key,
		uint32_t state)
{
	struct plugin_output_node *node, *tmp;
	int handled = 0;

	wl_list_for_each_safe(node, tmp, &framework->output_node_list, link) {
		/*
		 * Figure out if we the plugin supports this handler
		 */
		if(node->plugin->key_grab.interface &&
			node->plugin->key_grab.interface->key) {
			handled = 1;
			node->plugin->key_grab.interface->key(grab, time, key, state);
		}
	}

	/*
	 * If no plugin handled the event, we will send it to the default
	 * interface
	 */
	if(!handled) {
		grab->keyboard->default_grab.interface->key(grab, time, key, state);
	}
}

static void
input_grab_modifiers(struct weston_keyboard_grab *grab,
		uint32_t serial,
		uint32_t mods_depressed,
		uint32_t mods_latched,
		uint32_t mods_locked,
		uint32_t group)
{
	struct plugin_output_node *node, *tmp;
	int handled = 0;

	wl_list_for_each_safe(node, tmp, &framework->output_node_list, link) {
		/*
		 * Figure out if we the plugin supports this handler
		 */
		if(node->plugin->key_grab.interface &&
			node->plugin->key_grab.interface->modifiers) {
			handled = 1;
			node->plugin->key_grab.interface->modifiers(grab, serial,
					mods_depressed, mods_latched, mods_locked, group);
		}
	}

	/*
	 * If no plugin handled the event, we will send it to the default
	 * interface
	 */
	if(!handled) {
		grab->keyboard->default_grab.interface->modifiers(grab, serial,
				mods_depressed, mods_latched, mods_locked, group);
	}
}

static void input_grab_key_cancel(struct weston_keyboard_grab *grab)
{
	/* TODO */
}

static void layout_switch_to(struct weston_output *output,
		struct ias_plugin_info *plugin)
{
	struct plugin_output_node *node =
			calloc(1, sizeof(struct plugin_output_node));
	if (!node) {
		fprintf(stderr, "Failed to handle layout switch: out of memory!\n");
		return;
	}

	spug_init_all_lists();

	node->plugin = plugin;
	node->output = output;
	/* Insert this node in the plugin list */
	wl_list_insert(&framework->output_node_list, &node->link);
}

static void layout_switch_from(struct weston_output *output,
		struct ias_plugin_info *plugin)
{
	struct plugin_output_node *node;
	wl_list_for_each(node, &framework->output_node_list, link) {
		if(node->output == output) {
			/* Remove this node from the plugin list and free its memory */
			wl_list_remove(&node->link);
			free(node);
			break;
		}
	}

	if (wl_list_empty(&framework->output_node_list)) {
		/* destroy all hashtables when there are no more active layouts */
		spug_destroy_all_lists();
	}
}

WL_EXPORT spug_bool
spug_mouse_xy(int *x, int *y, spug_seat_id seat_id)
{
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat_id);
	struct weston_pointer *pointer = NULL;

	if(sseat && sseat->seat) {
		pointer = weston_seat_get_pointer(sseat->seat);

		if (pointer) {
			if(x) {
				*x = wl_fixed_to_int(pointer->x);
			}
			if(y) {
				*y = wl_fixed_to_int(pointer->y);
			}
			return SPUG_TRUE;
		}
	}

	IAS_ERROR("Mouse coordinates requested but no mouse found\n");
	*x = -1;
	*y = -1;
	return SPUG_FALSE;
}

WL_EXPORT spug_output_id
spug_get_output_id(int x, int y)
{
	struct spug_output *soutput;
	spug_output_id id = 0;
	GList *head, *cur;

	head =
		g_hash_table_get_values(framework->spug_hashtables[SPUG_WRAPPER_OUTPUT]);

	cur = head;
	while(cur) {
		soutput = (struct spug_output*)cur->data;
		if (soutput && pixman_region32_contains_point(&soutput->output->region, x, y, NULL)) {
			id = soutput->id;
			break;
		}
		cur = cur->next;
	}
	g_list_free(head);

	return id;
}

WL_EXPORT void
spug_point_output_relative_xy(int x, int y, int *rel_x, int *rel_y)
{
	struct weston_output *output;
	/* work out which output the given point is on, and return its
	 * coordinates relative to the origin of that output. */
	wl_list_for_each(output, &framework->compositor->output_list, link) {
		if (pixman_region32_contains_point(&output->region, x, y, NULL)) {
			/* silently ignore NULL pointers, assume the plugin doesn't care
			 * about that axis */
			if(rel_x) {
				*rel_x = x - output->region.extents.x1;
			}
			if(rel_y) {
				*rel_y = y - output->region.extents.y1;
			}
			return;
		}
	}

	*rel_x = -1;
	*rel_y = -1;
}

WL_EXPORT void
spug_get_output_region(spug_output_id id, int *x, int *y, int *width, int *height)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(id);

	if(!soutput) {
		IAS_ERROR("unable to get output region, invalid output id 0x%016"PRIXPTR"\n",
				(uintptr_t)id);
		return;
	}

	/* if they passed NULL pointers in, just ignore that value */
	if(x){
		*x = soutput->output->region.extents.x1;
	}
	if(y){
		*y = soutput->output->region.extents.y1;
	}
	if(width){
		*width = soutput->output->region.extents.x2;
	}
	if(height){
		*height = soutput->output->region.extents.y2;
	}
}

WL_EXPORT double
spug_fixed_to_double(spug_fixed_t f)
{
	return wl_fixed_to_double(f);
}

WL_EXPORT int
spug_fixed_to_int(spug_fixed_t f)
{
	return wl_fixed_to_int(f);
}

WL_EXPORT spug_fixed_t
spug_fixed_from_double(double d)
{
	return wl_fixed_from_double(d);
}

WL_EXPORT spug_fixed_t
spug_fixed_from_int(int i)
{
	return wl_fixed_from_int(i);
}

WL_EXPORT spug_matrix *
spug_get_matrix(spug_output_id id)
{
	struct spug_output *output = renderer_interface.get_output_from_id(id);

	return (spug_matrix*)(&output->output->matrix);
}

WL_EXPORT enum spug_output_transform
spug_get_output_transform(spug_output_id id)
{
	struct spug_output *soutput = renderer_interface.get_output_from_id(id);

	if(!soutput) {
		return 0;
	}

	return (enum spug_output_transform)soutput->output->transform;
}

WL_EXPORT void
spug_matrix_init(spug_matrix *matrix)
{
	weston_matrix_init((struct weston_matrix*)matrix);
}

WL_EXPORT void
spug_matrix_multiply(spug_matrix *m, const spug_matrix *n)
{
	weston_matrix_multiply((struct weston_matrix*)m, (struct weston_matrix*)n);
}

WL_EXPORT void
spug_matrix_translate(spug_matrix *matrix, float x, float y, float z)
{
	weston_matrix_translate((struct weston_matrix*)matrix, x, y, z);
}

WL_EXPORT void
spug_matrix_scale(spug_matrix *matrix, float x, float y,float z)
{
	weston_matrix_scale((struct weston_matrix*)matrix, x, y, z);
}

WL_EXPORT void
spug_matrix_rotate_xy(spug_matrix *matrix, float cos, float sin)
{
	weston_matrix_rotate_xy((struct weston_matrix*)matrix, cos, sin);
}

WL_EXPORT void
spug_matrix_transform(spug_matrix *matrix, struct weston_vector *v)
{
	weston_matrix_transform((struct weston_matrix*)matrix, v);
}

WL_EXPORT void
spug_surface_activate(spug_view_id view, spug_seat_id seat)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view);
	struct spug_seat *sseat = renderer_interface.get_seat_from_id(seat);

	if(!sview || !sview->view) {
		IAS_ERROR("Invalid view id\n");
		return;
	}

	if(!sseat || !sseat->seat) {
		IAS_ERROR("Invalid seat id\n");
		return;
	}

	weston_seat_set_keyboard_focus(sseat->seat, sview->view->surface);
}

static struct spug_surface*
get_surface_from_pid(uint32_t pid)
{
	struct spug_surface *ssurface = NULL;
	uint32_t pid_tmp;
	GList *cur, *head;

	head =
		g_hash_table_get_values(framework->spug_hashtables[SPUG_WRAPPER_SURFACE]);

	cur = head;
	while(cur) {
		ssurface = cur->data;

		if(ssurface) {
			ias_get_owning_process_info(ssurface->surface, &pid_tmp, NULL);
			if(pid == pid_tmp) {
				break;
			}
		}

		cur = cur->next;
	}
	g_list_free(head);

	return ssurface;
}

static struct spug_surface*
get_surface_from_pname(const char *pname)
{
	struct spug_surface *ssurface = NULL;
	char *pname_tmp = NULL;
	GList *cur, *head;

	head =
		g_hash_table_get_values(framework->spug_hashtables[SPUG_WRAPPER_SURFACE]);

	cur = head;
	while(cur) {
		ssurface = cur->data;

		if(ssurface) {
			ias_get_owning_process_info(ssurface->surface, NULL, &pname_tmp);
			/* only 15 characters + null terminator are stored in ias_surface->pname
			 * where is this defined? I'd rather avoid magic numbers */
			if(pname_tmp && strncmp(pname_tmp, pname, 15) == 0) {
				free(pname_tmp);
				break;
			} else if(pname_tmp) {
				free(pname_tmp);
				pname_tmp = NULL;
			}
		}

		cur = cur->next;
	}
	g_list_free(head);

	return ssurface;
}

static spug_bool
send_event_to_view(struct ipug_event_info *event_info, struct spug_view *sview)
{
	if(!event_info) {
		/* invalid event */
		return SPUG_FALSE;
	}

	if(!sview) {
		/* invalid view */
		return SPUG_FALSE;
	}

	switch(event_info->event_type) {
		case IPUG_POINTER_FOCUS:
		{
			struct ipug_event_info_pointer_focus *pointer_event =
							(struct ipug_event_info_pointer_focus*)event_info;
			struct weston_view *pointer_old_focus =
											pointer_event->grab->pointer->focus;
			struct weston_pointer *pointer = pointer_event->grab->pointer;

			/* set the pointer's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_pointer_set_focus(pointer,
									sview->view,
									pointer_event->x,
									pointer_event->y);

			pointer->default_grab.interface->focus(pointer_event->grab);
			weston_pointer_set_focus(pointer,
									pointer_old_focus,
									pointer_event->x,
									pointer_event->y);
			break;
		}
		case IPUG_POINTER_MOTION:
		{
			struct ipug_event_info_pointer_motion *pointer_event =
							(struct ipug_event_info_pointer_motion*)event_info;
			struct weston_view *pointer_old_focus =
											pointer_event->grab->pointer->focus;
			struct weston_pointer *pointer = pointer_event->grab->pointer;
			struct weston_pointer_motion_event event = { 0 };

			if(pointer_event->mask & WESTON_POINTER_MOTION_ABS) {
				event = (struct weston_pointer_motion_event) {
					.mask = WESTON_POINTER_MOTION_ABS,
					.x = pointer_event->x,
					.y = pointer_event->y,
				};
			} else {
				event = (struct weston_pointer_motion_event) {
					.mask = WESTON_POINTER_MOTION_REL,
					.dx = pointer_event->x,
					.dy = pointer_event->y,
				};
			}

			/* set the pointer's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_pointer_set_focus(pointer,
									sview->view,
									pointer->x,
									pointer->y);
			pointer->default_grab.interface->motion(pointer_event->grab,
									pointer_event->time,
									&event);
			weston_pointer_set_focus(pointer,
									pointer_old_focus,
									pointer->x,
									pointer->y);
			break;
		}
		case IPUG_POINTER_BUTTON:
		{
			struct ipug_event_info_pointer_button *pointer_event =
							(struct ipug_event_info_pointer_button*)event_info;
			struct weston_view *pointer_old_focus =
											pointer_event->grab->pointer->focus;
			struct weston_pointer *pointer = pointer_event->grab->pointer;

			/* set the pointer's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_pointer_set_focus(pointer,
									sview->view,
									pointer->sx,
									pointer->sy);

			pointer->default_grab.interface->button(pointer_event->grab,
									pointer_event->time,
									pointer_event->button,
									pointer_event->state);
			weston_pointer_set_focus(pointer,
									pointer_old_focus,
									pointer->sx,
									pointer->sy);
			break;
		}
		case IPUG_POINTER_CANCEL:
		{
			struct ipug_event_info_pointer_button *pointer_event =
							(struct ipug_event_info_pointer_button*)event_info;
			struct weston_view *pointer_old_focus =
											pointer_event->grab->pointer->focus;
			struct weston_pointer *pointer = pointer_event->grab->pointer;

			/* set the pointer's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_pointer_set_focus(pointer,
									sview->view,
									pointer->sx,
									pointer->sy);

			pointer->default_grab.interface->cancel(pointer_event->grab);
			weston_pointer_set_focus(pointer,
									pointer_old_focus,
									pointer->sx,
									pointer->sy);
			break;
		}
		case IPUG_KEYBOARD_KEY:
		{
			struct ipug_event_info_key_key *key_event =
								(struct ipug_event_info_key_key*)event_info;
			/* keyboards focus on a surface, not a view like
			 * pointers and touches */
			struct weston_surface *keyboard_old_focus =
								key_event->grab->keyboard->focus;
			struct weston_keyboard *keyboard = key_event->grab->keyboard;

			/* set the keyboard's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_keyboard_set_focus(keyboard, sview->view->surface);
			keyboard->default_grab.interface->key(key_event->grab,
													key_event->time,
													key_event->key,
													key_event->state);
			weston_keyboard_set_focus(keyboard, keyboard_old_focus);

			break;
		}
		case IPUG_KEYBOARD_MOD:
		{
			struct ipug_event_info_key_mod *key_event =
								(struct ipug_event_info_key_mod*)event_info;
			struct weston_surface *keyboard_old_focus =
								key_event->grab->keyboard->focus;
			struct weston_keyboard *keyboard = key_event->grab->keyboard;

			/* set the keyboard's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_keyboard_set_focus(keyboard, sview->view->surface);
			keyboard->default_grab.interface->modifiers(key_event->grab,
													key_event->serial,
													key_event->mods_depressed,
													key_event->mods_latched,
													key_event->mods_locked,
													key_event->group);
			weston_keyboard_set_focus(keyboard, keyboard_old_focus);

			break;
		}
		case IPUG_KEYBOARD_CANCEL:
		{
			struct ipug_event_info_key_cancel *key_event =
								(struct ipug_event_info_key_cancel*)event_info;
			struct weston_surface *keyboard_old_focus =
								key_event->grab->keyboard->focus;
			struct weston_keyboard *keyboard = key_event->grab->keyboard;

			/* set the keyboard's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_keyboard_set_focus(keyboard, sview->view->surface);
			keyboard->default_grab.interface->cancel(key_event->grab);
			weston_keyboard_set_focus(keyboard, keyboard_old_focus);
			break;
		}
		case IPUG_TOUCH_DOWN:
		{
			struct ipug_event_info_touch_down *touch_event =
								(struct ipug_event_info_touch_down*)event_info;
			struct weston_view *touch_old_focus =
											touch_event->grab->touch->focus;
			struct weston_touch *touch = touch_event->grab->touch;

			/* set the touch's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_touch_set_focus(touch, sview->view);
			touch->default_grab.interface->down(touch_event->grab,
												touch_event->time,
												touch_event->touch_id,
												touch_event->sx,
												touch_event->sy);
			weston_touch_set_focus(touch, touch_old_focus);
			break;
		}
		case IPUG_TOUCH_UP:
		{
			struct ipug_event_info_touch_up *touch_event =
								(struct ipug_event_info_touch_up*)event_info;
			struct weston_view *touch_old_focus =
											touch_event->grab->touch->focus;
			struct weston_touch *touch = touch_event->grab->touch;

			/* set the touch's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_touch_set_focus(touch, sview->view);
			touch->default_grab.interface->up(touch_event->grab,
												touch_event->time,
												touch_event->touch_id);
			weston_touch_set_focus(touch, touch_old_focus);
			break;
		}
		case IPUG_TOUCH_MOTION:
		{
			struct ipug_event_info_touch_motion *touch_event =
								(struct ipug_event_info_touch_motion*)event_info;
			struct weston_view *touch_old_focus =
											touch_event->grab->touch->focus;
			struct weston_touch *touch = touch_event->grab->touch;

			/* set the touch's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_touch_set_focus(touch, sview->view);
			touch->default_grab.interface->motion(touch_event->grab,
												touch_event->time,
												touch_event->touch_id,
												touch_event->sx,
												touch_event->sy);
			weston_touch_set_focus(touch, touch_old_focus);
			break;
		}
		case IPUG_TOUCH_FRAME:
		{
			struct ipug_event_info_touch_frame *touch_event =
								(struct ipug_event_info_touch_frame*)event_info;
			struct weston_view *touch_old_focus =
											touch_event->grab->touch->focus;
			struct weston_touch *touch = touch_event->grab->touch;

			/* set the touch's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_touch_set_focus(touch, sview->view);
			touch->default_grab.interface->frame(touch_event->grab);
			weston_touch_set_focus(touch, touch_old_focus);
			break;
		}
		case IPUG_TOUCH_CANCEL:
		{
			struct ipug_event_info_touch_cancel *touch_event =
								(struct ipug_event_info_touch_cancel*)event_info;
			struct weston_view *touch_old_focus =
											touch_event->grab->touch->focus;
			struct weston_touch *touch = touch_event->grab->touch;

			/* set the touch's focus to the surface we want to send the event
			 * to, send the event, then reset the focus to what it was before */
			weston_touch_set_focus(touch, sview->view);
			touch->default_grab.interface->cancel(touch_event->grab);
			weston_touch_set_focus(touch, touch_old_focus);
			break;
		}
		case IPUG_LAYOUT_SWITCH_TO:
		case IPUG_LAYOUT_SWITCH_FROM:
			/* switch to/from events aren't really appropriate for sending to a
			 * particular surface */
		default:
			return SPUG_FALSE;
	}

	return SPUG_TRUE;
}

WL_EXPORT spug_bool
ipug_send_event_to_pid(struct ipug_event_info *info, uint32_t pid)
{
	struct spug_surface *ssurface;

	ssurface = renderer_interface.get_surface_from_pid(pid);

	if(ssurface) {
		return renderer_interface.send_event_to_view(info, ssurface->parent_view);
	}

	return SPUG_FALSE;
}

WL_EXPORT spug_bool
ipug_send_event_to_pname(struct ipug_event_info *info, const char *pname)
{
	struct spug_surface *ssurface;

	ssurface = renderer_interface.get_surface_from_pname(pname);

	if(ssurface) {
		return renderer_interface.send_event_to_view(info, ssurface->parent_view);
	}

	return SPUG_FALSE;
}

WL_EXPORT spug_bool
ipug_send_event_to_default(struct ipug_event_info *event_info)
{
	switch(event_info->event_type) {
	case IPUG_POINTER_FOCUS:
	{
		struct ipug_event_info_pointer_focus *event_pointer_info =
								(struct ipug_event_info_pointer_focus*)event_info;
		renderer_interface.input_grab_focus(	event_pointer_info->grab,
							event_pointer_info->surface,
							event_pointer_info->x,
							event_pointer_info->y);
		break;
	}
	case IPUG_POINTER_MOTION:
	{
		struct ipug_event_info_pointer_motion *event_pointer_info =
								(struct ipug_event_info_pointer_motion*)event_info;
		struct weston_pointer_motion_event event = { 0 };

		weston_pointer_clamp(event_pointer_info->grab->pointer,
							&event_pointer_info->x,
							&event_pointer_info->y);
		renderer_interface.input_grab_motion(	event_pointer_info->grab,
							event_pointer_info->time,
							event_pointer_info->x,
							event_pointer_info->y);

		if(event_pointer_info->mask & WESTON_POINTER_MOTION_ABS) {
			event = (struct weston_pointer_motion_event) {
				.mask = WESTON_POINTER_MOTION_ABS,
				.x = event_pointer_info->x,
				.y = event_pointer_info->y,
			};
		} else {
			event = (struct weston_pointer_motion_event) {
				.mask = WESTON_POINTER_MOTION_REL,
				.dx = event_pointer_info->x,
				.dy = event_pointer_info->y,
			};
		}

		event_pointer_info->grab->pointer->skip_pointer_move = 1;
		weston_pointer_move(event_pointer_info->grab->pointer,
							&event);
		break;
	}
	case IPUG_POINTER_BUTTON:
	{
		struct ipug_event_info_pointer_button *event_pointer_info =
								(struct ipug_event_info_pointer_button*)event_info;
		renderer_interface.input_grab_button(	event_pointer_info->grab,
							event_pointer_info->time,
							event_pointer_info->button,
							event_pointer_info->state);
		break;
	}
	case IPUG_POINTER_CANCEL:
	{
		struct ipug_event_info_pointer_cancel *event_pointer_info =
								(struct ipug_event_info_pointer_cancel*)event_info;
		renderer_interface.input_grab_pointer_cancel(event_pointer_info->grab);
		break;
	}
	case IPUG_TOUCH_DOWN:
	{
		struct ipug_event_info_touch_down *event_touch_info =
								(struct ipug_event_info_touch_down*)event_info;
		renderer_interface.input_grab_touch_down(	event_touch_info->grab,
								event_touch_info->time,
								event_touch_info->touch_id,
								event_touch_info->sx,
								event_touch_info->sy);
		break;
	}
	case IPUG_TOUCH_UP:
	{
		struct ipug_event_info_touch_up *event_touch_info =
								(struct ipug_event_info_touch_up*)event_info;
		renderer_interface.input_grab_touch_up(	event_touch_info->grab,
								event_touch_info->time,
								event_touch_info->touch_id);
		break;
	}
	case IPUG_TOUCH_MOTION:
	{
		struct ipug_event_info_touch_motion *event_touch_info =
								(struct ipug_event_info_touch_motion*)event_info;
		renderer_interface.input_grab_touch_motion(	event_touch_info->grab,
									event_touch_info->time,
									event_touch_info->touch_id,
									event_touch_info->sx,
									event_touch_info->sy);
		break;
	}
	case IPUG_TOUCH_FRAME:
	{
		struct ipug_event_info_touch_frame *event_touch_info =
								(struct ipug_event_info_touch_frame*)event_info;
		renderer_interface.input_grab_touch_frame(event_touch_info->grab);
		break;
	}
	case IPUG_TOUCH_CANCEL:
	{
		struct ipug_event_info_touch_cancel *event_touch_info =
								(struct ipug_event_info_touch_cancel*)event_info;
		renderer_interface.input_grab_touch_cancel(event_touch_info->grab);
		break;
	}
	case IPUG_KEYBOARD_KEY:
	{
		struct ipug_event_info_key_key *event_key_info =
								(struct ipug_event_info_key_key*)event_info;
		renderer_interface.input_grab_key(	event_key_info->grab,
						event_key_info->time,
						event_key_info->key,
						event_key_info->state);
		break;
	}
	case IPUG_KEYBOARD_MOD:
	{
		struct ipug_event_info_key_mod *event_key_info =
								(struct ipug_event_info_key_mod*)event_info;
		renderer_interface.input_grab_modifiers(	event_key_info->grab,
								event_key_info->serial,
								event_key_info->mods_depressed,
								event_key_info->mods_latched,
								event_key_info->mods_locked,
								event_key_info->group);
		break;
	}
	case IPUG_KEYBOARD_CANCEL:
	{
		struct ipug_event_info_key_cancel *event_key_info =
								(struct ipug_event_info_key_cancel*)event_info;
		renderer_interface.input_grab_key_cancel(event_key_info->grab);
		break;
	}
	case IPUG_LAYOUT_SWITCH_TO:
	{
		struct ipug_event_info_layout_switch_to *event_switch_info =
								(struct ipug_event_info_layout_switch_to*)event_info;
		renderer_interface.layout_switch_to(	event_switch_info->output,
							event_switch_info->plugin);
		break;
	}
	case IPUG_LAYOUT_SWITCH_FROM:
	{
		struct ipug_event_info_layout_switch_from *event_switch_info =
								(struct ipug_event_info_layout_switch_from*)event_info;
		renderer_interface.layout_switch_from(	event_switch_info->output,
							event_switch_info->plugin);
		break;
	}
	default:
		return SPUG_FALSE;
	}

	return SPUG_TRUE;
}

static struct spug_view*
get_focus_for_event(enum ipug_event_type event_type)
{
	return framework->input_focus[event_type];
}

WL_EXPORT spug_bool
ipug_send_event_to_focus(struct ipug_event_info *event_info)
{
	struct spug_view *sview;

	/* get the focused view for this event type */
	sview = get_focus_for_event(event_info->event_type);

	if(sview) {
		return renderer_interface.send_event_to_view(event_info, sview);
	}

	return SPUG_FALSE;
}

WL_EXPORT spug_bool
ipug_send_event_to_view(struct ipug_event_info *event_info,
								const spug_view_id view_id)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view_id);

	if(sview) {
		return renderer_interface.send_event_to_view(event_info, sview);
	}

	return SPUG_FALSE;
}

struct event_enum_bit_pair {
	enum ipug_event_type event_type;
	int bit;
};

struct event_enum_bit_pair event_enum_bit_pairs[IPUG_NUM_EVENT_TYPES] =
{
	{IPUG_POINTER_FOCUS, IPUG_POINTER_FOCUS_BIT},
	{IPUG_POINTER_MOTION, IPUG_POINTER_MOTION_BIT},
	{IPUG_POINTER_BUTTON, IPUG_POINTER_BUTTON_BIT},
	{IPUG_POINTER_CANCEL, IPUG_POINTER_CANCEL_BIT},
	{IPUG_KEYBOARD_KEY, IPUG_KEYBOARD_KEY_BIT},
	{IPUG_KEYBOARD_MOD, IPUG_KEYBOARD_MOD_BIT},
	{IPUG_KEYBOARD_CANCEL, IPUG_KEYBOARD_CANCEL_BIT},
	{IPUG_TOUCH_DOWN, IPUG_TOUCH_DOWN_BIT},
	{IPUG_TOUCH_UP, IPUG_TOUCH_UP_BIT},
	{IPUG_TOUCH_MOTION, IPUG_TOUCH_MOTION_BIT},
	{IPUG_TOUCH_FRAME, IPUG_TOUCH_FRAME_BIT},
	{IPUG_TOUCH_CANCEL, IPUG_TOUCH_CANCEL_BIT},
	{IPUG_LAYOUT_SWITCH_TO, IPUG_LAYOUT_SWITCH_TO_BIT},
	{IPUG_LAYOUT_SWITCH_FROM, IPUG_LAYOUT_SWITCH_FROM_BIT},
};

WL_EXPORT void
ipug_set_input_focus(ipug_event_mask event_mask, const spug_view_id view)
{
	struct spug_view *sview;
	int i;

	if(view == 0) {
		/* pass in 0 to remove the focus */
		sview = NULL;
	} else {
		sview = renderer_interface.get_view_from_id(view);

		/* if the view id is not NULL but we couldn't find a matching spug_view,
		 * then it is invalid */
		if(!sview) {
			IAS_ERROR("Trying to set input focus to invalid view 0x%016"PRIXPTR"\n",
					(uintptr_t)view);
			return;
		}
	}

	/* set the focus to sview for each specified event type */
	for(i = 0; i < IPUG_NUM_EVENT_TYPES; i++) {
		if(event_mask & event_enum_bit_pairs[i].bit) {
			framework->input_focus[i] = sview;
		}
	}
}

WL_EXPORT ipug_event_mask
ipug_get_input_focus(const spug_view_id view_id)
{
	struct spug_view *sview = renderer_interface.get_view_from_id(view_id);
	ipug_event_mask event_mask = 0;
	int i;

	if(!sview) {
		IAS_ERROR("Trying to get focused events on invalid view 0x%016"PRIXPTR"\n",
				(uintptr_t)view_id);
		return 0;
	}

	/* set the focus to sview for each specified event type */
	for(i = 0; i < IPUG_NUM_EVENT_TYPES; i++) {
		if(framework->input_focus[event_enum_bit_pairs[i].event_type] == sview) {
			event_mask |= event_enum_bit_pairs[i].bit;
		}
	}

	return event_mask;
}

static int
spug_renderer_read_pixels(struct weston_output *output,
			       pixman_format_code_t format, void *pixels,
			       uint32_t x, uint32_t y,
			       uint32_t width, uint32_t height)
{
	/* nop for now */
	return 0;
}

static void
spug_renderer_repaint_output(struct weston_output *output,
			       pixman_region32_t *output_damage)
{
	struct ias_output *ioutput = (struct ias_output*)output;

	spug_update_all_lists();

	ioutput->plugin->info.draw(framework->spug_view_ids);

	/* do we need to do anything with output_damage? perhaps pass it to the
	 * layout plugin so it can decide what to update */
}

static void
spug_renderer_flush_damage(struct weston_surface *surface)
{
	/* nop for now */
}

static void
spug_renderer_attach(struct weston_surface *es, struct weston_buffer *buffer)
{
	/* Are we supposed to do anything differently to gl-renderer? If not just
	 * pass though to it */
	framework->compositor->renderer->attach(es, buffer);
}

static void
spug_renderer_surface_set_color(struct weston_surface *surface,
			       float red, float green,
			       float blue, float alpha)
{
	/* nop for now */
}

/* where should this be called from? we call the renderer's init function from
 * ias-plugin-framework.c's module_init(), but there doesn't seem to be a
 * module_close() or module_destroy() counterpart */
static void
spug_renderer_destroy(struct weston_compositor *ec)
{
	spug_destroy_all_lists();
}

static void
spug_renderer_repaint_output_base(struct weston_output *output,
			pixman_region32_t *output_damage)
{
	/* nop for now */
}

struct spug_renderer_interface renderer_interface = {
	.update_spug_ids = update_spug_ids,
	.confirm_hash = confirm_hash,

	.get_view_from_id = get_view_from_id,
	.get_surface_from_id = get_surface_from_id,
	.get_output_from_id = get_output_from_id,
	.get_seat_from_id = get_seat_from_id,
	.get_client_from_id = get_client_from_id,

	.get_view_texture = get_view_texture,
	.get_view_egl_image = get_view_egl_image,

	.destroy_spug_view_common = destroy_spug_view_common,
	.destroy_spug_surface_common = destroy_spug_surface_common,
	.destroy_spug_client_common = destroy_spug_client_common,
	.destroy_spug_seat = destroy_spug_seat,
	.destroy_spug_output = destroy_spug_output,
	.destroy_spug_plane = destroy_spug_plane,

	.create_spug_view_draw_info = create_spug_view_draw_info,
	.create_spug_surface_draw_info = create_spug_surface_draw_info,
	.create_spug_draw_info = create_spug_draw_info,
	.update_spug_draw_infos = update_spug_draw_infos,

	.create_spug_view = create_spug_view,
	.create_spug_surface = create_spug_surface,
	.create_spug_seat = create_spug_seat,
	.create_spug_plane = create_spug_plane,
	.create_spug_client = create_spug_client,
	.create_spug_output = create_spug_output,

	.call_bind_ext = call_bind_ext,

	.input_grab_focus = input_grab_focus,
	.input_grab_motion = input_grab_motion,
	.input_grab_button = input_grab_button,
	.input_grab_pointer_cancel = input_grab_pointer_cancel,
	.input_grab_touch_down = input_grab_touch_down,
	.input_grab_touch_up = input_grab_touch_up,
	.input_grab_touch_motion = input_grab_touch_motion,
	.input_grab_touch_frame = input_grab_touch_frame,
	.input_grab_touch_cancel = input_grab_touch_cancel,
	.input_grab_key = input_grab_key,
	.input_grab_modifiers = input_grab_modifiers,
	.input_grab_key_cancel = input_grab_key_cancel,
	.layout_switch_to = layout_switch_to,
	.layout_switch_from = layout_switch_from,

	.get_surface_from_pid = get_surface_from_pid,
	.get_surface_from_pname = get_surface_from_pname,

	.send_event_to_view = send_event_to_view,
};

WL_EXPORT void
spug_init_renderer(void)
{
	framework->base.read_pixels = spug_renderer_read_pixels;
	framework->base.repaint_output = spug_renderer_repaint_output;
	framework->base.flush_damage = spug_renderer_flush_damage;
	framework->base.attach = spug_renderer_attach;
	framework->base.surface_set_color = spug_renderer_surface_set_color;
	framework->base.destroy = spug_renderer_destroy;
	framework->base.repaint_output_base = spug_renderer_repaint_output_base;
}
