/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-shared.h
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
 *   VMDisplay header file shared between server library and input
 *-----------------------------------------------------------------------------
 */

#ifndef _VMDISPLAY_SHARED_H_
#define _VMDISPLAY_SHARED_H_

#include <stdint.h>

enum CommunicationChannelType {
	CommunicationChannelNetwork = 0,
	CommunicationChannelHyperDMABUF,
};

enum vmdisplay_msg_type {
	VMDISPLAY_INIT_MSG = 1,
	VMDISPLAY_METADATA_UPDATE_MSG,
	VMDISPLAY_NEW_OUTPUT_MSG,
	VMDISPLAY_CLEANUP_MSG,
};

struct vmdisplay_msg {
	enum vmdisplay_msg_type type;
	uint32_t display_num;
};

enum vmdisplay_input_event_type {
	VMDISPLAY_TOUCH_EVENT = 0x00,
	VMDISPLAY_KEY_EVENT,
	VMDISPLAY_POINTER_EVENT,
	VMDISPLAY_INPUT_EVENT_MAX,
};

struct vmdisplay_input_event_header {
	uint32_t type;
	uint32_t size;
};

enum vmdisplay_touch_event_type {
	VMDISPLAY_TOUCH_DOWN = 0x00,
	VMDISPLAY_TOUCH_UP,
	VMDISPLAY_TOUCH_MOTION,
	VMDISPLAY_TOUCH_FRAME,
	VMDISPLAY_TOUCH_CANCEL,
};

struct vmdisplay_touch_event {
	uint32_t type;
	uint32_t id;
	uint32_t x;
	uint32_t y;
};

enum vmdisplay_key_event_type {
	VMDISPLAY_KEY_ENTER = 0x00,
	VMDISPLAY_KEY_LEAVE,
	VMDISPLAY_KEY_KEY,
	VMDISPLAY_KEY_MODIFIERS,
};

struct vmdisplay_key_event {
	uint32_t type;
	uint32_t time;
	uint32_t key;
	uint32_t state;
	uint32_t mods_depressed;
	uint32_t mods_latched;
	uint32_t mods_locked;
	uint32_t group;
};

enum vmdisplay_pointer_event_type {
	VMDISPLAY_POINTER_ENTER = 0x00,
	VMDISPLAY_POINTER_LEAVE,
	VMDISPLAY_POINTER_MOTION,
	VMDISPLAY_POINTER_BUTTON,
	VMDISPLAY_POINTER_AXIS,
};

struct vmdisplay_pointer_event {
	uint32_t type;
	uint32_t time;
	uint32_t x;
	uint32_t y;
	union {
		uint32_t button;
		uint32_t axis;
	};
	union {
		uint32_t state;
		uint32_t value;
	};
};

#endif // _VMDISPLAY_SHARED_H_
