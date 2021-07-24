struct counting_semaphore{
    int value;          //the number of threads can access the CS at the same time.
    int S1;             //  protects the value; descriptor for binari semaphore
    int S2;             //  protectes the actual entrance to the CS; descriptor for binari semaphore
};



void csem_down(struct counting_semaphore *sem);
/*
If the value representing the count of the semaphore variable is not negative,
decrement it by 1. If the semaphore variable is now negative, the thread executing
acquire is blocked until the value is greater or equal to 1. Otherwise, the thread
continues execution.
*/

void csem_up(struct counting_semaphore *sem);
/*
Increments the value of the semaphore variable by 1. As before, you are free to build
the struct counting_semaphore as you wish.
*/

int csem_alloc(struct counting_semaphore *sem, int initial_value);
/*
Allocates a new counting semaphore, and sets its initial value. Return value is 0 upon
success, another number otherwise.
*/

void csem_free(struct counting_semaphore *sem);
/*
Frees semaphore.
Note that the behavior of freeing a semaphore while other threads “blocked” because
of it is undefined and should not be supported.
*/