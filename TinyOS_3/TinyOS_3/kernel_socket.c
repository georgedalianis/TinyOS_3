#include "kernel_socket.h"
#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_streams.h"

SCB* Port_Map[MAX_PORT];

int socket_read(void* scb_t, char *buf, unsigned int size)
{
	SCB* scb=(SCB*) scb_t;

	assert(scb != NULL);
	/*if (scb == NULL) {
		return -1;
	}*/

	if(scb->type != SOCKET_PEER || scb->peer_s.read_pipe == NULL){
		return -1;
	}

return pipe_read(scb->peer_s.read_pipe, buf, size);
}

int socket_write(void* scb_t, const char *buf, unsigned int size)
{
	
	SCB* scb=(SCB*) scb_t;

	assert(scb != NULL);
	/*if (scb == NULL) {
		return -1;
	}*/

	if(scb->type != SOCKET_PEER || scb->peer_s.write_pipe == NULL ){
		return -1;
	}

return pipe_write(scb->peer_s.write_pipe, buf, size);
}

int socket_close(void* scb_t)
{	
	SCB* scb = (SCB*)scb_t;
	//assert(scb != NULL);
	
	if (scb == NULL) {
		return -1;
	}

	switch(scb->type)
	{
	case SOCKET_PEER:
		if(scb->peer_s.peer != NULL)
		{
			pipe_writer_close(scb->peer_s.write_pipe);
			pipe_reader_close(scb->peer_s.read_pipe);
			scb->peer_s.peer->peer_s.peer = NULL;
		}
		break;

	case SOCKET_LISTENER:
	 		Port_Map[scb->port] = NULL;
		 	kernel_signal(&scb->listener_s.req_available);
		 	//scb->port = NOPORT;
		break;

	default:
		break;
	}

	scb->refcount--;
	if (scb->refcount <= 0)
	{
		free(scb);
		return 0;
	}

return -1;
	
}

SCB* findSCB(Fid_t fd){
	FCB* fcb = get_fcb(fd);
	
	if(fcb==NULL)
	{
		return NULL;
	}
	
return (SCB*)fcb->streamobj;
}

static file_ops Socket_file_ops = {
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};

Fid_t sys_Socket(port_t port)
{	
	SCB* scb = (SCB*)xmalloc(sizeof(SCB));
	assert(scb != NULL);
	Fid_t fid;					// Fcb reserve wants an array of fid_t and fcb
	FCB* fcb;

	if(port < 0 || port >MAX_PORT)
	{
		return NOFILE;
	}

	if (FCB_reserve(1, &fid, &fcb) == 0) 
	{
		return NOFILE;
	}

	
	scb->refcount = 0;
	scb->fcb = fcb;
	scb->type = SOCKET_UNBOUND;
	scb->port = port;

	fcb->streamobj = scb;
	fcb->streamfunc= &Socket_file_ops;

return fid;
	
}

int sys_Listen(Fid_t sock)
{ 
	SCB* scb = findSCB(sock);
		//assert(scb != NULL);

	if(scb == NULL || scb->type != SOCKET_UNBOUND)
	{
		return -1;
	}

	if(scb->port < 1 || scb->port > MAX_PORT || Port_Map[scb->port] != NULL)
	{
		return -1;
	}

	//Install the socket to the PORT_MAP[]
	Port_Map[scb->port] = scb;
	
	//Mark it as SOCKET_LISTENER
	scb->type = SOCKET_LISTENER;

	// Initialize the listener_socket fields of the union
	scb->listener_s.req_available = COND_INIT;
	rlnode_init(&scb->listener_s.queue, NULL);	// Initialize listeners queue of requests (connection_request_control_blocks)
	
	return 0;

}

Fid_t sys_Accept(Fid_t lsock)
{
	SCB* scbListener = findSCB(lsock);
	if(scbListener == NULL || scbListener->type != SOCKET_LISTENER)
		return NOFILE;


	//Increase refcount
	scbListener->refcount++;

	
	//While listener queue is empty and port is still valid wait for request
	while (is_rlist_empty(&scbListener->listener_s.queue) && Port_Map[scbListener->port] != NULL) {
		kernel_wait(&scbListener->listener_s.req_available, SCHED_PIPE);	// SCHED_PIPE is used for both pipes and sockets
	}
	
	//Check if the port is still valid (the socket may have been closed while we were sleeping)	- Check if while waiting, the listening socket @c lsock was closed
	if (Port_Map[scbListener->port] == NULL) {
		return NOFILE;
	}
	//Take the first connection request from the queue and try to honor it (req->admitted = 1)
	rlnode* connectionRequestNode = rlist_pop_front(&scbListener->listener_s.queue);
	connectionRequestNode->cr->admitted = 1;

	// take scb1 from connection request
	SCB* scb1 = connectionRequestNode->cr->peer;
	scb1->type = SOCKET_PEER;

	
	
	
	Fid_t fd2 = sys_Socket(scbListener->port);	


	SCB* scb2 = findSCB(fd2);
		if(scb2==NULL)
	{
		return NOFILE;
	}
	scb2->type = SOCKET_PEER;

	//Connect the 2 peers / initialize the connection ( server - client connection)		
	Pipe_CB* pipe_cb1 = (Pipe_CB*)xmalloc(sizeof(Pipe_CB));
	Pipe_CB* pipe_cb2 = (Pipe_CB*)xmalloc(sizeof(Pipe_CB));
	
	pipe_cb1->writer = NULL;
	pipe_cb1->reader = NULL;

	pipe_cb2->writer = NULL;
	pipe_cb2->reader = NULL;

	//Initialize the first pipe		
	pipe_cb1->writer = scb1->fcb;
	pipe_cb1->reader = scb2->fcb;
	pipe_cb1->has_data = COND_INIT;
	pipe_cb1->has_space = COND_INIT;
	pipe_cb1->r_position = 0;
	pipe_cb1->w_position = 0;
	pipe_cb1->word_length = 0;
	
	//Initialize the second pipe
	pipe_cb2->writer = scb2->fcb;
	pipe_cb2->reader = scb1->fcb;
	pipe_cb2->has_data = COND_INIT;
	pipe_cb2->has_space = COND_INIT;
	pipe_cb2->r_position = 0;
	pipe_cb2->w_position = 0;
	pipe_cb2->word_length = 0;

	//Connect the 2 peer sockets	
	scb1->peer_s.read_pipe = pipe_cb2;
	scb1->peer_s.write_pipe = pipe_cb1;

	scb2->peer_s.read_pipe = pipe_cb1;
	scb2->peer_s.write_pipe = pipe_cb2;

	scb1->peer_s.peer = scb2;
	scb2->peer_s.peer = scb1;

	//Signal the Connect side
	kernel_signal(&connectionRequestNode->cr->connected_cv);

	//In the end, decrease refcount
	scbListener->refcount--;

	if (scbListener->refcount < 0) 
	 	free(scbListener);

	return fd2;



/*
	SCB* scbL = findSCB(lsock);
	Fid_t fd2 = sys_Socket(scbL->port);

	if(lsock <0 || lsock> MAX_FILEID-1)
	{
		return NOFILE;
	}

	if(scbL == NULL)
	{
		return NOFILE;
	}

	if(scbL->type != SOCKET_LISTENER)
	{
		return NOFILE;
	}
	

	scbL->refcount++;

	//While listener queue is empty and port is still valid wait for request
	while (is_rlist_empty(&scbL->listener_s.queue) && Port_Map[scbL->port] != NULL) {
		kernel_wait(&scbL->listener_s.req_available, SCHED_PIPE);	// SCHED_PIPE is used for both pipes and sockets
	}
	
	//Check if the port is still valid (the socket may have been closed while we were sleeping)	- Check if while waiting, the listening socket @c lsock was closed
	if (Port_Map[scbL->port] == NULL) {
		return NOFILE;
	}

	//Take the first connection request from the queue and try to honor it (req->admitted = 1)
	rlnode* connectionRN = rlist_pop_front(&scbL->listener_s.queue);
	connectionRN->cr->admitted = 1;

	// take scb1 from connection request
	SCB* scb1 = connectionRN->cr->peer; // CLient
	scb1->type = SOCKET_PEER;

	SCB* scb2 = findSCB(fd2); // Server
		if(scb2==NULL)
	{
		return NOFILE;
	}
	scb2->type = SOCKET_PEER;


	//************************************/
	// Connection of the 2 peers
/*
	//Initialization of the first pipe
	//Pipe_CB* Pipe_CB1 =(Pipe_CB*)xmalloc(sizeof(Pipe_CB1));
	Pipe_CB* Pipe_CB1= acquire_pipe_cb();
	//Pipe_CB1->reader = NULL;
   // Pipe_CB1->writer = NULL;
    Pipe_CB1->w_position = 0;
    Pipe_CB1->r_position = 0;
    Pipe_CB1->has_space = COND_INIT;
    Pipe_CB1->has_data = COND_INIT;
    Pipe_CB1->word_length = 0;

    Pipe_CB1->writer = scb1->fcb;
    Pipe_CB1->reader = scb2->fcb; 

    //Initialization of the second pipe
    //Pipe_CB* Pipe_CB2 =(Pipe_CB*)xmalloc(sizeof(Pipe_CB2));
	Pipe_CB* Pipe_CB2= acquire_pipe_cb();
	//Pipe_CB2->reader = NULL;
   // Pipe_CB2->writer = NULL;
    Pipe_CB2->w_position = 0;
    Pipe_CB2->r_position = 0;
    Pipe_CB2->has_space = COND_INIT;
    Pipe_CB2->has_data = COND_INIT;
    Pipe_CB2->word_length = 0;

    Pipe_CB2->writer = scb2->fcb;
    Pipe_CB2->reader = scb1->fcb;

    //Set the functions to the sockets
   // scb2->fcb->streamfunc = &Socket_file_ops;
   // scb1->fcb->streamfunc = &Socket_file_ops;

    //Connection of the 2 peer sockets

    scb1->peer_s.read_pipe = Pipe_CB2;
	scb1->peer_s.write_pipe = Pipe_CB1;

	scb2->peer_s.read_pipe = Pipe_CB1;
	scb2->peer_s.write_pipe = Pipe_CB2;

	scb1->peer_s.peer = scb2;
	scb2->peer_s.peer = scb1;

	//Signal the Connect side
	kernel_signal(&connectionRN->cr->connected_cv);

	//In the end, decrease refcount
	scbL->refcount--;

	if(scbL->refcount <= 0){
		free(scbL);
	}

return fd2;	*/
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	SCB* scb = findSCB(sock);
	//assert(scb != NULL);
	if(scb==NULL || scb->type != SOCKET_UNBOUND || port > MAX_PORT || port < 1 || Port_Map[port] == NULL  || Port_Map[port]->type != SOCKET_LISTENER)
	{
		return -1;
	}

	scb->refcount++;

	//CR* req =acquire_CRCB();
	CR* req=(CR*)xmalloc(sizeof(CR));
	//assert(req != NULL);
	req->admitted =0;

	// Point to the parent peer 
	req->peer = scb; 
	req->connected_cv = COND_INIT;
	//initialise the rlnode of the request to point to itself(intrusive lists u know)
	rlnode_init(&req->queue_node, req);

	// Get listener's scb from portmap
	SCB* scbListener = Port_Map[port];

	//Add request to the listener's request queue and signal listener
	rlist_push_back(&scbListener->listener_s.queue, &req->queue_node);
	kernel_signal(&scbListener->listener_s.req_available);

	timeout_t timeout_result;  
	//While request is not admitted, the connect call will block for the specified amount of time 
	//At some point, Accept() will serve the request (req->admitted = 1) and signal the Connect side
		timeout_result=kernel_timedwait(&req->connected_cv, SCHED_PIPE, timeout);
		// If admitted is 0 after the kernel_timedwait it means that timeout expired and Accept() didn`t serve the request
		if (req->admitted == 0) {
			return -1;
		}

		if (timeout_result==0)
		{
		// the above condition satisfied means that the kernel wait was timed out
		// so we remove the request from the listener scb list and free its space
		rlist_remove(&req->queue_node);
		free(req);
		return -1;  // we failed to pass the request(it wasn't admitted)
		}

//decrease socket refcount
	scb->refcount--;
	if (scb->refcount < 0)
		free(scb);

	return 0;
	
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{	
	SCB* socket_cb = findSCB(sock);

	if( socket_cb == NULL || socket_cb->type != SOCKET_PEER)
		return -1;

	int ret;  /*the return value*/

	if(how==SHUTDOWN_READ)
	{	/*if(!(ret = pipe_reader_close(socket_cb->peer_s.read_pipe)))
				socket_cb->peer_s.read_pipe = NULL;
			return ret;*/
		return pipe_reader_close(socket_cb->peer_s.read_pipe);
	}
	else if(how==SHUTDOWN_WRITE)
	{
		if(!(ret = pipe_writer_close(socket_cb->peer_s.write_pipe)))
				socket_cb->peer_s.write_pipe = NULL;
			return ret;
	}
	else if(how==SHUTDOWN_BOTH)
	{
		if(!(ret = ( pipe_reader_close(socket_cb->peer_s.read_pipe) && pipe_writer_close(socket_cb->peer_s.write_pipe)))){
				socket_cb->peer_s.write_pipe = NULL;
				socket_cb->peer_s.read_pipe = NULL;
			}
			return ret;
	}
	else
	{
		return -1;
	}

return 0;
}
	












