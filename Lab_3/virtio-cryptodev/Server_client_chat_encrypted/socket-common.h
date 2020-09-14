/*
 * socket-common.h
 *
 * Simple TCP/IP communication using sockets
 *
 * Anastasis Stathopoulos <anas.stathop@gmail.com>
 */

#ifndef _SOCKET_COMMON_H
#define _SOCKET_COMMON_H

/* Compile-time options */
#define TCP_PORT    35001
#define TCP_BACKLOG 5

#define HELLO_THERE "Hello there!"

#define DATA_SIZE       256
#define BLOCK_SIZE      16
#define KEY_SIZE		16  /* AES128 */


#endif /* _SOCKET_COMMON_H */
