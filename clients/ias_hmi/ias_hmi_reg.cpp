/*
** Copyright 2017 Intel Corporation
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include "ias_hmi_reg.h"
#include <string.h>
#include "protocol/ias-shell-client-protocol.h"
#include "protocol/ivi-application-client-protocol.h"
#include "protocol/xdg-shell-unstable-v6-client-protocol.h"

extern "C" {

double level() {
	return MAKE_LEVEL(3, SUPPORT_NONE);
}

void *init() {
	return (void *) new ias_hmi_reg;
}

void deinit(void *var) {
	delete (ias_hmi_reg *)var;
}

}

/*******************************************************************************
 * Description
 *	A static function that calls the wl_global_v3 classes corresponding virtual
 *	function
 * Parameters
 *	void *data - a pointer to ias_hmi_reg
 *	struct wl_registry *registry - a pointer to the Wayland registry
 *	uint32_t name - The id of the interface
 *	const char *interface - The name of the interface
 *	uint32_t version - The version number for this interface
 * Return val
 *	void
 ******************************************************************************/
static void rhg(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version)
{
	((ias_hmi_reg *) data)->registry_handle_global(data, registry, name,
			interface, version);
}

/*******************************************************************************
 * Description
 *	A static function that calls the wl_global_v3 classes corresponding virtual
 *	function
 * Parameters
 *	void *data - a pointer to ias_hmi_reg
 *	struct wl_registry *registry - a pointer to the Wayland registry
 *	uint32_t name - The id of the interface
 * Return val
 *	void
 ******************************************************************************/
static void rhgr(void *data, struct wl_registry *registry,
	      uint32_t name)
{
	((ias_hmi_reg *) data)->registry_handle_global_remove(data, registry, name);
}

static const struct wl_registry_listener registry_listener = {
	rhg,
	rhgr,
};

/*******************************************************************************
 * Description
 *	This function adds a global registry listener.
 * Parameters
 *	None
 * Return val
 *	void
 ******************************************************************************/
void ias_hmi_reg::add_reg()
{
	wl_registry_add_listener(registry, &registry_listener, this);
	wl_display_roundtrip(display);
}

/*******************************************************************************
 * Description
 *	This function is a callback from the compositor to this client informing it of
 *	another surface having been created or a change to that surface having
 *	occurred.
 * Parameters
 *	void *data - a pointer to ias_hmi_reg class
 *	struct ias_hmi *hmi - A pointer to sruct ias_hmi
 *	uint32_t id - The id of the surface that got created/changed
 *	const char *name - The name of the surface that got created/changed
 *	uint32_t zorder  - The zorder of the surface that got created/changed
 *	int32_t x - The x coordinate position of the surface that got created/changed
 *	int32_t y - The y coordinate position of the surface that got created/changed
 *	uint32_t width - The width of the surface that got created/changed
 *	uint32_t height- The height of the surface that got created/changed
 *	uint32_t alpha - The alpha value of the surface that got created/changed
 *	uint32_t behavior_bits - The behavioral bits of the surface that got
 *		created/changed
 *	uint32_t pid - The process id of the surface that got created/changed
 *	const char *pname - The process name of the surface that got created/changed
 *	uint32_t dispno - The display number on which this surface is
 *	uint32_t flipped - Whether this surface is being flipped or not
 * Return val
 *	void
 ******************************************************************************/
void ias_hmi_reg::ias_hmi_surface_info(void *data,
		struct ias_hmi *hmi,
		uint32_t id,
		const char *name,
		uint32_t zorder,
		int32_t x, int32_t y,
		uint32_t width, uint32_t height,
		uint32_t alpha,
		uint32_t behavior_bits,
		uint32_t pid,
		const char *pname,
		uint32_t dispno,
		uint32_t flipped)
{
	surf_info_t *s, *existing;
	ias_hmi_reg *d = (ias_hmi_reg *) data;

	wl_list_for_each_reverse(existing, &d->surface_list, link) {
		if(id == existing->surf_id) {
			break;
		}
	}
	if (&existing->link == &d->surface_list) {
		existing = NULL;
	}

	if (!existing) {
		s = new surf_info_t;
		if(!s) {
			printf("Couldn't allocate a surface list\n");
			return;
		}
		s->modified = 0;
		s->shareable = 0;
	} else {
		s = existing;
		delete s->name;
	}

	s->surf_id = id;
	s->name = strdup(name);
	s->x = x;
	s->y = y;
	s->width = width;
	s->height = height;
	s->zorder = zorder;
	s->alpha = alpha;
	s->behavior_bits = behavior_bits;
	s->dispno = dispno;
	s->flipped = flipped;

	if (!existing) {
		wl_list_insert(&d->surface_list, &s->link);
	}

	/* If we know this surface, then move it to the appropriate place */
}

/*******************************************************************************
 * Description
 *	This function is a callback from the compositor to this client informing it of
 *	another surface having been destroyed
 * Parameters
 *	void *data - a pointer to ias_hmi_reg class
 *	struct ias_hmi *hmi - A pointer to sruct ias_hmi
 *	uint32_t id - The id of the surface that was destroyed
 *	const char *name - The name of the surface that was destroyed
 *	uint32_t pid - The process id of the surface that was destroyed
 *	const char *pname - The process name of the surface that was destroyed
 * Return val
 *	void
 ******************************************************************************/
void ias_hmi_reg::ias_hmi_surface_destroyed(void *data,
		struct ias_hmi *hmi,
		uint32_t id,
		const char *name,
		uint32_t pid,
		const char *pname)
{
	surf_info_t *s, *tmp;
	ias_hmi_reg *d = (ias_hmi_reg *) data;

	/* Find the surface and remove it from our surface list */
	wl_list_for_each_safe(s, tmp, &d->surface_list, link) {
		if (s->surf_id == id) {
			wl_list_remove(&s->link);
			delete s->name;
			delete s;
			return;
		}
	}

}

static const struct ias_hmi_listener hmi_listener = {
	ias_hmi_reg::ias_hmi_surface_info,
	ias_hmi_reg::ias_hmi_surface_destroyed,
};

/*******************************************************************************
 * Description
 *	This function handles the global registry handler. It is called at
 *	initialization time when a new interface is being advertised to the app.
 *	It cares about the wl_compositor and various shell interfaces that the IAS
 *	compositor handles. It is implemented in the base class and may also be
 *	called by inherited classes.
 * Parameters
 *	void *data - a pointer to ias_hmi_reg
 *	struct wl_registry *registry - a pointer to the Wayland registry
 *	uint32_t name - The id of the interface
 *	const char *interface - The name of the interface
 *	uint32_t version - The version number for this interface
 * Return val
 *	void
 ******************************************************************************/
void ias_hmi_reg::registry_handle_global(void *data,
	struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	ias_hmi_reg *d = (ias_hmi_reg *) data;

	if (strcmp(interface, "ias_hmi") == 0) {
		d->hmi = (struct ias_hmi *) wl_registry_bind(
				registry, name, &ias_hmi_interface, 1);
		ias_hmi_add_listener(d->hmi, &hmi_listener, d);
		wl_list_init(&d->surface_list);
	}

	global_wl2::registry_handle_global(data, registry, name, interface, version);

}

/*******************************************************************************
 * Description
 *	This function handles the global registry handler remove function. It is
 *	called when an interface is being removed by the compositor.
 * Parameters
 *	void *data - a pointer to ias_hmi_reg
 *	struct wl_registry *registry - a pointer to the Wayland registry
 *	uint32_t name - The id of the interface
 * Return val
 *	void
 ******************************************************************************/
void ias_hmi_reg::registry_handle_global_remove(void *data,
	struct wl_registry *registry, uint32_t name)
{
	global_wl2::registry_handle_global_remove(data, registry, name);
}

