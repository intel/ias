#include "vm_comm.h"
#include "renderer-gl/vm-shared.h"
#include <libxenvchan.h>

static struct libxenvchan *vchan;

static int hyper_communication_xen_init(int dom_id, int buffer_size,
					const char *args)
{
	(void) args;

	vchan = libxenvchan_server_init(NULL, dom_id, "data/shared_surfaces",
					buffer_size, buffer_size);
	return (vchan != 0);
}

static void hyper_communication_xen_cleanup(void)
{
	if (vchan) {
		libxenvchan_close(vchan);
		vchan = NULL;
	}
}

static int hyper_communication_xen_send_data(void *data, int len)
{
	if (vchan) {
		return libxenvchan_send(vchan, data, len);
	}
	return -1;
}

static int hyper_communication_xen_space(void)
{
	if (vchan)
		return libxenvchan_buffer_space(vchan);
	return 0;
}

int __attribute__((__visibility__("default")))
init_comm(struct hyper_communication_interface * comm_interface, int dom_id,
	  int buffer_size, const char *args)
{
	int result = hyper_communication_xen_init(dom_id, buffer_size, args);
	if (result != 1) {
		return -1;
	}
	comm_interface->cleanup = hyper_communication_xen_cleanup;
	comm_interface->send_data = hyper_communication_xen_send_data;
	comm_interface->available_space = hyper_communication_xen_space;
	return 0;
}
