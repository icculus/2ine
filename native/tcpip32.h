#ifndef _INCL_TCPIP32_H_
#define _INCL_TCPIP32_H_

#include "os2types.h"

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

#pragma pack(push, 1)
typedef struct OS2_sockaddr
{
    uint8 sa_len;
    uint8 sa_family;
    char sa_data[14];
} OS2_sockaddr;

typedef struct OS2_in_addr
{
    uint32 s_addr;
} OS2_in_addr;

typedef struct OS2_sockaddr_in
{
    uint8 sin_len;
    uint8 sin_family;
    uint16 sin_port;
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
    uint16 fd_count;
    uint16 padding;
    int fd_array[OS2_FD_SETSIZE];
} OS2_fd_set;

typedef struct OS2_timeval
{
    sint32 tv_sec;
    sint32 tv_usec;
} OS2_timeval;
#pragma pack(pop)

OS2EXPORT int OS2API OS2_sock_init(void);
OS2EXPORT int OS2API OS2_recv(int sock, void *buf, size_t len, int os2flags);
OS2EXPORT int OS2API OS2_connect(int sock, const OS2_sockaddr *os2addr, int addrlen);
OS2EXPORT int OS2API OS2_shutdown(int sock, int kind);
OS2EXPORT int OS2API OS2_socket(int family, int os2socktype, int protocol);
OS2EXPORT ssize_t OS2API OS2_send(int sock, const void *buf, size_t len, int os2flags);
OS2EXPORT int OS2API OS2_soclose(int sock);
OS2EXPORT int OS2API OS2_sock_errno(void);
OS2EXPORT unsigned long OS2API OS2_inet_addr(const char *name);
OS2EXPORT OS2_hostent * OS2API OS2_gethostbyname(const char *name);
OS2EXPORT unsigned short OS2API OS2_htons(unsigned short val);
OS2EXPORT char * OS2API OS2_inet_ntoa(OS2_in_addr os2inaddr);
OS2EXPORT OS2_servent * OS2API OS2_getservbyname(const char *name, const char *proto);
OS2EXPORT int OS2API OS2_select(int sock, OS2_fd_set *readfds, OS2_fd_set *writefds, OS2_fd_set *errorfds, OS2_timeval *timeout);
OS2EXPORT int OS2API OS2_os2_select(int *socks, int noreads, int nowrites, int noexcept, long timeout);

#ifdef __cplusplus
}
#endif

#endif

// end of tcpip32.h ...
