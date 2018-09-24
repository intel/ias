/*
 *-----------------------------------------------------------------------------
 * Filename: input.c
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
 *
 *-----------------------------------------------------------------------------
 * Description:
 *   Sample Input plugin
 *-----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <linux/input.h>
#include "ias-plugin-framework.h"
#include "ias-spug.h"


/* the input plugin entry point. Here we can block, modify, monitor or ignore 
 * all input.
 */
static void on_input_event(struct ipug_event_info *info)
{
	int handled = 0;

	/* handle any special cases here: global volume control, app specific keys
	 * (play/pause?), etc. We'll demonstrate by sending a z keypress to
	 * weston-fullscreen if we receive an F1 keypress
	 */
	if(info->event_type == IPUG_KEYBOARD_KEY) {
		struct ipug_event_info_key_key *key_event = 
							(struct ipug_event_info_key_key*)info;

		if(key_event->key == KEY_F1) {
			key_event->key = KEY_Z;
			handled = ipug_send_event_to_pname(info, "weston-fullscreen");
			key_event->key = KEY_F1;
		/* and on ESC, remove the input focus */
		} else if(key_event->key == KEY_ESC) {
			ipug_set_input_focus(IPUG_EVENTS_ALL_BIT, 0);
		}
	}

	/* send the event to the currently focused listener */
	if(!handled) {
		handled = ipug_send_event_to_focus(info);
	}

	/* if the currently focused listener didn't handle it,
	 * send it to the default listener */
	if(!handled) {
		ipug_send_event_to_default(info);
	}

}

/***
 *** Plugin initialization
 ***/

WL_EXPORT int
ias_input_plugin_init(struct ias_input_plugin_info *info,
		uint32_t version)
{
	info->layout_switch_to = NULL;
	info->layout_switch_from = NULL;

	info->mouse_grab = NULL;
	info->key_grab = NULL;
	info->touch_grab = NULL;

	info->on_input = &on_input_event;

	return 0;
}
