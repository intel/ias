/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-server-hyperdmabuf.h
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
 *   VMDisplay server: hyper_dmabuf functions
 *-----------------------------------------------------------------------------
 */

#ifndef _VMDISPLAY_SERVER_HYPERDMABUF_H_
#define _VMDISPLAY_SERVER_HYPERDMABUF_H_

#include "vmdisplay-server.h"
#include "vm-shared.h"

class HyperDMABUFCommunicator:public HyperCommunicatorInterface {
public:
	int init(int domid, HyperCommunicatorDirection direction,
		 const char *name);
	void cleanup();
	int recv_data(void *data, int len);
	int send_data(const void *data, int len);
	int recv_metadata(void **buffers);

private:
	 HyperCommunicatorDirection direction;
	int hyper_dmabuf_fd;
	char *metadata;
	struct vm_header *hdr;
	struct vm_buffer_info *buf_info;
	int offset[VM_MAX_OUTPUTS];
	int last_counter;
};

#endif // _VMDISPLAY_SERVER_HYPERDMABUF_H_
