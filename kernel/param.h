#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       1000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define SIG_DFL      0     // deafult signal handling
#define SIG_IGN      1     // ignore signal
#define SIGKILL      9     // signal
#define SIGSTOP      17    // signal
#define SIGCONT      19    // signal
#define NTHREAD      8     // maximal number of threads per proccess
#define MAX_STACK_SIZE       4000     // user stack max size
#define MAX_BSEM     128   // the maximum number of binary semaphores is MAX_BSEM