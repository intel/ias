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
 * client. This plugin uses the AVB StreamHandler to send the stream of
 * H264 frames over Ethernet.
 */
#include <stdio.h>
#include <wayland-util.h>
#include <libdrm/intel_bufmgr.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../shared/config-parser.h"
#include "../shared/helpers.h"

#include "transport_plugin.h"
#include "debug.h"

int debug_level = DBG_OFF;

struct private_data {
	int verbose;
	int debug_packetisation;
	char *avb_channel;
	char *packet_path;
	int dump_packets;
	FILE *packet_fp;
	GstElement *pipeline;
	GstElement *appsrc;
};

struct private_data *private_data = NULL;

WL_EXPORT int init(int *argc, char **argv, int verbose)
{
	debug_level = verbose;
	private_data = calloc(1, sizeof(*private_data));

	GstElement *pipeline    = NULL;
	GstElement *appsrc      = NULL;
	GstElement *h264parse   = NULL;
	GstElement *rtph264pay  = NULL;
	GstElement *avbh264sink = NULL;

	if (!private_data) {
		return(-ENOMEM);
	}
	INFO("Using avb remote display transport plugin...\n");

	const struct weston_option options[] = {
		{ WESTON_OPTION_INTEGER, "debug_packets", 0, &private_data->debug_packetisation},
		{ WESTON_OPTION_STRING,  "packet_path", 0, &private_data->packet_path},
		{ WESTON_OPTION_INTEGER, "dump_packets", 0, &private_data->dump_packets},
		{ WESTON_OPTION_STRING,  "channel", 0, &private_data->avb_channel},
	};

	parse_options(options, ARRAY_LENGTH(options), argc, argv);

	if (private_data->avb_channel == NULL) {
		private_data->avb_channel = "media_transport.avb_streaming.1";
		DBG("Defaulting to avb channel %s.\n", private_data->avb_channel);
	}

	if ((private_data->dump_packets != 0) && (private_data->packet_path == 0)) {
		ERROR("No packet path provided - see help (remotedisplay --plugin=avb --help).\n");
		free(private_data);
		return -1;
	}

	if (private_data->packet_path) {
		char *tmp = calloc(PATH_MAX, sizeof(char));

		if (tmp) {
			unsigned int max_write = 0;
			strncpy(tmp, private_data->packet_path, PATH_MAX);
			max_write = PATH_MAX - strlen(tmp) - 1;
			strncat(tmp, "packets.rtp", max_write);
		} else {
			ERROR("Failed to create packet file path.\n");
			free(private_data);
			return -1;
		}
		private_data->packet_path = tmp;

		/*
		 * branch gstreamer pipeline to dump RTP packets to a file
		 * i.e.) appsrc ! h264parse ! rtph264pay ! tee name=t ! avbh264sink t. ! filesink location=packet_path
		 */
	}

	DBG("Create AVB sender...\n");

	(void) gst_init (NULL, NULL);

	pipeline   = gst_pipeline_new ("pipeline");
	appsrc     = gst_element_factory_make ("appsrc", NULL);
	h264parse  = gst_element_factory_make ("h264parse", NULL);
	rtph264pay = gst_element_factory_make ("rtph264pay", NULL);
	avbh264sink  = gst_element_factory_make ("avbvideosink", NULL);
	if (!pipeline || !appsrc || !h264parse || !rtph264pay || !avbh264sink)
		goto gst_free;

	(void) g_object_set (G_OBJECT (avbh264sink), "stream-name", private_data->avb_channel, NULL);
	(void) gst_bin_add_many(GST_BIN (pipeline), appsrc, h264parse, rtph264pay, avbh264sink, NULL);
	if (!gst_element_link_many (appsrc, h264parse, rtph264pay, avbh264sink, NULL))
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
	if (avbh264sink)
		gst_object_unref (GST_OBJECT (avbh264sink));

gst_free_pipeline:
	if (pipeline) {
		(void) gst_element_set_state (pipeline, GST_STATE_NULL);
		(void) gst_object_unref (GST_OBJECT (pipeline));
	}

	ERROR("Failed to create sender.\n");

	return -1;
}


WL_EXPORT void help(void)
{
	PRINT("\tThe avb plugin uses the following parameters:\n");
	PRINT("\t--packet_path=<packet_path>\tset path for local capture of RTP packets to a file\n"
			"\t--dump_packets=1\t\tappend a copy of each RTP packet to <packet_path>/packets.rtp\n");
	PRINT("\t--ufipc=1\t\t\tvideo frames will be split into RTP packets and the packets sent over ufipc\n");
	PRINT("\t--channel=<avb_channel>\t\tufipc channel over which to send"
			" the image stream (e.g. 'media_transport.avb_streaming.3')\n");
	PRINT("\n\tNote that the default avb_channel is 'media_transport.avb_streaming.1'.\n\n");
	PRINT("\n\tThe receiver should be started using:\n");
	PRINT("\t\"gst-launch-1.0 avbvideosrc stream-type=\"rtp-h264\" stream-name=\"media_transport.avb_streaming.7 "
			"! rtph264depay ! h264parse ! mfxdecode live-mode=true ! mfxsinkelement\"\n");
}


WL_EXPORT int send_frame(drm_intel_bo *drm_bo, int32_t stream_size, uint32_t timestamp)
{
	uint8_t *readptr = drm_bo->virtual;

	GstBuffer *gstbuf = NULL;
	GstMapInfo gstmap;

	if (!private_data) {
		ERROR("No private data!\n");
		goto error;
	}

	DBG("Sending frame over AVB...\n");

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
	ERROR("Send failed.\n");

	if (gstbuf)
		gst_buffer_unref (gstbuf);

	return -1;
}


WL_EXPORT void destroy()
{
	if (private_data == NULL) {
		ERROR("Invalid avb plugin private data passed to destroy.\n");
		return;
	}

	if (private_data->pipeline) {
		(void) gst_element_set_state (private_data->pipeline, GST_STATE_NULL);
		(void) gst_object_unref (GST_OBJECT (private_data->pipeline));
	}

	if (private_data->packet_fp) {
		fclose(private_data->packet_fp);
		private_data->packet_fp = 0;
	}

	free(private_data->packet_path);

	DBG("Freeing avb plugin private data...\n");
	free(private_data);
}
