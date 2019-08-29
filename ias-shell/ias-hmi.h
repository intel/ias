/*
 *-----------------------------------------------------------------------------
 * Filename: ias-hmi.h
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
 *   Intel Automotive Solutions hmi interface for the shell module for Weston.
 *   This is a private header used internally by the IAS shell.
 *-----------------------------------------------------------------------------
 */

#ifndef __IAS_HMI_H__
#define __IAS_HMI_H__

#include <ias-common.h>

/*
 * HMI changed client callback list node
 */
struct hmi_callback {
	struct wl_resource *resource;
	struct wl_list link;
};

struct soc_node {
	uint32_t pid;
	uint32_t soc;
	struct wl_list link;
};

void
bind_ias_hmi(struct wl_client *client,
		void *data, uint32_t version, uint32_t id);

void
bind_ias_metrics(struct wl_client *client,
		void *data, uint32_t version, uint32_t id);

bool global_filter_func(const struct wl_client *client,
						const struct wl_global *global,
						void *data);
#endif
