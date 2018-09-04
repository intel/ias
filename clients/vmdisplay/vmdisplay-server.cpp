/*
 *-----------------------------------------------------------------------------
 * Filename: vmdisplay-server.cpp
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
 *   VMDisplay Server: Hyper DMABUF initialize & deamon to handle incoming data.
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
#include <poll.h>
#include "vm-shared.h"
#include "vmdisplay-shared.h"
#include "vmdisplay-server.h"
#include "vmdisplay-server-hyperdmabuf.h"
#include "vmdisplay-server-network.h"

/*
 * Number of pending connections that can be queued,
 * until new vmdisplay-wayland instances will be getting
 * error during connection
 */
#define SOCKET_BACKLOG 25

struct output_data {
	/*
	 * File descriptor to anonymous
	 * file that will contain metadata
	 * of surfaces placed on given output
	 */
	int shm_fd;

	/* Address of mmaped metadata file */
	void *shm_addr;
};

class VMDisplayServer {
public:
	VMDisplayServer():hyper_comm_metadata(NULL), hyper_comm_input(NULL),
	    running(false), domid(-1), current_buf(NULL) {
	} int init(int domid,
		   CommunicationChannelType surf_comm_type,
		   const char *surf_comm_args,
		   CommunicationChannelType input_comm_type,
		   const char *input_comm_args);
	int cleanup();
	int run();
	void stop();
	int process_metadata();
	int process_input();
private:
	int receive_metadata(char *buffer, int len);
	int send_message(int clinet_socket_fd,
			 enum vmdisplay_msg_type type, int32_t data);
	int init_outputs();

	HyperCommunicatorInterface *hyper_comm_metadata;
	HyperCommunicatorInterface *hyper_comm_input;
	bool running;
	pthread_t metadata_thread;
	pthread_t input_thread;
	pthread_mutex_t mutex;
	int server_socket;
	std::vector < int >client_sockets;

	char *current_buf;
	int current_buf_len;

	int domid;
	char socket_path[255];
	struct output_data outputs[VM_MAX_OUTPUTS];
};

void *metadata_processing_thread(void *arg)
{
	VMDisplayServer *server = (VMDisplayServer *) arg;

	if (!server)
		return NULL;

	server->process_metadata();

	return NULL;
}

void *input_processing_thread(void *arg)
{
	VMDisplayServer *server = (VMDisplayServer *) arg;

	if (!server)
		return NULL;

	server->process_input();

	return NULL;
}

int send_fd(int socket, int fd)
{
	char tmp = '?';
	struct msghdr hdr;
	struct iovec iovec;

	char msg_buf[CMSG_SPACE(sizeof(int))];

	iovec.iov_base = &tmp;
	iovec.iov_len = sizeof(tmp);

	memset(&hdr, 0, sizeof(hdr));

	hdr.msg_iov = &iovec;
	hdr.msg_iovlen = 1;
	hdr.msg_control = msg_buf;
	hdr.msg_controllen = CMSG_LEN(sizeof(int));

	struct cmsghdr *cmsg_hdr = CMSG_FIRSTHDR(&hdr);
	cmsg_hdr->cmsg_level = SOL_SOCKET;
	cmsg_hdr->cmsg_type = SCM_RIGHTS;
	cmsg_hdr->cmsg_len = CMSG_LEN(sizeof(int));

	*(int *)CMSG_DATA(cmsg_hdr) = fd;

	int ret = sendmsg(socket, &hdr, 0);

	if (ret == -1) {
		printf("Failed to share fd\n");
	}

	return ret;
}

int VMDisplayServer::init_outputs()
{
	char path[255];

	/* Create for each possible output its metadata file (and unlink it, so it becomes anonymouse one */
	for (int i = 0; i < VM_MAX_OUTPUTS; ++i) {
		snprintf(path, 255, "/run/vmdisplay_%d_metadata", i);
		outputs[i].shm_fd =
		    open(path, O_RDWR | O_CREAT, S_IRWXU | S_IRGRP);
		if (outputs[i].shm_fd < 0) {
			printf("Cannot create output metadata file\n");
			return -1;
		}

		unlink(path);
		if (ftruncate(outputs[i].shm_fd, METADATA_BUFFER_SIZE) < 0)
                        printf("truncating failed\n");

		outputs[i].shm_addr =
		    mmap(NULL, METADATA_BUFFER_SIZE,
			 PROT_READ | PROT_WRITE, MAP_SHARED,
			 outputs[i].shm_fd, 0);

		if (!outputs[i].shm_addr) {
			printf("Cannot mmap metadata file\n");
			return -1;
		}
	}

	return 0;
}

int VMDisplayServer::init(int domid,
			  CommunicationChannelType surf_comm_type,
			  const char *surf_comm_args,
			  CommunicationChannelType input_comm_type,
			  const char *input_comm_args)
{
	struct sockaddr_un addr;
	char *runtime_dir;

	runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		printf("XDG_RUNTIME_DIR not set\n");
		cleanup();
		return -1;
	}

	current_buf = new char[METADATA_BUFFER_SIZE];
	current_buf_len = 0;

	switch (surf_comm_type) {
	case CommunicationChannelNetwork:
		hyper_comm_metadata = new NetworkCommunicator();
		break;

	case CommunicationChannelHyperDMABUF:
		hyper_comm_metadata = new HyperDMABUFCommunicator();
		break;
	}

	if (hyper_comm_metadata->
	    init(domid, HyperCommunicatorInterface::Receiver, surf_comm_args)) {
		printf("Compositor not running in domain %d ?\n", domid);
		delete hyper_comm_metadata;
		hyper_comm_metadata = NULL;
		cleanup();
		return -1;
	}

	switch (input_comm_type) {
	case CommunicationChannelNetwork:
		hyper_comm_input = new NetworkCommunicator();
		break;
	}

	if (hyper_comm_input->init(domid, HyperCommunicatorInterface::Sender,
				   input_comm_args)) {
		delete hyper_comm_input;
		hyper_comm_input = NULL;
		cleanup();
		return -1;
	}

	if (init_outputs() < 0) {
		printf("Cannot initialize outputs\n");
		cleanup();
		return -1;
	}

	server_socket = socket(AF_UNIX, SOCK_STREAM, 0);

	if (server_socket < 0) {
		perror("Error while opening socket\n");
		cleanup();
		return -1;
	}

	snprintf(socket_path, 255, "%s/vmdisplay-%d", runtime_dir, domid);

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
	unlink(addr.sun_path);
	bind(server_socket, (struct sockaddr *)&addr, sizeof(addr));

	if (listen(server_socket, SOCKET_BACKLOG) == -1) {
		perror("Listening error\n");
		cleanup();
		return -1;
	}

	this->domid = domid;
	running = true;
	pthread_mutex_init(&mutex, NULL);
	pthread_create(&metadata_thread, NULL, metadata_processing_thread,
		       this);
	pthread_create(&input_thread, NULL, input_processing_thread, this);

	return 0;
}

int VMDisplayServer::cleanup()
{
	if (running) {
		pthread_join(metadata_thread, NULL);
		pthread_join(input_thread, NULL);
		pthread_mutex_destroy(&mutex);
	}

	if (hyper_comm_metadata) {
		hyper_comm_metadata->cleanup();
		delete hyper_comm_metadata;
		hyper_comm_metadata = NULL;
	}

	if (hyper_comm_input) {
		hyper_comm_input->cleanup();
		delete hyper_comm_input;
		hyper_comm_input = NULL;
	}

	std::vector < int >::iterator it;
	for (it = client_sockets.begin(); it != client_sockets.end(); ++it) {
		close(*(it));
	}

	if (server_socket) {
		close(server_socket);
		unlink(socket_path);
	}

	if (current_buf) {
		delete[]current_buf;
		current_buf = NULL;
	}

	return 0;
}

int VMDisplayServer::run()
{
	int client_sockfd;
	struct pollfd fds[] = {
		{server_socket, POLLIN, 0},
	};

	while (running) {
		int ret = poll(fds, 1, 1000);
		if (ret < 0) {
			running = false;
			break;
		} else if (ret > 0) {
			if ((client_sockfd =
			     accept(server_socket, NULL, NULL)) == -1) {
				perror("Accept error\n");
				continue;
			} else {
				/* New client has connected */
				pthread_mutex_lock(&mutex);
				/* Make its socket nonblocking */
				fcntl(client_sockfd, F_SETFL,
				      fcntl(client_sockfd, F_GETFL,
					    0) | O_NONBLOCK);
				client_sockets.push_back(client_sockfd);

				/*Send init message and fds of all outputs metadata files */
				send_message(client_sockfd, VMDISPLAY_INIT_MSG,
					     VM_MAX_OUTPUTS);
				for (int i = 0; i < VM_MAX_OUTPUTS; i++)
					send_fd(client_sockfd,
						outputs[i].shm_fd);
				pthread_mutex_unlock(&mutex);
			}
		}
	}

	return 0;
}

void VMDisplayServer::stop()
{
	running = false;
}

int VMDisplayServer::process_metadata()
{
	int rc;
	int output_num;
	void *surfaces_metadata[VM_MAX_OUTPUTS];

	for (int i = 0; i < VM_MAX_OUTPUTS; i++)
		surfaces_metadata[i] = outputs[i].shm_addr;

	while (running) {
		output_num =
		    hyper_comm_metadata->recv_metadata(surfaces_metadata);

		/* TODO decide if we should be waiting for weston to start again or just retun error and restart whole vmdisplay server */
		if (output_num < 0) {
			printf("Lost connection to Dom%d compositor\n", domid);
			running = false;
			break;
		}

		pthread_mutex_lock(&mutex);

		std::vector < int >::iterator it;
		for (it = client_sockets.begin(); it != client_sockets.end();) {
			rc = send_message((*it), VMDISPLAY_METADATA_UPDATE_MSG,
					  output_num);
			if (rc < 0) {
				if (errno == EPIPE) {
					printf("Client closed\n");
					close(*it);
					it = client_sockets.erase(it);
					continue;
				}
			}
			it++;
		}
		pthread_mutex_unlock(&mutex);
	}

	return 0;
}

int VMDisplayServer::process_input()
{
	std::vector < int >::iterator it;
	struct vmdisplay_input_event_header header;
	struct vmdisplay_touch_event touch_event;
	struct vmdisplay_key_event key_event;
	struct vmdisplay_pointer_event pointer_event;
	int rc, i;
	/* TODO assuming now that max 255 client apps will be running */
	struct pollfd fds[255];

	while (running) {
		pthread_mutex_lock(&mutex);
		i = 0;
		for (it = client_sockets.begin();
		     it != client_sockets.end() && i < 255; it++) {
			fds[i++] = (pollfd) {
			(*it), POLLIN, 0};
		}
		pthread_mutex_unlock(&mutex);

		int ret = poll(fds, i, 100);
		if (ret < 0) {
			running = false;
			break;
		} else if (ret > 0) {
			i = 0;
			pthread_mutex_lock(&mutex);
			for (it = client_sockets.begin();
			     it != client_sockets.end();) {
				if (fds[i++].revents != POLLIN) {
					it++;
					continue;
				}
				rc = recv((*it), &header, sizeof(header), 0);

				if (rc <= 0) {
					if (rc == 0 || errno == EPIPE) {
						printf("Cleint closed\n");
						it = client_sockets.erase(it);
						continue;
					}
				} else {
					switch (header.type) {
					case VMDISPLAY_TOUCH_EVENT:
						do {
							rc = recv((*it),
								  &touch_event,
								  header.size,
								  0);
						} while (rc <= 0 && rc != 0
							 && errno != EPIPE);

						if (hyper_comm_input) {
							rc = hyper_comm_input->
							    send_data(&header,
								      sizeof
								      (header));
							rc = hyper_comm_input->
							    send_data
							    (&touch_event,
							     sizeof
							     (touch_event));
						}
						break;
					case VMDISPLAY_KEY_EVENT:
						do {
							rc = recv((*it),
								  &key_event,
								  header.size,
								  0);
						} while (rc <= 0 && rc != 0
							 && errno != EPIPE);

						if (hyper_comm_input) {
							rc = hyper_comm_input->
							    send_data(&header,
								      sizeof
								      (header));
							rc = hyper_comm_input->
							    send_data
							    (&key_event,
							     sizeof(key_event));

						}
						break;
					case VMDISPLAY_POINTER_EVENT:
						do {
							rc = recv((*it),
								  &pointer_event,
								  header.size,
								  0);
						} while (rc <= 0 && rc != 0
							 && errno != EPIPE);

						if (hyper_comm_input) {
							rc = hyper_comm_input->
							    send_data(&header,
								      sizeof
								      (header));
							rc = hyper_comm_input->
							    send_data
							    (&pointer_event,
							     sizeof
							     (pointer_event));
						}
						break;
					default:
						printf
						    ("Unknown input event type %d\n",
						     header.type);
						break;
					}
				}
				it++;
			}
			pthread_mutex_unlock(&mutex);
		}
	}

	return 0;
}

int VMDisplayServer::send_message(int client_socket_fd,
				  enum vmdisplay_msg_type type, int32_t data)
{
	struct vmdisplay_msg msg;
	memset(&msg, 0, sizeof(msg));
	msg.type = type;
	msg.display_num = data;
	return send(client_socket_fd, &msg, sizeof(msg), 0);
}

static VMDisplayServer *vm_display_server = NULL;

static void signal_int(int signum)
{
	if (vm_display_server)
		vm_display_server->stop();
}

void signal_callback_handler(int signum)
{
	printf("Caught signal SIGPIPE %d\n", signum);
}

void print_usage(const char *path)
{
	printf
	    ("Usage: %s <dom_id> <surf_comm_type> <surf_comm_arg> <input_comm_type> <input_comm_args>\n",
	     path);
	printf
	    ("       dom_id if of remote domain that will be sharing surfaces\n");
	printf
	    ("       surf_comm_type type of communication channel used by remote domain to share surfaces metadata\n");
	printf
	    ("       surf_comm_arg communication channel specific arguments\n\n");
	printf
	    ("       input_comm_type type of communication channel used by local domain to share input\n");
	printf
	    ("       input_comm_arg communication channel specific arguments\n\n");
	printf("e.g.:\n");
	printf("%s 2 --xen \"shared_surfaces\" --xen \"shared_input\"\n", path);
	printf("%s 2 --net \"10.103.104.25:5555\" --net \"0:5554\"\n", path);
}

int main(int argc, char *argv[])
{
	VMDisplayServer server;
	struct sigaction sigint;
	CommunicationChannelType surf_comm_type;
	CommunicationChannelType input_comm_type;

	sigint.sa_handler = signal_int;
	sigemptyset(&sigint.sa_mask);
	sigint.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sigint, NULL);

	signal(SIGPIPE, signal_callback_handler);

	if (argc < 6) {
		print_usage(argv[0]);
		return -1;
	}

	if (strcmp(argv[2], "--net") == 0) {
		surf_comm_type = CommunicationChannelNetwork;
	} else if (strcmp(argv[2], "--hdma") == 0) {
		surf_comm_type = CommunicationChannelHyperDMABUF;
	} else {
		print_usage(argv[0]);
		return -1;
	}

	if (strcmp(argv[4], "--net") == 0) {
		input_comm_type = CommunicationChannelNetwork;
	} else {
		print_usage(argv[0]);
		return -1;
	}

	if (server.init(atoi(argv[1]), surf_comm_type, argv[3],
			input_comm_type, argv[5]) < 0) {
		printf("Server init failed\n");
		return -1;
	}
	printf("Starting vmdisplay server for domain %d\n", atoi(argv[1]));
	vm_display_server = &server;

	server.run();
	server.cleanup();

	return 0;
}
