#include <fcntl.h>     
#include <string.h>    
#include <sys/stat.h>  
#include <sys/types.h> 
#include <sys/wait.h>  
#include <unistd.h>    
#include <stdbool.h> 

int main(int argc,char *argv[]) {
  pid_t pid;
  bool flag=true;
   while(flag){
       pid=fork();
       if(pid==0){
           if(getpid()==32767){
               execve("./riddle",NULL,NULL);
           }
           else{
               return(0);
           }
        
       }
       else{
          wait(NULL);
         if(pid==32767){
           flag=false;
         }
       }
      
   }
  return 0;
}
