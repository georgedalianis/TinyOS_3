#include "kernel_pipe.h"
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_cc.h"


static file_ops Reader_file_ops = {
	.Open = NULL,
	.Read = pipe_read,
	.Write = NULL,
	.Close = pipe_reader_close
};

static file_ops Writer_file_ops = {
	.Open = NULL,
	.Read = NULL,
	.Write = pipe_write,
	.Close = pipe_writer_close
};


int sys_Pipe(pipe_t* pipe)
{
	// Arguments for FCB_reserve
    Fid_t fid[2];
    FCB* fcb[2];

    //  If reserve return 1 then connection successfully created
    // In case of error return -1 
    if(!FCB_reserve(2, fid, fcb)){
        return -1;
    }

    // Return read & write fid (by reference)
    pipe->read = fid[0];
    pipe->write = fid[1];

    // Initialize new Pipe Control block
    Pipe_CB* new_Pipe_CB = xmalloc(sizeof(Pipe_CB)); // Space allocation of the new pipe control block
    
    // Reader, Writer FCB's
    new_Pipe_CB->reader = NULL;
    new_Pipe_CB->writer = NULL;

    // Reader and writer position of the buffer
    new_Pipe_CB->w_position = 0;
    new_Pipe_CB->r_position = 0;

    // Condition variables initialized 
    new_Pipe_CB->has_space = COND_INIT;
    new_Pipe_CB->has_data = COND_INIT;

    // Current word length
    new_Pipe_CB->word_length = 0;

    // Set streams to point to the pipe_cb objects
    fcb[0]->streamobj = new_Pipe_CB;
    fcb[1]->streamobj = new_Pipe_CB;

    // Save the read and write FCBs 
    new_Pipe_CB->reader = fcb[0];
    new_Pipe_CB->writer = fcb[1];

    // Set the functions for read/write
    fcb[0]->streamfunc = &Reader_file_ops;
    fcb[1]->streamfunc = &Writer_file_ops;

    return 0;
}

int pipe_write(void* pipecb_t, const char *buf, unsigned int n){

	Pipe_CB* PIPE_CB = (Pipe_CB*) pipecb_t;
	int bytes_written = 0;
	int i=0;


	assert(PIPE_CB != NULL);

	if(PIPE_CB->writer == NULL || PIPE_CB->reader == NULL ){
		return -1;
	}

		while(i<n){

			//elegxei ama o buffer einai gematos kai iparxei kapoios gia na kanei read
			while((PIPE_CB->r_position == ((PIPE_CB->w_position + 1) % PIPE_BUFFER_SIZE)) && PIPE_CB->reader != NULL){

			//3ipna tous reader kai perimene
			kernel_broadcast(&PIPE_CB->has_data);
			kernel_wait(&PIPE_CB->has_space, SCHED_PIPE);

			}

			if (PIPE_CB->reader == NULL)
				return -1; 

			/* if writer closes, broadcast and return bytes written */
			if (PIPE_CB->writer == NULL)
				break;

			PIPE_CB->buffer[PIPE_CB->w_position] = buf[i];
			PIPE_CB->w_position = (PIPE_CB->w_position + 1) % PIPE_BUFFER_SIZE;
			bytes_written++;

		i++;	
	}

	kernel_broadcast(&PIPE_CB->has_data);

	return bytes_written;
}

int pipe_read(void* pipecb_t, char *buf, unsigned int n){

	Pipe_CB* PIPE_CB = (Pipe_CB*) pipecb_t;
	int bytes_read = 0;
	int j=0;

	assert(PIPE_CB != NULL);

	if(PIPE_CB->reader == NULL ){
		return -1;
	}

	while(j<n)
	{

			while(PIPE_CB->w_position == PIPE_CB->r_position && PIPE_CB->writer != NULL )
			{

			//3ipna tous writer kai perimene
			kernel_broadcast(&PIPE_CB->has_space);
			kernel_wait(&PIPE_CB->has_data, SCHED_PIPE);

			}

			if (PIPE_CB->reader == NULL)
				return -1; 

			if(PIPE_CB->w_position == PIPE_CB->r_position && PIPE_CB->writer == NULL )
				break;

			buf[j] = PIPE_CB->buffer[PIPE_CB->r_position];	// Read from r_position of BUFFERs
			PIPE_CB->r_position = (PIPE_CB->r_position + 1) % PIPE_BUFFER_SIZE;	
			bytes_read++;

		j++;
	}

	/* inform sleeping writers that there is free space in buffer, reading has finished */
	kernel_broadcast(&PIPE_CB->has_space);

	return bytes_read;
}

int pipe_writer_close(void* _pipecb){

	Pipe_CB* PIPE_CB = (Pipe_CB*)_pipecb;

	//if(PIPE_CB==NULL){
	//	return -1;
	//}

	assert(PIPE_CB != NULL);


	PIPE_CB->writer = NULL;

	if(PIPE_CB->reader != NULL){
		kernel_broadcast(&PIPE_CB->has_data);
	}

	if(PIPE_CB->reader == NULL){
		if(PIPE_CB->writer == NULL){
			free(PIPE_CB);
		}
	}

return 0;

} 

int pipe_reader_close(void* _pipecb){

	Pipe_CB* PIPE_CB = (Pipe_CB*)_pipecb;

	//if(PIPE_CB==NULL){
	//	return -1;
	//}

	assert(PIPE_CB != NULL);

	PIPE_CB->reader = NULL;

	if(PIPE_CB->writer != NULL){
		kernel_broadcast(&PIPE_CB->has_space);
	}

	if(PIPE_CB->writer == NULL){
		if(PIPE_CB->reader == NULL){
			free(PIPE_CB);
		}
	}

return 0;

}



