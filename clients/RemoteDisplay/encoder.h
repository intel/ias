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

#include "ias-shell-client-protocol.h"

#ifndef _REMOTE_DISPLAY_ENCODER_H_
#define _REMOTE_DISPLAY_ENCODER_H_

#define PROFILE_REMOTE_DISPLAY

#define NS_IN_US    1000
#define US_IN_SEC   1000000

struct rd_encoder;
struct wl_shm_buffer;

enum rd_encoder_format {
	RD_FORMAT_RGB,
	RD_FORMAT_NV12,
};

struct encoder_options {
	int encoder_tu;
	int fps;
	int encoder_qp;
};


struct rd_encoder *
rd_encoder_create(const int verbose, char *plugin, int *argc, char **argv);
int
rd_encoder_init(struct rd_encoder * const encoder,
				const int width, const int height,
				const int x, const int y,
				const int w, const int h,
				uint32_t surfid, struct ias_hmi * const hmi,
				struct wl_display *display,
				uint32_t output_number,
				struct encoder_options *options);
void
rd_encoder_destroy(struct rd_encoder *encoder);
int
rd_encoder_frame(struct rd_encoder * const encoder, int32_t va_buffer_handle,
					int32_t prime_fd, int32_t stride0, int32_t stride1, int32_t stride2,
					uint32_t timestamp, enum rd_encoder_format format,
					int32_t frame_number, uint32_t shm_surf_id, uint32_t buf_id,
					uint32_t image_id);
void
rd_encoder_enable_profiling(struct rd_encoder *encoder, int profile_level);
int
vsync_received(struct rd_encoder *encoder);
void
vsync_notify(struct rd_encoder *encoder);
void
clear_vsyncs(struct rd_encoder *encoder);

#endif /* _REMOTE_DISPLAY_ENCODER_H_ */
