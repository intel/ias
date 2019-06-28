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

#include <wayland-util.h>

#include "shared/helpers.h"
#include "shared/config-parser.h"

#include "input_sender.h"
#include "input_receiver.h"
#include "ias-shell-client-protocol.h"
#include "main.h" /* Need access to app_state */


struct tcpTransport {
	int sockDesc;
	struct sockaddr_in sockAddr;
	socklen_t len;
	char *ipaddr;
	unsigned short port;
	int connected;
};

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
	struct tcpTransport transport;
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
		printf("Cannot open uinput.\n");
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

	if (write(uinput_touch_fd, &uidev, sizeof(uidev)) < 0)
		return -1;

	if (ioctl(uinput_touch_fd, UI_DEV_CREATE) < 0)
		return -1;

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
		fprintf(stderr, "Failed to add slot to touch uinput.\n");
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
		fprintf(stderr, "Failed to add tracking id to touch uinput.\n");
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
		fprintf(stderr, "Failed to add x value to touch uinput.\n");
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_ABS;
	ev.code = ABS_MT_POSITION_Y;
	ev.value = (y + offset_y) * MAX_TOUCH_Y / appstate->output_height;
	ret = write(touch_fd, &ev, sizeof(ev));
	if (ret < 0) {
		fprintf(stderr, "Failed to add y value to touch uinput.\n");
	}
}


static void
write_touch_syn(int uinput_touch_fd)
{
	struct input_event ev;
	int ret;

	memset(&ev, 0, sizeof(ev));
	ev.type = EV_SYN;
	ret = write(uinput_touch_fd, &ev, sizeof(ev));
	if (ret < 0) {
		fprintf(stderr, "Failed to add syn to touch uinput.\n");
	}
}


static void
handle_output_touch_event(const struct remote_display_touch_event *event,
		struct app_state *appstate)
{
#if 0
	int touch_fd = appstate->ir_priv->input.uinput_touch_fd;

	switch(event->type) {
	case REMOTE_DISPLAY_TOUCH_DOWN:
		printf("Touch down at (%f,%f). ID=%d.\n",
			wl_fixed_to_double(event->x),
			wl_fixed_to_double(event->y),
			event->id);
		write_touch_slot(touch_fd, event->id);
		write_touch_tracking_id(touch_fd, event->id);
		write_touch_event_coords(appstate,
			wl_fixed_to_double(event->x),
			wl_fixed_to_double(event->y));
		write_touch_syn(touch_fd);
		break;
	case REMOTE_DISPLAY_TOUCH_UP:
		write_touch_slot(touch_fd, event->id);
		write_touch_tracking_id(touch_fd, event->id);
		write_touch_syn(touch_fd);
		break;
	case REMOTE_DISPLAY_TOUCH_MOTION:
		write_touch_slot(touch_fd, event->id);
		write_touch_event_coords(appstate,
			wl_fixed_to_double(event->x),
			wl_fixed_to_double(event->y));
		write_touch_syn(touch_fd);
		break;
	}
#endif
}

static int
init_surface_touch(void)
{
	printf("Init touch for surface.\n");
	return 0;
}



struct event_conv {
	uint32_t remote_display_event_type;
	uint32_t ias_event_type;
} event_conv_table[] = {
	{POINTER_HANDLE_ENTER, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_ENTER},
	{POINTER_HANDLE_LEAVE, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_LEAVE},
	{POINTER_HANDLE_MOTION, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_MOTION},
	{POINTER_HANDLE_BUTTON, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_BUTTON},
	{POINTER_HANDLE_AXIS, IAS_RELAY_INPUT_POINTER_EVENT_TYPE_AXIS},
	{KEYBOARD_HANDLE_KEYMAP, 0},
	{KEYBOARD_HANDLE_ENTER, IAS_RELAY_INPUT_KEY_EVENT_TYPE_ENTER},
	{KEYBOARD_HANDLE_LEAVE, IAS_RELAY_INPUT_KEY_EVENT_TYPE_LEAVE},
	{KEYBOARD_HANDLE_KEY, IAS_RELAY_INPUT_KEY_EVENT_TYPE_KEY},
	{KEYBOARD_HANDLE_MODIFIERS, IAS_RELAY_INPUT_KEY_EVENT_TYPE_MODIFIERS},
	{TOUCH_HANDLE_DOWN, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_DOWN},
	{TOUCH_HANDLE_UP, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_UP},
	{TOUCH_HANDLE_MOTION, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_MOTION},
	{TOUCH_HANDLE_FRAME, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_FRAME},
	{TOUCH_HANDLE_CANCEL, IAS_RELAY_INPUT_TOUCH_EVENT_TYPE_CANCEL},
};

uint32_t get_ias_type(uint32_t remote_display_event_type)
{
	unsigned long i;
	for(i = 0; i < ARRAY_LENGTH(event_conv_table); i++) {
		if(event_conv_table[i].remote_display_event_type ==
				remote_display_event_type) {
			break;
		}
	}

	return i >= ARRAY_LENGTH(event_conv_table) ? 0 :
			event_conv_table[i].ias_event_type;
}

static void
handle_surface_touch_event(struct ias_relay_input *ias_in, uint32_t surfid,
		const struct remote_display_touch_event *event, gstInputMsg *msg)
{
	printf("Touch event for surface.\n");
	uint32_t ias_type = get_ias_type(msg->type);

	switch(msg->type) {
	case TOUCH_HANDLE_DOWN:
		printf("Touch down at (%f,%f). ID=%d.\n",
			wl_fixed_to_double(event->x),
			wl_fixed_to_double(event->y),
			event->id);
		break;
	case TOUCH_HANDLE_UP:
		printf("Touch up.\n");
		break;
	case TOUCH_HANDLE_MOTION:
		printf("Touch motion at (%f,%f). ID=%d.\n",
			wl_fixed_to_double(event->x),
			wl_fixed_to_double(event->y),
			event->id);
		break;
	case TOUCH_HANDLE_FRAME:
		printf("Touch frame.\n");
		break;
	case TOUCH_HANDLE_CANCEL:
		printf("Touch cancel.\n");
		break;
	default:
		return;
		break;
	}

	ias_relay_input_send_touch(ias_in,
					ias_type,
					surfid, event->id,
					event->x, event->y,
					event->time);
}


static int
init_surface_keyboard(void)
{
	printf("Init keyboard for surface.\n");
	return 0;
}

static int
init_output_keyboard(int *uinput_keyboard_fd_out)
{
	int uinput_keyboard_fd;
	struct uinput_user_dev uidev;

	printf("Init keyboard for output.\n");
	*uinput_keyboard_fd_out = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (*uinput_keyboard_fd_out < 0) {
		printf("Cannot open uinput.\n");
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

	if (write(uinput_keyboard_fd, &uidev, sizeof(uidev)) < 0)
		return -1;

	if (ioctl(uinput_keyboard_fd, UI_DEV_CREATE) < 0)
		return -1;

	return 0;
}

static void
handle_output_key_event(const struct remote_display_key_event *event,
		struct app_state *appstate)
{
#if 0
	struct input_event ev;
	int uinput_keyboard_fd = appstate->ir_priv->input.uinput_keyboard_fd;
	int ret=0;

	printf("Keyboard event for output.\n");
	switch(event->type) {
	case REMOTE_DISPLAY_KEY_KEY:
		memset(&ev, 0, sizeof(ev));
		ev.type = EV_KEY;
		ev.code = event->key;
		ev.value = event->state;
		ret = write(uinput_keyboard_fd, &ev, sizeof(ev));
		if (ret < 0) {
			fprintf(stderr, "Failed to write keyboard key uinput.\n");
		}

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		ret = write(uinput_keyboard_fd, &ev, sizeof(ev));
		if (ret < 0) {
			fprintf(stderr, "Failed to write keyboard uinput syn.\n");
		}
		break;
	}
#endif
}


#define TOUCHID 3 //SJTODO
static void
convert_pointer_to_touch(struct remoteDisplayButtonState *button_state,
		gstInputMsg *msg,
		uint32_t *send_event)
{
	assert(button_state);
	switch(msg->type) {
	case POINTER_HANDLE_MOTION:
		printf("Pointer motion event at %f, %f.\n",
			wl_fixed_to_double(msg->p.x), wl_fixed_to_double(msg->p.y));
		/* Send touch up or down if the flag in button_state indicates
		 * a change in state, otherwise send a touch motion. */
		if (button_state->state_changed){
			button_state->state_changed = 0;
			if (button_state->touch_down && (button_state->button_states == 0)) {
				msg->type = TOUCH_HANDLE_UP;
				msg->t.id = TOUCHID;
				msg->t.x = msg->p.x;
				msg->t.y = msg->p.y;
				msg->t.time = msg->p.time;
				button_state->touch_down = 0;
				*send_event = 1;
			} else if ((button_state->touch_down == 0) &&
					button_state->button_states) {
				msg->type = TOUCH_HANDLE_DOWN;
				msg->t.id = TOUCHID;
				msg->t.x = msg->p.x;
				msg->t.y = msg->p.y;
				msg->t.time = msg->p.time;
				button_state->touch_down = 1;
				*send_event = 1;
			}
		} else if (button_state->button_states) {
			msg->type = TOUCH_HANDLE_MOTION;
			msg->t.id = TOUCHID;
			msg->t.x = msg->p.x;
			msg->t.y = msg->p.y;
			msg->t.time = msg->p.time;
			*send_event = 1;
		}
		break;
	case POINTER_HANDLE_ENTER:
		printf("Pointer enter event.\n");
		break;
	case POINTER_HANDLE_LEAVE:
		printf("Pointer leave event.\n");
		break;
	case POINTER_HANDLE_BUTTON:
		{
			uint32_t button_mask = 0;
			uint32_t button = msg->p.button - BTN_MOUSE;

			printf("Pointer button event. Button %u state = %u.\n",
				msg->p.button, msg->p.state);
			if (button >= MAX_BUTTONS) {
				fprintf(stderr, "Too many mouse buttons!\n");
				break;
			}
			button_mask = 1 << button;
			if (msg->p.state) {
				if (button_state->button_states == 0) {
					button_state->state_changed = 1;
				}
				button_state->button_states |= button_mask;
			} else {
				button_state->button_states &= ~button_mask;
				if (button_state->button_states == 0) {
					button_state->state_changed = 1;
				}
			}
		}
		break;
	case POINTER_HANDLE_AXIS:
		printf("Pointer axis event. Axis %u value = %u.\n",
			msg->p.axis, msg->p.value);
		break;
	default:

		break;
	}
}


static void
handle_output_pointer_event(const struct remote_display_pointer_event *event,
		struct remoteDisplayButtonState *button_state,
		struct app_state *appstate)
{
#if 0
	struct remote_display_touch_event touch_event;
	uint32_t send_event = 0;

	printf("Pointer event for output.\n");
	convert_pointer_to_touch(button_state, event, &touch_event, &send_event);
	if (send_event) {
		handle_output_touch_event(&touch_event, appstate);
	}
#endif
}

static void
handle_surface_pointer_event(struct ias_relay_input *ias_in, uint32_t surfid,
		struct remoteDisplayButtonState *button_state,
		gstInputMsg *msg)
{
	uint32_t send_event = 0;

	printf("Pointer event for surface.\n");
	convert_pointer_to_touch(button_state, msg, &send_event);
	if (send_event) {
		handle_surface_touch_event(ias_in, surfid, &msg->t, msg);
	}
}


static void
close_transport(struct tcpTransport *transport)
{
	printf("Closing transport...\n");
	if (transport->sockDesc >= 0) {
		close(transport->sockDesc);
		transport->sockDesc = -1;
	}
	transport->connected = 0;
	/* May need to consider tracking the state, so that any multi-touch slots
	 * that are currently "down" are sent an up event. */
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
init_transport(struct tcpTransport *transport)
{
	int ret = 0;
	char *hello = "Hello";
	struct timeval optTime = {0, 10}; /* {sec, msec} */
	transport->len = sizeof(transport->sockAddr);

	printf("Initialising transport on input receiver...\n");
	transport->sockDesc = socket(AF_INET , SOCK_DGRAM , 0);
	if (transport->sockDesc == -1) {
		fprintf(stderr, "Socket creation failed.\n");
		return;
	}
	if (setsockopt(transport->sockDesc, SOL_SOCKET, SO_SNDTIMEO, &optTime, sizeof(optTime)) < 0) {
		fprintf(stderr, "sendto timeout configuration failed\n");
		return;
	}

	transport->sockAddr.sin_family = AF_INET;
	transport->sockAddr.sin_addr.s_addr = inet_addr(transport->ipaddr);
	transport->sockAddr.sin_port = htons(transport->port);

	/* Send a simple hello message to the server so that it knows about us */
	ret = sendto(transport->sockDesc, (const char *)hello, strlen(hello),
			0, (const struct sockaddr *) &transport->sockAddr,
			transport->len);

	if (ret < 0) {
		fprintf(stderr, "Error with sending message to the server.\n");
		close(transport->sockDesc);
		transport->sockDesc = -1;
		return;
	}

	printf("Read to accept input events.\n");
	transport->connected = 1;
}

#define MESG_SIZE 100
static void *
receive_events(void * const priv_data)
{
	int ret = 0;
	struct remote_display_input_event_header header;
	struct remote_display_touch_event touch_event;
	struct remote_display_key_event key_event;
	struct remote_display_pointer_event pointer_event;
	struct input_receiver_private_data *data = priv_data;
	gstInputMsg msg;
	uint32_t ias_type;

	init_transport(&data->transport);
	data->running = 1;

	while (data->running) {
		if (data->transport.connected) {
			ret = recvfrom(data->transport.sockDesc, &msg, sizeof(msg), 0,
					(struct sockaddr *) &data->transport.sockAddr,
					&data->transport.len);
		}
		if (data->running == 0) {
			printf("Receive interrupted by shutdown.\n");
			continue;
		}
		if (data->transport.connected == 0) {
			/* Do nothing. */
		} else if (ret <= 0) {
			printf("Header receive failed.\n");
		} else if (data->appstate->surfid) {
			ias_type = get_ias_type(msg.type);
			switch(msg.type) {
			case TOUCH_HANDLE_DOWN:
			case TOUCH_HANDLE_UP:
			case TOUCH_HANDLE_MOTION:
			case TOUCH_HANDLE_FRAME:
			case TOUCH_HANDLE_CANCEL:
				ias_relay_input_send_touch(data->appstate->ias_in,
						ias_type,
						data->appstate->surfid, msg.t.id,
						msg.t.x, msg.t.y,
						msg.t.time);
				break;
			case KEYBOARD_HANDLE_ENTER:
			case KEYBOARD_HANDLE_LEAVE:
			case KEYBOARD_HANDLE_KEY:
			case KEYBOARD_HANDLE_MODIFIERS:
				ias_relay_input_send_key(data->appstate->ias_in,
						ias_type,
						data->appstate->surfid, msg.k.time,
						msg.k.key, msg.k.state,
						msg.k.mods_depressed, msg.k.mods_latched,
						msg.k.mods_locked, msg.k.group);
				break;
			case POINTER_HANDLE_ENTER:
			case POINTER_HANDLE_LEAVE:
			case POINTER_HANDLE_MOTION:
			case POINTER_HANDLE_BUTTON:
			case POINTER_HANDLE_AXIS:
				ias_relay_input_send_pointer(data->appstate->ias_in,
						ias_type,
						data->appstate->surfid,
						msg.p.x, msg.p.y,
						msg.p.button, msg.p.state,
						msg.p.axis, msg.p.value,
						msg.p.time);
				break;
			default:
				if (data->verbose > 1) {
					printf("Unknown event type for surface %d.\n",
							data->appstate->surfid);
				}
				break;
			}
			wl_display_flush(data->appstate->display);
		} else {
#if 0
			switch(header.type) {
			case REMOTE_DISPLAY_TOUCH_EVENT:
				if (data->verbose > 1) {
					printf("Touch event received for output %d...\n",
						data->appstate->output_number);
				}
				ret = recv(data->transport.sockDesc, &touch_event,
						header.size, 0);
				if (ret > 0) {
					handle_output_touch_event(&touch_event, data->appstate);
				}
				break;
			case REMOTE_DISPLAY_KEY_EVENT:
				if (data->verbose > 1) {
					printf("Key event received for output %d...\n",
						data->appstate->output_number);
				}
				ret = recv(data->transport.sockDesc, &key_event, header.size, 0);
				if (ret > 0) {
					handle_output_key_event(&key_event, data->appstate);
				}
				break;
			case REMOTE_DISPLAY_POINTER_EVENT:
				if (data->verbose > 1) {
					printf("Pointer event received for output %d...\n",
						data->appstate->output_number);
				}
				ret = recv(data->transport.sockDesc, &pointer_event, header.size, 0);
				if (ret > 0) {
					handle_output_pointer_event(&pointer_event,
							&data->button_state, data->appstate);
				}
				break;
			default:
				if (data->verbose > 1) {
					printf("Unknown event type for output %d.\n",
						data->appstate->output_number);
				}
				break;
			}
#endif
		}
		if (ret <= 0) {
			if (data->transport.connected) {
				printf("Sender has closed socket. Attempting to reconnect...\n");
				close_transport(&data->transport);
			}
			init_transport(&data->transport);
			if (!data->transport.connected) {
				sleep(1);
			}
		}
	}

	close_transport(&data->transport);
	cleanup_input(&data->input);
	free(priv_data);

	printf("Receive thread finished.\n");
	return 0;
}


void
start_event_listener(struct app_state *appstate, int *argc, char **argv)
{
	struct input_receiver_private_data *data = NULL;
	int ret = 0;
	int touch_ret = 0;
	int keyb_ret = 0;


	data = calloc(1, sizeof(*data));
	if (!data) {
		fprintf(stderr, "Failed to allocate memory for input receiver private data.\n");
		return;
	} else {
		int port = 0;
		const struct weston_option options[] = {
			{ WESTON_OPTION_STRING,  "relay_input_ipaddr", 0, &data->transport.ipaddr},
			{ WESTON_OPTION_INTEGER, "relay_input_port", 0, &port},
		};

		parse_options(options, ARRAY_LENGTH(options), argc, argv);
		data->transport.port = port;
	}

	if ((data->transport.ipaddr != NULL) && (data->transport.ipaddr[0] != 0)) {
		printf("Receiving input events from %s:%d.\n", data->transport.ipaddr,
			data->transport.port);
	} else {
		printf("Not listening for input events; network configuration not set.\n");
		free(data);
		data = NULL;
		return;
	}

	if (appstate->surfid) {
		touch_ret = init_surface_touch();
		keyb_ret = init_surface_keyboard();
	} else {
		int i = 0;
		struct output *output;

		touch_ret = init_output_touch(&(data->input.uinput_touch_fd));
		/* Assume that the outputs are listed in the same order. */
		wl_list_for_each_reverse(output, &appstate->output_list, link) {
			printf("Output %d is at %d, %d.\n", i, output->x, output->y);
			if (appstate->output_number == i) {
				printf("Sending events to output %d at %d, %d.\n",
					i, output->x, output->y);
				appstate->output_origin_x = output->x;
				appstate->output_origin_y = output->y;
				appstate->output_width = output->width;
				appstate->output_height = output->height;
			}
			i++;
		}
		keyb_ret = init_output_keyboard(&(data->input.uinput_keyboard_fd));
	}

	if (touch_ret) {
		fprintf(stderr, "Error initialising touch input - %d.\n", touch_ret);
		free(data);
		return;
	}
	if (keyb_ret) {
		fprintf(stderr, "Error initialising keyboard input - %d.\n", keyb_ret);
		free(data);
		return;
	}

	ret = pthread_create(&data->input_thread, NULL, receive_events, data);
	if (ret) {
		fprintf(stderr, "Transport thread creation failure: %d\n", ret);
		free(data);
		return;
	}

	data->appstate = appstate;
	data->verbose = appstate->verbose;
	appstate->ir_priv = data;
	printf("Input receiver started.\n");
}

void
stop_event_listener(struct input_receiver_private_data *priv_data)
{
	if (!priv_data) {
		return;
	}
	priv_data->running = 0;
	if (priv_data->verbose) {
		printf("Waiting for input receiver thread to finish...\n");
	}
	shutdown(priv_data->transport.sockDesc, SHUT_RDWR);
	pthread_join(priv_data->input_thread, NULL);
	printf("Input receiver thread stopped.\n");
}
