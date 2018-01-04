#include "os2native.h"
#include "tcpip32.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

int OS2_sock_init(void)
{
    TRACE_NATIVE("sock_init()");
    return 1;  // always succeeds.
} // OS2_sock_init

// !!! FIXME: currently expects you're doing IPv4
static void mapOS2SockAddrIn(const OS2_sockaddr *os2addr, struct sockaddr_in *bsdaddr)
{
    const OS2_sockaddr_in *os2addrin = (const OS2_sockaddr_in *) os2addr;
    memset(bsdaddr, '\0', sizeof (*bsdaddr));
    bsdaddr->sin_family = AF_INET;
    bsdaddr->sin_port = os2addrin->sin_port;
    bsdaddr->sin_addr.s_addr = os2addrin->sin_addr.s_addr;
} // mapOS2SockAddrIn

// !!! FIXME: currently expects you're doing IPv4
static void mapBSDSockAddrIn(const struct sockaddr_in *bsdaddr, OS2_sockaddr *os2addr)
{
    OS2_sockaddr_in *os2addrin = (OS2_sockaddr_in *) os2addr;
    memset(os2addrin, '\0', sizeof (*os2addrin));
    os2addrin->sin_family = OS2_AF_INET;
    os2addrin->sin_port = bsdaddr->sin_port;
    os2addrin->sin_addr.s_addr = bsdaddr->sin_addr.s_addr;
} // mapBSDSockAddrIn

static int mapOS2MsgFlags(const int os2flags)
{
    int bsdflags = 0;

    #define MAPSOCKFLAGS(f) if (os2flags & OS2_##f) { bsdflags |= f; }
    MAPSOCKFLAGS(MSG_OOB);
    MAPSOCKFLAGS(MSG_PEEK);
    MAPSOCKFLAGS(MSG_DONTROUTE);
    MAPSOCKFLAGS(MSG_EOR);
    MAPSOCKFLAGS(MSG_TRUNC);
    MAPSOCKFLAGS(MSG_CTRUNC);
    MAPSOCKFLAGS(MSG_WAITALL);
    MAPSOCKFLAGS(MSG_DONTWAIT);
    #undef MAPSOCKFLAGS

    // !!! FIXME: no such flag on Linux
    //MAPSOCKFLAGS(MSG_FULLREAD);

    return bsdflags;
} // mapOS2MsgFlags

int OS2_recv(int sock, void *buf, size_t len, int os2flags)
{
    TRACE_NATIVE("recv(%d, %p, %d, %d)", sock, buf, (int) len, os2flags);
    FIXME("this should fail if not a socket"); // otherwise you might be a file descriptor...
    return recv(sock, buf, len, mapOS2MsgFlags(os2flags));
} // OS2_recv

int OS2_connect(int sock, const struct OS2_sockaddr *os2addr, int addrlen)
{
    TRACE_NATIVE("connect(%d, %p, %d)", sock, os2addr, addrlen);

    FIXME("this should fail if not a socket"); // otherwise you might be a file descriptor...

    if (os2addr->sa_family != OS2_AF_INET) {
        FIXME("currently demands IPv4 socket");
        return -1;
    }

    struct sockaddr_in bsdaddr;
    mapOS2SockAddrIn(os2addr, &bsdaddr);
    return connect(sock, &bsdaddr, (socklen_t) sizeof (bsdaddr));
} // OS2_connect

int OS2_shutdown(int sock, int kind)
{
    TRACE_NATIVE("shutdown(%d, %d)", sock, kind);
    FIXME("this should fail if not a socket"); // otherwise you might be a file descriptor...
    return shutdown(sock, kind);
} // OS2_shutdown

int OS2_socket(int os2family, int os2socktype, int protocol)
{
    TRACE_NATIVE("socket(%d, %d, %d)", os2family, os2socktype, protocol);
    if (os2family != OS2_AF_INET) {
        FIXME("currently demands IPv4 socket");
        return -1;
    }

    if (protocol != 0) {
        FIXME("Only protocol 0 supported at the moment");
        return -1;
    }

    int bsdsocktype;
    if (os2socktype == OS2_SOCK_STREAM) {
        bsdsocktype = SOCK_STREAM;
    } else if (os2socktype == OS2_SOCK_DGRAM) {
        bsdsocktype = SOCK_DGRAM;
    } else {
        FIXME("Only SOCK_STREAM and SOCK_DGRAM supported at the moment");
        return -1;
    }

    return socket(AF_INET, bsdsocktype, 0);
} // OS2_socket

ssize_t OS2_send(int sock, const void *buf, size_t len, int os2flags)
{
    TRACE_NATIVE("send(%d, %p, %d, %d)", sock, buf, (int) len, os2flags);
    FIXME("this should fail if not a socket"); // otherwise you might be a file descriptor...
    return send(sock, buf, len, mapOS2MsgFlags(os2flags));
} // OS2_send

int OS2_soclose(int sock)
{
    TRACE_NATIVE("soclose(%d)", sock);
    FIXME("this should fail if not a socket"); // otherwise you might close a file descriptor...
    return close(sock);
} // OS2_soclose

int OS2_sock_errno(void)
{
    TRACE_NATIVE("sock_errno()");
    FIXME("this should cache errno when an OS/2 socket function fails");
    return errno;
} // OS2_sock_errno

unsigned long OS2_inet_addr(const char *name)
{
    TRACE_NATIVE("inet_addr('%s')", name);
    return inet_addr(name);
} // OS2_inet_addr

struct OS2_hostent *OS2_gethostbyname(const char *name)
{
    TRACE_NATIVE("gethostbyname('%s')", name);
    struct hostent *hent = gethostbyname(name);
    return (OS2_hostent *) hent;  // these happen to match up between Linux and OS/2.
} // OS2_gethostbyname

unsigned short OS2_htons(unsigned short val)
{
    TRACE_NATIVE("htons(%u)", (unsigned int) val);
    return htons(val);
} // OS2_htons

char *OS2_inet_ntoa(struct OS2_in_addr os2inaddr)
{
    TRACE_NATIVE("inet_ntoa(%u)", (unsigned int) os2inaddr.s_addr);
    struct in_addr bsdaddr;
    bsdaddr.s_addr = os2inaddr.s_addr;
    return inet_ntoa(bsdaddr);
} // OS2_inet_ntoa

struct OS2_servent *OS2_getservbyname(const char *name, const char *proto)
{
    TRACE_NATIVE("getservbyname('%s', '%s')", name, proto);
    struct servent *sent = getservbyname(name, proto);
    return (OS2_servent *) sent;  // these happen to match up between Linux and OS/2.
} // OS2_getservbyname

static void mapOS2FdSet(const OS2_fd_set *os2set, fd_set *bsdset)
{
    FD_ZERO(bsdset);
    const int total = os2set->fd_count;
    for (int i = 0; i < total; i++) {
        FD_SET(os2set->fd_array[i], bsdset);
    }
} // mapOS2FdSet

int OS2_select(int ndfs, OS2_fd_set *os2readfds, OS2_fd_set *os2writefds, OS2_fd_set *os2errorfds, OS2_timeval *os2timeout)
{
    TRACE_NATIVE("select(%d, %p, %p, %p, %p)", ndfs, os2readfds, os2writefds, os2errorfds, os2timeout);
    #define MAPFDS(t) \
    fd_set bsd##t##fds; \
    fd_set *t##fds = NULL; \
    if (os2##t##fds) { \
        t##fds = &bsd##t##fds; \
        mapOS2FdSet(os2##t##fds, t##fds); \
    }
    MAPFDS(read);
    MAPFDS(write);
    MAPFDS(error);
    #undef MAPFDS

    struct timeval bsdtv;
    struct timeval *pbsdtv = NULL;
    if (os2timeout) {
        pbsdtv = &bsdtv;
        bsdtv.tv_sec = os2timeout->tv_sec;
        bsdtv.tv_usec = os2timeout->tv_usec;
    }
    const int rc = select(ndfs, readfds, writefds, errorfds, pbsdtv);
    FIXME("fill in OS/2 fds");
    return rc;
} // OS2_select

int OS2_os2_select(int *socks, int noreads, int nowrites, int noexcept, long timeout)
{
    TRACE_NATIVE("os2_select(%p, %d, %d, %d, %d)", socks, noreads, nowrites, noexcept, (int) timeout);
    const int nfds = noreads + nowrites + noexcept;
    struct pollfd *fds = (struct pollfd *) alloca(sizeof (struct pollfd) * nfds);
    struct pollfd *origfds = fds;
    for (int i = 0; i < noreads; i++) {
        fds->fd = *(socks++);
        fds->events = POLLIN;
        fds++;
    }

    for (int i = 0; i < nowrites; i++) {
        fds->fd = *(socks++);
        fds->events = POLLOUT;
        fds++;
    }

    for (int i = 0; i < noexcept; i++) {
        fds->fd = *(socks++);
        fds->events = POLLERR;
        fds++;
    }

    return poll(origfds, nfds, timeout * 1000);
} // OS2_os2_select

int OS2_getsockname(int sock, OS2_sockaddr *os2name, int *os2namelen)
{
    TRACE_NATIVE("getsockname(%d, %p, %p)", sock, os2name, os2namelen);

    struct sockaddr_in bsdaddr;
    socklen_t namelen = sizeof (bsdaddr);
    const int rc = getsockname(sock, &bsdaddr, &namelen);
    if (rc == -1) {
        return -1;
    } else if (bsdaddr.sin_family != AF_INET) {
        FIXME("this only deals with IPv4 at the moment");
        return -1;
    } else if (*os2namelen < sizeof (OS2_sockaddr_in)) {
        return -1;
    }

    mapBSDSockAddrIn(&bsdaddr, os2name);
    *os2namelen = sizeof (OS2_sockaddr_in);
    return rc;
} // OS2_getsockname

int OS2_bind(int sock, const OS2_sockaddr *os2name, int os2namelen)
{
    TRACE_NATIVE("bind(%d, %p, %d)", sock, os2name, os2namelen);
    struct sockaddr_in bsdaddr;
    mapOS2SockAddrIn(os2name, &bsdaddr);
    return bind(sock, (struct sockaddr *) &bsdaddr, sizeof (bsdaddr));
} // OS2_bind

int OS2_listen(int sock, int backlog)
{
    TRACE_NATIVE("listen(%d, %d)", sock, backlog);
    return listen(sock, backlog);
} // OS2_listen

int OS2_accept(int sock, OS2_sockaddr *os2name, int *os2namelen)
{
    TRACE_NATIVE("bind(%d, %p, %p)", sock, os2name, os2namelen);
    struct sockaddr_in bsdaddr;
    socklen_t bsdaddrlen = sizeof (bsdaddr);
    const int retval = accept(sock, (struct sockaddr *) &bsdaddr, &bsdaddrlen);

    if (os2name != NULL) {
        mapBSDSockAddrIn(&bsdaddr, os2name);
    }

    if (os2namelen != NULL) {
        *os2namelen = sizeof (OS2_sockaddr_in);
    }

    return retval;
} // OS2_accept

static int mapOS2SocketOption(const int os2name)
{
    switch (os2name) {
        #define MAPSOOPT(f) case OS2_##f: return f
        MAPSOOPT(SO_DEBUG);
        MAPSOOPT(SO_ACCEPTCONN);
        MAPSOOPT(SO_REUSEADDR);
        MAPSOOPT(SO_KEEPALIVE);
        MAPSOOPT(SO_DONTROUTE);
        MAPSOOPT(SO_BROADCAST);
        //MAPSOOPT(SO_USELOOPBACK);
        MAPSOOPT(SO_LINGER);
        MAPSOOPT(SO_OOBINLINE);
        //MAPSOOPT(SO_L_BROADCAST);
        //MAPSOOPT(SO_RCV_SHUTDOWN);
        //MAPSOOPT(SO_SND_SHUTDOWN);
        MAPSOOPT(SO_REUSEPORT);
        //MAPSOOPT(SO_TTCP);
        MAPSOOPT(SO_SNDBUF);
        MAPSOOPT(SO_RCVBUF);
        MAPSOOPT(SO_SNDLOWAT);
        MAPSOOPT(SO_RCVLOWAT);
        MAPSOOPT(SO_SNDTIMEO);
        MAPSOOPT(SO_RCVTIMEO);
        MAPSOOPT(SO_ERROR);
        MAPSOOPT(SO_TYPE);
        //MAPSOOPT(SO_OPTIONS);
        #undef MAPSOOPT
        default: break;
    }

    FIXME("unsupported OS/2 socket option");
    return 0;
} // mapOS2SocketOption

int OS2_setsockopt(int sock, int os2level, int os2name, const void *value, int len)
{
    const int bsdname = mapOS2SocketOption(os2name);
    const int bsdlevel = SOL_SOCKET;
    if (!bsdname) {
        return -1;
    } else if (os2level != OS2_SOL_SOCKET) {
        return -1;
    }
    return setsockopt(sock, bsdlevel, bsdname, value, len);
} // OS2_setsockopt

int OS2_gettimeofday(OS2_timeval *os2tv, OS2_timezone *os2tz)
{
    struct timeval bsdtv;
    struct timezone bsdtz;
    const int retval = gettimeofday(&bsdtv, &bsdtz);

    if (os2tv) {
        os2tv->tv_sec = bsdtv.tv_sec;
        os2tv->tv_usec = bsdtv.tv_usec;
    }

    if (os2tz) {
        os2tz->tz_minuteswest = bsdtz.tz_minuteswest;
        os2tz->tz_dsttime = bsdtz.tz_dsttime;
    }

    return retval;
} // OS2_gettimeofday

// SOCKS support...

int OS2_Rgetsockname(int sock, OS2_sockaddr *os2name, int *os2namelen)
{
    // !!! FIXME: this doesn't talk to a SOCKS proxy at all.
    TRACE_NATIVE("Rgetsockname(%d, %p, %p)", sock, os2name, os2namelen);
    return OS2_getsockname(sock, os2name, os2namelen);
} // OS2_Rgetsockname

int OS2_Rbind(int sock, OS2_sockaddr *os2name, int os2namelen, OS2_sockaddr *os2remote)
{
    // !!! FIXME: this doesn't talk to a SOCKS proxy at all.
    TRACE_NATIVE("Rbind(%d, %p, %d, %p)", sock, os2name, os2namelen, os2remote);
    return OS2_bind(sock, os2name, os2namelen);
} // OS2_Rbind

int OS2_Raccept(int sock, OS2_sockaddr *os2name, int *os2namelen)
{
    // !!! FIXME: this doesn't talk to a SOCKS proxy at all.
    TRACE_NATIVE("Raccept(%d, %p, %p)", sock, os2name, os2namelen);
    return OS2_accept(sock, os2name, os2namelen);
} // OS2_Raccept

int OS2_Rconnect(int sock, const OS2_sockaddr *os2name, int os2namelen)
{
    // !!! FIXME: this doesn't talk to a SOCKS proxy at all.
    TRACE_NATIVE("Rconnect(%d, %p, %d)", sock, os2name, os2namelen);
    return OS2_connect(sock, os2name, os2namelen);
} // OS2_Rconnect

int OS2_Rlisten(int sock, int backlog)
{
    // !!! FIXME: this doesn't talk to a SOCKS proxy at all.
    TRACE_NATIVE("Rlisten(%d, %d)", sock, backlog);
    return OS2_listen(sock, backlog);
} // OS2_Rlisten

OS2_hostent *OS2_Rgethostbyname(const char *name)
{
    // !!! FIXME: this doesn't talk to a SOCKS proxy at all.
    TRACE_NATIVE("Rgethostbyname('%s')", name);
    return OS2_gethostbyname(name);
} // OS2_Rgethostbyname


LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_accept, "accept", 1),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_bind, "bind", 2),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_connect, "connect", 3),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_getsockname, "getsockname", 6),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_listen, "listen", 9),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_recv, "recv", 10),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_os2_select, "os2_select", 12),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_send, "send", 13),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_setsockopt, "setsockopt", 15),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_socket, "socket", 16),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_soclose, "soclose", 17),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_sock_errno, "sock_errno", 20),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_shutdown, "shutdown", 25),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_sock_init, "sock_init", 26),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_select, "select", 32),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_gettimeofday, "gettimeofday", 102),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_inet_addr, "inet_addr", 105),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_inet_ntoa, "inet_ntoa", 110),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_gethostbyname, "gethostbyname", 111),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_getservbyname, "getservbyname", 124),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_Raccept, "Raccept", 156),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_Rbind, "Rbind", 157),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_Rconnect, "Rconnect", 158),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_Rgetsockname, "Rgetsockname", 159),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_Rlisten, "Rlisten", 160),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_Rgethostbyname, "Rgethostbyname", 161),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_htons, "htons", 205)
LX_NATIVE_MODULE_INIT_END()

// end of tcpip32.c ...

