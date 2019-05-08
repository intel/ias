/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay.h
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
 *   Library for vmdisplay which has the helper function
 *-----------------------------------------------------------------------------
 */

#ifndef _VMDISPLAY_H_
#define _VMDISPLAY_H_

#include <sys/types.h>
typedef int32_t s32;

#include <stdint.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#ifdef YOCTO_BUILD
#include <igvtg-kernel-headers/i915_drm.h>
#include <igvtg-kernel-headers/drm.h>
#else
#include <libdrm/drm.h>
#endif
#include "vm-shared.h"
#include "vmdisplay-shared.h"

#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define HYPER_DMABUF_DEV_PATH_LEGACY 	"/dev/xen/hyper_dmabuf"
#define HYPER_DMABUF_DEV_PATH 		"/dev/hyper_dmabuf"

typedef struct buffer_rec {
	uint32_t hyper_dmabuf_id;
	GLuint textureId[2];
	struct wl_buffer *buffer;
	uint32_t width;
	uint32_t height;
	int age;
	uint32_t gem_handle;
} buffer_rec;

typedef struct buffer_list {
	struct buffer_rec *l;
	int len;
} buffer_list;

typedef struct egl_manager {
	EGLDisplay dpy;
	EGLContext ctx;
} egl_manager;

extern int g_Dbg;

typedef struct vmdisplay_socket {
	int socket_fd;
	struct output {
		int mem_fd;
		void *mem_addr;
	} outputs[VM_MAX_OUTPUTS];
} vmdisplay_socket;

extern uint32_t vgt_address;
extern int enable_print_vbt;
extern int surf_index;
extern uint64_t surf_id;
extern int enable_fps_info;
extern uint32_t show_window;

int open_drm(void);
void init_buffers(void);
int init_hyper_dmabuf(int dom);
void clear_hyper_dmabuf_list(void);
void create_new_hyper_dmabuf_buffer(void);
int check_for_new_buffer(void);
void received_frames(void);
int vmdisplay_socket_init(vmdisplay_socket * socket, int domid);
void vmdisplay_socket_cleanup(vmdisplay_socket * socket);
int send_input_event(vmdisplay_socket * socket,
		     struct vmdisplay_input_event_header *header, void *event);

#endif // _VMDISPLAY_H_
