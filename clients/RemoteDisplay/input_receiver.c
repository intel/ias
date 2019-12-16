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
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <signal.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <errno.h>

#include <wayland-util.h>

#include "shared/helpers.h"
#include <libweston/config-parser.h>

#include "input_sender.h"
#include "input_receiver.h"
#include "ias-shell-client-protocol.h"
#include "main.h" /* Need access to app_state */
#include "udp_socket.h"
#include "debug.h"


struct remoteDisplayInput {
	int uinput_touch_fd;
	int uinput_keyboard_fd;
	int uinput_pointer_fd;
};

#define MAX_BUTTONS 30
struct remoteDisplayButtonState{
		unsigned int button_states : MAX_BUTTONS;
		unsigned int touch_down : 1;
		unsigned int state_changed : 1;
	};

struct input_receiver_private_data {
	struct udp_socket *udp_socket;
	int num_addr;
	struct remoteDisplayInput input;
	volatile int running;
	int verbose;
	struct app_state *appstate;
	struct remoteDisplayButtonState button_state;
	/* input receiver thread */
	pthread_t input_thread;
};


static int
init_output_touch(int *uinput_touch_fd_out)
{
	struct uinput_user_dev uidev;
	int uinput_touch_fd;

	*uinput_touch_fd_out = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (*uinput_touch_fd_out < 0) {
		ERROR("Cannot open uinput.\n");
		return -1;
	}
	uinput_touch_fd = *uinput_touch_fd_out;

	ioctl(uinput_touch_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(uinput_touch_fd, UI_SET_KEYBIT, BTN_TOUCH);

	ioctl(uinput_touch_fd, UI_SET_EVBIT, EV_ABS);
	ioctl(uinput_touch_fd, UI_SET_ABSBIT, ABS_MT_SLOT);
	ioctl(uinput_touch_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
	ioctl(uinput_touch_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
	ioctl(uinput_touch_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
	ioctl(uinput_touch_fd, UI_SET_ABSBIT, ABS_X);
	ioctl(uinput_touch_fd, UI_SET_ABSBIT, ABS_Y);

	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "remote-display-input-touch");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 0x8086;
	uidev.id.product = 0xf0f0; /* TODO - get new product ID. */
	uidev.id.version = 0x01;

	uidev.absmin[ABS_MT_POSITION_X] = 0;
	uidev.absmax[ABS_MT_POSITION_X] = MAX_TOUCH_X;
	uidev.absmin[ABS_MT_POSITION_Y] = 0;
	uidev.absmax[ABS_MT_POSITION_Y] = MAX_TOUCH_Y;
	uidev.absmin[ABS_MT_SLOT] = 0;
	uidev.absmax[ABS_MT_SLOT] = 7;

	uidev.absmin[ABS_X] = 0;
	uidev.absmax[ABS_X] = MAX_TOUCH_X;
	uidev.absmin[ABS_Y] = 0;
	uidev.absmax[ABS_Y] = MAX_TOUCH_Y;

	if (write(uinput_touch_fd, &uidev, sizeof(uidev)) < 0) {
		close(uinput_touch_fd);
		return -1;
	}

	if (ioctl(uinput_touch_fd, UI_DEV_CREATE) < 0) {
		close(uinput_touch_fd);
		return -1;
	}
	return 0;
}


static void
write_touch_slot(int uinput_touch_fd, uint32_t id)
{
	struct input_event ev;
	int ret;

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_ABS;
	ev.code = ABS_MT_SLOT;
	ev.value = id;
	ret = write(uinput_touch_fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to add slot to touch uinput.\n");
	}
}


static void
write_touch_tracking_id(int uinput_touch_fd, uint32_t id)
{
	struct input_event ev;
	int ret;

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_ABS;
	ev.code = ABS_MT_TRACKING_ID;
	ev.value = id;
	ret = write(uinput_touch_fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to add tracking id to touch uinput.\n");
	}
}


static void
write_touch_event_coords(struct app_state *appstate, uint32_t x, uint32_t y)
{
	struct input_event ev;
	int ret;
	int touch_fd = appstate->ir_priv->input.uinput_touch_fd;
	int offset_x = appstate->output_origin_x;
	int offset_y = appstate->output_origin_y;

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_ABS;
	ev.code = ABS_MT_POSITION_X;
	ev.value = (x + offset_x) * MAX_TOUCH_X / appstate->output_width;
	ret = write(touch_fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to add x value to touch uinput.\n");
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_ABS;
	ev.code = ABS_MT_POSITION_Y;
	ev.value = (y + offset_y) * MAX_TOUCH_Y / appstate->output_height;
	ret = write(touch_fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to add y value to touch uinput.\n");
	}
}


static void
write_syn(int uinput_touch_fd)
{
	struct input_event ev;
	int ret;

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_SYN;
	ret = write(uinput_touch_fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to add syn to touch uinput.\n");
	}
}

static void write_msc(int fd)
{
	struct input_event ev;
	int ret=0;

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_MSC;
	ev.code = MSC_SCAN;
	ev.value = 90001;
	ret = write(fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to write button.\n");
		return;
	}
}

static void write_key(int fd, uint32_t btn, uint32_t state)
{
	struct input_event ev;
	int ret=0;

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_KEY;
	ev.code = btn;
	ev.value = state;
	ret = write(fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to write button.\n");
		return;
	}
}

#if 0
static void write_rel(int fd, uint32_t xy, uint32_t pos)
{
	struct input_event ev;
	int ret=0;

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_REL;
	ev.code = xy;
	ev.value = pos;
	ret = write(fd, &ev, sizeof(ev));
	if (ret < 0) {
		ERROR("Failed to write button.\n");
		return;
	}
}
#endif

static void surf_pointer_func(
		struct ias_relay_input *ias_in,
		uint32_t ias_event_type,
		uint32_t surfid,
		gstInputMsg *msg)
{
	ias_relay_input_send_pointer(ias_in,
			ias_event_type,
			surfid,
			msg->p.x, msg->p.y,
			msg->p.button, msg->p.state,
			msg->p.axis, msg->p.value,
			msg->p.time);
}

static void surf_keyboard_func(
		struct ias_relay_input *ias_in,
		uint32_t ias_event_type,
		uint32_t surfid,
		gstInputMsg *msg)
{
	ias_relay_input_send_key(ias_in,
			ias_event_type,
			surfid, msg->k.time,
			msg->k.key, msg->k.state,
			msg->k.mods_depressed, msg->k.mods_latched,
			msg->k.mods_locked, msg->k.group);
}

static void surf_touch_func(
		struct ias_relay_input *ias_in,
		uint32_t ias_event_type,
		uint32_t surfid,
		gstInputMsg *msg)
{
	ias_relay_input_send_touch(ias_in,
			ias_event_type,
			surfid, msg->t.id,
			msg->t.x, msg->t.y,
			msg->t.time);
}


static void pointer_button_func(struct app_state *appstate, gstInputMsg *msg)
{
	int uinput_pointer_fd = appstate->ir_priv->input.uinput_pointer_fd;

	write_msc(uinput_pointer_fd);
	write_key(uinput_pointer_fd, msg->p.button, msg->p.state);
	write_syn(uinput_pointer_fd);
}

static void pointer_motion_func(struct app_state *appstate, gstInputMsg *msg)
{
#if 0
	int uinput_pointer_fd = appstate->ir_priv->input.uinput_pointer_fd;

	DBG("%s: %d, x = %u, fixed_x = %f, y = %u, fixed_y = %f\n",
			__FUNCTION__, __LINE__, msg->p.x, wl_fixed_to_double(msg->p.x),
			msg->p.y, wl_fixed_to_double(msg->p.y));

	write_rel(uinput_pointer_fd, REL_X, wl_fixed_to_double(msg->p.x));
	write_rel(uinput_pointer_fd, REL_Y, wl_fixed_to_double(msg->p.y));
	write_syn(uinput_pointer_fd);
#endif
}


static void key_func(struct app_state *appstate, gstInputMsg *msg)
{
	int uinput_keyboard_fd = appstate->ir_priv->input.uinput_keyboard_fd;

	write_key(uinput_keyboard_fd, msg->k.key, msg->k.state);
	write_syn(uinput_keyboard_fd);
}

static void touch_down_func(struct app_state *appstate, gstInputMsg *msg)
{
	int touch_fd = appstate->ir_priv->input.uinput_touch_fd;
	write_touch_slot(touch_fd, msg->t.id);
	write_touch_tracking_id(touch_fd, msg->t.id);
	write_touch_event_coords(appstate,
		wl_fixed_to_double(msg->t.x),
		wl_fixed_to_double(msg->t.y));
	write_syn(touch_fd);
}

static void touch_up_func(struct app_state *appstate, gstInputMsg *msg)
{
	int touch_fd = appstate->ir_priv->input.uinput_touch_fd;
	write_touch_slot(touch_fd, msg->t.id);
	write_touch_tracking_id(touch_fd, msg->t.id);
	write_syn(touch_fd);
}

static void touch_motion_func(struct app_state *appstate, gstInputMsg *msg)
{
	int touch_fd = appstate->ir_priv->input.uinput_touch_fd;
	write_touch_slot(touch_fd, msg->t.id);
	write_touch_event_coords(appstate,
		wl_fixed_to_double(msg->t.x),
		wl_fixed_to_double(msg->t.y));
	write_syn(touch_fd);
}


typedef void (*wl_surf_event_func) (
		struct ias_relay_input *ias_in,
		uint32_t ias_event_type,
		uint32_t surfid,
		gstInputMsg *msg);

typedef void (*wl_output_event_func) (
		struct app_state *appstate,
		gstInputMsg *msg);

struct event_conv {
	uint32_t remote_display_event_type;
	uint32_t ias_event_type;
	wl_surf_event_func surf_event_func;
	wl_output_event_func output_event_func;
} event_conv_table[] = {
	{POINTER_HANDLE_ENTER, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_ENTER,
			surf_pointer_func, NULL},
	{POINTER_HANDLE_LEAVE, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_LEAVE,
			surf_pointer_func, NULL},
	{POINTER_HANDLE_MOTION, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_MOTION,
			surf_pointer_func, pointer_motion_func},
	{POINTER_HANDLE_BUTTON, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_BUTTON,
			surf_pointer_func, pointer_button_func},
	{POINTER_HANDLE_AXIS, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_AXIS,
			surf_pointer_func, NULL},
	{KEYBOARD_HANDLE_KEYMAP, 0,
			NULL, NULL},
	{KEYBOARD_HANDLE_ENTER, IAS_RELAY_INPUT_KEY_EVENT_TYPE_ENTER,
			surf_keyboard_func, NULL},
	{KEYBOARD_HANDLE_LEAVE, IAS_RELAY_INPUT_KEY_EVENT_TYPE_LEAVE,
			surf_keyboard_func, NULL},
	{KEYBOARD_HANDLE_KEY, IAS_RELAY_INPUT_KEY_EVENT_TYPE_KEY,
			surf_keyboard_func, key_func},
	{KEYBOARD_HANDLE_MODIFIERS, IAS_RELAY_INPUT_KEY_EVENT_TYPE_MODIFIERS,
			surf_keyboard_func, NULL},
	{TOUCH_HANDLE_DOWN, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_DOWN,
			surf_touch_func, touch_down_func},
	{TOUCH_HANDLE_UP, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_UP,
			surf_touch_func, touch_up_func},
	{TOUCH_HANDLE_MOTION, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_MOTION,
			surf_touch_func, touch_motion_func},
	{TOUCH_HANDLE_FRAME, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_FRAME,
			surf_touch_func, NULL},
	{TOUCH_HANDLE_CANCEL, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_CANCEL,
			surf_touch_func, NULL},
};

struct event_conv *get_matching_event(uint32_t remote_display_event_type)
{
	unsigned long i;
	for(i = 0; i < ARRAY_LENGTH(event_conv_table); i++) {
		if(event_conv_table[i].remote_display_event_type ==
				remote_display_event_type) {
			break;
		}
	}

	return i >= ARRAY_LENGTH(event_conv_table) ? NULL :
			&event_conv_table[i];
}

static int
init_output_pointer(int *uinput_pointer_fd_out)
{
	int uinput_pointer_fd;
	struct uinput_user_dev uidev;

	*uinput_pointer_fd_out = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (*uinput_pointer_fd_out < 0) {
		ERROR("Cannot open uinput.\n");
		return -1;
	}
	uinput_pointer_fd = *uinput_pointer_fd_out;

	ioctl(uinput_pointer_fd, UI_SET_EVBIT, EV_REL);
	ioctl(uinput_pointer_fd, UI_SET_RELBIT, REL_X);
	ioctl(uinput_pointer_fd, UI_SET_RELBIT, REL_Y);
	ioctl(uinput_pointer_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(uinput_pointer_fd, UI_SET_KEYBIT, BTN_MOUSE);
	ioctl(uinput_pointer_fd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(uinput_pointer_fd, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl(uinput_pointer_fd, UI_SET_KEYBIT, BTN_MIDDLE);
	ioctl(uinput_pointer_fd, UI_SET_EVBIT, EV_MSC);
	ioctl(uinput_pointer_fd, UI_SET_MSCBIT, MSC_SCAN);

	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "remote-display-input-pointer");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 0x8086;
	uidev.id.product = 0xf0f2; /* TODO - get new product ID. */
	uidev.id.version = 0x01;

	if (write(uinput_pointer_fd, &uidev, sizeof(uidev)) < 0) {
		close(uinput_pointer_fd);
		return -1;
	}

	if (ioctl(uinput_pointer_fd, UI_DEV_CREATE) < 0) {
		close(uinput_pointer_fd);
		return -1;
	}
	return 0;
}

static int
init_output_keyboard(int *uinput_keyboard_fd_out)
{
	int uinput_keyboard_fd;
	struct uinput_user_dev uidev;

	*uinput_keyboard_fd_out = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (*uinput_keyboard_fd_out < 0) {
		ERROR("Cannot open uinput.\n");
		return -1;
	}
	uinput_keyboard_fd = *uinput_keyboard_fd_out;

	ioctl(uinput_keyboard_fd, UI_SET_EVBIT, EV_KEY);
	for (unsigned int i = 0; i < 248; i++) {
		ioctl(uinput_keyboard_fd, UI_SET_KEYBIT, i);
	}

	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "remote-display-input-keyboard");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 0x8086;
	uidev.id.product = 0xf0f1; /* TODO - get new product ID. */
	uidev.id.version = 0x01;

	if (write(uinput_keyboard_fd, &uidev, sizeof(uidev)) < 0) {
		close(uinput_keyboard_fd);
		return -1;
	}

	if (ioctl(uinput_keyboard_fd, UI_DEV_CREATE) < 0) {
		close(uinput_keyboard_fd);
		return -1;
	}
	return 0;
}

static void
close_transport()
{
}

static void
cleanup_input(struct remoteDisplayInput *input)
{
	if (input->uinput_pointer_fd >= 0) {
		ioctl(input->uinput_pointer_fd, UI_DEV_DESTROY);
		close(input->uinput_pointer_fd);
		input->uinput_pointer_fd = -1;
	}

	if (input->uinput_keyboard_fd >= 0) {
		ioctl(input->uinput_keyboard_fd, UI_DEV_DESTROY);
		close(input->uinput_keyboard_fd);
		input->uinput_keyboard_fd = -1;
	}

	if (input->uinput_touch_fd >= 0) {
		ioctl(input->uinput_touch_fd, UI_DEV_DESTROY);
		close(input->uinput_touch_fd);
		input->uinput_touch_fd = -1;
	}
}


static void
init_transport(struct input_receiver_private_data *data)
{
	int ret = 0;
	struct timeval optTime = {0, 10}; /* {sec, msec} */

	/* TODO: This 0 will have to change if we have more than one udp sockets */
	struct udp_socket *transport = &data->udp_socket[0];

	transport->input.len = sizeof(transport->input.addr);

	INFO("Initialising transport on input receiver...\n");
	transport->input.sock_desc = socket(AF_INET, SOCK_DGRAM , 0);
	if (transport->input.sock_desc == -1) {
		ERROR("Socket creation failed.\n");
		return;
	}
	if (setsockopt(transport->input.sock_desc, SOL_SOCKET, SO_SNDTIMEO, &optTime, sizeof(optTime)) < 0) {
		ERROR("sendto timeout configuration failed\n");
		return;
	}

	transport->input.addr.sin_family = AF_INET;
	transport->input.addr.sin_addr.s_addr = htonl(INADDR_ANY);
	transport->input.addr.sin_port = htons(transport->input.port);

	ret = bind(transport->input.sock_desc, (struct sockaddr *) &transport->input.addr,
			sizeof (transport->input.addr));
	if (ret < 0) {
		ERROR("bind function failed. errno is %d.\n",errno);
		close(transport->input.sock_desc);
		transport->input.sock_desc = -1;
		return;
	}

	data->running = 1;
	INFO("Ready to accept input events.\n");
}

static void *
receive_events(void * const priv_data)
{
	int ret = 0;
	struct input_receiver_private_data *data = priv_data;
	gstInputMsg msg;
	struct event_conv *ev;

	/* TODO: This 0 will have to change if we have more than one udp sockets */
	struct udp_socket *transport = &data->udp_socket[0];

	init_transport(data);

	while (data->running) {
		ret = recvfrom(transport->input.sock_desc, &msg, sizeof(msg), 0,
				(struct sockaddr *) &transport->input.addr,
				&transport->input.len);

		if (data->running == 0) {
			INFO("Receive interrupted by shutdown.\n");
			continue;
		}
		if (ret <= 0) {
			INFO("Receive failed.\n");
		} else if (data->appstate->surfid) {
			ev = get_matching_event(msg.type);
			if(ev && ev->surf_event_func) {
				ev->surf_event_func(data->appstate->ias_in,
						ev->ias_event_type,
						data->appstate->surfid, &msg);
				wl_display_flush(data->appstate->display);
			}
		} else {
			ev = get_matching_event(msg.type);
			if(ev && ev->output_event_func) {
				ev->output_event_func(data->appstate, &msg);
			}
		}
	}

	close_transport();
	cleanup_input(&data->input);
	free(priv_data);

	INFO("Receive thread finished.\n");
	return 0;
}


void
start_event_listener(struct app_state *appstate, int *argc, char **argv)
{
	struct input_receiver_private_data *data = NULL;
	int ret = 0;
	int touch_ret = 0;
	int keyb_ret = 0;
	int pointer_ret = 0;
	struct udp_socket *transport;

	data = calloc(1, sizeof(*data));

	if (data == NULL)
	{
	    INFO("Memory allocation failed...\n");
	    return;
	}
	/* In case of udp transport, we should get the  */
	if(!strcmp(appstate->transport_plugin, "udp") &&
		appstate->get_sockaddr_fptr) {
		appstate->get_sockaddr_fptr(&data->udp_socket, &data->num_addr);
	}

	/* TODO: This 0 will have to change if we have more than one udp sockets */
	transport = &data->udp_socket[0];

	if (transport && transport->input.port != 0) {
		INFO("Receiving input events from %s:%d.\n", transport->str_ipaddr,
			transport->input.port);
	} else {
		INFO("Not listening for input events; network configuration not set.\n");
		free(data);
		data = NULL;
		return;
	}

	if (!appstate->surfid) {
		int i = 0;
		struct output *output;

		touch_ret = init_output_touch(&(data->input.uinput_touch_fd));
		/* Assume that the outputs are listed in the same order. */
		wl_list_for_each_reverse(output, &appstate->output_list, link) {
			DBG("Output %d is at %d, %d.\n", i, output->x, output->y);
			if (appstate->output_number == i) {
				DBG("Sending events to output %d at %d, %d.\n",
					i, output->x, output->y);
				appstate->output_origin_x = output->x;
				appstate->output_origin_y = output->y;
				appstate->output_width = output->width;
				appstate->output_height = output->height;
			}
			i++;
		}
		keyb_ret = init_output_keyboard(&(data->input.uinput_keyboard_fd));
		pointer_ret = init_output_pointer(&(data->input.uinput_pointer_fd));
	}

	if (touch_ret) {
		ERROR("Error initialising touch input - %d.\n", touch_ret);
		free(data);
		return;
	}
	if (keyb_ret) {
		ERROR("Error initialising keyboard input - %d.\n", keyb_ret);
		free(data);
		return;
	}
	if (pointer_ret) {
		ERROR("Error initialising pointer input - %d.\n", keyb_ret);
		free(data);
		return;
	}


	ret = pthread_create(&data->input_thread, NULL, receive_events, data);
	if (ret) {
		ERROR("Transport thread creation failure: %d\n", ret);
		free(data);
		return;
	}

	data->appstate = appstate;
	appstate->ir_priv = data;
	INFO("Input receiver started.\n");
}

void
stop_event_listener(struct input_receiver_private_data *priv_data)
{
	if (!priv_data || !priv_data->udp_socket) {
		return;
	}
	priv_data->running = 0;
	DBG("Waiting for input receiver thread to finish...\n");
	shutdown(priv_data->udp_socket[0].input.sock_desc, SHUT_RDWR);
	pthread_join(priv_data->input_thread, NULL);
	INFO("Input receiver thread stopped.\n");
}
