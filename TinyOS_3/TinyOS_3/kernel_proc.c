
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "util.h"
#include "kernel_sched.h"


/* 
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
  pcb->child_exit = COND_INIT;

  pcb->threadCounter = 0;
  rlnode_init(& pcb->PTCB_List, NULL);
}


static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */

/*
	This function is provided as an argument to spawn,
	to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;

  exitval = call(argl,args);
  Exit(exitval);
}

void start_multi_threads(){
  int exitval;

  TCB* current_tcb=cur_thread();
  PTCB* current_ptcb = current_tcb->owner_ptcb;

  Task call = current_ptcb->task;
  int argl = current_ptcb->argl;
  void* args = current_ptcb->args;

  exitval = call(argl,args);
  sys_ThreadExit(exitval);

}


/*
	System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void* args)
{
  PCB *curproc, *newproc;
  
  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process) 
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }


  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;

  /* 
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if(call != NULL) {
    
    newproc->main_thread = spawn_thread(newproc, start_main_thread);
    
    newproc->threadCounter++;

    //PTCB* ptcb=(PTCB*)xmalloc(sizeof(PTCB));
    
    //
    //PTCB* ptcb=PTCB_INIT(newproc->main_task,newproc->argl,newproc->args);

    PTCB* ptcb = xmalloc(sizeof(PTCB)); //desmevi xwro gia to neo PTCB
    rlnode_init(&ptcb->PTCB_list_node,ptcb);

    ptcb->task=newproc->main_task;
    ptcb->argl=newproc->argl;
    ptcb->args=newproc->args;

    // otan ginetai init
    ptcb->detached = 0;
    ptcb->exited = 0;
    ptcb->exit_cv = COND_INIT;

    //To PTCB pou molis dimiourgithike den exei alla PTCBs na to perimenoune
    ptcb->refcount = 0;

    //rlnode_init(&ptcb->PTCB_list_node,ptcb);  // Initialisation of ptcb_list_node
    
    newproc->main_thread->owner_ptcb=ptcb;
    rlist_push_back(&newproc->PTCB_List, &ptcb->PTCB_list_node);
    //ptcb->tcb=newproc->main_thread;

    assert(newproc->main_thread != NULL);
    assert(ptcb != NULL);

    wakeup(newproc->main_thread);
  }


finish:
  return get_pid(newproc);
}


/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}


Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    kernel_wait(& parent->child_exit, SCHED_USER);
  
  cleanup_zombie(child, status);
  
finish:
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  int no_children, has_exited;
  while(1) {
    no_children = is_rlist_empty(& parent->children_list);
    if( no_children ) break;

    has_exited = ! is_rlist_empty(& parent->exited_list);
    if( has_exited ) break;

    kernel_wait(& parent->child_exit, SCHED_USER);    
  }

  if(no_children)
    return NOPROC;

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

  return cpid;
}


Pid_t sys_WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void sys_Exit(int exitval)
{

  PCB *curproc = CURPROC;  /* cache for efficiency */

  /* First, store the exit status */
  curproc->exitval = exitval;

  /* 
    Here, we must check that we are not the init task. 
    If we are, we must wait until all child processes exit. 
   */
  if(get_pid(curproc) == 1) { /* If we are the initial task we have pid = 1 */
    while(sys_WaitChild(NOPROC,NULL) != NOPROC);
  }
 
  sys_ThreadExit(exitval);
  
}

/**********************************************************************************/
/* PROCESS INFO */ 

static file_ops ProcInfo_Ops = {
  .Open = NULL,
  .Read = procinfo_read,
  .Write = NULL,
  .Close = procinfo_close
};

Fid_t sys_OpenInfo()
{
  Fid_t fid;
  FCB*  fcb;

  if(!FCB_reserve(1,&fid,&fcb)){
    return NOFILE;  // not -1 because nofile is handled without throwing an exception in SysInfo()
  }

  //initialize the procinfo of cb
  procinfo_CB* Pr_info= (procinfo_CB*)xmalloc(sizeof(procinfo_CB));
  /*set the cursor to 0*/
  Pr_info->PT_cursor = 0;

  Pr_info->process_info.pid = 0;
  Pr_info->process_info.ppid = 0;
  Pr_info->process_info.alive = 0;
  Pr_info->process_info.thread_count = 0;
  Pr_info->process_info.argl = 0;

  fcb->streamobj = Pr_info;  // Link FCB--->procinfo struct
  fcb->streamfunc = &ProcInfo_Ops;  // Link FCB--->procinfo_ops

  return fid;   
}

int procinfo_read(void* info, char* buf, unsigned int size)
{ 
  procinfo_CB* ProcInfoCB = (procinfo_CB*) info;
  /* Get the PCB from the current place in the PT*/
  PCB* cur_pcb = &PT[ProcInfoCB->PT_cursor]; 

  if(ProcInfoCB == NULL)
  {
    return -1;
  }

  if(ProcInfoCB->PT_cursor > MAX_PROC-1)
  {
    return -1;
  }

  /*
  while(ProcInfoCB->PT_cursor <= MAX_PROC-1)
  {
    if(cur_pcb->pstate == FREE)
    {
      ProcInfoCB->PT_cursor++;
    }
    else 
    {
      return -1;
      cur_pcb = &PT[ProcInfoCB->PT_cursor];
    }
  }
  */
  ProcInfoCB->process_info.pid = get_pid(cur_pcb);
  ProcInfoCB->process_info.ppid = get_pid(cur_pcb->parent);

  /*
  switch(cur_pcb->pstate)
  {

  case(cur_pcb->pstate == FREE):
      while(cur_pcb->pstate == FREE)
      {
        ProcInfoCB->PT_cursor++;
        if(ProcInfoCB->PT_cursor >= MAX_PROC)
          cur_pcb = &PT[ProcInfoCB->PT_cursor];
          break;
      }
  case(cur_pcb->pstate == ZOMBIE):
      ProcInfoCB->process_info.alive = 0;
      break;

  case(cur_pcb->pstate == ALIVE):
      ProcInfoCB->process_info.alive = 1;
      break;

  default:
        break;
  }*/
  if(cur_pcb->pstate == FREE)
  {
    while(cur_pcb->pstate == FREE)
    {
      ProcInfoCB->PT_cursor++;
        if(ProcInfoCB->PT_cursor >= MAX_PROC){
          return -1; }
          cur_pcb = &PT[ProcInfoCB->PT_cursor];
    }
  }
  else if(cur_pcb->pstate == ZOMBIE)
  {
    ProcInfoCB->process_info.alive = 0;
  }
  else
  {
    ProcInfoCB->process_info.alive = 1;
  }

  ProcInfoCB->process_info.thread_count = cur_pcb->threadCounter;
  ProcInfoCB->process_info.main_task = cur_pcb->main_task;


  ProcInfoCB->process_info.argl = cur_pcb->argl;
  if(cur_pcb->args != NULL)
  {
    //Passing the chars of args of the cur PCB to the ProcInfoCB
    memcpy(&ProcInfoCB->process_info.args, cur_pcb->args, ProcInfoCB->process_info.argl);
  }

  //The ProcInfoCB is cast in the form of a char array by memcpy(), hence we transfer the array to a buffer
    if(buf == NULL){
      return -1;
    }
    memcpy(buf,&ProcInfoCB->process_info,sizeof(ProcInfoCB->process_info));

    //Increase the PT cursor in order to move to the next PCb
    ProcInfoCB->PT_cursor++;


  /*  Return how many characters we sent back to the caller*/
  return sizeof(ProcInfoCB->process_info);
}


int procinfo_close(void* info)
{
  /*  Cast the proc_info back to a procinfo pointer */
  procinfo_CB* process_info = (procinfo_CB*) info;

  assert(process_info != NULL);

  free(process_info);
  return 0;
}



