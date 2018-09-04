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
 *   VMDisplay server: Header file for socket related functions
 *-----------------------------------------------------------------------------
 */
#ifndef _VMDISPLAY_SERVER_NETWORK_H_
#define _VMDISPLAY_SERVER_NETWORK_H_

#include "vmdisplay-server.h"
#include <pthread.h>

class NetworkCommunicator:public HyperCommunicatorInterface {
public:
	int init(int domid, HyperCommunicatorDirection direction,
		 const char *name);
	void cleanup();
	int recv_data(void *data, int len);
	int send_data(const void *data, int len);
	int recv_metadata(void **surfaces_metadata);

	void listen_for_connection();
private:
	 HyperCommunicatorDirection direction;
	int sock_fd;
	int client_sock_fd;
	pthread_t listener_thread;
	bool running;
	char *metadata;
	int metadata_offset;
};

#endif // _VMDISPLAY_SERVER_NETWORK_H_
