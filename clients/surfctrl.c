/*
 *---------------------------------------------------------------------------
 * Filename: surfctrl.c
 *---------------------------------------------------------------------------
 * Copyright (c) 2012, Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *----------------------------------------------------------------------------
 * Simple Wayland client that lets the user control other clients' surfaces.
 *----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <signal.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#include "../shared/config-parser.h"
#include "../shared/helpers.h"

#include "ias-shell-client-protocol.h"
#include <sys/types.h>
#include <unistd.h>
#include "protocol/ivi-application-client-protocol.h"

struct wayland {
	struct wl_display *display;
	struct wl_registry *registry;
	struct ias_hmi *hmi;
	struct ivi_application *ivi_application;
	struct wl_list surface_list;
	struct wl_callback *mode_callback;
	uint32_t mask;

	unsigned int modeset_retcode;
};

struct surf_list {
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
};

struct wayland wayland = { 0 };

static void
ias_hmi_surface_info(void *data,
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
	struct surf_list *s, *existing;
	struct wayland *w = data;

	wl_list_for_each_reverse(existing, &w->surface_list, link) {
		if(id == existing->surf_id) {
			break;
		}
	}
	if (&existing->link == &w->surface_list) {
		existing = NULL;
	}

	if (!existing) {
		s = calloc(1, (sizeof(*s)));
		if(!s) {
			fprintf(stderr, "Couldn't allocate a surface list\n");
			return;
		}
		s->modified = 0;
		s->shareable = 0;
	} else {
		s = existing;
		free(s->name);
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
		wl_list_insert(&w->surface_list, &s->link);
	}

	ias_hmi_get_surface_sharing_info(hmi, id);
	wl_display_roundtrip(wayland.display);
}

static void
ias_hmi_surface_destroyed(void *data,
			struct ias_hmi *hmi,
			uint32_t id,
			const char *name,
			uint32_t pid,
			const char *pname)
{
	struct surf_list *s, *tmp;
	struct wayland *wayland = data;

	fprintf(stderr, "Client surface, \"%s\", was destroyed id = %u,"
			" pid = %d, pname = %s\n", name, id, pid, pname);

	/* Find the surface and remove it from our surface list */
	wl_list_for_each_safe(s, tmp, &wayland->surface_list, link) {
		if (s->surf_id == id) {
			wl_list_remove(&s->link);
			free(s->name);
			free(s);
			return;
		}
	}
}

static void
ias_hmi_surface_sharing_info(void *data,
		struct ias_hmi *hmi,
		uint32_t id,
		const char *name,
		uint32_t shareable,
		uint32_t pid,
		const char *pname)
{
	struct surf_list *s;
	struct wayland *w = data;

	wl_list_for_each_reverse(s, &w->surface_list, link) {
		if(id == s->surf_id) {
			break;
		}
	}

	if (&s->link != &w->surface_list) {
		s->shareable = shareable;

		fprintf(stderr, "Client surface, \"%s\", was %s id = %u  %d x %d @ %d, %d"
			" zorder = 0x%x, alpha = %d, pid = %d, pname = %s, behaviour = 0x%x,"
			"dispno=%d, flipped=%d, shareable=%d\n",
			s->name, s->modified ? "modified" : "created",
			id, s->width, s->height, s->x, s->y, s->zorder, s->alpha, pid,
			pname, s->behavior_bits, s->dispno, s->flipped, shareable);
		s->modified = 1;
	}
}

static const struct ias_hmi_listener listener = {
	ias_hmi_surface_info,
	ias_hmi_surface_destroyed,
	ias_hmi_surface_sharing_info
};

static void
ivi_application_surface_sharing_info(void *data,
		struct ivi_application *ivi_application,
		uint32_t id,
		const char *name,
		uint32_t shareable,
		uint32_t pid,
		const char *pname)
{
	struct surf_list *s, *existing;
	struct wayland *w = data;

	wl_list_for_each_reverse(existing, &w->surface_list, link) {
		if(id == existing->surf_id) {
			break;
		}
	}
	if (&existing->link == &w->surface_list) {
		existing = NULL;
	}

	fprintf(stderr, "Client surface was %s, id = %u, pid = %d, shareable = %d\n",
			existing ? "modified" : "created",id, pid, shareable);

	if (!existing) {
		s = calloc(1, (sizeof(*s)));
		if(!s) {
			fprintf(stderr, "Couldn't allocate a surface list\n");
			return;
		}
	} else {
		s = existing;
		free(s->name);
	}

	s->surf_id = id;
	s->shareable = shareable;

	if (!existing) {
		wl_list_insert(&w->surface_list, &s->link);
	}
}

static const struct ivi_application_listener ivi_application_listener = {
		ivi_application_surface_sharing_info
};

static void
registry_handle_global(void *data, struct wl_registry *registry, uint32_t id,
		      const char *interface, uint32_t version)
{
	struct wayland *w = data;

	if (strcmp(interface, "ias_hmi") == 0) {
		w->hmi = wl_registry_bind(registry, id, &ias_hmi_interface, 1);
		ias_hmi_add_listener(w->hmi, &listener, w);
	} else if (strcmp(interface, "ivi_application") == 0) {
		w->ivi_application = wl_registry_bind(registry, id,
				&ivi_application_interface, 1);
		ivi_application_add_listener(w->ivi_application, &ivi_application_listener, w);
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
};

static int
surface_set_xy(struct wayland *wayland,
		char *surfname, int surfid, unsigned int x, unsigned int y)
{
	struct surf_list *s;

	wl_list_for_each(s, &wayland->surface_list, link) {
		if(surfid != -1) {
			if(s->surf_id == (uint32_t) surfid) {
				fprintf(stderr, "Moving %d to %d, %d\n", s->surf_id, x, y);
				ias_hmi_move_surface(wayland->hmi, s->surf_id, x, y);
			}
		} else if(strcmp(s->name, surfname) == 0) {
			fprintf(stderr, "Moving %s to %d, %d\n", s->name, x, y);
			ias_hmi_move_surface(wayland->hmi, s->surf_id, x, y);
		}
	}

	return 0;
}

static int
surface_resize(struct wayland *wayland,
		char *surfname, int surfid, unsigned int x, unsigned int y)
{
	struct surf_list *s;

	wl_list_for_each(s, &wayland->surface_list, link) {
		if(surfid != -1) {
			if(s->surf_id == (uint32_t) surfid) {
				fprintf(stderr, "Resizing %d to %d, %d\n", s->surf_id, x, y);
				ias_hmi_resize_surface(wayland->hmi, s->surf_id, x, y);
			}
		} else if(strcmp(s->name, surfname) == 0) {
			fprintf(stderr, "Resizing %s to %d, %d\n", s->name, x, y);
			ias_hmi_resize_surface(wayland->hmi, s->surf_id, x, y);
		}
	}

	return 0;
}

static int
surface_visible(struct wayland *wayland,
		char *surfname, int surfid, int visible)
{
	struct surf_list *s;

	wl_list_for_each(s, &wayland->surface_list, link) {
		if(surfid != -1) {
			if(s->surf_id == (uint32_t) surfid) {
				fprintf(stderr, "Setting visibility of %d to %d\n",
						s->surf_id, visible);
				ias_hmi_set_visible(wayland->hmi, s->surf_id,
						visible
						? IAS_HMI_VISIBLE_OPTIONS_VISIBLE
						: IAS_HMI_VISIBLE_OPTIONS_HIDDEN);
			}
		} else if(strcmp(s->name, surfname) == 0) {
			fprintf(stderr, "Setting visibility of %s to %d\n", s->name, visible);
			ias_hmi_set_visible(wayland->hmi, s->surf_id,
					visible
					? IAS_HMI_VISIBLE_OPTIONS_VISIBLE
					: IAS_HMI_VISIBLE_OPTIONS_HIDDEN);
		}
	}

	return 0;
}

static int
surface_alpha(struct wayland *wayland,
		char *surfname, int surfid, int alpha)
{
	struct surf_list *s;

	wl_list_for_each(s, &wayland->surface_list, link) {
		if(surfid != -1) {
			if(s->surf_id == (uint32_t) surfid) {
				fprintf(stderr, "Setting constant alpha of %d to %d\n",
						s->surf_id, alpha);
				ias_hmi_set_constant_alpha(wayland->hmi, s->surf_id, alpha);
			}
		} else if(strcmp(s->name, surfname) == 0) {
			fprintf(stderr, "Setting constant alpha of %s to %d\n", s->name, alpha);
			ias_hmi_set_constant_alpha(wayland->hmi, s->surf_id, alpha);
		}
	}

	return 0;
}

static int
surface_zorder(struct wayland *wayland,
		char *surfname, int surfid, int zorder)
{
	struct surf_list *s;

	wl_list_for_each(s, &wayland->surface_list, link) {
		if(surfid != -1) {
			if(s->surf_id == (uint32_t) surfid) {
				fprintf(stderr, "Zordering %d to %d\n", s->surf_id, zorder);
				ias_hmi_zorder_surface(wayland->hmi, s->surf_id, zorder);
			}
		} else if(strcmp(s->name, surfname) == 0) {
			fprintf(stderr, "Zordering %s to %d\n", s->name, zorder);
			ias_hmi_zorder_surface(wayland->hmi, s->surf_id, zorder);
		}
	}

	return 0;
}

static int
surface_shareable(struct wayland *wayland,
		char *surfname, int surfid, int shareable)
{
	struct surf_list *s;

	wl_list_for_each(s, &wayland->surface_list, link) {
		if(surfid != -1) {
			if(s->surf_id == (uint32_t) surfid) {
				fprintf(stderr, "Setting shareable flag of %d to %d\n",
						s->surf_id, shareable);
				if (wayland->hmi) {
					ias_hmi_set_shareable(wayland->hmi, s->surf_id,	shareable);
				}
				if (wayland->ivi_application) {
					ivi_application_set_shareable(wayland->ivi_application, s->surf_id,
							shareable);
				}
			}
		} else if(strcmp(s->name, surfname) == 0) {
			fprintf(stderr, "Setting shareable flag of %s to %d\n", s->name, shareable);
			if (wayland->hmi) {
				ias_hmi_set_shareable(wayland->hmi, s->surf_id,	shareable);
			}
			if (wayland->ivi_application) {
				ivi_application_set_shareable(wayland->ivi_application, s->surf_id,
						shareable);
			}
		}
	}

	return 0;
}

static char *pos_str = "";
static char *size_str = "";
static char *surfname = 0;
static char *surfid_str = 0;

int
main(int argc, char **argv)
{
	int remaining_argc;
	int ret = 0, orig_argc = argc;
	struct surf_list *s, *tmp;

	/* cmdline options */
	int zorder = -1, alpha = -1, visible = -1, surfid = -1, watch_mode = 0;
	int shareable = -1;

	const struct weston_option options[] = {
		{ WESTON_OPTION_STRING, "surfname", 0, &surfname },
		{ WESTON_OPTION_STRING, "surfid", 0, &surfid_str },
		{ WESTON_OPTION_STRING, "pos", 'c', &pos_str },
		{ WESTON_OPTION_STRING, "size", 'm', &size_str },
		{ WESTON_OPTION_INTEGER, "zorder", 0, &zorder },
		{ WESTON_OPTION_INTEGER, "alpha", 0, &alpha },
		{ WESTON_OPTION_INTEGER, "visible", 0, &visible },
		{ WESTON_OPTION_INTEGER, "watch", 0, &watch_mode },
		{ WESTON_OPTION_INTEGER, "shareable", 0, &shareable },
	};

	remaining_argc = parse_options(options, ARRAY_LENGTH(options), &argc, argv);

	if (remaining_argc > 1 ||
			(surfname == 0 && surfid_str == 0 && orig_argc > 1 && !watch_mode)) {
		printf("Usage:\n");
		printf("  surftrl --surfname=<name of the surface> | "
				"--surfid=<id of the surface>\n");
		printf("         [--pos=XxY]\n");
		printf("         [--size=XxY]\n");
		printf("         [--zorder=<0 based number> ]\n");
		printf("         [--alpha=<0 based number>]\n");
		printf("         [--watch=1]\n");
		printf("         [--shareable=<0-disable,1-enable>]\n");
		return -1;
	}

	if(surfid_str) {
		surfid = strtoul(surfid_str, NULL, 0);
	}

	wl_list_init(&wayland.surface_list);

	wayland.display = wl_display_connect(NULL);
	if (!wayland.display) {
		fprintf(stderr, "Failed to open wayland display\n");
		return -1;
	}

	/* Listen for global object broadcasts */
	wayland.registry = wl_display_get_registry(wayland.display);
	wl_registry_add_listener(wayland.registry, &registry_listener, &wayland);
	wl_display_dispatch(wayland.display);
	wl_display_roundtrip(wayland.display);

	if (wayland.ivi_application) {
		ivi_application_get_surface_sharing_info(wayland.ivi_application);
		wl_display_roundtrip(wayland.display);
	}

	/* Handle moving output */
	if (strlen(pos_str) > 0) {
		int x, y;

		if (sscanf(pos_str, "%dx%d", &x, &y) == 2) {
			ret = surface_set_xy(&wayland, surfname, surfid, x, y);
		}
	}

	if (strlen(size_str) > 0) {
		int x, y;

		if (sscanf(size_str, "%dx%d", &x, &y) == 2) {
			ret = surface_resize(&wayland, surfname, surfid, x, y);
		}
	}

	if (zorder != -1) {
		ret = surface_zorder(&wayland, surfname, surfid, zorder);
	}

	if(alpha != -1) {
		ret = surface_alpha(&wayland, surfname, surfid, alpha);
	}

	if(visible != -1) {
		ret = surface_visible(&wayland, surfname, surfid, visible);
	}

	if(shareable != -1) {
		ret = surface_shareable(&wayland, surfname, surfid, shareable);
	}

	wl_display_roundtrip(wayland.display);

	ret = 0;
	while (watch_mode && ret != -1) {
		ret = wl_display_dispatch(wayland.display);
	}

	wl_display_disconnect(wayland.display);

	wl_list_for_each_safe(s, tmp, &wayland.surface_list, link) {
		wl_list_remove(&s->link);
		free(s->name);
		free(s);
	}
	return ret;
}
