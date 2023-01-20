#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

#include "bios.h"
#include "tinyos.h"
#include "util.h"
#include "kernel_pipe.h"
#include "kernel_streams.h"
#include "kernel_dev.h"

typedef enum {
	SOCKET_LISTENER,
	SOCKET_UNBOUND,
	SOCKET_PEER
}socket_type;

typedef struct socket_control_block SCB;
//SCB* PORT_MAP[MAX_PORT+1]={NULL};

typedef struct connection_request {
	int admitted; 
	SCB* peer;
	CondVar connected_cv;
	rlnode queue_node;
}CR;


typedef struct listener_socket {
	rlnode queue;
	CondVar req_available;
}listener_socket;

typedef struct unbound_socket {
	rlnode unbound_socket;
}unbound_socket;


typedef struct peer_socket {
	SCB* peer;
	Pipe_CB* write_pipe;
	Pipe_CB* read_pipe;
}peer_socket;




// Socket CB
typedef struct socket_control_block {
	int refcount;		// uint
	FCB* fcb;
	socket_type type;
	port_t port;

	union {
		listener_socket listener_s;
		unbound_socket unbound_s;
		peer_socket peer_s;
	};
}SCB;


// PORT Map table
	
Fid_t sys_Socket(port_t port);
int sys_Listen(Fid_t sock);
Fid_t sys_Accept(Fid_t lsock);
int sys_Connect(Fid_t sock, port_t port, timeout_t timeout);
int sys_ShutDown(Fid_t sock, shutdown_mode how);

int socket_write(void* scb_t, const char *buf, unsigned int size);
int socket_read(void* scb_t, char *buf, unsigned int size);
int socket_close(void* scb_t);
SCB* findSCB(Fid_t fd);


#endif