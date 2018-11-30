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
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/time.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include "../shared/config-parser.h"
#include "../shared/helpers.h"
#include "transport_plugin.h"


#define TO_Mb(bytes) ((bytes)/1024/1024*8)
#define BENCHMARK_INTERVAL 1
#define RTP_BUFFER_SIZE 1400
#define RTP_HEADER_SIZE (3*4)
#define RTP_PAYLOAD_SIZE (RTP_BUFFER_SIZE - RTP_HEADER_SIZE)
#define NAL_HEADER_SIZE 1
#define FU_HEADER_SIZE 1
#define FU_INDICATOR_SIZE 1
#define NAL_MARKER_SIZE 3
#define SPS_PPS_MARKER_SIZE 4
#define NRI_MASK 0x60
#define NAL_TYPE_MASK 0x1F
#define FU_A_TYPE 28

struct udpSocket {
	int sockDesc; struct sockaddr_in sockAddr;
};


struct private_data {
	int verbose;
	int debug_packetisation;
	struct udpSocket socket;
	char *ipaddr;
	char *tp;
	unsigned short port;
	GstElement *pipeline;
	GstElement *appsrc;
	uint32_t benchmark_time, frames, total_stream_size;
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
		{ WESTON_OPTION_STRING,  "tp", 0, &private_data->tp},
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

	if(!private_data->tp) {
		private_data->tp = strdup("native");
	}

	if(!strcmp(private_data->tp, "gst")) {
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
		printf("Using gstreamer based transport\n");

	} else if(!strcmp(private_data->tp, "native")) {

		private_data->socket.sockDesc = socket(AF_INET, SOCK_DGRAM, 0);
		if (private_data->socket.sockDesc < 0) {
			fprintf(stderr, "Socket creation failed.\n");
			free(private_data);
			*plugin_private_data = NULL;
			return -1;
		}

		private_data->socket.sockAddr.sin_addr.s_addr = inet_addr(private_data->ipaddr);
		private_data->socket.sockAddr.sin_family = AF_INET;
		private_data->socket.sockAddr.sin_port = htons(private_data->port);

		printf("Using native transport\n");
	}

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
	printf("\t--tp=<gst/native> (Optional)\t\tTransport mechanism to use."
			" Either native (default) or gstreamer based\n");
	printf("\n\tThe receiver should be started using:\n");
	printf("\t\"gst-launch-1.0 udpsrc port=<port_number>"
			"! h264parse ! mfxdecode live-mode=true ! mfxsinkelement\"\n");
}

/* This function assumes that we are allowed to overwrite the twelve bytes
 * that precede the incoming payload address. */
static int
send_packet(uint8_t *payload, size_t size, uint32_t timestamp,
		int marker_bit, struct private_data *private_data)
{
	uint8_t *rtpBase8 = (uint8_t *)(payload - RTP_HEADER_SIZE);
	uint16_t *rtpBase16 = (uint16_t *)(payload - RTP_HEADER_SIZE);
	uint32_t *rtpBase32 = (uint32_t *)(payload - RTP_HEADER_SIZE);
	static uint16_t sequence_number = 1;

	if (size > RTP_PAYLOAD_SIZE) {
		fprintf(stderr, "Payload size %d too large (>1388).\n", (int)size);
		return 1;
	}

	/* Fill packet using h.264 RTP style */
	rtpBase8[0] = 0x80; /* RFC 1889 version(2) */
	if (marker_bit) {
		rtpBase8[1] = 0xE0; /* marker bit + payload type: hardcoded H264 */
	} else {
		rtpBase8[1] = 0x60; /* no marker bit + payload type: hardcoded H264 */
	}

	rtpBase32[1] = htonl(timestamp);
	rtpBase32[2] = htonl(0x4120db95); /* SSRC hard-coded */
	rtpBase16[1] = htons(sequence_number++);

	if (!private_data) {
		fprintf(stderr, "No private data!\n");
		return -1;
	}

	int rval = sendto(private_data->socket.sockDesc, rtpBase8, size + RTP_HEADER_SIZE, 0,
			(struct sockaddr *) &private_data->socket.sockAddr, sizeof(private_data->socket.sockAddr));

	if (rval <= 0) {
		fprintf(stderr, "Send failed with %m\n");
	}

	return 0;
}

static int
get_ps_write_size(uint8_t *readptr)
{
	int i = 0;
	int found_end = 0;

	while (!found_end) {
		if (readptr[i] == 0x00) {
			if (readptr[i+1] == 0x00) {
				if (readptr[i+2] == 0x00) {
					if (readptr[i+3] == 0x01) {
						found_end = 1;
						continue;
					}
				} else if (readptr[i+2] == 0x01) {
					found_end = 1;
					continue;
				}
			}
		}
		i++;
	}
	return i;
}

static int send_frame_gst(void *plugin_private_data, drm_intel_bo *drm_bo,
		int32_t stream_size, uint32_t timestamp)
{
	uint8_t *readptr = drm_bo->virtual;

	struct private_data *private_data = (struct private_data *)plugin_private_data;

	GstBuffer *gstbuf = NULL;
	GstMapInfo gstmap;
	struct timeval tv;
	uint32_t time;

	if (!private_data) {
		fprintf(stderr, "No private data!\n");
		goto error;
	}

	if (private_data->verbose) {
			gettimeofday(&tv, NULL);
			time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
			if (private_data->frames == 0)
				private_data->benchmark_time = time;
			if (time - private_data->benchmark_time >= (BENCHMARK_INTERVAL * 1000)) {
				printf("%d frames in %d seconds: %f fps, %f Mb sent\n",
						private_data->frames,
						BENCHMARK_INTERVAL,
						(float) private_data->frames / BENCHMARK_INTERVAL,
						 TO_Mb((float)(private_data->total_stream_size / BENCHMARK_INTERVAL)));
				private_data->benchmark_time = time;
				private_data->frames = 0;
				private_data->total_stream_size = 0;
			}
			private_data->frames++;
			private_data->total_stream_size += stream_size;
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

static int send_frame_native(void *plugin_private_data, drm_intel_bo *drm_bo,
		int32_t stream_size, uint32_t timestamp)
{
	int num_packets = 0;
	int bytes_written = 0;
	uint16_t count = 0;
	uint8_t rtp_buffer[RTP_BUFFER_SIZE];
	uint8_t *rtp_payload = &rtp_buffer[RTP_HEADER_SIZE];
	uint8_t *fu_start;
	uint8_t *readptr = drm_bo->virtual;
	int32_t write_size = stream_size;
	uint8_t nal_header = 0;
	int start = 1, end = 0;
	/* Allow for FU indicator size and FU header size */
	const int step = RTP_PAYLOAD_SIZE - FU_HEADER_SIZE - FU_INDICATOR_SIZE;
	int spspps = 0;
	uint8_t *bufdata;
	volatile sig_atomic_t send_packages;
	int err = 0;

	struct private_data *private_data = (struct private_data *)plugin_private_data;

	if (!private_data) {
		fprintf(stderr, "No private data!\n");
		return -1;
	}

	if (private_data->verbose) {
		printf("Sending frame over AVB...\n");
	}

	/* SPS and PPS are preceded by the bytes 00 00 00 01 (in our case).
	 * 00 00 01 means we have an ordinary NAL unit containing an h264
	 * encoded frame. This may well be different and more nuanced with
	 * other implementations. */
	bufdata = ((uint8_t *)(drm_bo->virtual));
	if (bufdata[0] == 0x00) {
		if (bufdata[1] == 0x00) {
			if (bufdata[2] == 0x00) {
				if (bufdata[3] == 0x01) {
					/* SPS or PPS */
					if (private_data->debug_packetisation) {
						printf("SPS or PPS frame\n");
					}
					spspps = 1;
				} else {
					fprintf(stderr, "Invalid start of stream.\n");
					return 1;
				}
			} else if (bufdata[2] == 0x01) {
				/* Ordinary NAL unit containing an h264 encoded frame */
				if (private_data->debug_packetisation) {
					printf("00 00 01 - start of frame?\n");
				}
			} else {
				fprintf(stderr, "Invalid frame.\n");
				return 1;
			}
		}
	}

	send_packages = 1;
	while (send_packages){
		num_packets++;
		if (spspps == 1) {
			int32_t sps_size, pps_size;
			/* Skip 00 00 00 01 */
			readptr += SPS_PPS_MARKER_SIZE;
			sps_size = get_ps_write_size(readptr);

			/* Send SPS... */
			memcpy(rtp_payload, readptr, sps_size);
			err = send_packet(rtp_payload, sps_size, timestamp, 1,
					private_data);
			bytes_written += sps_size;
			if (err) {
				fprintf(stderr, "Warning: Sending SPS packet returned %d.\n",
						err);
				err = 0;
			}

			/* Advance readptr by SPS + second 00 00 00 01 marker... */
			readptr += sps_size + SPS_PPS_MARKER_SIZE;
			pps_size = get_ps_write_size(readptr);

			if (private_data->debug_packetisation) {
				printf("Skipping second 00 00 00 01 marker and writing"
				   " %d bytes + 12 byte header\n", pps_size);
				nal_header = readptr[0];
				printf("PPS - nal_type = 0x%x\n",
					nal_header & NAL_TYPE_MASK);
			}

			/* Send PPS... */
			/* We avoid doing a memcpy where possible, writing
			 * the headers to the mapped buffer instead. (This
			 * trashes the previous 12 bytes in the buffer, but we
			 * don't need to worry about that.) */
			if (bytes_written >= RTP_HEADER_SIZE) {
				err = send_packet(readptr, pps_size, timestamp,
					1, private_data);
			} else {
				memcpy(rtp_payload, readptr, pps_size);
				err = send_packet(rtp_payload, pps_size, timestamp, 1,
						private_data);
			}
			bytes_written += pps_size;
			if (err) {
				fprintf(stderr, "Warning: Sending PPS packet returned %d.\n",
						err);
				err = 0;
			}

			/* There will be a NAL unit in buffer after SPS and PPS. */
			readptr += pps_size;
			write_size = stream_size -
				(sps_size + pps_size + 2 * SPS_PPS_MARKER_SIZE);
			spspps = 0;
		}

		if (start == 1) {
			/* Skip 00 00 01 marker before NAL unit starts... */
			readptr += NAL_MARKER_SIZE;
			write_size -= NAL_MARKER_SIZE;
			nal_header = readptr[0];
			if (private_data->debug_packetisation) {
				printf("Skipped 00 00 01 marker.\n");
				printf("nal_type = 0x%x\n",
					nal_header & NAL_TYPE_MASK);
			}
		}

		/* NAL packetisation into Fragmentation Units (FUs)...*/
		if (RTP_PAYLOAD_SIZE >= write_size) {
			/* Stream is smaller than a packet, no FUs. */
			if (private_data->debug_packetisation) {
				printf("Small packet, only writing %d bytes.\n", write_size);
			}
			if (bytes_written >= RTP_HEADER_SIZE) {
				err = send_packet(readptr, write_size, timestamp,
					1, private_data);
			} else {
				memcpy(rtp_payload, readptr, write_size);
				err = send_packet(rtp_payload, write_size, timestamp, 1,
						private_data);
			}
			bytes_written += write_size;
			if (err) {
				fprintf(stderr, "Warning: Sending small packet returned %d.\n",
						err);
				err = 0;
			}
			send_packages = 0;
		} else if (step * count < write_size) {
			count++;

			/* FU indicator - as per section 5.8 of rfc6184. */
			*rtp_payload = (nal_header & NRI_MASK) | FU_A_TYPE;

			/* FU header - as per section 5.8 of rfc6184. */
			*(rtp_payload + 1) = (start << 7) | (end << 6) |
				(nal_header & NAL_TYPE_MASK);

			if (private_data->debug_packetisation) {
				printf("%s FU. Indicator 0x%x Header 0x%x\n",
					start ? "First" : "Middle",
					/* Sometimes fprintf tries to interpret
					 * these as 64-bit ints. The 0xFF & fixes that. */
					0xFF & *rtp_payload, 0xFF & *(rtp_payload + 1));
			}

			if (start) {
				/* Skip the NAL header because the info is
				 * already in the FU indicator and header. */
				readptr += NAL_HEADER_SIZE;
				start = 0;
			}

			if (bytes_written >= RTP_HEADER_SIZE) {
				/* We have the marker bits free as well as the header size. */
				fu_start = readptr - FU_HEADER_SIZE - FU_INDICATOR_SIZE;
				fu_start[0] = *rtp_payload;
				fu_start[1] = *(rtp_payload + 1);
				err = send_packet(fu_start, RTP_PAYLOAD_SIZE,
					timestamp, 0, private_data);
			} else {
				memcpy(rtp_payload + FU_HEADER_SIZE + FU_INDICATOR_SIZE,
					readptr, step);
				err = send_packet(rtp_payload, RTP_PAYLOAD_SIZE,
					timestamp, 0, private_data);
			}
			bytes_written += step;
			if (err) {
				fprintf(stderr, "Warning: Sending FU packet returned %d.\n",
						err);
				err = 0;
			}

			readptr += step;
		} else {
			/* Last FU... */
			int bytes_left = write_size - (step*(count-1)) - NAL_HEADER_SIZE;

			end = 1;
			*rtp_payload = (nal_header & NRI_MASK) | FU_A_TYPE;
			*(rtp_payload + 1) = (start << 7) | (end << 6) |
						(nal_header & NAL_TYPE_MASK);

			if (private_data->debug_packetisation) {
				printf("Last FU. Indicator: 0x%x, Header: 0x%x, size: %d\n",
					*rtp_payload, *(rtp_payload + 1), bytes_left);
			}

			if (bytes_written >= RTP_HEADER_SIZE) {
				/* We have the marker bits free as well as the header size. */
				fu_start = readptr - FU_HEADER_SIZE - FU_INDICATOR_SIZE;
				fu_start[0] = *rtp_payload;
				fu_start[1] = *(rtp_payload + 1);
				err = send_packet(fu_start, bytes_left
					+ FU_HEADER_SIZE + FU_INDICATOR_SIZE,
					timestamp, 1, private_data);
			} else {
				memcpy(rtp_payload + FU_HEADER_SIZE
					+ FU_INDICATOR_SIZE,
					readptr, bytes_left);
				err = send_packet(rtp_payload, bytes_left
					+ FU_HEADER_SIZE + FU_INDICATOR_SIZE,
					timestamp, 1, private_data);
			}
			bytes_written += bytes_left;
			if (err) {
				fprintf(stderr, "Warning: Sending final packet returned %d.\n",
						err);
				err = 0;
			}

			send_packages = 0;
		}
	}

	if (private_data->verbose || private_data->debug_packetisation) {
		printf("Packets for frame = %d packets.\n", num_packets);
	}
	return 0;
}

WL_EXPORT int send_frame(void *plugin_private_data, drm_intel_bo *drm_bo,
		int32_t stream_size, uint32_t timestamp)
{
	struct private_data *private_data = (struct private_data *)plugin_private_data;

	if(!private_data) {
		return -1;
	} else {
		return !strcmp(private_data->tp, "gst")
				? send_frame_gst(plugin_private_data, drm_bo,
						stream_size, timestamp)
				: send_frame_native(plugin_private_data, drm_bo,
						stream_size, timestamp);
	}
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

	if(!strcmp(private_data->tp, "gst") && private_data->pipeline) {
		(void) gst_element_set_state (private_data->pipeline, GST_STATE_NULL);
		(void) gst_object_unref (GST_OBJECT (private_data->pipeline));
	} else if(!strcmp(private_data->tp, "native")) {
		close(private_data->socket.sockDesc);
		private_data->socket.sockDesc = -1;
		memset(&private_data->socket.sockAddr, 0, sizeof(private_data->socket.sockAddr));
	}

	if (private_data->verbose) {
		fprintf(stdout, "Freeing plugin private data...\n");
	}

	free(private_data->ipaddr);
	free(private_data->tp);
	free(private_data);
	*plugin_private_data = NULL;
}
