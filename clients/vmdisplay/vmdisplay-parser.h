/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-parser.h
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
#ifndef _VMRECEPTOR_H_
#define _VMRECEPTOR_H_

#include <stdint.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm/i915_drm.h>
#include "vm-shared.h"
#include "vmdisplay.h"

extern uint32_t surf_width;
extern uint32_t surf_height;
extern uint32_t surf_bpp;
extern uint32_t surf_stride[3];
extern uint32_t surf_offset[3];
extern uint32_t surf_format;
extern uint32_t surf_tile_format;
extern uint32_t surf_rotation;
extern hyper_dmabuf_id_t hyper_dmabuf_id;
extern int32_t surf_disp_x;
extern int32_t surf_disp_y;
extern int32_t surf_disp_w;
extern int32_t surf_disp_h;
extern int32_t disp_w;
extern int32_t disp_h;

int parse_event_metadata(int fd, int *counter);
int parse_socket_metadata(vmdisplay_socket * socket, int *counter);

#endif
