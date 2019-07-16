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

#ifndef _REMOTE_DISPLAY_MAIN_H_
#define _REMOTE_DISPLAY_MAIN_H_

#include "udp_socket.h"

struct input_receiver_private_data;
struct encoder_options;

enum encoder_state {
	ENC_STATE_ERROR = -1,
	ENC_STATE_NONE = 0,
	ENC_STATE_INIT = 1,
	ENC_STATE_RUN = 2,
};

struct app_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct ias_hmi *hmi;
	struct ias_relay_input *ias_in;
	int recording;
	int term_signal;
	int verbose;
	int profile;
	uint32_t surfid;
	uint32_t tracksurfid;
	int src_width;
	int src_height;
	int x;
	int y;
	int w;
	int h;
	int output_number;
	int output_origin_x;
	int output_origin_y;
	int output_width;
	int output_height;
	enum encoder_state encoder_state;
	char *transport_plugin;
	char *plugin_fullname;
	struct rd_encoder *rd_encoder;
	struct input_receiver_private_data *ir_priv;

	struct wl_list surface_list;
	struct wl_list output_list;

	/* Encoder init thread */
	pthread_t encoder_init_thread;
	char *surfname;
	char *pname;
	struct encoder_options *enc_options;
	void (*get_sockaddr_fptr)(struct udp_socket **udp_sock, int *num_addr);
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
	struct wl_list link;
};

struct output {
	struct wl_output *output;
	int x, y;
	int width, height;
	struct wl_list link;
};

#endif /* _REMOTE_DISPLAY_MAIN_H_ */
