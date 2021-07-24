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


int test_failed=0;

//tests for 2.1-2.3
void kernel_signals_1();
void kernel_signals_2();
void simple_kill(); //should print nothing.sa
//void simple_ignore();
void sigaction_test();
void sigaction_fill_up_oldact_test();
void cont_before_stop();
void stop_cont_complicted();
//void mask_test();
void cant_change_stop_or_kill();
void handler_waists_time(int);


//tests for 2.4
void fake_func();
void user_func_to_handle(int);
void test_user_delivering_send_by_father();

void test_user_delivering();

//tests for mask
void sigprocmask_syscall();
void sigprocmask_syscall_inherit();
void sigaction_mask_really_blocks_with_stop_and_maskcont();

void signal_mask_test_send_signal(); 

//extra function asked for in 2.5
void restoring_previous_handlers_using_the_sigaction_oldact();
//void several_kill_pairs();




//add test to check we reallly do fill up the old act in sigaction
//add test for stop cont complicated scenarios.


//int global_var=0;


void fake_func()
{
    printf("fake_func! this func just takes space\n"); 
}

void
user_func_to_handle(int i){
    printf("!!! Ho Great!! we made it!!!  finally handling user_func_to_handle\n");
}

void
test_user_delivering(){
    fprintf(2,"%d\n",&fake_func);
    int st;
    int cpid = fork();
    
    if (cpid==0){//son
        struct sigaction sa;
        sa.sa_handler = &user_func_to_handle;
        //sa.sigmask = 0;
        sigaction(2, &sa, 0);
        printf("son did sigaction\n");
        for (int i=0; i<120; i++){
            printf("*");
        }

        printf("son about to send himself signal\n");
        kill(getpid(), 2);
        printf("son sent himself kill\n");

        for (int i=0; i<120; i++){
            printf("=");
        }

        sleep(10);
        printf("son about to exit\n");
        exit(0);
    }
    else{ //father
        printf("father will now wait for son\n"); 
        wait(&st);
        printf("father about to exit\n");
        //exit(0);
    }
}

void
test_user_delivering_send_by_father(){
    fprintf(2,"%d\n",&fake_func);
    int st;
    int cpid = fork();
    printf("cpid is: %d\n", cpid);
    if (cpid==0){//son
        struct sigaction sa;
        sa.sa_handler = &user_func_to_handle;
        //sa.sigmask = 0;
        sigaction(2, &sa, 0);
        printf("son did sigaction\n");

        sleep(5);
        printf("son about to exit\n");
        exit(0);
    }
    else{ //father
         printf("gonna sleeppppppppppp\n");

        sleep(3);
        printf("gonna killllllllllllllllllll\n");

        kill(cpid, 2);
        printf("gonna waitttttttt\n");
        //printf("father will now wait for son\n");
        
        wait(&st);
        //printf("father about to exit\n");
        //exit(0);
    }
}



void simple_kill(){
    int cpid = fork();
    if (cpid!=0){
        kill(cpid, 2);
       // kill(cpid, SIGKILL);
        printf("sentkill\n");
        
    }
    else{
        int i =0;
        while(i<1000000000){
            i++;
        }
        test_failed = 1;
        printf("\n      simple_kill test Failed).\n");
        exit(0);
    }
    int status;
    wait(&status);
    
    //exit(0);
}

void sigaction_test(){
    struct sigaction old;
    struct sigaction new;
    new.sa_handler = (void *)SIG_IGN;
    new.sigmask=0;
    int cpid = fork();
    if (cpid!=0){
        sleep(1);
        printf("before sentkill\n");
        kill(cpid, 2);
        //kill(cpid, SIGKILL);
        printf("after sentkill\n");
        
    }
    else{
        printf("about to sigaction\n");
        sigaction(2, &new, &old);
        printf(" finished sigaction, goona sleeeeep\n");
        sleep(3);
        printf("if we print it its good!!!!!!!!!!!!!!!!!!11!!!!!!!!!!\n");
        exit(0);
    }
    int status;
    wait(&status);
    
    exit(0);
}

void sigaction_fill_up_oldact_test(){
    struct sigaction old;
    old.sa_handler = (void *)SIG_IGN;
    old.sigmask=6;
    struct sigaction new;
    new.sa_handler = (void *)SIG_IGN;
    new.sigmask=5;
    int cpid = fork();
    if (cpid!=0){
        sleep(1);
        printf("before sentkill\n");
        kill(cpid, 2);
        //kill(cpid, SIGKILL);
        printf("after sentkill\n");
        
    }
    else{ //son
        printf("about to sigaction\n");
        sigaction(2, &new, &old);
        printf(" finished sigaction, goona sleeeeep\n");
        sleep(3);
        printf("if we print it its good!!!!!!!!!!!!!!!!!!11!!!!!!!!!!\n");
        printf("old.sa_handler = %d , old.sigmask= %d\n", old.sa_handler, old.sigmask); //should print 0 and -1
        sigaction(2, &new, &old);
        printf("old.sa_handler = %d , old.sigmask= %d\n", old.sa_handler, old.sigmask); //should print 1 (SIG_IGN) and 5
        exit(0);
    }
    int status;
    wait(&status);
    
    exit(0);
}

void kernel_signals_1(){
    int cpid = fork();
    if (cpid == 0){ //son
        int i=0;
        while(i<100){
            i++;
            printf("son printing i=%d\n", i);
        }
    }
    else{//popa
        sleep(1);
        kill(cpid, SIGSTOP);
        printf("----------popa stopped child\n");
        sleep(15);
        kill(cpid, SIGCONT);
        printf("----------popa continued child\n");
        sleep(2);
        int status;
        wait(&status);
    }
    exit(0);
}

void stop_cont_complicted(){
    struct sigaction old;
    old.sa_handler = (void *)SIG_IGN;
    old.sigmask=6;
    struct sigaction newS;
    newS.sa_handler = (void *)SIGSTOP;
    newS.sigmask=0;

    
    struct sigaction newC;
    newC.sa_handler = (void *)SIGCONT;
    newC.sigmask=2;
    
    int cpid = fork();
    printf("cpid id: %d\n", cpid);
    if (cpid == 0){ //son
        sigaction(3, &newS, &old);
        sigaction(4, &newC, &old);
        printf("child changed sigactions 3 & 4...\n");
        sleep(1);
        printf("child !!!!!!!!\n");
        sleep(3);
        printf("child ~2~ !!!!!!!!\n");
        exit(0);
    }
    else{//popa
        sleep(1);
        printf("about to stop child\n");
        kill(cpid, 3);
        kill(cpid, SIGSTOP);
        //kill(cpid, SIGSTOP);
        printf("stopped child\n");
        sleep(20);
        printf("about to continued child\n");
        kill(cpid, 4);
        kill(cpid, SIGCONT);
        printf("continued child\n");
        sleep(1);
        kill(cpid, 3);
        printf("stopped child\n");
        sleep(20);
        kill(cpid, 4);
        printf("continued child\n");
        int status;
        wait(&status);
    }
    exit(0);
}

void cont_before_stop(){
    int cpid = fork();
    printf("cpid id: %d\n", cpid);
    if (cpid == 0){ //son
        printf("child about to sleep...\n");
        sleep(1);
        printf("child !!!!!!!!\n");
        exit(0);
    }
    else{//popa
        printf("about to stop child\n");
        kill(cpid, SIGCONT);
        kill(cpid, SIGSTOP);
        kill(cpid, SIGSTOP);
        printf("stopped child\n");
        sleep(20);
        printf("about to continued child\n");
        kill(cpid, SIGCONT);
        printf("continued child\n");
        int status;
        wait(&status);
    }
    exit(0);
}

void kernel_signals_2(){
    int cpid = fork();
    printf("cpid id: %d\n", cpid);
    if (cpid == 0){ //son
        printf("child about to sleep...\n");
        sleep(3);
        printf("child !!!!!!!!\n");
        exit(0);
    }
    else{//popa
        sleep(1);
        printf("about to stop child\n");
        kill(cpid, SIGSTOP);
        printf("stopped child\n");
        sleep(20);
        printf("about to continued child\n");
        kill(cpid, SIGCONT);
        printf("continued child\n");
        int status;
        wait(&status);
    }
    exit(0);
}

void
cant_change_stop_or_kill(){ // Expected output - when trying to change kill - it just ends without printing the "it's bad..", and when trying to change stop - the same, just doesn't end, deadlock.
    struct sigaction old;
    struct sigaction new;
    new.sa_handler = (void *)SIG_IGN;
    new.sigmask=0;
    
    int cpid = fork();
    if (cpid!=0){ // father
        sleep(1);
        printf("before sentkill\n");
        //kill(cpid, SIGSTOP);
        kill(cpid, SIGKILL);
        printf("after sentkill\n");
        
    }
    else{ // son
        printf("about to sigaction\n");
        //sigaction(SIGSTOP, &new, &old);
        sigaction(SIGKILL, &new, &old);
        printf(" finished sigaction, goona sleeeeep\n");
        sleep(3);
        printf("cant_change_stop_or_kill - FAILED\n");
        exit(0);
    }
    int status;
    wait(&status);
    
    exit(0);
}

void sigprocmask_syscall(){ //checking the sys call works
    int cpid = fork();
    int status;
    printf("cpid id: %d\n", cpid);
    if (cpid == 0){ //son
        printf("child original sigmask is: %d, and now he is changing it\n", sigprocmask((1<<2)));
        sleep(3);
         printf("child previous sigmask needs to be 2 (4 to binary) and is: %d, and now he is changing it again\n", sigprocmask((1<<3)));
        exit(0);
    }
    else{//popa
        sleep(1);
        printf("father is waiting for child\n");
        wait(&status);
    }
    exit(0);
}




void signal_mask_test_send_signal(){
    int cpid = fork();
    int status;
    printf("cpid id: %d\n", cpid);
    if (cpid == 0){ //son
        printf("child original sigmask is: %d, and now he is changing it\n", sigprocmask((1<<SIGKILL)));
        sleep(5);
        printf("if we print this its baddd!!!!!!!!!!!!!!!!!!!1\n");
        exit(0);
    }
    else{//popa
        sleep(3);
        kill(cpid, 2);
        printf("father sent kill and is waiting for child\n");
        wait(&status);
    }
    exit(0);
}

void sigprocmask_syscall_inherit(){
    printf("father original sigmask is: %d, and now he is changing it\n", sigprocmask((1<<2)));
    int cpid = fork();
    int status;
    printf("cpid id: %d\n", cpid);
    if (cpid == 0){ //son
        printf("child original sigmask should be 2 (in binary 100) and is: %d, and now he is changing it\n", sigprocmask((1<<3)));
        //sleep(5);
        //printf("if we print this its good!!!!!!!!!!!!!!!!!!!1\n");
        //exit(0);
    }
    else{//popa
        sleep(3);
        //kill(cpid, 2);
        printf("father waits for child\n");
        wait(&status);
    }
    exit(0);
}

void sigaction_mask_really_blocks_with_stop_and_maskcont(){
    struct sigaction old;
    old.sa_handler = (void *)SIG_IGN;
    old.sigmask=6;

    struct sigaction newS;
    newS.sa_handler = (void *)SIGSTOP;
    newS.sigmask=(1<<15);

    
    struct sigaction newC;
    newC.sa_handler = (void *)SIGCONT;
    newC.sigmask=0;
    
    int cpid = fork();
    printf("cpid id: %d\n", cpid);
    if (cpid == 0){ //son
        sigaction(10, &newS, &old);
        sigaction(15, &newC, &old);
        printf("child changed sigactions 10 & 15...\n");
        sleep(5);
        printf("child got released fron stop - if we print it its bad!!!!!!!!\n");
        exit(0);
    }
    else{//popa
        sleep(3);
        printf("about to stop child\n");
        kill(cpid, 10);
        //kill(cpid, SIGSTOP);
        printf("stopped child\n");
        sleep(10);
        printf("about to continued child but with a blocked signal\n");
        kill(cpid, 15);
        //sleep(20);
        //printf("rescuiing child - sending continue\n");
        //kill(cpid, SIGCONT);
        printf("waiting for child (forever)\n");
        int status;
        wait(&status);
    }
    //exit(0);
}


void restoring_previous_handlers_using_the_sigaction_oldact(){

    struct sigaction old;
    old.sa_handler = (void *)user_func_to_handle;
    old.sigmask=6;

    struct sigaction new;
    new.sa_handler = (void *)SIG_IGN;
    //new.sa_handler = (void *)user_func_to_handle;
    //new.sa_handler = (void *)SIGSTOP;
    new.sigmask=5;

    int cpid = fork();
    if (cpid!=0){

        printf("father going to sleep\n");
        sleep(10);
        printf("father waking up\n");
        
        printf("before sentkill\n");
        kill(cpid, 2);
        //kill(cpid, SIGKILL);
        printf("after sentkill\n");
        
    }
    else{ //son
        printf("about to sigaction\n");
        sigaction(2, &new, &old);
        printf(" finished sigaction, goona sleeeeep\n");
        sleep(3);
        //printf("if we print it its good!!!!!!!!!!!!!!!!!!11!!!!!!!!!!\n");
        printf("old.sa_handler = %d , old.sigmask= %d\n", old.sa_handler, old.sigmask); //should print 0 and -1
        sigaction(2, &new, &old);
        printf("old.sa_handler = %d , old.sigmask= %d\n", old.sa_handler, old.sigmask); //should print 1 (SIG_IGN) and 5
        sigaction(2, &old, &new);
        printf("the new= old, printing the filled up current sigaction -> new.sa_handler = %d , new.sigmask= %d\n", new.sa_handler, new.sigmask); //should print 1 (SIG_IGN) and 5
        
        printf("son going to sleep\n");
        sleep(20);
        printf("son waking up\n");
        
        exit(0);
        
    }
    int status;
    wait(&status);
    
    exit(0);

}

void handler_waists_time(int i){
    printf("process %d about to waste time\n", i);
    int counter=0;
    while (counter<1000000000){
        counter++;
    }
    printf("process %d after it\n", i);
    exit(0);
}

/*
void several_kill_pairs(){
    int cpid[5];
    for (int i=0; i<5; i++){
        cpid[i] = fork();
        if (cpid[i] == 0){
            handler_waists_time(cpid[i]);
            exit(0);
        }
    }
    //char* s = "kill 4 9 5 9 6 17"  
    char* s = "/kill 4 9 5 9 6 9 7 9 8 9";  
    char* program_name  = "kill";
    exec(program_name, &s);
}
*/

int
main(int argc, char *argv[])
{
    printf("hello world\n");
    //kernel_signals_2();
    simple_kill();
    //simple_ignore();
    //sigaction_test();
    //sigaction_fill_up_oldact_test();
    //cont_before_stop();
    //stop_cont_complicted();
 
    //test_user_delivering();
    //test_user_delivering_send_by_father();
    //cant_change_stop_or_kill();

    //sigprocmask_syscall();
    
    //signal_mask_test_send_signal();
    //sigprocmask_syscall_inherit();
    //sigaction_mask_really_blocks_with_stop_and_maskcont();
    //restoring_previous_handlers_using_the_sigaction_oldact();
    //several_kill_pairs();


    /*
    if (test_failed==1){
        printf("\n\nsome tests Failed\n");
    }
    else{
        printf("\n\nall tests passed\n");
    }
    */

    exit(0);
}