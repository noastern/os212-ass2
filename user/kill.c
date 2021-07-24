#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int
main(int argc, char **argv)
{
  int i;
  printf("-----------------------------we git to kill!\n");
  if(argc < 2){
    fprintf(2, "usage: kill pid...\n");
    exit(1);
  }
  /*
  for(i=1; i<argc; i++)
    kill(atoi(argv[i]), SIGKILL);
*/
   i = 1;
   while(i<argc-1){
    kill(atoi(argv[i]), atoi(argv[i+1]));
    i = i+2;
   } 
  exit(0);
}
