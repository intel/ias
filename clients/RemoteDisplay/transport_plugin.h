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
 * This file defines the API for a Remote Display transport plugin.
 */

#ifndef __REMOTE_DISPLAY_TRANSPORT_PLUGIN_H__
#define __REMOTE_DISPLAY_TRANSPORT_PLUGIN_H__

/**
 * Initialisation of the plugin.
 * This must clean up after itself and set *plugin_private_data to
 * NULL if returning failure.
 *
 * @param argc Passed straight through from the main application.
 * @param argv Passed straight through from the main application.
 * @param plugin_private_data Output to hold pointer to plugin private data.
 * @param verbose Flag to indicate trace level.
 * @return Error code. 0 on success.
 */
int init(int *argc, char **argv, void **plugin_private_data, int verbose);

/**
 * Print help information specific to the plugin.
 *
 */
void help(void);

/**
 * Send a frame over the plugin-specific transport.
 *
 * @param plugin_private_data Pointer to plugin private data.
 * @param drm_bo Buffer of data to be sent.
 * @param stream_size Size of data to be sent.
 * @param timestamp RTP-style timestamp for frame.
 * @return Error code. 0 on success.
 */
int send_frame(void *plugin_private_data, drm_intel_bo *drm_bo,
		int32_t stream_size, uint32_t timestamp);

/**
 * Destruction of the plugin.
 * This must clean up any resources that are tracked using
 * plugin_private_data, including the private data itself, and set
 * *plugin_private_data to NULL.
 *
 * @param plugin_private_data Pointer to pointer to plugin private data.
 */
void destroy(void **plugin_private_data);

#endif /* __REMOTE_DISPLAY_TRANSPORT_PLUGIN_H__ */
