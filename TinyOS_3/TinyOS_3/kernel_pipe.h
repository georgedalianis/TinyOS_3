#ifndef __KERNEL_PIPE_H
#define __KERNEL_PIPE_H

#include "tinyos.h"
#include "util.h"
#include "kernel_dev.h"

/* Buffers can be between 4 and 16kb, we chose 16kb->16384 bytes in binary*/
#define PIPE_BUFFER_SIZE 16384 

/*  */
int sys_Pipe(pipe_t* pipe);
int pipe_write(void* pipecb_t, const char *buf, unsigned int n);
int pipe_read(void* pipecb_t, char *buf, unsigned int n);
int pipe_writer_close(void* _pipecb);
int pipe_reader_close(void* _pipecb);

typedef struct pipe_control_block {
    /* Pointers to read/write from buffer*/
    FCB *reader, *writer;

    /* For blocking writer if no space is available*/
    CondVar has_space;
    /* For blocking reader until data are available*/ 
    CondVar has_data;
    /* Write and Read position in buffer*/
    int w_position, r_position;
    /* Bounded (cyclic) byte buffer*/
    char buffer[PIPE_BUFFER_SIZE];

    int word_length;
    
} Pipe_CB;

#endif