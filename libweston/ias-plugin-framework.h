/*
 *-----------------------------------------------------------------------------
 * Filename: ias-plugin-framework.h
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
 *   This public header provides the interface by which customer layout
 *   and input plugins may interact with internal data structures of the Intel
 *   Automotive Solutions shell.
 *-----------------------------------------------------------------------------
 */

#ifndef IAS_PLUGIN_MANAGER_H
#define IAS_PLUGIN_MANAGER_H

#include <wayland-server.h>

/*
 * Work around C++ reserved word; without this, plugins can't be written
 * in C++.
 */
#define private privdata

#include "compositor.h"
//plugins may have previously included gl through the compositor
//#define GL_EXT_unpack_subimage;
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "weston-egl-ext.h"
#include <wayland-server.h>
#include "ias-plugin-framework-definitions.h"
#undef private


#endif
