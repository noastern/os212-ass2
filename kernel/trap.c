#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  struct thread *t = mythread();
  
  // save user program counter.
  t->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed){
      //printf("entered first p->killed in usertrap\n");
      exit(-1);
    }
      
    if(t->killed){
       kthread_exit(-1);
    }
    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    t->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed){
    //printf("entered second p->killed in usertrap\n");
    exit(-1);
  }

  if(t->killed){
    kthread_exit(-1);
  }


  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();
  
  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();
  struct thread *t = mythread();


  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

    //our code - calling signals_hanling from which we will handle both signals on the kernel side and the user side
  signals_handling(p);

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  t->trapframe->kernel_satp = r_satp();         // kernel page table
  t->trapframe->kernel_sp = t->kstack + PGSIZE; // thread's kernel stack
  t->trapframe->kernel_trap = (uint64)usertrap;
  t->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(t->trapframe->epc);



  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);
  //printf("about to move to user space, tid: %d\n", t->tid);
  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);

  //((void (*)(uint64,uint64))fn)(TRAPFRAME + ((t-p->threads) * sizeof(struct trapframe)), satp);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME + (t->trapframe - p->threads[0].trapframe) *sizeof(struct trapframe), satp);
  //((void (*)(uint64,uint64))fn)(TRAPFRAME , satp);
}

//
// our code - searchong and handling stop and cont
// prioritized over other signals
void
handling_stop_cont(struct proc *p){
  while(p->stopped){

    if(p->killed){
      printf("found out i am killed\n");
      p->stopped =0;
      return;
    }

    release(&p->lock);
    yield();
    acquire(&p->lock);
  }
}

//
// our code - handling the current process signals
//
void
signals_handling(struct proc *p)
{

  acquire(&p->lock);

  handling_stop_cont(p); //handling it first!!
  
  
  //printf("after handling stop cont \n");
  for (int signum=0; signum<32; signum++){
    if(p->pending_signals & (1<<signum)){
      if(!(p->signal_mask & (1<<signum))){
        if(p->signal_handlers[signum]== (void *)SIG_DFL){//if default we wanna commit kill
          if(p->handling_signals ==0){
            p->handling_signals = 1;
            release(&p->lock);
            exit(-1);
          }
          else{
            release(&p->lock);
            kthread_exit(-1);
          }

        }
        else{ // user space handle!!! not stop, cont, kill, default or ignore (ignore was dealt with on sigaction)
          p->handling_signal_counter++;
          if (p->handling_signal_counter == 1){
            p->signal_mask_backup = p->signal_mask;
          }
          p->signal_mask = p->signal_handlers_maskes[signum];
          //maybe release?
          user_signal_handlng(p, signum);
          //maybe aquire?
          p->handling_signal_counter--;
          p->signal_mask = p->signal_mask_backup;
        }
        //turn off the bit of the signal we have just handled
        p->pending_signals = p->pending_signals ^ (1<<signum); 
      }
    } 
  }
  release(&p->lock);
}

void
user_signal_handlng(struct proc *p, int signum){
  struct thread *t = mythread();
  //first - backing up
  memmove( t->user_trap_frame_backup, t->trapframe, sizeof(struct trapframe));

  //for li a7, SYS_sigret (ecall)
  char array[] = {0x93 ,0x08 ,0x80 ,0x01 ,0x73 ,0x00 ,0x00 ,0x00};

  //injecting the sigret ecall into the user stack 
  copyout(p->pagetable, t->trapframe->sp, array, 8);

  // updating the return address to the sigret
  t->trapframe->ra = t->trapframe->sp;

  struct sigaction * func_address = p->signal_handlers[signum];

  // Move SP down
  t->trapframe->sp = t->trapframe->sp - 8;

  // set the argument of the function
  t->trapframe->a0 = signum;

  // set the user place to go to (epc) to the function of handling got from the user.
  t->trapframe->epc = (uint64)(func_address);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && mythread() != 0 && mythread()->state == T_RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

