/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-server-network.cpp
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
 *   VMDisplay server: Socket related functions
 *-----------------------------------------------------------------------------
 */

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>
#include "vmdisplay-server-network.h"
#include "vm-shared.h"

static void *listener_thread_func(void *arg)
{
	NetworkCommunicator *comm = (NetworkCommunicator *) arg;

	comm->listen_for_connection();

	return NULL;
}

int NetworkCommunicator::init(int domid, HyperCommunicatorDirection dir,
			      const char *args)
{
	struct hostent *server;
	int portno;
	struct sockaddr_in server_addr;
	/* Local copy of args, as strtok is modyfing it */
	char args_tmp[255];
	char *addr;
	char *port;
	int enable = 1;

	UNUSED(domid);
	strncpy(args_tmp, args, 254);

	addr = strtok(args_tmp, ":");
	if (!addr) {
		printf("Cannot parse parameters\n");
		return -1;
	}

	port = strtok(NULL, ":");
	if (!port) {
		printf("Cannot parse parameters\n");
		return -1;
	}

	client_sock_fd = -1;
	portno = atoi(port);
	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		printf("Cannot open socket\n");
		return -1;
	}

	direction = dir;

	if (dir == HyperCommunicatorInterface::Receiver) {
		metadata_offset = 0;
		metadata = new char[METADATA_BUFFER_SIZE];
		if (metadata == NULL) {
			printf("Cannot allocate memory\n");
			return -1;
		}

		server = gethostbyname(addr);

		if (!server) {
			printf("Cannot find hostname\n");
			cleanup();
			return -1;
		}

		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		memcpy(&server_addr.sin_addr.s_addr,
		       (char *)server->h_addr, server->h_length);
		server_addr.sin_port = htons(portno);
		if (connect
		    (sock_fd, (struct sockaddr *)&server_addr,
		     sizeof(server_addr)) < 0) {
			printf("cannot connect\n");
			cleanup();
			return -1;
		}
	} else {
		setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
			   sizeof(int));

		memset(&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		server_addr.sin_addr.s_addr = inet_addr(addr);
		server_addr.sin_port = htons(portno);

		if (bind
		    (sock_fd, (struct sockaddr *)&server_addr,
		     sizeof(server_addr)) < 0) {
			printf("Cannot bind socket %s:%d\n", addr, portno);
			cleanup();
			return -1;
		}
		listen(sock_fd, 1);

		pthread_create(&listener_thread, NULL, listener_thread_func,
			       this);
	}

	running = true;

	return 0;
}

void NetworkCommunicator::listen_for_connection()
{
	struct sockaddr_in client_addr;
	socklen_t clilen;
	clilen = sizeof(client_addr);

	struct pollfd fds[] = {
		{sock_fd, POLLIN, 0},
	};

	while (running) {
		int ret = poll(fds, 1, 1000);
		if (ret < 0) {
			running = false;
			break;
		} else if (ret > 0) {
			client_sock_fd =
			    accept(sock_fd, (struct sockaddr *)&client_addr,
				   &clilen);
		}
	}
}

void NetworkCommunicator::cleanup()
{
	running = false;
	if (client_sock_fd >= 0) {
		close(client_sock_fd);
		client_sock_fd = -1;
	}

	if (direction == HyperCommunicatorInterface::Receiver && metadata) {
		delete[]metadata;
		metadata = NULL;
	} else if (direction == HyperCommunicatorInterface::Sender) {
		pthread_join(listener_thread, NULL);
	}

	if (sock_fd >= 0) {
		close(sock_fd);
		sock_fd = -1;
	}
}

int NetworkCommunicator::recv_data(void *buffer, int max_len)
{
	int ret = -1;
	if (direction == HyperCommunicatorInterface::Receiver) {
		ret = recv(sock_fd, buffer, max_len, 0);
		if (ret == 0) {
			/* 0 return means socket closed, singal this as error */
			ret = -1;
		}
	}

	return ret;
}

int NetworkCommunicator::recv_metadata(void **surfaces_metadata)
{
	int len;
	int start_offset = -1;
	int end_offset = -1;
	struct vm_header *header;
	int output_num = -1;

	while (1) {
		len = recv_data(&metadata[metadata_offset],
				METADATA_BUFFER_SIZE - metadata_offset);
		if (len < 0)
			return -1;

		metadata_offset += len;

		start_offset = -1;
		end_offset = -1;

		/* Look for frame metadata start/end markers */
		for (int i = 0; i < metadata_offset; i++) {
			int marker = *(int *)(&metadata[i]);

			if (marker == METADATA_STREAM_START) {
				start_offset = i + sizeof(int);
				continue;
			}

			if (marker == METADATA_STREAM_END) {
				end_offset = i;
				if (start_offset != -1)
					break;
			}
		}

		/*
		 * If whole frame metadata was received, parse for which output
		 * it is and return
		 */
		if (start_offset != -1 && end_offset != -1
		    && end_offset > start_offset) {
			header = (struct vm_header *)(&metadata[start_offset]);
			if (header->output < VM_MAX_OUTPUTS) {
				memcpy(surfaces_metadata[header->output],
				       header, end_offset - start_offset);
			}
			output_num = header->output;

			memmove(metadata, &metadata[end_offset + sizeof(int)],
				METADATA_BUFFER_SIZE - (end_offset +
							sizeof(int)));

			metadata_offset -= (end_offset + sizeof(int));

			return output_num;
		}
	}
}

int NetworkCommunicator::send_data(const void *buffer, int len)
{
	int ret = -1;
	if (direction == HyperCommunicatorInterface::Sender
	    && client_sock_fd >= 0) {
		ret = send(client_sock_fd, buffer, len, 0);
		if (ret <= 0) {
			close(client_sock_fd);
			client_sock_fd = -1;
			pthread_create(&listener_thread, NULL,
				       listener_thread_func, this);
			/* 0 return means socket closed, singal this as error */
			ret = -1;
		}
	}

	return ret;
}
