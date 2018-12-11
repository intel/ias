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
#include <gst/gst.h>
#include <glib.h>
#include <gst/video/videooverlay.h>
#include <gst/video/gstvideosink.h>

#define XCASESTRING(A) case A: return #A;

char *GetT(unsigned int a)
{
	switch(a) {
		XCASESTRING(GST_STREAM_STATUS_TYPE_CREATE );
		XCASESTRING(GST_STREAM_STATUS_TYPE_ENTER  );
		XCASESTRING(GST_STREAM_STATUS_TYPE_LEAVE  );
		XCASESTRING(GST_STREAM_STATUS_TYPE_DESTROY);
		XCASESTRING(GST_STREAM_STATUS_TYPE_START  );
		XCASESTRING(GST_STREAM_STATUS_TYPE_PAUSE  );
		XCASESTRING(GST_STREAM_STATUS_TYPE_STOP   );
	}
	return "???";
}

char *GetM(unsigned int a)
{
	switch (a) {
		XCASESTRING(  GST_MESSAGE_EOS               );
		XCASESTRING(  GST_MESSAGE_ERROR             );
		XCASESTRING(  GST_MESSAGE_WARNING           );
		XCASESTRING(  GST_MESSAGE_INFO              );
		XCASESTRING(  GST_MESSAGE_TAG               );
		XCASESTRING(  GST_MESSAGE_BUFFERING         );
		XCASESTRING(  GST_MESSAGE_STATE_CHANGED     );
		XCASESTRING(  GST_MESSAGE_STATE_DIRTY       );
		XCASESTRING(  GST_MESSAGE_STEP_DONE         );
		XCASESTRING(  GST_MESSAGE_CLOCK_PROVIDE     );
		XCASESTRING(  GST_MESSAGE_CLOCK_LOST        );
		XCASESTRING(  GST_MESSAGE_NEW_CLOCK         );
		XCASESTRING(  GST_MESSAGE_STRUCTURE_CHANGE  );
		XCASESTRING(  GST_MESSAGE_STREAM_STATUS     );
		XCASESTRING(  GST_MESSAGE_APPLICATION       );
		XCASESTRING(  GST_MESSAGE_ELEMENT           );
		XCASESTRING(  GST_MESSAGE_SEGMENT_START     );
		XCASESTRING(  GST_MESSAGE_SEGMENT_DONE      );
		XCASESTRING(  GST_MESSAGE_DURATION_CHANGED  );
		XCASESTRING(  GST_MESSAGE_LATENCY           );
		XCASESTRING(  GST_MESSAGE_ASYNC_START       );
		XCASESTRING(  GST_MESSAGE_ASYNC_DONE        );
		XCASESTRING(  GST_MESSAGE_REQUEST_STATE     );
		XCASESTRING(  GST_MESSAGE_STEP_START        );
		XCASESTRING(  GST_MESSAGE_QOS               );
		XCASESTRING(  GST_MESSAGE_PROGRESS          );
		XCASESTRING(  GST_MESSAGE_TOC               );
		XCASESTRING(  GST_MESSAGE_RESET_TIME        );
		XCASESTRING(  GST_MESSAGE_STREAM_START      );
		XCASESTRING(  GST_MESSAGE_NEED_CONTEXT      );
		XCASESTRING(  GST_MESSAGE_HAVE_CONTEXT      );
	}
	return "???";
}

typedef struct _AppData
{
    GstElement *pipeline;
    GstElement *source;
    GstElement *demuxer;
    GstElement *parser;
    GstElement *decoder;
    GstElement *sink;
    GMainLoop *loop;
    gboolean looping;
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
            //g_print("unhandle %s\n", GetM(GST_MESSAGE_TYPE(msg)));
            break;
    }

    return TRUE;
}


static void on_pad_added (GstElement *element, GstPad *pad, gpointer data)
{
    GstPad *sinkpad;
    GstElement *decoder = (GstElement *) data;

    sinkpad = gst_element_get_static_pad (decoder, "sink");

    gst_pad_link (pad, sinkpad);

    gst_object_unref (sinkpad);
}

static void usage()
{
    g_printerr ("\nUsage:\n\t mediaplayer <Video filename> <x,y,w,h> <loop>\n\n");
    g_printerr ("This is a H264 content player that plays a content repeatedly"
            "with the same surface id for 2019 CES demo.\n"
            "\tex) mediaplayer sample.mp4 0,0,1920,1080 loop\n");
    exit(-1);
}

int main (int argc, char *argv[])
{
    AppData appdata;
    GstBus *bus;
    guint bus_watch_id;
    guint x = 0, y = 0, w = 3440, h = 1440;

    /* Initialisation */
    gst_init (&argc, &argv);

    appdata.loop = g_main_loop_new (NULL, FALSE);

    /* Create gstreamer elements */
    appdata.pipeline = gst_pipeline_new ("video-player");
    appdata.source   = gst_element_factory_make ("filesrc", "file-source");
    appdata.demuxer  = gst_element_factory_make ("qtdemux", "qt-demuxer");
    appdata.parser   = gst_element_factory_make ("h264parse", "h264-parser");
    appdata.decoder  = gst_element_factory_make ("msdkh264dec", "msdk-decoder");
    appdata.sink     = gst_element_factory_make ("glimagesink", "video-output");

    if (!appdata.pipeline || !appdata.source || !appdata.demuxer || !appdata.parser || !appdata.decoder || !appdata.sink) {
        g_printerr ("One element could not be created. Exiting.\n");
        return -1;
    }

    /* Check input arguments */
    if (argc == 4) {
        if (!strcmp(argv[3], "loop"))
            appdata.looping = TRUE;
        else
            appdata.looping = FALSE;
        sscanf(argv[2], "%d,%d,%d,%d", &x, &y, &w, &h);
    }
    else if (argc == 3) {
		appdata.looping = FALSE;
        sscanf(argv[2], "%d,%d,%d,%d", &x, &y, &w, &h);
    }
    else if (argc != 2) {
        usage();
    }

    /* Set up the pipeline */

    /* Set the input filename to the source element */
    g_object_set (G_OBJECT (appdata.source), "location", argv[1], NULL);

    /* Set render ractangle for window */
    gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY (appdata.sink), x, y, w, h);

    /* Add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (appdata.pipeline));
    bus_watch_id = gst_bus_add_watch (bus, bus_call, &appdata);
    gst_object_unref (bus);

    /* Add all elements into the pipeline */
    /* file-source | qt-demuxer | h264-parser | msdk-decoder | video-output */
    gst_bin_add_many (GST_BIN (appdata.pipeline),
            appdata.source, appdata.demuxer, appdata.parser, appdata.decoder, appdata.sink, NULL);

    /* Link the elements together */
    gst_element_link (appdata.source, appdata.demuxer);
    gst_element_link_many (appdata.parser, appdata.decoder, appdata.sink, NULL);
    g_signal_connect (appdata.demuxer, "pad-added", G_CALLBACK (on_pad_added), appdata.parser);

    /* Set the pipeline to "playing" state*/
    g_print ("Now playing: %s\n", argv[1]);
    gst_element_set_state (appdata.pipeline, GST_STATE_PLAYING);

    /* Iterate */
    g_print ("Running...\n");
    g_main_loop_run (appdata.loop);


    /* Out of the main loop, clean up nicely */
    g_print ("Returned, stopping playback\n");
    gst_element_set_state (appdata.pipeline, GST_STATE_NULL);

    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (appdata.pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (appdata.loop);

    return 0;
}
