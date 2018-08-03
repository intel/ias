#ifndef __VM_COMM_H__
#define __VM_COMM_H__

struct hyper_communication_interface {
	void (*cleanup)(void);
	int (*send_data)(void *data, int len);
	int (*available_space)(void);
};

typedef int (*init_comm_interface)(struct hyper_communication_interface * comm_interface,
				   int dom_id, int buffer_size,
				   const char *args);

int init_comm(struct hyper_communication_interface * comm_interface,
	      int dom_id, int buffer_size, const char *args);

#endif // __VM_COMM_H__
