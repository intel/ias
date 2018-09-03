/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-parser.c
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
 *   VMDisplay parse metadata
 *-----------------------------------------------------------------------------
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <poll.h>
#include "hyper_dmabuf.h"

#include "vmdisplay-parser.h"
#include "vmdisplay.h"

struct vm_header vbt_header;
struct vm_buffer_info *vbt;
uint32_t surf_width = 0;
uint32_t surf_height = 0;
hyper_dmabuf_id_t hyper_dmabuf_id = { 0, {0, 0, 0} };

uint32_t surf_stride[3];
uint32_t surf_offset[3];
uint32_t surf_tile_format = 0;
uint32_t surf_format = 0;
uint32_t surf_rotation = 0;
int32_t surf_disp_x = 0;
int32_t surf_disp_y = 0;
int32_t surf_disp_w = 0;
int32_t surf_disp_h = 0;
int32_t disp_w = 0;
int32_t disp_h = 0;

uint32_t show_window = 0;
extern int use_event_poll;
extern unsigned int pipe_id;

#define VMDISPLAY_VBT_VERSION 3

#define ALIGN(x, y) ((x + y - 1) & ~(y - 1))

int parse_event_metadata(int fd, int *counter)
{
	int ret;
	struct pollfd fds = { 0 };
	hyper_dmabuf_id_t *new_hid;
	char meta_data[sizeof(struct vm_header) +
		       sizeof(struct vm_buffer_info) +
		       sizeof(struct hyper_dmabuf_event_hdr)];

	fds.fd = fd;
	fds.events = POLLIN;

repoll:
	do {
		ret = poll(&fds, 1, -1);

		if (ret > 0) {
			if (fds.revents & (POLLERR | POLLNVAL)) {
				errno = EINVAL;
				return 1;
			}
			break;
		} else if (ret == 0) {
			/* should not get here */
			errno = ETIME;
			return 1;
		}
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));

reread:
	ret = read(fd, meta_data, sizeof(meta_data));

	if (ret <= 0) {
		printf("no event found\n");
		goto repoll;
	}

	vbt = (struct vm_buffer_info *)&meta_data[sizeof(struct vm_header) +
						  sizeof(struct
							 hyper_dmabuf_event_hdr)];

	/* go back and try to fetch the next event */
	if (vbt->surf_index != surf_index) {
		goto reread;
	}

	/* getting vbt_header from meta_data from hyper_dmabuf driver */
	memcpy(&vbt_header, &meta_data[sizeof(struct hyper_dmabuf_event_hdr)],
	       sizeof(vbt_header));

	/* update hyper_dmabuf_id with valid one generated for the buffer */
	new_hid = (hyper_dmabuf_id_t *) & meta_data[sizeof(int)];
	vbt->hyper_dmabuf_id = *new_hid;

	if (vbt_header.version != VMDISPLAY_VBT_VERSION) {
		printf
		    ("Mismatched VBT versions! Expected %d, but received %d.\n",
		     VMDISPLAY_VBT_VERSION, vbt_header.version);
		return 1;
	}

	if (vbt_header.n_buffers <= 0) {
		printf("Bad n_buffers value\n");
		return 1;
	}

	disp_w = vbt_header.disp_w;
	disp_h = vbt_header.disp_h;

	if (surf_id != 0) {
		if (vbt->surface_id != surf_id) {
			printf("Bad surface id\n");
			show_window = 0;
			return 1;
		}
	}

	surf_width = vbt->width;
	surf_height = vbt->height;
	surf_stride[0] = vbt->pitch[0];
	surf_stride[1] = vbt->pitch[1];
	surf_stride[2] = vbt->pitch[2];
	surf_offset[0] = vbt->offset[0];
	surf_offset[1] = vbt->offset[1];
	surf_offset[2] = vbt->offset[2];
	surf_format = vbt->format;
	surf_tile_format = vbt->tile_format;
	surf_rotation = vbt->rotation;
	surf_disp_x = vbt->bbox[0];
	surf_disp_y = vbt->bbox[1];
	surf_disp_w = vbt->bbox[2];
	surf_disp_h = vbt->bbox[3];

	hyper_dmabuf_id = vbt->hyper_dmabuf_id;

	*counter = vbt->counter;

	show_window = 1;

	return 0;
}

int parse_socket_metadata(vmdisplay_socket * socket, int *counter)
{
	int len;
	struct vmdisplay_msg msg;

	/* Wait for message about metadata update for given pipe/output */
	do {
		len = recv(socket->socket_fd, &msg, sizeof(msg), 0);
	} while (len > 0 && (msg.type != VMDISPLAY_METADATA_UPDATE_MSG ||
			     msg.display_num != pipe_id));

	if (len <= 0) {
		int err = errno;
		printf("recv returned invalid status: %d, errno = %d", len,
		       err);
		return 1;
	}

	memcpy(&vbt_header, socket->outputs[pipe_id].mem_addr,
	       sizeof(vbt_header));

	if (vbt_header.version != VMDISPLAY_VBT_VERSION) {
		printf
		    ("Mismatched VBT versions! Expected %d, but received %d.\n",
		     VMDISPLAY_VBT_VERSION, vbt_header.version);
		return 1;
	}

	if (vbt_header.n_buffers <= 0) {
		printf("Bad n_buffers value\n");
		return 1;
	}

	disp_w = vbt_header.disp_w;
	disp_h = vbt_header.disp_h;

	vbt =
	    (struct vm_buffer_info *)(socket->outputs[pipe_id].mem_addr +
				      sizeof(struct vm_header));

	/*
	 * Report bad index only if surf_id was not provided.
	 * When surf_id is provided surf_index is set to whatever
	 * index has a surface with provided surf_id (and will stay
	 * set to this value until next metadata will be received).
	 * It may happen that in next metadata this index will become
	 * invalid (eg. some surfaces will be unshared in meantime)
	 * and below condition check will report error
	 * without finding new index value of given surf_id.
	 */

	if (!surf_id) {
		if (surf_index >= vbt_header.n_buffers || surf_index < 0) {
			printf("Bad buffer table index value\n");
			return 1;
		}
	}

	if (surf_id != 0) {
		int found = 0;
		for (int i = 0; i < vbt_header.n_buffers; i++) {
			if (vbt[i].surface_id == surf_id) {
				surf_index = i;
				found = 1;
				break;
			}
		}
		if (!found) {
			printf("Bad surface id\n");
			show_window = 0;
			return 1;
		}
	}
	surf_width = vbt[surf_index].width;
	surf_height = vbt[surf_index].height;
	surf_stride[0] = vbt[surf_index].pitch[0];
	surf_stride[1] = vbt[surf_index].pitch[1];
	surf_stride[2] = vbt[surf_index].pitch[2];
	surf_offset[0] = vbt[surf_index].offset[0];
	surf_offset[1] = vbt[surf_index].offset[1];
	surf_offset[2] = vbt[surf_index].offset[2];
	surf_format = vbt[surf_index].format;
	surf_tile_format = vbt[surf_index].tile_format;
	surf_rotation = vbt[surf_index].rotation;
	surf_disp_x = vbt[surf_index].bbox[0];
	surf_disp_y = vbt[surf_index].bbox[1];
	surf_disp_w = vbt[surf_index].bbox[2];
	surf_disp_h = vbt[surf_index].bbox[3];

	hyper_dmabuf_id = vbt[surf_index].hyper_dmabuf_id;

	*counter = vbt[surf_index].counter;

	show_window = 1;

	return 0;
}
