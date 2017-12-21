#include "os2native.h"
#include "tcpip32.h"

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
    memset(&bsdaddr, '\0', sizeof (bsdaddr));
    bsdaddr.sin_family = AF_INET;
    bsdaddr.sin_port = ((const OS2_sockaddr_in *) os2addr)->sin_port;
    bsdaddr.sin_addr.s_addr = ((const OS2_sockaddr_in *) os2addr)->sin_addr.s_addr;
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


LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_connect, "connect", 3),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_recv, "recv", 10),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_os2_select, "os2_select", 12),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_send, "send", 13),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_socket, "socket", 16),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_soclose, "soclose", 17),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_sock_errno, "sock_errno", 20),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_shutdown, "shutdown", 25),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_sock_init, "sock_init", 26),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_select, "select", 32),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_inet_addr, "inet_addr", 105),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_inet_ntoa, "inet_ntoa", 110),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_gethostbyname, "gethostbyname", 111),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_getservbyname, "getservbyname",124),
    LX_NATIVE_EXPORT_DIFFERENT_NAME(OS2_htons, "htons", 205)
LX_NATIVE_MODULE_INIT_END()

// end of tcpip32.c ...

