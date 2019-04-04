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
#include <wayland-util.h>
#include <libdrm/intel_bufmgr.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <libweston/config-parser.h>
#include "../shared/helpers.h"

#include "transport_plugin.h"
#include "debug.h"

int debug_level = DBG_OFF;


struct tcpSocket {
	int sockDesc;
	struct sockaddr_in sockAddr;
};


struct private_data {
	int verbose;
	struct tcpSocket socket;
	char *ipaddr;
	unsigned short port;
};

struct private_data *private_data = NULL;

WL_EXPORT int init(int *argc, char **argv, int verbose)
{
	debug_level = verbose;
	private_data = calloc(1, sizeof(*private_data));

	if (!private_data) {
		return(-ENOMEM);
	}
	INFO("Using TCP remote display transport plugin...\n");

	int port = 0;
	const struct weston_option options[] = {
		{ WESTON_OPTION_STRING,  "ipaddr", 0, &private_data->ipaddr},
		{ WESTON_OPTION_INTEGER, "port", 0, &port},
	};
	parse_options(options, ARRAY_LENGTH(options), argc, argv);
	private_data->port = port;

	if ((private_data->ipaddr != NULL) && (private_data->ipaddr[0] != 0)) {
		INFO("Sending to %s:%d.\n", private_data->ipaddr, port);
	} else {
		ERROR("Invalid network configuration.\n");
		free(private_data);
		return -1;
	}

	private_data->socket.sockDesc = socket(AF_INET, SOCK_STREAM, 0);
	if (private_data->socket.sockDesc < 0) {
		ERROR("Socket creation failed.\n");
		free(private_data);
		return -1;
	}

	private_data->socket.sockAddr.sin_addr.s_addr = inet_addr(private_data->ipaddr);
	private_data->socket.sockAddr.sin_family = AF_INET;
	private_data->socket.sockAddr.sin_port = htons(private_data->port);
	if (connect(private_data->socket.sockDesc,
			(struct sockaddr *) &private_data->socket.sockAddr,
			sizeof(private_data->socket.sockAddr)) < 0) {
		ERROR("Error connecting to receiver.\n");
		free(private_data);
		return -1;
	}

	return 0;
}


WL_EXPORT void help(void)
{
	PRINT("\tThe tcp plugin uses the following parameters:\n");
	PRINT("\t--ipaddr=<ip_address>\t\tIP address of receiver.\n");
	PRINT("\t--port=<port_number>\t\tPort to use on receiver.\n");
	PRINT("\n\tThe receiver should be started using:\n");
	PRINT("\t\"gst-launch-1.0 tcpserversrc  host=<ip_address> port=<port_number> ! h264parse ! mfxdecode live-mode=true ! mfxsinkelement\"\n");
}


WL_EXPORT int send_frame(drm_intel_bo *drm_bo, int32_t stream_size, uint32_t timestamp)
{
	uint8_t *bufdata = (uint8_t *)(drm_bo->virtual);

	if (private_data == NULL) {
		ERROR("Private data is null!\n");
		return -1;
	}

	DBG("Sending frame over TCP...\n");

	int rval = write(private_data->socket.sockDesc, bufdata, stream_size);

	if (rval <= 0) {
		ERROR("Send failed.\n");
	}

	return 0;
}

WL_EXPORT void destroy()
{
	if (private_data == NULL) {
		return;
	}

	DBG("Closing network connection...\n");
	close(private_data->socket.sockDesc);
	private_data->socket.sockDesc = -1;
	memset(&private_data->socket.sockAddr, 0, sizeof(private_data->socket.sockAddr));

	DBG("Freeing plugin private data...\n");
	free(private_data);
}
