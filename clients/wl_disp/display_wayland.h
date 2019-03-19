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


#ifndef _DISPLAY_WAYLAND_H_
#define _DISPLAY_WAYLAND_H_

#include <global_wayland.h>
#include <memory.h>

typedef struct _surf_info {
	uint32_t surf_id;
	char *name;
	int32_t x;
	int32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t zorder;
	uint32_t alpha;
	uint32_t behavior_bits;
	uint32_t dispno;
	uint32_t flipped;
	int shareable;
	int modified;
	struct wl_list link;
} surf_info_t;

typedef struct _surf {
	struct wl_egl_window *native;
	struct wl_surface *surface;
	void *shell_surface;
	struct xdg_toplevel *xdg_toplevel;
	bool wait_for_configure;
	surf_info_t si;
} surf_t ;

class global_wl2 : public global_wl {
	protected:


	public:
		global_wl2() { }
		~global_wl2() { }
		virtual void add_reg();
		virtual void registry_handle_global(void *data,
				struct wl_registry *registry, uint32_t name,
				const char *interface, uint32_t version);
		virtual void registry_handle_global_remove(void *data,
				struct wl_registry *registry, uint32_t name);

		friend class disp_wl;
};

class disp_wl {
	protected:
		/* To be initialized for each surface */
		/* a pointer to global class */
		global_wl2 *g;
		/* surface attributes */
		surf_t s;

	public:
		disp_wl(global_wl2 *gw) { g = gw; memset(&s, 0, sizeof(surf_t)); }
		virtual ~disp_wl() { }
		virtual bool get_res(int *w, int *h);
		virtual EGLNativeWindowType create_surface(int width, int height, const char* title);
		virtual void destroy_surface();
		static void ias_handle_ping(void *data, struct ias_surface *ias_surface,
				uint32_t serial);
		static void ias_handle_configure(void *data,
				struct ias_surface *ias_surface, int32_t width, int32_t height);
		static void handle_surface_configure(void *data,
				struct xdg_surface *surface, uint32_t serial);
		static void handle_toplevel_configure(void *data,
				struct xdg_toplevel *toplevel,
				int32_t width, int32_t height,
				struct wl_array *states);
		static void handle_toplevel_close(void *data,
				struct xdg_toplevel *xdg_toplevel);
		void configure(int width, int height);
};


#endif
