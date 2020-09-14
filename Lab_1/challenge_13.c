#include <fcntl.h>     
#include <string.h>    
#include <sys/stat.h>  
#include <sys/types.h> 
#include <sys/wait.h>  
#include <unistd.h>    

int main(int argc,char *argv[]) {
  int file_descriptor = open(".hello_there",O_RDWR);
  pid_t child =fork();
  if (child==0)
  {
      execve("./riddle",NULL,NULL);
  }
  sleep(1);
  ftruncate(file_descriptor,32768);
  return 0;
}
