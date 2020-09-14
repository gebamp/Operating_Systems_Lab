/*
 * client-cryptodev.c
 * Encrypted TCP/IP communication using sockets
 *
 * Giorgos Bampilis
 */

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <crypto/cryptodev.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "socket-common.h"


unsigned char input_buffer[256];
unsigned char key[] = "georgebampilis";
unsigned char iv [] = "kimonidesalexandros";


/* Insist until all of the data has been written */
ssize_t insist_write(int fd, const void *input_buffer, size_t cnt)
{
        ssize_t ret;
        size_t orig_cnt = cnt;

        while (cnt > 0) {
                ret = write(fd, input_buffer, cnt);
                if (ret < 0)
                        return ret;
                input_buffer += ret;
                cnt -= ret;
        }

        return orig_cnt;
}

int cipher_encrypt(int cipher_fd,struct session_op sess)
{
        int i=0;
		struct crypt_op cryp;
		struct {
			unsigned char encrypted[DATA_SIZE];
		} data;
        memset(&cryp, 0, sizeof(cryp));
        /*
         * cipher_encrypt data.in to data.encrypted
         */
        cryp.ses = sess.ses;
		cryp.len = sizeof(input_buffer);
		cryp.src = input_buffer;
		cryp.dst = data.encrypted;
		cryp.iv =  iv;
		cryp.op = COP_ENCRYPT;
        if (ioctl(cipher_fd, CIOCCRYPT, &cryp)) {
		perror("ioctl(CIOCCRYPT)");
		return 1;
		}
		memset(input_buffer, '\0', 256);
    	for (i = 0; i < DATA_SIZE; i++) {
			input_buffer[i] = data.encrypted[i];
		}
	return 0;
}
int cipher_decrypt(int cipher_fd,struct session_op sess){

        int i=0;
		struct crypt_op cryp;
		struct {
		unsigned char decrypted[DATA_SIZE];

		} data;
        memset(&cryp, 0, sizeof(cryp));

        /*
         * decrypt data.encrypted to data.decrypted
         */
        cryp.ses = sess.ses;
		cryp.len = sizeof(input_buffer);
		cryp.src = input_buffer;
		cryp.dst = data.decrypted;
		cryp.iv =  iv;
		cryp.op = COP_DECRYPT;
    
 		if (ioctl(cipher_fd, CIOCCRYPT, &cryp)) {
			perror("ioctl(CIOCCRYPT)");
			return 1;
		}
	
		memset(input_buffer, '\0', 256);
       	for (i = 0; i < DATA_SIZE; i++) {
			input_buffer[i] = data.decrypted[i];
		}

        return 0;
}
int main(int argc, char *argv[])
{		struct session_op sess;
		struct hostent *hp;
		struct sockaddr_in sa;
		char *hostname;
        int socket_fd,polling,port,cryptodev_fd;
        ssize_t input_bytes;
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
    
	cryptodev_fd = open("/dev/cryptodev0", O_RDWR);
	if (cryptodev_fd < 0) {
		perror("open(/dev/cryptodev0)");
		return 1;
		}
	memset(&sess, 0, sizeof(sess));
	// Get crypto session for AES128

	sess.cipher = CRYPTO_AES_CBC;
	sess.keylen = KEY_SIZE;
	sess.key  = key;
	
	if (ioctl(cryptodev_fd, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
		}
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
				memset(input_buffer, '\0', 256);	
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
				if(cipher_decrypt(cryptodev_fd,sess) != 0){
					perror("\nSomething went wrong when decrypting your message!");
				}

               	if(insist_write(1,input_buffer,input_bytes) != input_bytes){
						perror("Something went wrong when writing to the stdout!");
				}
				fprintf(stdout,"Alice: ");
				fflush(stdout);
			}

			if(FD_ISSET(0,&set_of_files_to_be_polled)){
				memset(input_buffer, '\0', sizeof(input_buffer));
				input_bytes = read(0,input_buffer,sizeof(input_buffer));
				if(input_bytes < 0 ){
					perror("Something went wrong with the read!\n");
					exit(1);
				}
				if(cipher_encrypt(cryptodev_fd,sess)){
					perror("Something went wrong when encrypting your message!");
				}
				
				if(insist_write(socket_fd,input_buffer,sizeof(input_buffer)) != sizeof(input_buffer)){
						perror("Something went wrong when writing to the stdout!");
				}
				fprintf(stdout,"Alice: ");
				fflush(stdout);
			}

        }
	/* Finish crypto session */
        if (ioctl(cryptodev_fd, CIOCFSESSION, &sess.ses)) {
                perror("ioctl(CIOCFSESSION)");
                return 1;
        }
	
        if (close(cryptodev_fd) < 0) {
                perror("close(cryptodev_fd)");
                return 1;
        }

        fprintf(stderr, "\nDone.\n");
        return 0;
}
