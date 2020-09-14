#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
  char file_name[5];
  for(int i=0;i<10;i++){
  sprintf(file_name,"bf0%d",i); // Composes a string of size 5 bf0i
  int file_descriptor_1=open(file_name,O_RDWR|O_TRUNC|O_CREAT,S_IRWXU);
  lseek(file_descriptor_1,1073741824,SEEK_SET);
  write(file_descriptor_1,"abcdefghijklmopq",16);}
  execve("./riddle",NULL,NULL);
  return 0;}
