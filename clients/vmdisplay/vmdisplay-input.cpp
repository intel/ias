/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-input.cpp
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
 *   Imports the inputs events from keyboard/mouse/touch to the exporter domain
 *-----------------------------------------------------------------------------
 */

#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <vector>
#include <pthread.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include "vm-shared.h"
#include "vmdisplay-shared.h"
#include "vmdisplay-server.h"
#include "vmdisplay-server-network.h"

typedef int32_t wl_fixed_t;

static inline double wl_fixed_to_double(wl_fixed_t f)
{
	union {
		double d;
		int64_t i;
	} u;

	u.i = ((1023LL + 44LL) << 52) + (1LL << 51) + f;

	return u.d - (3LL << 43);
}

class VMDisplayInput {
public:
	VMDisplayInput():hyper_comm_input(NULL),
	    running(false),
	    uinput_touch_fd(-1), uinput_keyboard_fd(-1), uinput_pointer_fd(-1) {
	};
	int init(int domid, CommunicationChannelType comm_type,
		 const char *comm_arg);
	int cleanup();
	int run();
	void stop();
private:
	void handle_touch_event(const vmdisplay_touch_event & event);
	void handle_key_event(const vmdisplay_key_event & event);
	void handle_pointer_event(const vmdisplay_pointer_event & event);
	int init_touch();
	int init_keyboard();
	int inti_pointer();

	HyperCommunicatorInterface *hyper_comm_input;
	bool running;
	int uinput_touch_fd;
	int uinput_keyboard_fd;
	int uinput_pointer_fd;
};

int VMDisplayInput::init_touch()
{
	struct uinput_user_dev uidev;

	uinput_touch_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (uinput_touch_fd < 0) {
		printf("Cannot open uinput\n");
		return -1;
	}

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
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "vmdisplay-input-touch");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 0x5853;
	uidev.id.product = 0xfffe;
	uidev.id.version = 0x01;

	uidev.absmin[ABS_MT_POSITION_X] = 0;
	uidev.absmax[ABS_MT_POSITION_X] = 32767;
	uidev.absmin[ABS_MT_POSITION_Y] = 0;
	uidev.absmax[ABS_MT_POSITION_Y] = 32767;
	uidev.absmin[ABS_MT_SLOT] = 0;
	uidev.absmax[ABS_MT_SLOT] = 7;

	uidev.absmin[ABS_X] = 0;
	uidev.absmax[ABS_X] = 32767;
	uidev.absmin[ABS_Y] = 0;
	uidev.absmax[ABS_Y] = 32767;

	if (write(uinput_touch_fd, &uidev, sizeof(uidev)) < 0)
		return -1;

	if (ioctl(uinput_touch_fd, UI_DEV_CREATE) < 0)
		return -1;

	return 0;
}

int VMDisplayInput::init_keyboard()
{
	int uinput_keyboard_fd;
	struct uinput_user_dev uidev;

	uinput_keyboard_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (uinput_keyboard_fd < 0) {
		printf("Cannot open uinput\n");
		return -1;
	}

	ioctl(uinput_keyboard_fd, UI_SET_EVBIT, EV_KEY);
	for (unsigned int i = 0; i < 248; i++) {
		ioctl(uinput_keyboard_fd, UI_SET_KEYBIT, i);
	}

	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "vmdisplay-input-keyboard");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 0x5853;
	uidev.id.product = 0xfffe;
	uidev.id.version = 0x01;

	if (write(uinput_keyboard_fd, &uidev, sizeof(uidev)) < 0)
		return -1;

	if (ioctl(uinput_keyboard_fd, UI_DEV_CREATE) < 0)
		return -1;

	return 0;
}

static int init_pointer()
{
	int uinput_pointer_fd;
	struct uinput_user_dev uidev;

	uinput_pointer_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (uinput_pointer_fd < 0) {
		printf("Cannot open uinput\n");
		return -1;
	}

	ioctl(uinput_pointer_fd, UI_SET_EVBIT, EV_ABS);
	ioctl(uinput_pointer_fd, UI_SET_ABSBIT, ABS_X);
	ioctl(uinput_pointer_fd, UI_SET_ABSBIT, ABS_Y);
	ioctl(uinput_pointer_fd, UI_SET_EVBIT, EV_REL);
	ioctl(uinput_pointer_fd, UI_SET_RELBIT, REL_WHEEL);
	ioctl(uinput_pointer_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(uinput_pointer_fd, UI_SET_KEYBIT, BTN_MOUSE);
	ioctl(uinput_pointer_fd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(uinput_pointer_fd, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl(uinput_pointer_fd, UI_SET_KEYBIT, BTN_MIDDLE);

	ioctl(uinput_pointer_fd, UI_SET_EVBIT, EV_MSC);
	ioctl(uinput_pointer_fd, UI_SET_MSCBIT, MSC_SCAN);

	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "vmdisplay-input-pointer");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 0x5853;
	uidev.id.product = 0xfffe;
	uidev.id.version = 0x01;

	uidev.absmin[ABS_X] = 0;
	uidev.absmax[ABS_X] = 32767;
	uidev.absmin[ABS_Y] = 0;
	uidev.absmax[ABS_Y] = 32767;

	if (write(uinput_pointer_fd, &uidev, sizeof(uidev)) < 0)
		return -1;

	if (ioctl(uinput_pointer_fd, UI_DEV_CREATE) < 0)
		return -1;

	return 0;
}

int VMDisplayInput::init(int domid, CommunicationChannelType comm_type,
			 const char *comm_args)
{
	switch (comm_type) {
	case CommunicationChannelNetwork:
		hyper_comm_input = new NetworkCommunicator();
		break;
	}

	if (hyper_comm_input->init(domid, HyperCommunicatorInterface::Receiver,
				   comm_args)) {
		printf("client init failed\n");
		delete hyper_comm_input;
		hyper_comm_input = NULL;
		cleanup();
		return -1;
	}

	if (init_touch()) {
		printf("Cannot initialize touch device\n");
		cleanup();
		return -1;
	}

	if (init_pointer()) {
		printf("Cannot initialize pointer device\n");
		cleanup();
		return -1;
	}

	if (init_keyboard()) {
		printf("Cannot initialize keyboard device\n");
		cleanup();
		return -1;
	}

	running = true;

	return 0;
}

int VMDisplayInput::cleanup()
{
	if (hyper_comm_input) {
		hyper_comm_input->cleanup();
		delete hyper_comm_input;
		hyper_comm_input = NULL;
	}

	if (uinput_pointer_fd >= 0) {
		ioctl(uinput_pointer_fd, UI_DEV_DESTROY);
		close(uinput_pointer_fd);
		uinput_pointer_fd = -1;
	}

	if (uinput_keyboard_fd >= 0) {
		ioctl(uinput_keyboard_fd, UI_DEV_DESTROY);
		close(uinput_keyboard_fd);
		uinput_keyboard_fd = -1;
	}

	if (uinput_touch_fd >= 0) {
		ioctl(uinput_touch_fd, UI_DEV_DESTROY);
		close(uinput_touch_fd);
		uinput_touch_fd = -1;
	}

	return 0;
}

void VMDisplayInput::handle_touch_event(const vmdisplay_touch_event & event)
{
	struct input_event ev;
	float x, y;
        ssize_t ret;

	switch (event.type) {
	case VMDISPLAY_TOUCH_DOWN:
		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_SLOT;
		ev.value = event.id;
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_TRACKING_ID;
		ev.value = event.id;
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		x = wl_fixed_to_double(event.x);
		y = wl_fixed_to_double(event.y);

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_POSITION_X;
		ev.value = wl_fixed_to_double(event.x);
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_POSITION_Y;
		ev.value = wl_fixed_to_double(event.y);
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		break;

	case VMDISPLAY_TOUCH_UP:
		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_SLOT;
		ev.value = event.id;
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_TRACKING_ID;
		ev.value = -1;
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		break;

	case VMDISPLAY_TOUCH_MOTION:
		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_SLOT;
		ev.value = event.id;
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_POSITION_X;
		ev.value = wl_fixed_to_double(event.x);
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_MT_POSITION_Y;
		ev.value = wl_fixed_to_double(event.y);
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		ret = write(uinput_touch_fd, &ev, sizeof(ev));

		break;
	}
    if (ret < 0)
        printf("failed to handle input touch event\n");
}

void VMDisplayInput::handle_key_event(const vmdisplay_key_event & event)
{
	struct input_event ev;
        ssize_t ret;

	switch (event.type) {
	case VMDISPLAY_KEY_KEY:
		memset(&ev, 0, sizeof(ev));
		ev.type = EV_KEY;
		ev.code = event.key;
		ev.value = event.state;
		ret = write(uinput_keyboard_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		ret = write(uinput_keyboard_fd, &ev, sizeof(ev));

		break;
	}
        if (ret < 0)
            printf("failed to handle input key event\n");

}

void VMDisplayInput::handle_pointer_event(const vmdisplay_pointer_event & event)
{
	struct input_event ev;
        ssize_t ret = 0;

	switch (event.type) {
	case VMDISPLAY_POINTER_BUTTON:
		memset(&ev, 0, sizeof(ev));
		ev.type = EV_MSC;
		ev.code = MSC_SCAN;
		ev.value = 90001;
		ret = write(uinput_pointer_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_KEY;
		ev.code = event.button;
		ev.value = event.state;
		ret = write(uinput_pointer_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		ret = write(uinput_pointer_fd, &ev, sizeof(ev));

		break;

	case VMDISPLAY_POINTER_MOTION:
		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_X;
		ev.value = wl_fixed_to_double(event.x);
		ret = write(uinput_pointer_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_ABS;
		ev.code = ABS_Y;
		ev.value = wl_fixed_to_double(event.y);
		ret = write(uinput_pointer_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		ret = write(uinput_pointer_fd, &ev, sizeof(ev));
		break;

	case VMDISPLAY_POINTER_AXIS:
		memset(&ev, 0, sizeof(ev));
		ev.type = EV_REL;
		ev.code = REL_WHEEL;
		ev.value = event.value;
		ret = write(uinput_pointer_fd, &ev, sizeof(ev));

		memset(&ev, 0, sizeof(ev));
		ev.type = EV_SYN;
		ev.code = 0;
		ev.value = 0;
		ret = write(uinput_pointer_fd, &ev, sizeof(ev));

		break;
	}
    if (ret < 0)
        printf("failed to handle input pointer event\n");
}

int VMDisplayInput::run()
{
	struct vmdisplay_input_event_header header;
	struct vmdisplay_touch_event touch_event;
	struct vmdisplay_key_event key_event;
	struct vmdisplay_pointer_event pointer_event;
	int ret = -1;

	while (running) {
		if (hyper_comm_input) {
			ret =
			    hyper_comm_input->recv_data(&header,
							sizeof(header));

			if (ret < 0) {
				printf("Lost connection with server\n");
				running = false;
				break;
			}

			switch (header.type) {
			case VMDISPLAY_TOUCH_EVENT:
				ret =
				    hyper_comm_input->recv_data(&touch_event,
								header.size);
				handle_touch_event(touch_event);
				break;
			case VMDISPLAY_KEY_EVENT:
				ret =
				    hyper_comm_input->recv_data(&key_event,
								header.size);
				handle_key_event(key_event);
				break;
			case VMDISPLAY_POINTER_EVENT:
				ret =
				    hyper_comm_input->recv_data(&pointer_event,
								header.size);
				handle_pointer_event(pointer_event);
				break;
			default:
				printf("Unknown event type\n");
				break;
			}
		}
	}

	return 0;
}

void VMDisplayInput::stop()
{
	running = false;
}

static VMDisplayInput *input_server = NULL;

static void signal_int(int signum)
{
	if (input_server)
		input_server->stop();
}

void signal_callback_handler(int signum)
{
	printf("Caught signal SIGPIPE %d\n", signum);
}

void print_usage(const char *path)
{
	printf("Usage: %s <dom_id> <comm_type> <comm_arg>\n", path);
	printf
	    ("       dom_id if of remote domain that will be sharing input\n");
	printf
	    ("       comm_type type of communication channel used by remote domain to share input\n");
	printf("       comm_arg communication channel specific arguments\n\n");
	printf("e.g.:\n");
	printf("%s 2 --xen \"shared_input\"\n", path);
	printf("%s 2 --net \"10.103.104.25:5555\"\n", path);
}

int main(int argc, char *argv[])
{
	struct sigaction sigint;
	CommunicationChannelType comm_type;

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);
	signal(SIGPIPE, signal_callback_handler);
	VMDisplayInput server;

	if (argc < 4) {
		print_usage(argv[0]);
		return -1;
	}

	if (strcmp(argv[2], "--net") == 0) {
		comm_type = CommunicationChannelNetwork;
	} else {
		print_usage(argv[0]);
		return -1;
	}

	if (server.init(atoi(argv[1]), comm_type, argv[3])) {
		printf("Cannot initialize input server\n");
		return -1;
	}

	input_server = &server;

	server.run();
	server.cleanup();
}
