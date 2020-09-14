/*
 * socket-client.c
 * Simple TCP/IP communication using sockets
 *
 * Vangelis Koukis <vkoukis@cslab.ece.ntua.gr>
 */

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "socket-common.h"

/* Insist until all of the data has been written */
ssize_t insist_write(int fd, const void *buf, size_t cnt)
{
	ssize_t ret;
	size_t orig_cnt = cnt;
	
	while (cnt > 0) {
	        ret = write(fd, buf, cnt);
	        if (ret < 0)
	                return ret;
	        buf += ret;
	        cnt -= ret;
	}

	return orig_cnt;
}

int main(int argc, char *argv[])
{

	char *hostname;
	struct hostent *hp;
	char input_buffer[100];
	int socket_fd,polling,port;
	ssize_t input_bytes;
	struct sockaddr_in sa;
	fd_set set_of_files_to_be_polled;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s hostname port\n", argv[0]);
		exit(1);
	}
	hostname = argv[1];
	port = atoi(argv[2]); /* Needs better error checking */

	/* Create TCP/IP socket, used as main chat channel */
	if ((socket_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	fprintf(stderr, "Created TCP socket\n");
	
	/* Look up remote hostname on DNS */
	if ( !(hp = gethostbyname(hostname))) {
		printf("DNS lookup failed for host %s\n", hostname);
		exit(1);
	}

	/* Connect to remote TCP port */
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	memcpy(&sa.sin_addr.s_addr, hp->h_addr, sizeof(struct in_addr));
	fprintf(stderr, "Connecting to remote host... "); fflush(stderr);
	if (connect(socket_fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		perror("connect");
		exit(1);
	}
	fprintf(stderr, "Connected.\n");
	FD_ZERO(&set_of_files_to_be_polled);
	fprintf(stdout,"Alice: ");
	fflush(stdout);
	/* Read answer and write it to standard output */
		for (;;) {
			FD_SET(socket_fd,&set_of_files_to_be_polled);
			FD_SET(0,&set_of_files_to_be_polled);
			polling = select(socket_fd+1,&set_of_files_to_be_polled,NULL,NULL,NULL);
			if((polling == -1)){
				fprintf(stdout,"Error in select!");
				fflush(stdout);
				exit(1);
			}
			if(FD_ISSET(socket_fd,&set_of_files_to_be_polled)){
				input_bytes = read(socket_fd,input_buffer,sizeof(input_buffer));
				if (input_bytes <= 0) {
				if (input_bytes < 0)
					perror("read from remote peer failed");
				else
					fprintf(stderr, "\nBob went away\n");
				break;
				}
				fprintf(stdout,"\nBob: ");
				fflush(stdout);
				if(insist_write(1,input_buffer,input_bytes) != input_bytes){
					perror("Something went wrong when writing to the stdout!");
				}
				fprintf(stdout,"Alice: ");
				fflush(stdout);
			}
			if(FD_ISSET(0,&set_of_files_to_be_polled)){
				input_bytes = read(0,input_buffer,sizeof(input_buffer));
				if(input_bytes < 0 ){
					// If input from read less than input bytes 
					perror("Something went wrong with the read!\n");
					exit(1);
				}
				fprintf(stdout,"Alice: ");
				fflush(stdout);
				if(insist_write(socket_fd,input_buffer,input_bytes) != input_bytes){
					perror("Something went wrong when writing to the stdout!");
					
				}	
		}	
	}
	fprintf(stderr,"Done.\n");
	fflush(stderr);
	return 0;
}