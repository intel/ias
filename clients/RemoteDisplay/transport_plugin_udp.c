/*
 * Copyright (C) 2018 Intel Corporation
 *
 * Intel Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions: The
 * above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <stdio.h>
#include <wayland-util.h>
#include <libdrm/intel_bufmgr.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "../shared/config-parser.h"
#include "../shared/helpers.h"
#include <signal.h>

#include "transport_plugin.h"
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>


struct udpSocket {
	int sockDesc;
	struct sockaddr_in sockAddr;
};


struct private_data {
	int verbose;
	int debug_packetisation;
	struct udpSocket socket;
	char *ipaddr;
	unsigned short port;
	GstElement *pipeline;
	GstElement *appsrc;
};

WL_EXPORT int init(int *argc, char **argv, void **plugin_private_data, int verbose)
{
	printf("Using UDP remote display transport plugin...\n");
	struct private_data *private_data = calloc(1, sizeof(*private_data));

	GstElement *pipeline   = NULL;
	GstElement *appsrc     = NULL;
	GstElement *h264parse  = NULL;
	GstElement *rtph264pay = NULL;
	GstElement *udpsink = NULL;

	*plugin_private_data = (void *)private_data;
	if (private_data) {
		private_data->verbose = verbose;
	} else {
		return(-ENOMEM);
	}

	int port = 0;
	const struct weston_option options[] = {
		{ WESTON_OPTION_STRING,  "ipaddr", 0, &private_data->ipaddr},
		{ WESTON_OPTION_INTEGER, "port", 0, &port},
	};
	parse_options(options, ARRAY_LENGTH(options), argc, argv);
	private_data->port = port;

	if ((private_data->ipaddr != NULL) && (private_data->ipaddr[0] != 0)) {
		printf("Sending to %s:%d.\n", private_data->ipaddr, port);
	} else {
		fprintf(stderr, "Invalid network configuration.\n");
		free(private_data);
		*plugin_private_data = NULL;
		return -1;
	}

	(void) gst_init (NULL, NULL);

	pipeline   = gst_pipeline_new ("pipeline");
	appsrc     = gst_element_factory_make ("appsrc", NULL);
	h264parse  = gst_element_factory_make ("h264parse", NULL);
	rtph264pay = gst_element_factory_make ("rtph264pay", NULL);
	udpsink    = gst_element_factory_make ("udpsink", NULL);
	if (!pipeline || !appsrc || !h264parse || !rtph264pay || !udpsink)
		goto gst_free;

	(void) g_object_set (G_OBJECT (udpsink), "host", private_data->ipaddr, "port", port, NULL);
	(void) gst_bin_add_many(GST_BIN (pipeline), appsrc, h264parse, rtph264pay, udpsink, NULL);
	if (!gst_element_link_many (appsrc, h264parse, rtph264pay, udpsink, NULL))
		goto gst_free_pipeline;

	if (gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
		goto gst_free_pipeline;

	private_data->appsrc   = appsrc;
	private_data->pipeline = pipeline;

	return 0;

gst_free:
	if (appsrc)
		gst_object_unref (GST_OBJECT (appsrc));
	if (h264parse)
		gst_object_unref (GST_OBJECT (h264parse));
	if (rtph264pay)
		gst_object_unref (GST_OBJECT (rtph264pay));
	if (udpsink)
		gst_object_unref (GST_OBJECT (udpsink));

gst_free_pipeline:
	if (pipeline) {
		(void) gst_element_set_state (pipeline, GST_STATE_NULL);
		(void) gst_object_unref (GST_OBJECT (pipeline));
	}

	fprintf(stderr, "Failed to create sender.\n");

	return -1;
}


WL_EXPORT void help(void)
{
	printf("\tThe udp plugin uses the following parameters:\n");
	printf("\t--ipaddr=<ip_address>\t\tIP address of receiver.\n");
	printf("\t--port=<port_number>\t\tPort to use on receiver.\n");
	printf("\n\tThe receiver should be started using:\n");
	printf("\t\"gst-launch-1.0 udpsrc port=<port_number>"
			"! h264parse ! mfxdecode live-mode=true ! mfxsinkelement\"\n");
}

WL_EXPORT int send_frame(void *plugin_private_data, drm_intel_bo *drm_bo,
		int32_t stream_size, uint32_t timestamp)
{
	uint8_t *readptr = drm_bo->virtual;

	struct private_data *private_data = (struct private_data *)plugin_private_data;

	GstBuffer *gstbuf = NULL;
	GstMapInfo gstmap;

	if (!private_data) {
		fprintf(stderr, "No private data!\n");
		goto error;
	}

	if (private_data->verbose) {
		printf("Sending frame over AVB...\n");
	}

	gstbuf = gst_buffer_new_and_alloc (stream_size);
	if (!readptr || !gstbuf)
		goto error;

	if (!gst_buffer_map (gstbuf, &gstmap, GST_MAP_WRITE))
		goto error;

	(void) memcpy (gstmap.data, readptr, stream_size);
	(void) gst_buffer_unmap(gstbuf, &gstmap);

	if (gst_app_src_push_buffer (GST_APP_SRC (private_data->appsrc), gstbuf) != GST_FLOW_OK)
		goto error;

	return 0;

error:
	fprintf(stderr, "Send failed.\n");

	if (gstbuf)
		gst_buffer_unref (gstbuf);

	return -1;
}

WL_EXPORT void destroy(void **plugin_private_data)
{
	struct private_data *private_data = (struct private_data *)*plugin_private_data;

	if (private_data == NULL) {
		return;
	}

	if (private_data->verbose) {
		fprintf(stdout, "Closing network connection...\n");
	}

	if (private_data->pipeline) {
		(void) gst_element_set_state (private_data->pipeline, GST_STATE_NULL);
		(void) gst_object_unref (GST_OBJECT (private_data->pipeline));
	}

	if (private_data->verbose) {
		fprintf(stdout, "Freeing plugin private data...\n");
	}
	free(private_data);
	*plugin_private_data = NULL;
}
