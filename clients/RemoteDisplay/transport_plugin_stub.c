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
/*
 * This file contains a transport plugin for the remote display wayland
 * client. This plugin is a stub that simply discards the stream of H264
 * frames.
 */
#include <stdio.h>
#include <wayland-util.h>
#include <libdrm/intel_bufmgr.h>
#include <errno.h>
#include <stdlib.h>

#include "transport_plugin.h"
#include "debug.h"

int debug_level = DBG_OFF;


struct private_data {
	int verbose;
};

struct private_data *private_data = NULL;

WL_EXPORT int init(int *argc, char **argv, int verbose)
{
	debug_level = verbose;
	private_data = calloc(1, sizeof(*private_data));

	if (!private_data) {
		return(-ENOMEM);
	}

	INFO("Using stub remote display transport plugin...\n");

	return 0;
}


WL_EXPORT void help(void)
{
	PRINT("\tThe stub plugin takes no parameters.\n");
	PRINT("\tFrame data is simply discarded.\n\n");
}


WL_EXPORT int send_frame(drm_intel_bo *drm_bo, int32_t stream_size, uint32_t timestamp)
{
	INFO("Discarding frame...\n");
	return 0;
}


WL_EXPORT void destroy()
{
	if (private_data) {
		DBG("Freeing plugin private data...\n");
	}

	free(private_data);
}
