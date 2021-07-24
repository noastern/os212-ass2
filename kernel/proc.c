#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

int nexttid = 1;
struct spinlock tid_lock;

extern void forkret(void);

static void freeproc(struct proc *p);
static struct Bsemaphore binary_semaphores[MAX_BSEM]; 
struct spinlock binary_semaphores_lock;

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;

  for (int i=0; i<MAX_BSEM; i++){
    binary_semaphores[i].descriptor =-1;
  } 
  
  initlock(&pid_lock, "nextpid");
  initlock(&tid_lock, "nexttid");
  initlock(&wait_lock, "wait_lock");
  initlock(&binary_semaphores_lock, "binary_semaphores_lock");

  for(p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->threads->kstack = KSTACK((int) (p - proc));
    //p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// Return the current struct thread *, or zero if none.
struct thread*
mythread(void) {
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->thread;
  pop_off();
  return t;
}


int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

int
alloctid() {
  int tid;
  
  acquire(&tid_lock);
  tid = nexttid;
  nexttid = nexttid + 1;
  release(&tid_lock);

  return tid;
}

// Look in the threads table of the given proc for an UNUSED thread.
// If found, initialize state required to run in the kernel,
// entered and return with p->lock held, unless there was a problen and then - releasing the lock.
// If there are no free threads, or a memory allocation fails, return 0.
struct thread*
allocthread(struct proc *p)
{
  struct thread *t;

  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    if(t->state == T_UNUSED) {
      goto found;
    }
    else if (t->state == T_ZOMBIE){
      freethread(t);
      goto found;
    }
  }
  release(&p->lock);
  

  return 0;

found:
  //check if not first and if not first allocate kstack
  if(t != p->threads){
    if( (t->kstack = (uint64)kalloc()) == 0){
      freethread(t);
      release(&p->lock);
      
      return 0;
    }
  }

  t->tid = alloctid();
  t->state = T_USED;
  t->my_p = p;

  if ((t->user_trap_frame_backup = (struct trapframe*) kalloc()) == 0){
    freethread(t);
    release(&p->lock);
    
    return 0;
  }


  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;
  t->context.sp = t->kstack + PGSIZE;

  return t;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc*
allocproc(void)
{
  struct proc *p;
  struct thread *t;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  //dealing with signals inheritence 
  p->pending_signals=0; //initializing the pending signals array with no signals (0)
  p->signal_mask=0;
  p->stopped=0;
  p->handling_signals = 0;
  p->handling_signal_counter = 0;
  for (int i_signal=0; i_signal<32; i_signal++){
    p->signal_handlers[i_signal]= (void *)SIG_DFL;
    p->signal_handlers_maskes[i_signal] = -1;
  }
  // Allocate a trapframe page for all of the threads of the process
  //memset(&p->threads, 0, sizeof(p->threads));
  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    t->state = T_UNUSED;
  }

  //kalloc for all the processes threads 
  uint64 start_of_trapframes_address = (uint64)kalloc();
  if (start_of_trapframes_address==0){
    freeproc(p);
    release(&p->lock);
    
    return 0;
  }

  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    t->trapframe = (struct trapframe *) (start_of_trapframes_address + (uint64)((t-p->threads) * sizeof(struct trapframe)));
  }

        // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
   
    return 0;
  }

  return p;
}


// free a thread structure and the data hanging from it,
// p->lock must be held.
void
freethread(struct thread *t)
{

  if(t != t->my_p->threads){
    if(t->kstack){
      kfree((void*)t->kstack);
    }
    t->kstack=0;
  }

  if(t->user_trap_frame_backup){
    kfree((void*)t->user_trap_frame_backup);
  }
  t->user_trap_frame_backup = 0;

  t->tid = 0;
  t->name[0] = 0;
  t->chan = 0;
  t->killed = 0;
  t->xstate = 0;
  t->state = T_UNUSED;
  t->my_p = 0;
}



// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
 void
freeproc(struct proc *p)
{
  //first freeing all !UNUSED threads of the process
  struct thread *t;

  if(p->threads->trapframe)
    kfree((void*)p->threads->trapframe);
  p->threads->trapframe = 0;

  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    if(t->state != T_UNUSED) {
      freethread(t);
    }
  }

  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;

  //dealing with the signals fields
  p->pending_signals = 0;
  p->signal_mask =0;
  p->signal_mask_backup =0;
  p->handling_signal_counter=0;     // counts how many signals we are handling
  p->handling_signals = 0;
  
  for (int i_signal=0; i_signal<32; i_signal++){
    p->signal_handlers[i_signal]= (void *)SIG_DFL;
    p->signal_handlers_maskes[i_signal] = 0;
  }   
  p->stopped = 0;
}

//
// Creates a new thread within the context of the calling
// process.
int kthread_create ( void ( *start_func ) ( ) , void *stack ){
  
  struct thread *t = mythread();
  struct thread *nt;
  struct proc *p= myproc();

  acquire(&p->lock); // need to get to allocthread with p->lock held

  if((nt = allocthread(p)) == 0){
    return -1; // release(&p->lock) was done in the failure stage in allocthread
  }
  // copy saved user registers.
  *(nt->trapframe) = *(t->trapframe);

  nt->trapframe->epc = (uint64)start_func; //should we use copyin? casting?
  nt->trapframe->sp = (uint64)(stack) + MAX_STACK_SIZE - 16; // keep the 16? the STACK_SIZE? 
  nt->state=T_RUNNABLE;
  //t->context.ra = (uint64)usertrapret;


  release(&p->lock);
  return nt->tid;
}

//
// returns the caller thread’s id
//
int kthread_id(){
  struct thread *t = mythread();
  struct proc *p = t->my_p;
  
  acquire(&p->lock);
  int tid = t->tid;
  release(&p->lock);
  
  if(tid<=0){
    return -1;
  }
  return tid;
}

//
// checks if a thread is the last thread of a process in one of theses states:
// running, runnable, used, sleeping
// p->lock must be held when entering and leaving the function
//
int check_if_last( struct thread *t){
  struct proc *p = t->my_p;
  struct thread *ot;
  for(ot = p->threads; ot < &p->threads[NTHREAD]; ot++) {
    if(ot != t){
      if((ot->state==T_RUNNABLE) | (ot->state==T_RUNNING) | (ot->state==T_SLEEPING) | (ot->state==T_USED)){
        return 0; //given thread is not last
      }
    }
  }
  return 1; //given thread is last
}


//
// terminates the execution of the calling thread.
//
void kthread_exit(int status){
  struct proc *p = myproc();

  acquire(&p->lock);
  struct thread *t = mythread();

  //first checking if I am the last thread of the process
  int am_i_last = check_if_last(t);

  release(&p->lock);
  
  if(am_i_last){  //if last - exit the process after ending all we need to do
    exit(status);
  }


  else{ //if not last -  change our selves to ZOMBIE anf update xstatus (only the current thread)
    acquire(&wait_lock);
    wakeup(t);
    acquire(&p->lock);
    t->xstate = status;
    t->state = T_ZOMBIE;

    if(check_if_last(t)){  // if last - exit the process after ending all we need to do
                    // checking again to support parallel run and assuring exit() will be executed.
      release(&p->lock);
      release(&wait_lock);
      exit(status);
    }

    //release(&p->lock);
    release(&wait_lock);
    // Jump into the scheduler, never to return.
    sched();
    panic("zombie thread exit");
  }
}


int join(int thread_id){
  struct thread *t = mythread();
  struct proc *p = t->my_p;
  struct thread *t_to_wait_for;

  acquire(&wait_lock);

  acquire(&p->lock);

  for(t_to_wait_for = p->threads; t_to_wait_for < &p->threads[NTHREAD]; t_to_wait_for++) {
    if(t_to_wait_for != t){
      while(t_to_wait_for->tid == thread_id){ // found the thread we were looking for
        if(t_to_wait_for->state == T_ZOMBIE){
          freethread(t_to_wait_for);
          release(&p->lock);
          release(&wait_lock);
          return t_to_wait_for->xstate;
        }
        if(t->killed){
          release(&p->lock);
          release(&wait_lock);
          return -199;
        }

        //found the thread but it is not ZOMBIE! therefore sleeping on it.
        release(&p->lock);
        sleep(t_to_wait_for, &wait_lock);  
        acquire(&p->lock); 
      }
    }
  }
  //didn't find the required thread (does not exist)
  release(&p->lock);
  release(&wait_lock);
  return -199;
}


//
// suspends the execution of the calling thread until the target thread,
// indicated by the argument thread id, terminates
// we don't hold p->lock when entering and when releasing
//
int kthread_join(int thread_id, int* status){
  struct proc *p = myproc();
  int xstate = join(thread_id);
  if (xstate == -199){
    return -1;
  }

  acquire(&p->lock);
  if(status != 0 && copyout(p->pagetable, (uint64)status, (char *)&xstate,
                                sizeof(xstate)) < 0) { //if copyout status failed
    release(&p->lock);
    return -1;
  }
  release(&p->lock);
  return 0;
}



// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;
  
  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }
  
  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->threads->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};


// Set up first user process.
void
userinit(void)
{
  
  struct proc *p = allocproc();
  struct thread *t = allocthread(p);
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  t->trapframe->epc = 0;      // user program counter
  t->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  safestrcpy(t->name, "initcode", sizeof(t->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  t->state = T_RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  
  uint sz;
  struct proc *p = myproc();
  acquire(&p->lock); // our code
  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      release(&p->lock);
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  release(&p->lock); // our code
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct thread *nt;
  struct proc *p = myproc();
  struct thread *t = mythread();


  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  //np = nt->my_p;
  if((nt = allocthread(np)) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(nt->trapframe) = *(t->trapframe);

  // Cause fork to return 0 in the child.
  nt->trapframe->a0 = 0;
  //for signals inheritence
  np->signal_mask =p->signal_mask;
  for (int i_signal=0; i_signal<32; i_signal++){
    np->signal_handlers[i_signal] = p->signal_handlers[i_signal];
    np->signal_handlers_maskes[i_signal] = p->signal_handlers_maskes[i_signal];
  }

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));
  safestrcpy(nt->name, t->name, sizeof(t->name));


  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  nt->state = T_RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

//
//
//
void kill_all_threads_besides_myself_and_wait( struct thread *t){
  //go in loop 
  // change t->killed=1
  // another loop to wait for each thread to wait (join?) 

  struct proc *p = t->my_p;
  struct thread *ot;
  acquire(&p->lock);
  for(ot = p->threads; ot < &p->threads[NTHREAD]; ot++) {
    if(ot->tid != t->tid && ot->tid != 0){

      if((ot->state==T_RUNNABLE) | (ot->state==T_RUNNING) | (ot->state==T_SLEEPING) | (ot->state==T_USED)){
       ot->killed = 1; //given thread is not last
       if((ot->state==T_SLEEPING)){
          ot->state = T_RUNNABLE;
       }
      }
    }
  }
  
  for(ot = p->threads; ot < &p->threads[NTHREAD]; ot++) {
    if(ot->tid != t->tid && ot->tid != 0){
      release(&p->lock);
      join(ot->tid); 
      acquire(&p->lock);
    }
  }
  release(&p->lock);
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");
  
  struct thread *t = mythread();
  kill_all_threads_besides_myself_and_wait(t);
  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  wakeup(t);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  t->state = T_ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct thread *t;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.

        for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
          if (t->state == T_RUNNABLE){
            
            t->state = T_RUNNING;
            c->proc = p;
            c->thread = t;
            swtch(&c->context, &t->context);
            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
            c->thread = 0;
          }
        }
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();
  struct thread *t = mythread();

  if(!holding(&p->lock)){
    panic("sched p->lock");
  }
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(t->state == T_RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  struct thread *t= mythread();
  acquire(&p->lock);
  //p->state = RUNNABLE;
  t->state = T_RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;
  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// switch to thread sleeping rather then process
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  struct thread *t = mythread();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  //p->chan = chan;
  t->chan = chan;
  //p->state = SLEEPING;
  t->state = T_SLEEPING;

  sched();

  // Tidy up.
  //p->chan = 0;
  t->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;
  struct thread *t;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock); //our code
    if(p->state == RUNNABLE) {
      for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
        if ((t->state == T_SLEEPING) & (t->chan == chan)){
          t->state = T_RUNNABLE;
        }
      }
    }
    release(&p->lock);
  }
}

void 
handle_SIGKILL(struct proc *p, int signum){
  p->pending_signals = ( p->pending_signals  | (1<<signum) );
  p->killed = 1;
  struct thread *t;
  for(t = p->threads; t < &p->threads[NTHREAD]; t++) {
    if(t->state == T_SLEEPING){
      // Wake process from sleep() ao it will know it needs to die
      t->state = T_RUNNABLE;
      break;
    }
  }
}

// our code 
// new kill system call - send a signal 
int
kill(int pid, int signum)
{
  if ((signum < 0) | (signum > 31) | (pid==0) ){ //validate we got a valid sugnum and valid pid
    return -1;
  }
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      if(p->state==UNUSED){ //no reason to signal a process that is unused
        return -1;
      }
      if(p->signal_handlers[signum] != (void *)SIG_IGN){
        if((signum == SIGKILL) | (p->signal_handlers[signum] == (void *)SIGKILL)){
          if(!(p->signal_mask & (1<<signum))){
            handle_SIGKILL(p, signum);
            //p->signal_mask = p->signal_mask_backup;
          }
        }
        
        
        else if ( (signum!=SIGSTOP) & (signum!=SIGCONT) & (p->signal_handlers[signum] == (void *)SIG_DFL)){
          if(!(p->signal_mask & (1<<signum))){
            handle_SIGKILL(p, signum);
            //p->signal_mask = p->signal_mask_backup;
          }
        }
        
        
        //handlling case cont got before stop - therefore not turnning it on. 
        else if (signum==SIGSTOP) {
          p->handling_signal_counter++;
          p->stopped=1; //handle sigstop
        }

        else if ((p->signal_handlers[signum] == (void *)SIGSTOP)){
          if(!(p->signal_mask & (1<<signum))){
            p->handling_signal_counter++;
            p->stopped=1; //handle sigstop
            if (p->handling_signal_counter == 1){
              p->signal_mask_backup = p->signal_mask;
            }
            p->signal_mask = p->signal_handlers_maskes[signum];
          }
        }  

        else if( ((signum==SIGCONT) & (p->signal_handlers[signum] == (void *)SIG_DFL)) | (p->signal_handlers[signum] == (void *)SIGCONT)){
          if(!((p->signal_mask & (1<<signum)))){
            if (p->stopped == 1){
              p->handling_signal_counter--; //for end dealing with the stop signal
              p->signal_mask = p->signal_mask_backup;
                
            } 
            p->stopped=0; //handle sigcont
          }
        }
        else{
          p->pending_signals = ( p->pending_signals  | (1<<signum) );//turnning on signal bit
        }
        release(&p->lock);
        return 0;
      }
    }
    release(&p->lock);
  }
  return -1; //if pid is not in the range of our pids we'll fall here
}

//function that receieves a mask and returns 1 if valid 
// and -1 if the mask is not valid -> SIGKILL or SIGSTOP are blocked
int
check_valid_mask(uint sigmask){

  if ( (sigmask & (1<<SIGKILL)) | (sigmask & (1<<SIGSTOP)) ){
    return -1;
  }
  if (sigmask <0){
    return -1;
  }
  return 1;
}

//our code
// sigprocmask implementation
uint
sigprocmask(uint sigmask)
{
  struct proc *p = myproc();
  acquire(&p->lock); //not neccesary now, but seems right fot threads.
  uint oldsigmask=p->signal_mask;
  if (check_valid_mask(sigmask)==-1){ //results in error and leaves the sig mask as it is. 
    release(&p->lock);
    return oldsigmask;
  }
  p->signal_mask=sigmask;
  //p->signal_mask=(p->signal_mask | sigmask)); 
  release(&p->lock);
  return oldsigmask;   

}

//our code
//sigaction implementation
int
sigaction (int signum, const struct sigaction *act, struct sigaction *oldact)
{
  //validations
  if ((signum<0) | (signum>31) | (signum==SIGKILL) | (signum==SIGSTOP) ){   
    return -1;
  }
  struct proc *p = myproc();
  acquire(&p->lock);
  //fill up oldact.
  if(oldact!=0){
    copyout(p->pagetable, (uint64)oldact, (char *)&p->signal_handlers[signum], sizeof(((struct sigaction*)0)->sa_handler));
    copyout(p->pagetable, (uint64)oldact+sizeof(((struct sigaction*)0)->sa_handler), (char *)&p->signal_handlers_maskes[signum], sizeof(((struct sigaction*)0)->sigmask));
  }

  //update p->signal_handlers[signum] according to act
  if (act != 0){
    struct sigaction address;

    copyin(p->pagetable, (char *)&address, (uint64)act, sizeof(struct sigaction));

    if (check_valid_mask(address.sigmask) == -1){
      return -1;
    }
    p->signal_handlers[signum] = address.sa_handler;
    p->signal_handlers_maskes[signum] = address.sigmask;

  }
  release(&p->lock);
  
  return 0;
}

//our code
//sigret implementation
void
sigret(void)
{
  struct thread *t = mythread();
  //acquire(&p->lock);
  memmove(t->trapframe, t->user_trap_frame_backup, sizeof(struct trapframe));
  //release(&p->lock);
}


// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [RUNNABLE]  "runble",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ SEMAPHORES~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`
/*
Allocates a new binary semaphore and returns its descriptor(-1 if failure). You are not
restricted on the binary semaphore internal structure, but the newly allocated binary
semaphore should be in unlocked state
*/

int bsem_alloc(){
  int descriptor = -1;
  int i=0;
  acquire(&binary_semaphores_lock);
  for (i=0; i< MAX_BSEM; i++){
    if (binary_semaphores[i].descriptor == -1){
      binary_semaphores[i].descriptor=i;
      descriptor =i;
      break;
    }
  }
  release(&binary_semaphores_lock);
  if (descriptor != -1){
    binary_semaphores[descriptor].free=1;
    for (int j=0; j<(NPROC * NTHREAD); j++){
      binary_semaphores[descriptor].threasd_array[j]=0;
    }
    initlock(&binary_semaphores[descriptor].lock, "binary_semaphores_array_lock");
  }
  return descriptor;
}


/*
Frees the binary semaphore with the given descriptor. Note that the behavior of
freeing a semaphore while other threads “blocked” because of it is undefined and
should not be supported
*/
void bsem_free(int descriptor){
  acquire(&binary_semaphores_lock);
  binary_semaphores[descriptor].descriptor = -1;
  release(&binary_semaphores_lock);
}

/*
Attempt to acquire (lock) the semaphore, in case that it is already acquired (locked),
block the current thread until it is unlocked and then acquire it.
*/
void bsem_down(int descriptor){
  struct proc *p = myproc();
  struct Bsemaphore *sem = &binary_semaphores[descriptor];
  acquire(&sem->lock);
  if(sem->free == 0){ //the semaphore is locked
    struct thread *t = mythread();
    int index = (p - proc) * NTHREAD + (t - p->threads);
    sem->threasd_array[index] = t;
    acquire(&p->lock);
    t->state= T_SLEEPING;
    release(&sem->lock);
    sched();
    release(&p->lock);
  }
  else{// the semaphore is unlocked
    sem->free = 0;
    release(&sem->lock);
  }
}

/*
Releases (unlock) the semaphore. Note that the binary semaphore you are required
to implement must satisfy the mutual exclusion and deadlock freedom conditions but
is not required to be starvation free
*/
void bsem_up(int descriptor){
  struct Bsemaphore *sem = &binary_semaphores[descriptor];
  acquire(&sem->lock);
  for (int j=0; j<(NPROC * NTHREAD); j++){
    if (sem->threasd_array[j] != 0){ //we found someone we can wake up
      struct thread *t = sem->threasd_array[j];
      acquire(&myproc()->lock);
      t->state = T_RUNNABLE;
      release(&myproc()->lock);
      sem->threasd_array[j] = 0;
      release(&sem->lock);
      return;
    }
  }
  //no one is waiting
  sem->free = 1; 
  release(&sem->lock);
}
