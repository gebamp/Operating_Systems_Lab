#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
  int fd = open("challenge_5.txt",O_CREAT|O_RDWR);
  dup2(fd,99);
  execve("./riddle",NULL,NULL);
  close(fd);
  return 0;
 }
