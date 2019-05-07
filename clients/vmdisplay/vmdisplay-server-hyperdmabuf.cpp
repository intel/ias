/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-server-hyperdmabuf.cpp
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

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include "vmdisplay-server-hyperdmabuf.h"

int HyperDMABUFCommunicator::init(int domid,
				  HyperCommunicatorDirection dir,
				  const char *args)
{
	UNUSED(domid);
	UNUSED(args);

	if (dir == HyperCommunicatorInterface::Receiver) {
		hyper_dmabuf_fd = open("/dev/hyper_dmabuf", O_RDWR);
		if (hyper_dmabuf_fd < 0)
			hyper_dmabuf_fd = open("/dev/xen/hyper_dmabuf", O_RDWR);

		if (hyper_dmabuf_fd < 0)
			return -1;
	} else {
		return -1;
	}

	direction = dir;

	metadata = new char[sizeof(struct vm_header) +
			    sizeof(struct vm_buffer_info) +
			    sizeof(struct hyper_dmabuf_event_hdr)];

	hdr = NULL;
	buf_info = NULL;

	/* intializing frame paratmers */
	for (int i = 0; i < VM_MAX_OUTPUTS; i++) {
		last_counter[i] = -1;
		num_buffers[i] = 0;
		offset[i] = sizeof(struct vm_header);
	}

	return 0;
}

void HyperDMABUFCommunicator::cleanup()
{
	if (hyper_dmabuf_fd >= 0) {
		close(hyper_dmabuf_fd);
		hyper_dmabuf_fd = -1;
	}

	delete[]metadata;
	metadata = NULL;
}

int HyperDMABUFCommunicator::recv_data(void *buffer, int max_len)
{
	struct pollfd fds = { };
	int ret = -1;

	fds.fd = hyper_dmabuf_fd;
	fds.events = POLLIN;

	if (direction == HyperCommunicatorInterface::Receiver) {
		do {
			ret = poll(&fds, 1, -1);
			if (ret > 0) {
				if (fds.revents & (POLLERR | POLLNVAL)) {
					errno = EINVAL;
					return -1;
				}
				break;
			}
		} while (ret == -1 && (errno == EINTR || errno == EAGAIN));

		ret = read(hyper_dmabuf_fd, buffer, max_len);
	}

	return ret;
}

int HyperDMABUFCommunicator::send_data(const void *buffer, int len)
{
	UNUSED(buffer);
	UNUSED(len);

	return -1;
}

int HyperDMABUFCommunicator::recv_metadata(void **buffer)
{
	int len;

	struct hyper_dmabuf_event_hdr *event_hdr;

	while (1) {
		/*
		 * In case when we received alreay buffer for next frame,
		 * append its metadata to new frame metadata.
		 */
		if (hdr && hdr->counter != last_counter[hdr->output]) {
			/* Copy metatadat to mmaped file of given output */
			memcpy((char *)buffer[hdr->output] +
			       offset[hdr->output], buf_info,
			       sizeof(struct vm_buffer_info));
			offset[hdr->output] += sizeof(struct vm_buffer_info);
			num_buffers[hdr->output]++;
			last_counter[hdr->output] = hdr->counter;
		} else if (hdr) {
			/* invalidating previous hdr */
			last_counter[hdr->output] = -1;
		}

		do {
			len = recv_data(metadata,
					sizeof(struct vm_header) +
					sizeof(struct vm_buffer_info) +
					sizeof(struct hyper_dmabuf_event_hdr));

		} while(len < 0);

		event_hdr = (struct hyper_dmabuf_event_hdr *)&metadata[0];

		hdr =
		    (struct vm_header *)
		    &metadata[sizeof(struct hyper_dmabuf_event_hdr)];

		buf_info =
		    (struct vm_buffer_info *)
		    &metadata[sizeof(struct hyper_dmabuf_event_hdr) +
			      sizeof(struct vm_header)];

		/* Copy HID from event_hdr */
		buf_info->hyper_dmabuf_id = event_hdr->hid;

		if (hdr->output < VM_MAX_OUTPUTS) {
			/*
			 * Check if we received buffer from the same frame,
			 * if so, append its metadata to frame metadata.
			 */
			if (last_counter[hdr->output] == -1 ||
			    hdr->counter == last_counter[hdr->output]) {
				/* Copy metadata to mmaped file of
				 * given output */
				memcpy((char *)buffer[hdr->output] +
				       offset[hdr->output], buf_info,
				       sizeof(struct vm_buffer_info));
				offset[hdr->output] +=
				    sizeof(struct vm_buffer_info);
				num_buffers[hdr->output]++;
				last_counter[hdr->output] = hdr->counter;
			}

			/*
			 * In case that we received buffer for new frame, or
			 * we received all buffers for current frame, send out
			 * new frame metadata to clients.
			 */
			if (hdr->counter != last_counter[hdr->output] ||
			    num_buffers[hdr->output] >= hdr->n_buffers) {
				memcpy(buffer[hdr->output], hdr,
				       sizeof(struct vm_header));
				num_buffers[hdr->output] = 0;
				offset[hdr->output] = sizeof(struct vm_header);
				return hdr->output;
			}
		}
	}
}
