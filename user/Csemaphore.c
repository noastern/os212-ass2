#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"


#include "kernel/spinlock.h"  // NEW INCLUDE FOR ASS2
#include "Csemaphore.h"   // NEW INCLUDE FOR ASS 2
#include "kernel/proc.h"         // NEW INCLUDE FOR ASS 2, has all the signal definitions and sigaction definition.  Alternatively, copy the relevant things into user.h and include only it, and then no need to include spinlock.h .


void csem_down(struct counting_semaphore *sem){
    bsem_down(sem->S2);
    bsem_down(sem->S1);
    sem->value--;
    if(sem->value > 0){
        bsem_up(sem->S2);
    }
    bsem_up(sem->S1);
}
/*
If the value representing the count of the semaphore variable is not negative,
decrement it by 1. If the semaphore variable is now negative, the thread executing
acquire is blocked until the value is greater or equal to 1. Otherwise, the thread
continues execution.
*/

void csem_up(struct counting_semaphore *sem){
    bsem_down(sem->S1);
    sem->value++;
    if(sem->value == 1){
        bsem_up(sem->S2);
    }
    bsem_up(sem->S1);
}
/*
Increments the value of the semaphore variable by 1. As before, you are free to build
the struct counting_semaphore as you wish.
*/

int csem_alloc(struct counting_semaphore *sem, int initial_value){
    sem->value = initial_value; //should add validations on this value?
    sem->S1 = bsem_alloc();
    if(sem->S1 == -1){
        return -1;
    }
    sem->S2 = bsem_alloc();
    if(sem->S2 == -1){
        return -1;
    }
    if (initial_value == 0){
        bsem_down(sem->S2);
    }
    return 0;
}
/*
Allocates a new counting semaphore, and sets its initial value. Return value is 0 upon
success, another number otherwise.
*/

void csem_free(struct counting_semaphore *sem){
    sem->value = -1; //should add validations on this value?
    bsem_free(sem->S1); 
    bsem_free(sem->S2); 
}
/*
Frees semaphore.
Note that the behavior of freeing a semaphore while other threads “blocked” because
of it is undefined and should not be supported.
*/