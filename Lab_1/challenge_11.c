#include <fcntl.h>     
#include <string.h>    
#include <sys/stat.h>  
#include <sys/types.h> 
#include <sys/wait.h>  
#include <unistd.h>    

int main() {
   int file_descriptor_1= open("secret_number",O_RDWR|O_CREAT|O_TRUNC,0600);
   char buffer[4096];
   pid_t fake_link = fork();
   if(fake_link==0)
   {
       execve("./riddle",NULL,NULL);
   }
   sleep(1);
   int new_file_descriptor = open("i_said_open_the_gates",O_RDWR|O_CREAT|O_TRUNC,0777);
   read(file_descriptor_1,buffer,4096);
   write(new_file_descriptor,buffer,4096);
   wait(NULL);
   return(0);
}
