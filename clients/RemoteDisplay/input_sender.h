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

#ifndef __REMOTE_DISPLAY_INPUT_SENDER_H__
#define __REMOTE_DISPLAY_INPUT_SENDER_H__

#include <stdint.h>

/* Based on vmdisplay-shared.h */

/* For now, assume that these are the same on sender and receiver. */
#define MAX_TOUCH_X 32767
#define MAX_TOUCH_Y 32767

enum remote_display_msg_type {
	REMOTE_DISPLAY_INIT_MSG = 1,
	REMOTE_DISPLAY_METADATA_UPDATE_MSG,
	REMOTE_DISPLAY_NEW_OUTPUT_MSG,
	REMOTE_DISPLAY_CLEANUP_MSG,
};

struct remote_display_msg {
	enum remote_display_msg_type type;
	uint32_t display_num;
};

enum remote_display_input_event_type {
	REMOTE_DISPLAY_TOUCH_EVENT = 0x00,
	REMOTE_DISPLAY_KEY_EVENT,
	REMOTE_DISPLAY_POINTER_EVENT,
	REMOTE_DISPLAY_INPUT_EVENT_MAX,
};

struct remote_display_input_event_header {
	uint32_t type;
	uint32_t size;
};

enum remote_display_touch_event_type {
	REMOTE_DISPLAY_TOUCH_DOWN = 0x00,
	REMOTE_DISPLAY_TOUCH_UP,
	REMOTE_DISPLAY_TOUCH_MOTION,
	REMOTE_DISPLAY_TOUCH_FRAME,
	REMOTE_DISPLAY_TOUCH_CANCEL,
};


struct remote_display_touch_event {
	uint32_t type;
	uint32_t id;
	uint32_t x;
	uint32_t y;
	uint32_t time;
};

enum remote_display_key_event_type {
	REMOTE_DISPLAY_KEY_ENTER = 0x00,
	REMOTE_DISPLAY_KEY_LEAVE,
	REMOTE_DISPLAY_KEY_KEY,
	REMOTE_DISPLAY_KEY_MODIFIERS,
};

struct remote_display_key_event {
	uint32_t type;
	uint32_t time;
	uint32_t key;
	uint32_t state;
	uint32_t mods_depressed;
	uint32_t mods_latched;
	uint32_t mods_locked;
	uint32_t group;
};

enum remote_display_pointer_event_type {
	REMOTE_DISPLAY_POINTER_ENTER = 0x00,
	REMOTE_DISPLAY_POINTER_LEAVE,
	REMOTE_DISPLAY_POINTER_MOTION,
	REMOTE_DISPLAY_POINTER_BUTTON,
	REMOTE_DISPLAY_POINTER_AXIS,
};

struct remote_display_pointer_event {
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

#endif /* __REMOTE_DISPLAY_INPUT_SENDER_H__ */
