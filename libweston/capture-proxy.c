/*
 * Copyright © 2018 Intel Corporation. All Rights Reserved.
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
 */

#include "config.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pthread.h>

#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>
#include <va/va_enc_h264.h>

#include "compositor.h"
#include "capture-proxy.h"
#include "ias-shell-server-protocol.h"
#include "../shared/timespec-util.h"

/* The queuing system in the client limits the reasonable number of
 * outstanding frames. */
#define MAX_FRAMES_IN_FLIGHT 3

struct capture_proxy {
	int drm_fd;
	int profile_capture;
	int verbose_capture;

	int frame_count;
	int num_vsyncs;
	int num_frames_in_flight;

	int width;
	int height;

	VADisplay va_dpy;

	/* Resource for client that asked us to start capturing, to which
	 * we will send buffer handles. */
	struct wl_resource *resource;
	/* We need to listen for the resource being destroyed, so that we
	 * don't try to use it when it no longer exists. */
	struct wl_listener resource_listener;
	/* Store client in order to flush events when sending HMI messages
	 * this improves performance. */
	struct wl_client *client;
};


static void
handle_resource_destroyed(struct wl_listener *listener, void *data)
{
	struct capture_proxy *cp =
		container_of(listener, struct capture_proxy, resource_listener);
	cp->resource = NULL;
}


struct capture_proxy *
capture_proxy_create(const int drm_fd, struct wl_client *client)
{
	struct capture_proxy *cp;
	VAStatus status;
	int major, minor;

	cp = zalloc(sizeof(*cp));
	if (cp == NULL) {
		return NULL;
	}

	cp->client = client;

	cp->va_dpy = vaGetDisplayDRM(drm_fd);
	if (!cp->va_dpy) {
		weston_log("[capture proxy]: Failed to create VA display.\n");
		free(cp);
		return NULL;
	}

	status = vaInitialize(cp->va_dpy, &major, &minor);
	if (status != VA_STATUS_SUCCESS) {
		weston_log("[capture proxy]: Failed to initialize display.\n");
		vaTerminate(cp->va_dpy);
		free(cp);
		return NULL;
	}


	wl_list_init(&cp->resource_listener.link);
	cp->resource_listener.notify = handle_resource_destroyed;
	cp->drm_fd = drm_fd;

	weston_log("[capture proxy]: Capture proxy created.\n");
	return cp;
}

void
capture_proxy_set_size(struct capture_proxy *cp, int width, int height)
{
	if (cp) {
		cp->width = width;
		cp->height = height;
	}
}

void
capture_proxy_destroy(struct capture_proxy *cp)
{
	wl_list_remove(&cp->resource_listener.link);
	close(cp->drm_fd);

	if (cp->resource) {
		wl_resource_destroy(cp->resource);
	}
	vaTerminate(cp->va_dpy);
	free(cp);
	weston_log("[capture proxy]: Capture proxy destroyed.\n");
}


void
capture_proxy_set_resource(struct capture_proxy * const cp,
		struct wl_resource * const resource)
{
	if (cp) {
		assert(cp->resource == NULL);
		cp->resource = resource;
		if (cp->resource) {
			weston_log("[capture proxy]: Setting listener for recorder resource destruction...\n");
			wl_resource_add_destroy_listener(cp->resource, &cp->resource_listener);
		}
	}
}


static int
capture_proxy_shm_frame(struct capture_proxy * const cp,
		struct wl_shm_buffer * const shm_buffer, int stride,
		enum capture_proxy_format format, uint32_t timestamp)
{
	VASurfaceID src_surface;
	VAStatus status;
	void *surface_p = NULL;
	void *shm_buffer_data = NULL;
	VAImage rgb_image;
	VABufferInfo buf_info;
	uint32_t shm_format;

	wl_shm_buffer_begin_access(shm_buffer);
	shm_buffer_data = wl_shm_buffer_get_data(shm_buffer);

	if (shm_buffer_data == NULL) {
		weston_log("[capture proxy]: Failed to get data pointer from shm_buffer.\n");
		goto error_data_pointer;
	}

	shm_format = wl_shm_buffer_get_format(shm_buffer);
	if (shm_format != WL_SHM_FORMAT_XRGB8888 && shm_format != WL_SHM_FORMAT_ARGB8888 &&
		shm_format != WL_SHM_FORMAT_RGB565) {
		weston_log("[capture proxy]: shm buffer not of RGB32 type.\n");
		goto error_data_pointer;
	}

	status = vaCreateSurfaces(cp->va_dpy, VA_RT_FORMAT_RGB32,
			wl_shm_buffer_get_width(shm_buffer),
			wl_shm_buffer_get_height(shm_buffer),
			&src_surface, 1, NULL, 0);
	if (status != VA_STATUS_SUCCESS) {
		weston_log("[capture proxy]: Failed to create shm source surface.\n");
		goto error_data_pointer;
	}

	status = vaDeriveImage(cp->va_dpy, src_surface, &rgb_image);
	if (status != VA_STATUS_SUCCESS) {
		weston_log("[capture proxy]: Failed to get shm source image.\n");
		goto error_source_image;
	}

	status = vaMapBuffer(cp->va_dpy, rgb_image.buf, &surface_p);
	if (status != VA_STATUS_SUCCESS) {
		weston_log("[capture proxy]: Failed to map shm source image.\n");
		goto error_map_image;
	}

	/* Shared memory buffer and the VAImage may not have the same stride. */
	if (rgb_image.pitches[0] == (uint32_t)wl_shm_buffer_get_stride(shm_buffer)) {
		memcpy(surface_p, shm_buffer_data,
			wl_shm_buffer_get_height(shm_buffer) * wl_shm_buffer_get_stride(shm_buffer));
	} else {
		int i;
		const char *src = (char *)shm_buffer_data;
		char *dst = (char *)surface_p;
		int32_t stride = wl_shm_buffer_get_stride(shm_buffer);
		int32_t height = wl_shm_buffer_get_height(shm_buffer);
		size_t count = (size_t)stride <
			(size_t)(rgb_image.pitches[0]) ?
			(size_t)stride :
			(size_t)(rgb_image.pitches[0]);

		for (i = 0; i < height; i++) {
			memcpy(dst, src, count);
			src += stride;
			dst += rgb_image.pitches[0];
		}
	}

	status = vaUnmapBuffer(cp->va_dpy, rgb_image.buf);
	if (status != VA_STATUS_SUCCESS) {
		weston_log("[capture proxy]: Failed to unmap image.\n");
	}

	memset(&buf_info, 0, sizeof(buf_info));
	buf_info.mem_type = VA_SURFACE_ATTRIB_MEM_TYPE_KERNEL_DRM;
	status = vaAcquireBufferHandle(cp->va_dpy, rgb_image.buf, &buf_info);
	wl_shm_buffer_end_access(shm_buffer);

	ias_hmi_send_raw_buffer_handle(cp->resource, buf_info.handle, timestamp,
		cp->frame_count, rgb_image.pitches[0], 0, 0, 0,
		cp->width, cp->height, src_surface, rgb_image.buf,
		rgb_image.image_id);

	return 0;

error_map_image:
	vaDestroyImage(cp->va_dpy, rgb_image.image_id);
error_source_image:
	vaDestroySurfaces(cp->va_dpy, &src_surface, 1);
error_data_pointer:
	wl_shm_buffer_end_access(shm_buffer);
	return -1;
}

int
capture_proxy_handle_frame(struct capture_proxy * const cp,
		struct wl_shm_buffer * const shm_buffer, int prime_fd, int stride,
		enum capture_proxy_format format, uint32_t timestamp)
{
	if (cp->resource == NULL) {
		weston_log("[capture proxy]: No client to receive frame.\n");
		if (prime_fd >= 0) {
			close(prime_fd);
		}
		return -1;
	}

	if (cp->num_frames_in_flight > MAX_FRAMES_IN_FLIGHT) {
		weston_log("[capture proxy]: Too many frames in flight.\n");
		if (prime_fd >= 0) {
			close(prime_fd);
		}
		return EBUSY;
	}

	if (prime_fd >= 0) {
		ias_hmi_send_raw_buffer_fd(cp->resource, prime_fd, timestamp,
				cp->frame_count, stride,
				0, 0, format, cp->width, cp->height);
		close(prime_fd);
	} else if (shm_buffer) {
		capture_proxy_shm_frame(cp, shm_buffer, stride, format, timestamp);
	} else {
		weston_log("[capture proxy]: Unsupported buffer type.\n");
	}
	cp->frame_count++;
	cp->num_frames_in_flight++;

	return 0;
}

int
capture_proxy_release_buffer(struct capture_proxy *cp, uint32_t surfid,
		uint32_t bufid, uint32_t imageid)
{
	VAStatus status = VA_STATUS_SUCCESS;

	if (cp) {
		if (surfid) {
			VABufferID buf_id = bufid;
			VASurfaceID surface_id = surfid;
			VAImageID image_id = imageid;

			status = vaReleaseBufferHandle(cp->va_dpy, buf_id);
			if (status != VA_STATUS_SUCCESS) {
				weston_log("[capture proxy release]: Failed to release handle for buffer %u.\n",
						bufid);
			}

			status = vaDestroyImage(cp->va_dpy, image_id);
			if (status != VA_STATUS_SUCCESS) {
				weston_log("[capture proxy]: Failed to destroy image.\n");
			}

			status = vaDestroySurfaces(cp->va_dpy, &surface_id, 1);
			if (status != VA_STATUS_SUCCESS) {
				weston_log("[capture proxy release]: Failed to destroy surface %u.\n",
						surfid);
			}
		}
	} else {
		weston_log("[capture proxy release]: No capture proxy\n");
		return -1;
	}

	cp->num_frames_in_flight--;
	return 0;
}


int
capture_proxy_profiling_is_enabled(struct capture_proxy *cp)
{
	if (cp) {
		return cp->profile_capture;
	} else {
		return 0;
	}
}

void
capture_proxy_enable_profiling(struct capture_proxy *cp, int profile_level)
{
	if (cp) {
		if (profile_level) {
			cp->profile_capture = profile_level;
		} else {
			cp->profile_capture = 0;
		}
	}
}

int
capture_proxy_verbose_is_enabled(struct capture_proxy *cp)
{
	if (cp) {
		return cp->verbose_capture;
	} else {
		return 0;
	}
}

void
capture_proxy_set_verbose(struct capture_proxy *cp, int verbose)
{
	if (cp) {
		cp->verbose_capture = verbose;
	}
}

int
vsync_received(struct capture_proxy *cp)
{
	if (cp) {
		return cp->num_vsyncs;
	} else {
		return 0;
	}
}

void
vsync_notify(struct capture_proxy *cp)
{
	if (cp) {
		cp->num_vsyncs++;
	}
}

void
clear_vsyncs(struct capture_proxy *cp)
{
	if (cp) {
		cp->num_vsyncs = 0;
	}
}

int
capture_get_frame_count(struct capture_proxy *cp)
{
	if (cp) {
		return cp->frame_count;
	} else {
		return 0;
	}
}
