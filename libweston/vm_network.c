#include "vm_comm.h"
#include "renderer-gl/vm-shared.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <stdlib.h>

int sock_fd;
int client_sock_fd;
int portno;
struct sockaddr_in server_addr, client_addr;
pthread_t thread;
void* listener_thread(void *arg);


void* listener_thread(void *arg)
{
	socklen_t clilen;

	clilen = sizeof(client_addr);
	client_sock_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &clilen);

	return NULL;
}

static void signal_callback_handler(int signum)
{
	if (client_sock_fd >= 0) {
		printf("VM client disconnected\n");
		close(client_sock_fd);
		client_sock_fd = -1;
		/* Kick off thread to start listening for new connection */
		pthread_create(&thread, NULL, listener_thread, NULL);
	}
}

static int hyper_communication_network_init(int dom_id, int buffer_size,
					    const char *args)
{
	(void) dom_id;
	(void) buffer_size;
	/* Local copy of args, as strtok is modyfing them */
	char args_tmp[256];
	char *addr;
	char *port;
	int reuse_enable = 1;
	int tcp_nodelay_enable = 1;

	if (!strlen(args)) {
		printf("No valid parameters for network plugin\n");
		return -1;
	}

	strncpy(args_tmp, args, 255);
	args_tmp[255] = '\0';

	addr = strtok(args_tmp, ":");
	if (!addr) {
		printf("Cannot parse parameters\n");
		return -1;
	}

	port = strtok(NULL, ":");
	if (!port) {
		printf("Cannot parse parameters\n");
		return -1;
	}

	printf("Network socket listening on %s:%s\n", addr, port);

	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		printf("Cannot create socket\n");
		return -1;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	portno = atoi(port);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(addr);
	server_addr.sin_port = htons(portno);
	setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_enable, sizeof(reuse_enable));
	setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay_enable, sizeof(tcp_nodelay_enable));

	if (bind(sock_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
		printf("Cannot bind socket\n");
		close(sock_fd);
		sock_fd = -1;
		return -1;
	}

	/*
	 * Start listening on socket and register SIGPIPE signal handler,
	 * otherwise whenever client will disconnect, weston will be killed
	 */
	listen(sock_fd, 1);
	signal(SIGPIPE, signal_callback_handler);

	/* Kick off thread to accept connection from client, to not block main thread */
	pthread_create(&thread, NULL, listener_thread, NULL);

	return 0;
}

static void hyper_communication_network_cleanup(void)
{
	if (client_sock_fd >= 0) {
		close(client_sock_fd);
		client_sock_fd = -1;
	} else {
		pthread_join(thread, NULL);
	}

	if (sock_fd >= 0) {
		close(sock_fd);
		sock_fd = -1;
	}
}

static int hyper_communication_network_send_data(void *data, int len)
{
	if (client_sock_fd >= 0) {
		return send(client_sock_fd, data, len, 0);
	}
	return -1;
}

static int hyper_communication_network_space(void)
{
	/*
	 * It seems there is no way to query what is size
	 * of internal buffer used by network socket,
	 * so just assume that is big enought for our needs
	 */
	return INT_MAX;
}

int __attribute__((__visibility__("default")))
init_comm(struct hyper_communication_interface * comm_interface, int dom_id,
	  int buffer_size, const char *args)
{
	int result = hyper_communication_network_init(dom_id,
						      buffer_size, args);
	if (result != 0) {
		return -1;
	}
	comm_interface->cleanup = hyper_communication_network_cleanup;
	comm_interface->send_data = hyper_communication_network_send_data;
	comm_interface->available_space = hyper_communication_network_space;
	return 0;
}
