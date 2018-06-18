/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_TCPIP32_H_
#define _INCL_TCPIP32_H_

#include "os2types.h"
#include <stdio.h>  // !!! FIXME: don't use size_t in here.

#ifdef __cplusplus
extern "C" {
#endif

// These are (mostly) the same symbols as BSD sockets, so to prevent conflicts
//  with the _actual_ C runtime on the host, we name them OS2_*. The LX
//  loader exports them with the correct names. If you're recompiling an
//  OS/2 app and using this for API support, just use real BSD sockets
//  instead.

// !!! FIXME: lots of "int" and such in here that should probably be int32, etc.

#define OS2_MSG_OOB         0x1
#define OS2_MSG_PEEK        0x2
#define OS2_MSG_DONTROUTE   0x4
#define OS2_MSG_FULLREAD    0x8
#define OS2_MSG_EOR         0x10
#define OS2_MSG_TRUNC       0x20
#define OS2_MSG_CTRUNC      0x40
#define OS2_MSG_WAITALL     0x80
#define OS2_MSG_DONTWAIT    0x100

#define OS2_AF_INET 2

#define OS2_SOCK_STREAM     1
#define OS2_SOCK_DGRAM      2

#define OS2_SOL_SOCKET 0xffff

#define OS2_SO_DEBUG        0x0001
#define OS2_SO_ACCEPTCONN   0x0002
#define OS2_SO_REUSEADDR    0x0004
#define OS2_SO_KEEPALIVE    0x0008
#define OS2_SO_DONTROUTE    0x0010
#define OS2_SO_BROADCAST    0x0020
#define OS2_SO_USELOOPBACK  0x0040
#define OS2_SO_LINGER       0x0080
#define OS2_SO_OOBINLINE    0x0100
#define OS2_SO_L_BROADCAST  0x0200
#define OS2_SO_RCV_SHUTDOWN 0x0400
#define OS2_SO_SND_SHUTDOWN 0x0800
#define OS2_SO_REUSEPORT    0x1000
#define OS2_SO_TTCP         0x2000
#define OS2_SO_SNDBUF       0x1001
#define OS2_SO_RCVBUF       0x1002
#define OS2_SO_SNDLOWAT     0x1003
#define OS2_SO_RCVLOWAT     0x1004
#define OS2_SO_SNDTIMEO     0x1005
#define OS2_SO_RCVTIMEO     0x1006
#define OS2_SO_ERROR        0x1007
#define OS2_SO_TYPE         0x1008
#define OS2_SO_OPTIONS      0x1010


#pragma pack(push, 1)
typedef struct OS2_sockaddr
{
    uint8_t sa_len;
    uint8_t sa_family;
    char sa_data[14];
} OS2_sockaddr;

typedef struct OS2_in_addr
{
    uint32_t s_addr;
} OS2_in_addr;

typedef struct OS2_sockaddr_in
{
    uint8_t sin_len;
    uint8_t sin_family;
    uint16_t sin_port;
    struct OS2_in_addr sin_addr;
    char sin_zero[8];
} OS2_sockaddr_in;

typedef struct OS2_hostent
{
    char *h_name;        /* official name of host */
    char **h_aliases;    /* alias list */
    int h_addrtype;     /* host address type */
    int h_length;       /* length of address */
    char **h_addr_list;  /* list of addresses from name server */
} OS2_hostent;

typedef struct OS2_servent
{
    char *s_name;
    char **s_aliases;
    int s_port;
    char *s_proto;
} OS2_servent;

#define OS2_FD_SETSIZE 64
typedef struct OS2_fd_set
{
    uint16_t fd_count;
    uint16_t padding;
    int fd_array[OS2_FD_SETSIZE];
} OS2_fd_set;

typedef struct OS2_timeval
{
    int32_t tv_sec;
    int32_t tv_usec;
} OS2_timeval;

typedef struct OS2_timezone
{
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
} OS2_timezone;
#pragma pack(pop)

OS2EXPORT int OS2API OS2_sock_init(void) OS2APIINFO(ord=26,name=sock_init);
OS2EXPORT int OS2API OS2_recv(int sock, void *buf, size_t len, int os2flags) OS2APIINFO(ord=10,name=recv);
OS2EXPORT int OS2API OS2_connect(int sock, const OS2_sockaddr *os2addr, int addrlen) OS2APIINFO(ord=3,name=connect);
OS2EXPORT int OS2API OS2_shutdown(int sock, int kind) OS2APIINFO(ord=25,name=shutdown);
OS2EXPORT int OS2API OS2_socket(int family, int os2socktype, int protocol) OS2APIINFO(ord=16,name=socket);
OS2EXPORT ssize_t OS2API OS2_send(int sock, const void *buf, size_t len, int os2flags) OS2APIINFO(ord=13,name=send);
OS2EXPORT int OS2API OS2_soclose(int sock) OS2APIINFO(ord=17,name=soclose);
OS2EXPORT int OS2API OS2_sock_errno(void) OS2APIINFO(ord=20,name=sock_errno);
OS2EXPORT unsigned long OS2API OS2_inet_addr(const char *name) OS2APIINFO(ord=105,name=inet_addr);
OS2EXPORT OS2_hostent * OS2API OS2_gethostbyname(const char *name) OS2APIINFO(ord=111,name=gethostbyname);
OS2EXPORT unsigned short OS2API OS2_htons(unsigned short val) OS2APIINFO(ord=205,name=htons);
OS2EXPORT char * OS2API OS2_inet_ntoa(OS2_in_addr os2inaddr) OS2APIINFO(ord=110,name=inet_ntoa);
OS2EXPORT OS2_servent * OS2API OS2_getservbyname(const char *name, const char *proto) OS2APIINFO(ord=124,name=getservbyname);
OS2EXPORT int OS2API OS2_select(int sock, OS2_fd_set *readfds, OS2_fd_set *writefds, OS2_fd_set *errorfds, OS2_timeval *timeout) OS2APIINFO(ord=32,name=select);
OS2EXPORT int OS2API OS2_os2_select(int *socks, int noreads, int nowrites, int noexcept, long timeout) OS2APIINFO(ord=12,name=os2_select);
OS2EXPORT int OS2API OS2_getsockname(int sock, OS2_sockaddr *os2name, int *namelen) OS2APIINFO(ord=6,name=getsockname);
OS2EXPORT int OS2API OS2_setsockopt(int sock, int os2level, int os2name, const void *value, int len) OS2APIINFO(ord=15,name=setsockopt);
OS2EXPORT int OS2API OS2_bind(int sock, const OS2_sockaddr *os2name, int os2namelen) OS2APIINFO(ord=2,name=bind);
OS2EXPORT int OS2API OS2_accept(int sock, OS2_sockaddr *os2name, int *os2namelen) OS2APIINFO(ord=1,name=accept);
OS2EXPORT int OS2API OS2_Rgetsockname(int sock, OS2_sockaddr *os2name, int *namelen) OS2APIINFO(ord=159,name=Rgetsockname);
OS2EXPORT int OS2API OS2_Rbind(int sock, OS2_sockaddr *os2name, int os2namelen, OS2_sockaddr *os2remote) OS2APIINFO(ord=157,name=Rbind);
OS2EXPORT int OS2API OS2_Raccept(int sock, OS2_sockaddr *os2name, int *os2namelen) OS2APIINFO(ord=156,name=Raccept);
OS2EXPORT int OS2API OS2_Rconnect(int sock, const OS2_sockaddr *os2name, int os2namelen) OS2APIINFO(ord=158,name=Rconnect);
OS2EXPORT int OS2API OS2_Rlisten(int sock, int backlog) OS2APIINFO(ord=160,name=Rlisten);
OS2EXPORT OS2_hostent * OS2API OS2_Rgethostbyname(const char *name) OS2APIINFO(ord=161,name=Rgethostbyname);
OS2EXPORT int OS2API OS2_gettimeofday(OS2_timeval *os2tv, OS2_timezone *os2tz) OS2APIINFO(ord=102,name=gettimeofday);

#ifdef __cplusplus
}
#endif

#endif

// end of tcpip32.h ...
