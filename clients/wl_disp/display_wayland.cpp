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

#include "display_wayland.h"
#include <sys/types.h>
#include <unistd.h>
#include "protocol/ias-shell-client-protocol.h"
#include "protocol/ivi-application-client-protocol.h"
#include "protocol/xdg-shell-unstable-v6-client-protocol.h"

extern "C" {

double level() {
	return MAKE_LEVEL(2, SUPPORT_DISP_WL);
}

void *init() {
	return (void *) new global_wl2;
}

void deinit(void *var) {
	delete (global_wl2 *)var;
}

void *disp_init(void *var) {
	return (void *) new disp_wl((global_wl2 *) var);
}

void disp_deinit(void *var) {
	delete (disp_wl *)var;
}

}

/*******************************************************************************
 * Description
 *	A static function that calls the wl_global_v2 classes corresponding virtual
 *	function
 * Parameters
 *	void *data - a pointer to global_wl2
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
	((global_wl2 *) data)->registry_handle_global(data, registry, name,
			interface, version);
}

/*******************************************************************************
 * Description
 *	A static function that calls the wl_global_v2 classes corresponding virtual
 *	function
 * Parameters
 *	void *data - a pointer to global_wl2
 *	struct wl_registry *registry - a pointer to the Wayland registry
 *	uint32_t name - The id of the interface
 * Return val
 *	void
 ******************************************************************************/
static void rhgr(void *data, struct wl_registry *registry,
	      uint32_t name)
{
	((global_wl2 *) data)->registry_handle_global_remove(data, registry, name);
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
void global_wl2::add_reg()
{
	wl_registry_add_listener(registry, &registry_listener, this);
	wl_display_roundtrip(display);
}

/*******************************************************************************
 * Description
 *	This function handles the global registry handler. It is called at
 *	initialization time when a new interface is being advertised to the app.
 *	It cares about the wl_compositor and various shell interfaces that the IAS
 *	compositor handles. It is implemented in the base class and may also be
 *	called by inherited classes.
 * Parameters
 *	void *data - a pointer to global_wl2
 *	struct wl_registry *registry - a pointer to the Wayland registry
 *	uint32_t name - The id of the interface
 *	const char *interface - The name of the interface
 *	uint32_t version - The version number for this interface
 * Return val
 *	void
 ******************************************************************************/
void global_wl2::registry_handle_global(void *data,
	struct wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version)
{
	/* Just call the base class's registry global handler */
	global_wl::registry_handle_global(data, registry, name, interface, version);
}

/*******************************************************************************
 * Description
 *	This function handles the global registry handler remove function. It is
 *	called when an interface is being removed by the compositor.
 * Parameters
 *	void *data - a pointer to global_wl2
 *	struct wl_registry *registry - a pointer to the Wayland registry
 *	uint32_t name - The id of the interface
 * Return val
 *	void
 ******************************************************************************/
void global_wl2::registry_handle_global_remove(void *data,
	struct wl_registry *registry, uint32_t name)
{
	/* Just call the base class's registry global handler */
	global_wl::registry_handle_global_remove(data, registry, name);
}

/*******************************************************************************
 * Description
 *	This is a callback handler from the compositor for ias shell ping. Upon
 *	receiving a ping call from the compositor, this app must respond back with
 *	a pong.
 * Parameters
 *	void *data - a pointer to disp_wl
 *	struct zxdg_shell_v6 *shell - a pointer to shell
 *	uint32_t serial - serial of the ping event
 * Return val
 *	void
 ******************************************************************************/
void
disp_wl::ias_handle_ping(void *data, struct ias_surface *ias_surface,
		uint32_t serial)
{
	ias_surface_pong(ias_surface, serial);
}

/*******************************************************************************
 * Description
 *	This function resizes the egl window based on incoming width/height from the
 *	compositor
 * Parameters
 *	int width - New width
 *	int height - New height
 * Return val
 *	void
 ******************************************************************************/
void
disp_wl::configure(int width, int height)
{
	if(width > 0 && height > 0) {
		if(s.native) {
			wl_egl_window_resize(s.native, width, height, 0, 0);
		}
		s.si.width = width;
		s.si.height = height;
	}
}

/*******************************************************************************
 * Description
 *	This is a callback handler from the compositor for ias shell's configure.
 *	Upon receiving this configure callback, the app must resize its buffers
 *	according to the width/height provided by the compositor.
 * Parameters
 *	void *data - A pointer to the disp_wl class
 *	struct ias_surface *ias_surface - A pointer to the ias_surface structure
 *	int32_t width - The new width
 *	int32_t height - The new height
 * Return val
 *	void
 ******************************************************************************/
void
disp_wl::ias_handle_configure(void *data, struct ias_surface *ias_surface,
		int32_t width, int32_t height)
{
	disp_wl *d = (disp_wl *) data;
	d->configure(width, height);
}

static struct ias_surface_listener ias_surface_listener = {
	disp_wl::ias_handle_ping,
	disp_wl::ias_handle_configure,
};

/*******************************************************************************
 * Description
 *	This is a callback handler from the compositor for zxdg surface's configure.
 *	Upon receiving this configure callback, the app must acknowledge back to the
 *	compositor that it received a configure event.
 * Parameters
 *	void *data - A pointer to the disp_wl class
 *	struct zxdg_surface_v6 *surface - a pointer to shell's surface
 *	uint32_t serial - serial of configure event
 * Return val
 *	void
 ******************************************************************************/
void
disp_wl::handle_surface_configure(void *data, struct zxdg_surface_v6 *surface,
			 uint32_t serial)
{
	disp_wl *d = (disp_wl *) data;
	zxdg_surface_v6_ack_configure(surface, serial);
	d->s.wait_for_configure = false;
}

static const struct zxdg_surface_v6_listener xdg_surface_listener = {
	disp_wl::handle_surface_configure
};

/*******************************************************************************
 * Description
 *	This is a callback handler from the compositor for ivi shell's configure.
 *	Upon receiving this configure callback, the app must resize its buffers
 *	according to the width/height provided by the compositor.
 * Parameters
 *	void *data - A pointer to the disp_wl class
 *	struct ivi_surface *ivi_surface - A pointer to the ivi_surface structure
 *	int32_t width - The new width
 *	int32_t height - The new height
 * Return val
 *	void
 ******************************************************************************/
void
disp_wl::handle_ivi_surface_configure(void *data, struct ivi_surface *ivi_surface,
                             int32_t width, int32_t height)
{
	disp_wl *d = (disp_wl *) data;
	d->configure(width, height);
}

static const struct ivi_surface_listener ivi_surface_listener = {
	disp_wl::handle_ivi_surface_configure,
};

/*******************************************************************************
 * Description
 *	This is a callback handler from the compositor for zxdg shell's configure.
 *	Upon receiving this configure callback, the app must resize its buffers
 *	according to the width/height provided by the compositor.
 * Parameters
 *	void *data - A pointer to the disp_wl class
 *	struct zxdg_toplevel_v6 *toplevel- A pointer to the zxdg_toplevel_v6 structure
 *	int32_t width - The new width
 *	int32_t height - The new height
 *	struct wl_array *states
 * Return val
 *	void
 ******************************************************************************/
void disp_wl::handle_toplevel_configure(void *data,
		struct zxdg_toplevel_v6 *toplevel,
		int32_t width, int32_t height,
		struct wl_array *states)
{
	disp_wl *d = (disp_wl *) data;
	d->configure(width, height);
}

/*******************************************************************************
 * Description
 *	When toplevel of a surface closes, this function is called. Currently we do
 *	nothing here.
 * Parameters
 *	void *data - A pointer the disp_wl class
 *	struct zxdg_toplevel_v6 *xdg_toplevel - A pointer to zxdg_toplevel_v6 struct
 * Return val
 *	void
 ******************************************************************************/
void disp_wl::handle_toplevel_close(void *data,
		struct zxdg_toplevel_v6 *xdg_toplevel)
{
}

static const struct zxdg_toplevel_v6_listener xdg_toplevel_listener = {
	disp_wl::handle_toplevel_configure,
	disp_wl::handle_toplevel_close,
};


/*******************************************************************************
 * Description
 *	This function creates a Wayland surface and also a shell surface. Since there
 *	are multiple shells (ias/ivi/zxdg), it checks to see which one did this
 *	client bind to and calls the appropriate shell's create surface routine.
 *	Finally, it creates an EGL window with the provided width and height.
 * Parameters
 *	int width - The width of the surface
 *	int height - The height of the surface
 *	const char* title - The title to give to this surface
 * Return val
 *	EGLNativeWindowType - The EGL window type handle returned by
 *	wl_egl_window_create
 ******************************************************************************/
EGLNativeWindowType disp_wl::create_surface(int width, int height, const char* title)
{
	uint32_t ivi_surf_id;


	s.surface = wl_compositor_create_surface(g->compositor);
	if (g->ias_shell) {
		s.shell_surface = ias_shell_get_ias_surface((struct ias_shell *) g->ias_shell,
				s.surface, title);
		ias_surface_add_listener((struct ias_surface *) s.shell_surface,
				&ias_surface_listener, this);
	}
	if (g->wl_shell) {
		s.shell_surface = zxdg_shell_v6_get_xdg_surface(
				(struct zxdg_shell_v6 *) g->wl_shell, s.surface);
		zxdg_surface_v6_add_listener((struct zxdg_surface_v6 *) s.shell_surface,
				&xdg_surface_listener, this);
		s.xdg_toplevel =
			zxdg_surface_v6_get_toplevel(
					(struct zxdg_surface_v6 *) s.shell_surface);
		zxdg_toplevel_v6_add_listener(s.xdg_toplevel,
				&xdg_toplevel_listener, this);

		zxdg_toplevel_v6_set_title(s.xdg_toplevel,
				title);
		s.wait_for_configure = true;
		wl_surface_commit(s.surface);
	}
	if (g->ivi_application) {
		ivi_surf_id = (uint32_t) getpid();
		s.ivi_surface =
			ivi_application_surface_create(
					(struct ivi_application *) g->ivi_application,
					ivi_surf_id, s.surface);

		ivi_surface_add_listener(s.ivi_surface,
					 &ivi_surface_listener, this);
	}

	s.native = wl_egl_window_create(s.surface,
					width,
					height);
	s.si.width = width;
	s.si.height = height;

	return (EGLNativeWindowType) s.native;
}

/*******************************************************************************
 * Description
 *	This function destroys a surface. It goes about destroying the EGL window,
 *	followed by the shell surface and finally the Wayland surface.
 * Parameters
 *	None
 * Return val
 *	void
 ******************************************************************************/
void disp_wl::destroy_surface()
{

	wl_egl_window_destroy(s.native);

	if (s.xdg_toplevel)
		zxdg_toplevel_v6_destroy(s.xdg_toplevel);
	if (g->wl_shell)
		zxdg_surface_v6_destroy((zxdg_surface_v6 *) s.shell_surface);
	if (g->ivi_application)
		ivi_surface_destroy(s.ivi_surface);
	if (g->ias_shell) {
		ias_surface_destroy((struct ias_surface *) s.shell_surface);
	}
	wl_surface_destroy(s.surface);
}

/*******************************************************************************
 * Description
 *	This function gets the resolution (width/height) of a surface
 * Parameters
 *	int *w - The pointer to width that we should fill with current width
 *	int *h - The pointer to height that we should fill with current height
 * Return val
 *	bool - true if we filled the width and height with a new value, false if
 *	our current width/height are 0 and we didn't fill the params with new values
 ******************************************************************************/
bool disp_wl::get_res(int *w, int *h)
{
	bool ret = false;
	if(s.si.width > 0 && s.si.height > 0) {
		*w = s.si.width;
		*h = s.si.height;
		ret = true;
	}
	return ret;
}
