
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "util.h"

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  PCB* curproc=CURPROC;

  assert(task!= NULL);


  PTCB* new_ptcb = xmalloc(sizeof(PTCB)); //desmevi xwro gia to neo PTCB

  new_ptcb->task=task;
  new_ptcb->argl=argl;
  new_ptcb->args=args;

  // otan ginetai init
  new_ptcb->detached = 0;
  new_ptcb->exited = 0;
  new_ptcb->exit_cv = COND_INIT;

  //To PTCB pou molis dimiourgithike den exei alla PTCBs na to perimenoune
  new_ptcb->refcount = 0;

  rlnode_init(&new_ptcb->PTCB_list_node,new_ptcb);  // Initialisation of ptcb_list_node

  //PTCB* new_ptcb=PTCB_INIT(task,argl,args); // ftiaxnei new PTCB
  //vazei to new PTCB node stin lista me to PTCBS 
  rlist_push_back(&curproc->PTCB_List, &new_ptcb->PTCB_list_node);

  new_ptcb->tcb = spawn_thread(curproc, start_multi_threads);
  new_ptcb->tcb->owner_ptcb = new_ptcb; // sindesi meta3i tou PTCB kai tou kainourgiou TCB
  curproc->threadCounter++;
  
  wakeup(new_ptcb->tcb); // thetei to TCB state se ready kai to vazei sto Q
  return (Tid_t) new_ptcb; // girnaei Tid_t tou kainourgiou PTCB
	
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) (cur_thread()->owner_ptcb);
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	  //casting ton unint to kanoume pointer se PTCB
  PTCB* ptcb = (PTCB*) tid;

  if(rlist_find(&CURPROC->PTCB_List , ptcb, NULL) == NULL || tid == NOTHREAD || ptcb->detached == 1 || cur_thread()->owner_ptcb == ptcb){

               return -1;

    }

    ptcb->refcount++; // posa threads perimenoun sto wait list gia na kanoyn Join

    //oso to detached den einai 1 kai oute to exited tote mporei na valei to Thread stin wait List
    while(ptcb->detached != 1 && ptcb->exited != 1){

      assert(ptcb->detached==0);  
      assert(ptcb->exited==0);

      kernel_wait(&ptcb->exit_cv, SCHED_USER);

    }

    //afou ekane detatched or exited
    ptcb->refcount--;

    /* If ptcb_T2 becomes detached while waiting then joining threads is not possible */
    if(ptcb->detached==1){
      return -1;
    }

    // If thread tid exits, which is the standard case, store its exit value. Additionally, if the refcount has become 0, the tid thread can 
    // deleted.
      
      if(exitval != NULL){
      *exitval = ptcb->exitval;
    }
  

    //an to refcount == 0 tote den iparxoun alla threads gia na kanoun join
    if(ptcb->refcount==0){
      rlist_remove(&ptcb->PTCB_list_node);
      free(ptcb);
    } 

  return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	//casting ton unint to kanoume pointer se PTCB
  PTCB* ptcb = (PTCB*) tid;


  if(tid == NOTHREAD){
   return -1;
                    }

  if(rlist_find(& CURPROC->PTCB_List , ptcb, NULL) == NULL || ptcb->exited == 1){
    return -1;
  }

  /*to flag tou detached ginetai 1 */
  ptcb->detached = 1;
  kernel_broadcast(&(ptcb->exit_cv)) ;

  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{
    PCB *curproc = CURPROC;
    TCB* curThread = cur_thread();
    PTCB* curPtcb = curThread->owner_ptcb;

    curPtcb->exitval = exitval;
    curPtcb->exited = 1;
    curPtcb->tcb = NULL;

    //fprintf(stderr, "\nThreadExit: Decreiazing the thread_count...\n");
    /* Decreasing the threads of the currnet pcb */
    curproc->threadCounter--;

    assert((cur_thread()->owner_ptcb) != NULL);

    //fprintf(stderr, "\nThreadExit: Sending wakeup signal...\n");
    /* Send a signal to the waiting processes if there are */
    if(curPtcb->refcount !=0){
      kernel_broadcast(&curPtcb->exit_cv);
    }
    //fprintf(stderr, "\nThreadExit: Just wake the up...\n");


    /* If the current thread is the last one of the process */
    if(curproc->threadCounter == 0){
      //fprintf(stderr, "\nThreadExit: Last Process");
      if(get_pid(curproc) != 1){
          /* Reparent any children of the exiting process to the 
             initial task */
          PCB* initpcb = get_pcb(1);
          while(!is_rlist_empty(& curproc->children_list)) {
            rlnode* child = rlist_pop_front(& curproc->children_list);
            child->pcb->parent = initpcb;
            rlist_push_front(& initpcb->children_list, child);
          }

          /* Add exited children to the initial task's exited list 
            and signal the initial task */
          if(!is_rlist_empty(& curproc->exited_list)) {
            rlist_append(& initpcb->exited_list, &curproc->exited_list);
            kernel_broadcast(& initpcb->child_exit);
          }

          /* Put me into my parent's exited list */
          rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
          kernel_broadcast(& curproc->parent->child_exit);
      }
        

        assert(is_rlist_empty(& curproc->children_list));
        assert(is_rlist_empty(& curproc->exited_list));


        /* 
          Do all the other cleanup we want here, close files etc. 
        */

        /* Release the args data */
        if(curproc->args) {
          free(curproc->args);
          curproc->args = NULL;
        }

        /* Clean up FIDT */
        for(int i=0;i<MAX_FILEID;i++) {
          if(curproc->FIDT[i] != NULL) {
            FCB_decref(curproc->FIDT[i]);
            curproc->FIDT[i] = NULL;
          }
        }

        /* Disconnect my main_thread */
        curproc->main_thread = NULL;

        /* Now, mark the process as exited. */
        curproc->pstate = ZOMBIE;

    }
    //fprintf(stderr, "\nThreadExit: Not last thread\n");
  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
  //fprintf(stderr, "\nExiting ThreadExit");

}

