// Saved registers for kernel context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

//for signal handling
struct sigaction {
  void (*sa_handler)(int);
  uint sigmask;
};

// Per-CPU state.
struct cpu {
  struct proc *proc;          // The process' thread running on this cpu, or null.
  struct thread *thread;      // The thread running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int noff;                   // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// the sscratch register points here.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate { UNUSED, USED, RUNNABLE, ZOMBIE };
enum threadstate { T_UNUSED, T_USED, T_SLEEPING, T_RUNNABLE, T_RUNNING, T_ZOMBIE };


// Per-thread state
struct thread {

  // t->lock must be held when using these:
  enum threadstate state;        // thread state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int tid;                     // thread ID
  struct proc *my_p;           // the process I belong to 

  // proc_tree_lock must be held when using this:

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  char name[16];               // thread name (debugging)

  //fields for signals - our code
  struct trapframe* user_trap_frame_backup;

 };

// Per-process state
struct proc {
  struct spinlock lock;

  //p->lock must be held when using these:
  enum procstate state;        // Process state
  //void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // proc_tree_lock must be held when using this:
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  struct thread threads[NTHREAD]; //the arrrrrray for all the threads oof the process

  //fields for signals - our code
  uint pending_signals;
  uint signal_mask;
  uint signal_mask_backup; 
  int handling_signal_counter;     // counts how many signals we are handling
  
  void* signal_handlers[32];        // array of the handlings - can be an address of a function from the user OR a number (SIG_IGN for example) 
  int signal_handlers_maskes[32];   // array of the maskes - per signal and its cause is the signal handler
  struct trapframe* user_trap_frame_backup;
  int stopped;
  struct trapframe *trapframe; // data page for trampoline.S
  int handling_signals;

};

 
//Binary Semaphore
struct Bsemaphore {
  int descriptor;
  int free;
  struct thread *threasd_array[NPROC * NTHREAD]; //if null - the thread with this index is not waiting for the resource' otherwise it is waiting and we havee its address.
  struct spinlock lock;
};