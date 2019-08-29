/*
 * Copyright © 2018 Intel Corporation
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

#ifndef _CAPTURE_PROXY_H_
#define _CAPTURE_PROXY_H_

/* #define PROFILE_REMOTE_DISPLAY */

#define NS_IN_US 1000
#define US_IN_SEC 1000000


struct capture_proxy;
struct wl_shm_buffer;

enum capture_proxy_format {
	CP_FORMAT_RGB,
	CP_FORMAT_NV12,
};

struct capture_proxy *
capture_proxy_create(int drm_fd, struct wl_client *client);
void
capture_proxy_set_size(struct capture_proxy *cp, int width, int height);
void
capture_proxy_destroy(struct capture_proxy *cp);
int
capture_proxy_handle_frame(struct capture_proxy *cp,
		struct wl_shm_buffer *shm_buffer,
		int prime_fd, int stride,
		enum capture_proxy_format format,
		uint32_t timestamp);
int
capture_proxy_release_buffer(struct capture_proxy *cp, uint32_t surfid,
								uint32_t bufid, uint32_t imageid);
void
capture_proxy_enable_profiling(struct capture_proxy *cp, int profile_level);
int
capture_proxy_profiling_is_enabled(struct capture_proxy *cp);
void
capture_proxy_set_verbose(struct capture_proxy *cp, int verbose);
int
capture_proxy_verbose_is_enabled(struct capture_proxy *cp);
int
vsync_received(struct capture_proxy *cp);
void
vsync_notify(struct capture_proxy *cp);
void
clear_vsyncs(struct capture_proxy *cp);
int
capture_get_frame_count(struct capture_proxy *cp);
void
capture_proxy_set_resource(struct capture_proxy * const cp, struct wl_resource * const resource);

#endif /* _CAPTURE_PROXY_H_ */
