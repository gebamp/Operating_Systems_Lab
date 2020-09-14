#include <fcntl.h>     
#include <string.h>    
#include <sys/stat.h>  
#include <sys/types.h> 
#include <sys/wait.h>  
#include <unistd.h>    

int main(int argc,char *argv[]) {
  char *datapath = argv[1];
  char *new_value = argv[2];
  int file_descriptor = open(datapath,O_WRONLY);
  lseek(file_descriptor,0x6f,SEEK_SET);
  write(file_descriptor,new_value,sizeof(new_value));
  return 0;
}