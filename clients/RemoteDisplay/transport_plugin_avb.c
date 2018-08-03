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
 * H264 frames over Ethernet. The plugin is responsible for breaking the
 * frames up into RTP packets and then passing them to the StreamHandler.
 */
#include <stdio.h>
#include <wayland-util.h>
#include <libdrm/intel_bufmgr.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <netinet/in.h>
#include <linux/limits.h>

#include <media_transport/avb_ufipc_bridge/iasavbbridge.h>
#include "../shared/config-parser.h"
#include "../shared/helpers.h"

#include "transport_plugin.h"


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


struct private_data {
	int verbose;
	int debug_packetisation;
	ias_avbbridge_sender *sender;
	char *avb_channel;
	char *packet_path;
	int to_ufipc;
	int dump_packets;
	FILE *packet_fp;
};


WL_EXPORT int init(int *argc, char **argv, void **plugin_private_data,
			int verbose)
{
	printf("Using avb remote display transport plugin...\n");
	struct private_data *private_data = calloc(1, sizeof(*private_data));
	*plugin_private_data = (void *)private_data;
	if (private_data) {
		private_data->verbose = verbose;
	} else {
		return(-ENOMEM);
	}

	const struct weston_option options[] = {
		{ WESTON_OPTION_INTEGER, "debug_packets", 0, &private_data->debug_packetisation},
		{ WESTON_OPTION_INTEGER, "ufipc", 0, &private_data->to_ufipc},
		{ WESTON_OPTION_STRING,  "packet_path", 0, &private_data->packet_path},
		{ WESTON_OPTION_INTEGER, "dump_packets", 0, &private_data->dump_packets},
		{ WESTON_OPTION_STRING,  "channel", 0, &private_data->avb_channel},
	};

	parse_options(options, ARRAY_LENGTH(options), argc, argv);

	if (private_data->avb_channel == NULL) {
		private_data->avb_channel = "media_transport.avb_streaming.1";
		if (private_data->verbose) {
			printf("Defaulting to avb channel %s.\n", private_data->avb_channel);
		}
	}

	if ((private_data->dump_packets != 0) && (private_data->packet_path == 0)) {
		fprintf(stderr, "No packet path provided - see help (remotedisplay --plugin=avb --help).\n");
		free(private_data);
		*plugin_private_data = NULL;
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
			fprintf(stderr, "Failed to create packet file path.\n");
			free(private_data);
			*plugin_private_data = NULL;
			return -1;
		}
		private_data->packet_path = tmp;
	}

	if (private_data->to_ufipc) {
		if (private_data->verbose) {
			printf("Create AVB sender...\n");
		}
		private_data->sender = ias_avbbridge_create_sender(private_data->avb_channel);
		if(!private_data->sender) {
			fprintf(stderr, "Failed to create sender.\n");
			free(private_data->packet_path);
			free(private_data);
			*plugin_private_data = NULL;
			return -1;
		}
	}

	return 0;
}


WL_EXPORT void help(void)
{
	printf("\tThe avb plugin uses the following parameters:\n");
	printf("\t--packet_path=<packet_path>\tset path for local capture of RTP packets to a file\n"
		"\t--dump_packets=1\t\tappend a copy of each RTP packet to <packet_path>/packets.rtp\n");
	printf("\t--ufipc=1\t\t\tvideo frames will be split into RTP packets and the packets sent over ufipc\n");
	printf("\t--channel=<avb_channel>\t\tufipc channel over which to send"
		" the image stream (e.g. 'media_transport.avb_streaming.3')\n");
	printf("\n\tNote that the default avb_channel is 'media_transport.avb_streaming.1'.\n\n");
}


/* This function assumes that we are allowed to overwrite the twelve bytes
 * that precede the incoming payload address. */
static int
send_packet(uint8_t *payload, size_t size, uint32_t timestamp,
		int marker_bit, struct private_data *private_data)
{
	ias_avbbridge_buffer p;
	uint8_t *rtpBase8 = (uint8_t *)(payload - RTP_HEADER_SIZE);
	uint16_t *rtpBase16 = (uint16_t *)(payload - RTP_HEADER_SIZE);
	uint32_t *rtpBase32 = (uint32_t *)(payload - RTP_HEADER_SIZE);
	static uint16_t sequence_number = 1;

	if (size > RTP_PAYLOAD_SIZE) {
		fprintf(stderr, "Payload size %d too large (>1388).\n", (int)size);
		return 1;
	}

	p.size = size + RTP_HEADER_SIZE;
	p.data = rtpBase8;
  if (private_data->debug_packetisation) {
    printf("Sending packet of size %d...\n", (int)(p.size));
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

	/* Send packet over the wire... */
	if (private_data->to_ufipc) {
    int result = ias_avbbridge_send_packet_H264(private_data->sender, &p);
		if (IAS_AVB_RES_OK != result) {
			fprintf(stderr, "Failed to send packet: %d\n", result);
			return 1;
		}
	}

	/* Dump packet to file, using the same layout as gstreamer's RTP
	 * stream file format... */
	if (private_data->dump_packets) {
		size_t count = 0;
		uint16_t packet_size = (uint16_t)(p.size);

		if (private_data->packet_fp == 0) {
			printf("Opening packet output file: %s.\n", private_data->packet_path);
			private_data->packet_fp = fopen(private_data->packet_path, "wb");
			if (private_data->packet_fp == 0) {
				fprintf(stderr, "Failed to open packet output file: %s.\n",
					private_data->packet_path);
				return 1;
			}
		}

		/* Match the endianness of packet size expected by gstreamer... */
		char* pstreamheader = (char *)(&packet_size);
		count = fwrite((void *)(&pstreamheader[1]), sizeof(char), sizeof(char),
			private_data->packet_fp);
		if (count != sizeof(char)) {
			fprintf(stderr, "Error writing header to file. Tried to write "
					"%zu bytes, %zu bytes actually written.\n", sizeof(char),
					count);
		}
		count = fwrite((void *)(&pstreamheader[0]), sizeof(char), sizeof(char),
				private_data->packet_fp);
		if (count != sizeof(char)) {
			fprintf(stderr, "Error writing header to file. Tried to write "
					"%zu bytes, %zu bytes actually written.\n", sizeof(char), count);
		}

		count = fwrite(p.data, sizeof(char), p.size, private_data->packet_fp);
		if(count != p.size) {
			fprintf(stderr, "Error dumping packet to file. Tried to write "
					"%zu bytes, %zu bytes actually written.\n", p.size, count);
		}
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


WL_EXPORT int send_frame(void *plugin_private_data, drm_intel_bo *drm_bo,
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
	uint8_t nal_header;
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

			if (private_data->debug_packetisation) {
				nal_header = readptr[0];
				printf("Skipping 00 00 00 01 marker and writing %d "
					"bytes + 12 byte header\n", sps_size);
				printf("SPS - nal_type = 0x%x\n",
					nal_header & NAL_TYPE_MASK);
			}
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


WL_EXPORT void destroy(void **plugin_private_data)
{
	struct private_data *private_data = (struct private_data *)*plugin_private_data;
	if (private_data == NULL) {
		fprintf(stderr, "Invalid avb plugin private data passed to destroy.\n");
		return;
	}

	if (private_data->packet_fp) {
		ias_avbbridge_destroy_sender(private_data->sender);
	}

	if (private_data->packet_fp) {
		fclose(private_data->packet_fp);
		private_data->packet_fp = 0;
	}

	free(private_data->packet_path);

	if (private_data && private_data->verbose) {
		printf("Freeing avb plugin private data...\n");
	}
	free(private_data);
	*plugin_private_data = NULL;
}
