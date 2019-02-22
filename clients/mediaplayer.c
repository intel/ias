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

#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <gst/gst.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gst/video/videooverlay.h>
#include <gst/video/gstvideosink.h>
#include <gst/allocators/allocators.h>
#include <gst/base/gstbasesrc.h>

#define CLOCK_START_TIME 5 // seconds
#define CLOCK_INTERVAL 2   // seconds

typedef struct _AppData
{
	GstElement *pipeline;
	GstElement *source;
	GstElement *demuxer;
	GstElement *videoqueue;
	GstElement *parser;
	GstElement *decoder;
	GstElement *textoverlay;
	GstElement *videosink;
	GstElement *audioqueue;
	GstElement *decodebin;
	GstElement *audioconvert;
	GstElement *audioresample;
	GstElement *audiosink;
	GMainLoop *loop;
	gboolean looping;
	gboolean audio;
	guint printtext;
	gchar text[64];
} AppData;

static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
	AppData *appdata = (AppData *) data;
	GMainLoop *loop = appdata->loop;

	switch (GST_MESSAGE_TYPE (msg)) {

		case GST_MESSAGE_EOS:
			g_print ("End of stream\n");
			if (appdata->looping) {
				/* restart playback if at end */
				if (!gst_element_seek(appdata->pipeline,
							1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
							GST_SEEK_TYPE_SET, 0,
							GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
					g_printerr("Seek failed and stop playback!\n");
					g_main_loop_quit (loop);
				}
			}
			else
				g_main_loop_quit (loop);
			break;

		case GST_MESSAGE_ERROR:
			{
				gchar  *debug;
				GError *error;

				gst_message_parse_error (msg, &error, &debug);
				g_free (debug);

				g_printerr ("Error: %s\n", error->message);
				g_error_free (error);

				g_main_loop_quit (loop);
				break;
			}
		default:
			break;
	}

	return TRUE;
}

static gboolean timeout_reached (GstClock *clock, GstClockTime time, GstClockID id, gpointer data)
{
	AppData *appdata = (AppData *) data;

	if (appdata->printtext == 2) {
		g_object_set (G_OBJECT (appdata->textoverlay), "text", "", NULL);
		appdata->printtext = 0;
	}
	else {
		g_object_set (G_OBJECT (appdata->textoverlay), "text", appdata->text, NULL);
		appdata->printtext = 2;
	}

	return TRUE;
}

static void on_pad_added (GstElement *element, GstPad *pad, gpointer data)
{
	GstPad *sinkpad;
	AppData *appdata = (AppData *) data;

	sinkpad = gst_element_get_static_pad (appdata->parser, "sink");

	gst_pad_link (pad, sinkpad);
	gst_object_unref (sinkpad);
}

static void demux_on_pad_added (GstElement *element, GstPad *pad, gpointer data)
{
	GstPad *sinkpad = NULL;
	AppData *appdata = (AppData *) data;
	gchar *name;

	name = gst_pad_get_name (pad);
	if (g_strrstr(name, "video_0")) {
		sinkpad = gst_element_get_static_pad (appdata->videoqueue, "sink");
		gst_pad_link (pad, sinkpad);
	} else if (g_strrstr(name, "audio_0")) {
		sinkpad = gst_element_get_static_pad (appdata->audioqueue, "sink");
		gst_pad_link (pad, sinkpad);
	}

	if (sinkpad != NULL)
		gst_object_unref (sinkpad);

	g_free (name);
}

static void decode_on_pad_added (GstElement *element, GstPad *pad, gpointer data)
{
	AppData *appdata = (AppData *) data;
	GstCaps *caps;
	GstStructure *str;
	GstPad *audiopad;

	/* only link once */
	audiopad = gst_element_get_static_pad (appdata->audioconvert, "sink");
	if (GST_PAD_IS_LINKED (audiopad)) {
		g_object_unref (audiopad);
		return;
	}

	/* check media type */
	caps = gst_pad_query_caps (pad, NULL);
	str = gst_caps_get_structure (caps, 0);
	if (!g_strrstr (gst_structure_get_name (str), "audio")) {
		gst_caps_unref (caps);
		gst_object_unref (audiopad);
		return;
	}
	gst_caps_unref (caps);

	gst_pad_link (pad, audiopad);
	g_object_unref (audiopad);
}

static void usage (void)
{
	g_printerr ("\nUsage:\n\t mediaplayer -f <Video filename> -r <x,y,w,h> -l <0:off, 1:loop> -a <0:w/o audio, 1:w/ audio> -t <0:off, 1:always, 2:blink>\n\n");
	g_printerr ("This is a H264 content player that plays a content repeatedly "
			"with the same surface id for 2019 CES demo.\n"
			"ex) mediaplayer -f sample.mp4 -r 0,0,1920,1080 -l 1 -a 1 -t 1\n");
	exit(-1);
}

static gboolean pad_support_dmabuf (GstElement *element)
{
	gboolean ret = FALSE;
	GstPad *pad = NULL;
	GstCaps *caps = NULL;
	guint i;

	pad = gst_element_get_static_pad (element, "src");
	if (!pad) {
		g_printerr ("[%s] Could not retrieve pad\n", __FUNCTION__);
		goto exit;
	}

	/* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
	caps = gst_pad_get_current_caps (pad);
	if (!caps)
		caps = gst_pad_query_caps (pad, NULL);

	if (!gst_caps_is_any (caps) && !gst_caps_is_empty (caps)) {
		for (i = 0; i < gst_caps_get_size (caps); i++) {
			GstCapsFeatures *features = gst_caps_get_features (caps, i);

			if (gst_caps_features_is_any (features) ||
					gst_caps_features_contains (features,
						GST_CAPS_FEATURE_MEMORY_DMABUF)) {
				ret = TRUE;
				break;
			}
		}
	}

exit:
	if (caps)
		gst_caps_unref (caps);
	if (pad)
		gst_object_unref (pad);

	return ret;
}

static gboolean video_pipeline_link(gpointer data)
{
	AppData *appdata = (AppData *) data;
	GstCaps *caps = NULL;
	gboolean ret = FALSE;

	if (appdata->audio) {
		if (gst_element_link_many (appdata->videoqueue, appdata->parser, \
					appdata->decoder, appdata->textoverlay, appdata->videosink, NULL) != TRUE) {
			g_printerr("Error: video linking with audio\n");
			goto exit;
		}
	} else {
		if (gst_element_link_many (appdata->parser, \
					appdata->decoder, appdata->textoverlay, appdata->videosink, NULL) != TRUE) {
			g_printerr("Error: video linking w/o audio\n");
			goto exit;
		}
	}

	if (pad_support_dmabuf(appdata->decoder) == TRUE) {
		caps = gst_caps_new_simple ("video/x-raw",
				"format", G_TYPE_STRING, "NV12",
				NULL);
		gst_element_unlink(appdata->decoder, appdata->textoverlay);
		gst_caps_set_features (caps, 0,
				gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DMABUF, NULL));
		ret = gst_element_link_filtered(appdata->decoder, appdata->textoverlay, caps);
	}
	else {
		ret = TRUE;
	}

exit:
	if (caps)
		gst_caps_unref (caps);

	return ret;
}

int main (int argc, char *argv[])
{
	AppData appdata = {0,};
	GstBus *bus;
	guint bus_watch_id;
	gchar *rect = NULL;
	gchar *filename = NULL;
	guint x = 0, y = 0, w = 3440, h = 1440;
	GOptionContext *ctx;
	GError *err = NULL;
	GstClock *clock = NULL;
	GstClockID id = 0;
	GstClockTime start_time = 0;
	GstClockTime interval = CLOCK_INTERVAL;
	GOptionEntry options[] = {
		{"loop", 'l', 0, G_OPTION_ARG_INT, &appdata.looping,
			N_("Play video indefinitely"), NULL},
		{"rect", 'r', 0, G_OPTION_ARG_STRING, &rect,
			N_("Set render rectangle, <x,y,w,h>"), NULL},
		{"file", 'f', 0, G_OPTION_ARG_STRING, &filename,
			N_("Media file location"), NULL},
		{"audio", 'a', 0, G_OPTION_ARG_INT, &appdata.audio,
			N_("Play audio as well"), NULL},
		{"text", 't', 0, G_OPTION_ARG_INT, &appdata.printtext,
			N_("Play audio as well"), NULL},
		{NULL}
	};

	/* Initialisation */
	appdata.looping = FALSE;
	appdata.audio = FALSE;
	ctx = g_option_context_new ("ARGUMENTS");
	g_option_context_add_main_entries (ctx, options, NULL);
	g_option_context_add_group (ctx, gst_init_get_option_group ());
	if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
		if (err)
			g_printerr ("Error initializing: %s\n", GST_STR_NULL (err->message));
		else
			g_printerr ("Error initializing: Unknown error!\n");
		g_clear_error (&err);
		g_option_context_free (ctx);
		return -1;
	}
	g_option_context_free (ctx);

	if (filename == NULL)
		usage();

	if (rect != NULL)
		sscanf(rect, "%d,%d,%d,%d", &x, &y, &w, &h);

	appdata.loop = g_main_loop_new (NULL, FALSE);

	/* Create gstreamer elements */
	appdata.pipeline = gst_pipeline_new ("video-player");
	appdata.source   = gst_element_factory_make ("filesrc", "file-source");
	if (appdata.audio) {
		g_print ("Playing audio as well\n");
		appdata.audioqueue   = gst_element_factory_make ("queue", "audio_queue");
		appdata.decodebin = gst_element_factory_make ("decodebin", "audio_decode");
		appdata.audioconvert   = gst_element_factory_make ("audioconvert", "audio_convert");
		appdata.audioresample   = gst_element_factory_make ("audioresample", "audio_resample");
		appdata.audiosink   = gst_element_factory_make ("autoaudiosink", "audio_sink");
		appdata.videoqueue   = gst_element_factory_make ("queue", "video_queue");
	} else {
		g_print ("Playing video only\n");
	}
	appdata.demuxer  = gst_element_factory_make ("qtdemux", "qt-demuxer");
	appdata.parser   = gst_element_factory_make ("h264parse", "h264-parser");
	appdata.decoder  = gst_element_factory_make ("msdkh264dec", "msdk-decoder");
	appdata.textoverlay = gst_element_factory_make ("textoverlay", "text-overlay");
	appdata.videosink     = gst_element_factory_make ("glimagesink", "video-output");

	if (appdata.audio) {
		if (!appdata.pipeline || !appdata.source || !appdata.demuxer || \
				!appdata.parser || !appdata.decoder || !appdata.textoverlay || !appdata.videosink || \
				!appdata.audioqueue || !appdata.decodebin || !appdata.audioconvert || \
				!appdata.audioresample || !appdata.audiosink) {
			g_printerr ("One element could not be created. Exiting.\n");
			return -1;
		}
	} else {
		if (!appdata.pipeline || !appdata.source || !appdata.demuxer || \
				!appdata.parser || !appdata.decoder || !appdata.textoverlay || !appdata.videosink) {
			g_printerr ("One element could not be created. Exiting.\n");
			return -1;
		}
	}

	/* Set up the pipeline */
	/* Set the input filename to the source element */
	g_object_set (G_OBJECT (appdata.source), "location", filename, NULL);

	/* set text to textoverlay in format of [hostname] - [filename] */
	if (appdata.printtext) {
		gethostname(appdata.text, 10);
		sprintf(appdata.text+strlen(appdata.text), " - %s", basename(filename));
		g_object_set (G_OBJECT (appdata.textoverlay), "text", appdata.text, NULL);
	}

	/* Set render ractangle for window */
	gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY (appdata.videosink), x, y, w, h);

	/* Add a message handler */
	bus = gst_pipeline_get_bus (GST_PIPELINE (appdata.pipeline));
	bus_watch_id = gst_bus_add_watch (bus, bus_call, &appdata);
	gst_object_unref (bus);

	/* Add all elements into the pipeline */
	if (appdata.audio) {
		gst_bin_add_many (GST_BIN (appdata.pipeline), appdata.source, \
				appdata.demuxer, appdata.videoqueue, appdata.parser, \
				appdata.decoder, appdata.textoverlay, appdata.videosink, appdata.audioqueue, \
				appdata.decodebin, appdata.audioconvert, appdata.audioresample, \
				appdata.audiosink, NULL);
	} else {
		gst_bin_add_many (GST_BIN (appdata.pipeline), appdata.source, \
				appdata.demuxer, appdata.parser, appdata.decoder, \
				appdata.textoverlay, appdata.videosink, NULL);
	}

	/* Link the elements */
	/* Link#1 : source -> demux */
	if (gst_element_link_many (appdata.source, appdata.demuxer, NULL) != TRUE) {
		g_printerr(" Error: main linking\n");
		goto exit;
	}

	if (appdata.audio) {
		/* Link#2 - video : queue -> parse -> decode -> sink */
		if (video_pipeline_link(&appdata) != TRUE) {
			goto exit;
		}

		/* Link#3 - audio : audioconvert -> audioresample -> audiosink */
		if ( gst_element_link_many (appdata.audioconvert, \
					appdata.audioresample, appdata.audiosink, NULL) != TRUE ) {
			g_printerr(" Error: audio linking\n");
			goto exit;
		}

		/* Link#4 : audioqueue ->  decodebin */
		if ( gst_element_link (appdata.audioqueue, appdata.decodebin) != TRUE ) {
			g_printerr(" Error: decodebin linking\n");
			goto exit;
		}
	} else {
		/* Link#2 - video : parse -> decode -> sink */
		if (video_pipeline_link(&appdata) != TRUE) {
			goto exit;
		}
	}

	if (appdata.audio) {
		g_signal_connect (appdata.demuxer, "pad-added", \
				G_CALLBACK (demux_on_pad_added), &appdata);
		g_signal_connect (appdata.decodebin, "pad-added", \
				G_CALLBACK (decode_on_pad_added), &appdata);
	} else {
		g_signal_connect (appdata.demuxer, "pad-added", \
				G_CALLBACK (on_pad_added), &appdata);
	}

	/* Set the pipeline to "playing" state*/
	g_print ("Now playing: %s\n", filename);
	gst_element_set_state (appdata.pipeline, GST_STATE_PLAYING);

	if (appdata.printtext == 2) {
		clock = gst_pipeline_get_pipeline_clock(GST_PIPELINE (appdata.pipeline));
		if (clock != NULL) {
			GstClockTime cur;

			cur = gst_clock_get_time(clock);
			start_time = cur + (CLOCK_START_TIME * GST_SECOND);
			id = gst_clock_new_periodic_id(clock, start_time, interval * GST_SECOND);
			if (id == NULL) {
				g_printerr("Error: failed to get clock id\n");
				goto exit;
			}
			gst_clock_id_wait_async(id, (GstClockCallback) timeout_reached, &appdata, NULL);
			gst_clock_id_wait(id, NULL);
		}
	}

	/* Iterate */
	g_print ("Running...\n");
	g_main_loop_run (appdata.loop);


	/* Out of the main loop, clean up nicely */
	g_print ("Returned, stopping playback\n");
	gst_element_set_state (appdata.pipeline, GST_STATE_NULL);

exit:
	/* unref clock resources */
	if (id != NULL) {
		gst_clock_id_unschedule (id);
		gst_clock_id_unref(id);
	}
	if (clock != NULL)
		gst_object_unref (clock);

	g_print ("Deleting pipeline\n");
	gst_object_unref (GST_OBJECT (appdata.pipeline));
	g_source_remove (bus_watch_id);
	g_main_loop_unref (appdata.loop);

	return 0;
}
