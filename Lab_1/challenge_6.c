#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
  int file_descriptor_1[2];
  int file_descriptor_2[2];
  pipe(file_descriptor_1);
  pipe(file_descriptor_2);
  dup2(file_descriptor_1[1],34);
  dup2(file_descriptor_1[0],33);
  dup2(file_descriptor_2[0],53);
  dup2(file_descriptor_2[1],54);
    execve("./riddle",NULL,NULL);
  return 0;}
