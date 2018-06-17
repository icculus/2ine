/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 199309
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "lib2ine.h"

extern char **environ;

LxLoaderState GLoaderState;

static pthread_key_t tlskey;

static void *readfile(const char *fname, size_t *_len)
{
    const size_t chunklen = 128;
    const int fd = open(fname, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Can't open '%s': %s\n", fname, strerror(errno));
        return NULL;
    }

    char *buf = NULL;
    size_t bw = 0;

    while (1) {
        buf = (char *) realloc(buf, bw + chunklen);
        if (!buf) {
            fprintf(stderr, "Out of memory\n");
            free(buf);
            close(fd);
            return NULL;
        }

        const ssize_t br = read(fd, buf + bw, chunklen);
        if (br < 0) {
            fprintf(stderr, "Read fail on '%s': %s\n", fname, strerror(errno));
            free(buf);
            close(fd);
            return NULL;
        }

        bw += br;
        if (br < chunklen) {
            break;
        }
    }

    close(fd);
    *_len = bw;
    return buf;
}

static void initPib(void)
{
    LxPIB *pib = &GLoaderState.pib;
    memset(pib, '\0', sizeof (*pib));

    size_t cmdlinebuflen = 0;
    char *cmdlinebuf = readfile("/proc/self/cmdline", &cmdlinebuflen);
    if (!cmdlinebuf) {
        abort();
    }

    int argc = 0;
    for (size_t i = 0; i < cmdlinebuflen; i++) {
        if (cmdlinebuf[i] == '\0') {
            argc++;
        }
    }

    char **argv = (char **) calloc(argc + 1, sizeof (char *));
    if (!argv) {
        fprintf(stderr, "Out of memory\n");
        abort();
    }

    argv[0] = cmdlinebuf;
    argc = 0;
    for (size_t i = 0; i < cmdlinebuflen; i++) {
        if (cmdlinebuf[i] == '\0') {
            argc++;
            argv[argc] = &cmdlinebuf[i + 1];
        }
    }
    argv[argc] = NULL;


    // !!! FIXME: HUGE HACK
    int is_lx_loader = 0;
    {
        const char *ptr = strrchr(argv[0], '/');
        if (ptr) { ptr++; } else { ptr = argv[0]; }
        is_lx_loader = (strcmp(ptr, "lx_loader") == 0);
    }

    if (is_lx_loader) {
        argv++;
        argc--;
    }


    // !!! FIXME: this is incomplete.

    // Eventually, the environment table looks like this (double-null to terminate list):  var1=a\0var2=b\0var3=c\0\0
    // The command line looks like this: \0argv0\0argv1 argv2 argvN\0\0
    // Between env and cmd is the exe name: argv0\0

    size_t len = 1;
    for (int i = 0; i < argc; i++) {
        int needs_escape = 0;
        int num_backslashes = 0;
        for (const char *arg = argv[i]; *arg; arg++) {
            const char ch = *arg;
            if ((ch == ' ') || (ch == '\t'))
                needs_escape = 1;
            else if ((ch == '\\') || (ch == '\"'))
                num_backslashes++;
            len++;
        } // for

        if (needs_escape) {
            len += 2 + num_backslashes;
        } // if

        len++;  // terminator
    } // for

    len += strlen(argv[0]) + 1;  // for the exe name.

    char **envp = environ;

    const char *default_os2path = "PATH=C:\\home\\icculus\\Dropbox\\emx\\bin;C:\\WATCOM\\binp;C:\\home\\icculus";  // !!! FIXME: noooooope.
    for (int i = 0; envp[i]; i++) {
        const char *str = envp[i];
        if (strncmp(str, "PATH=", 5) == 0) {
            if (!GLoaderState.subprocess)
                str = default_os2path;
        } else if (strncmp(str, "IS_2INE=", 8) == 0) {
            continue;
        } // if
        len += strlen(str) + 1;
    } // for

    len += 4;  // null terminators.

    // align this to 64k, because it has to be at the start of a segment
    //  for 16-bit code.
    // cmd.exe uses realloc on the enviroment seg so make it 64k
    //  (!!! FIXME: this works even though it doesn't use allocSegment because
    //  we currently just allocate the full 64k and never actually realloc
    //  later. allocSegment isn't up and running at this point. But we might
    //  want lx_loader to copy this to an alloc'd segment later so we
    //  definitely have an tileable address below 512mb.)
    char *env = NULL;
    if (posix_memalign((void **) &env, 0x10000, 0x10000) != 0) {
        fprintf(stderr, "Out of memory\n");
        abort();
    } // if

    const char *libpath = NULL;
    char *ptr = env;
    for (int i = 0; envp[i]; i++) {
        const char *str = envp[i];
        if (strncmp(str, "PATH=", 5) == 0) {
            if (!GLoaderState.subprocess)
                str = default_os2path;
        } else if (strncmp(str, "LIBPATH=", 8) == 0) {
            libpath = str + 8;
        } else if (strncmp(str, "IS_2INE=", 8) == 0) {
            continue;
        } // if

        strcpy(ptr, str);
        ptr += strlen(str) + 1;
    }
    *(ptr++) = '\0';

    // we keep a copy of LIBPATH for undocumented API
    //  DosQueryHandleInfo(..., QHINF_LIBPATH, ...). I guess in case the app
    //  has changed the environment var after start? Java uses this.
    if (libpath == NULL)
        libpath = "";

    GLoaderState.libpath = strdup(libpath);
    GLoaderState.libpathlen = strlen(libpath) + 1;

    // put the exe name between the environment and the command line.
    strcpy(ptr, argv[0]);
    ptr += strlen(argv[0]) + 1;

    char *cmd = ptr;
    strcpy(ptr, argv[0]);
    ptr += strlen(argv[0]);
    *(ptr++) = '\0';
    for (int i = 1; i < argc; i++) {
        int needs_escape = 0;
        for (const char *arg = argv[i]; *arg; arg++) {
            const char ch = *arg;
            if ((ch == ' ') || (ch == '\t'))
                needs_escape = 1;
        } // for

        if (needs_escape) {
            *(ptr++) = '"';
            for (const char *arg = argv[i]; *arg; arg++) {
                const char ch = *arg;
                if ((ch == '\\') || (ch == '\n'))
                    *(ptr++) = '\\';
                *(ptr++) = ch;
            } // for
            *(ptr++) = '"';
        } else {
            for (const char *arg = argv[i]; *arg; arg++)
                *(ptr++) = *arg;
        } // if

        if (i < (argc-1))
            *(ptr++) = ' ';
    } // for

    *(ptr++) = '\0';

    pib->pib_ulpid = (uint32) getpid();
    pib->pib_ulppid = (uint32) getppid();
    pib->pib_pchcmd = cmd;
    pib->pib_pchenv = env;
    //pib->pib_hmte is filled in later during loadModule()
    // !!! FIXME: uint32 pib_flstatus;
    // !!! FIXME: uint32 pib_ultype;

    if (is_lx_loader) {
        argv--;
    }

    free(cmdlinebuf);
    free(argv);
} // initPib

static void dosExit_lib2ine(uint32 action, uint32 result) {}

static void initOs2Tib_lib2ine(uint8 *tibspace, void *_topOfStack, const size_t stacklen, const uint32 tid)
{
    uint8 *topOfStack = (uint8 *) _topOfStack;

    LxTIB *tib = (LxTIB *) tibspace;
    LxTIB2 *tib2 = (LxTIB2 *) (tib + 1);

    FIXME("This is probably 50% wrong");
    tib->tib_pexchain = NULL;
    tib->tib_pstack = topOfStack - stacklen;
    tib->tib_pstacklimit = (void *) topOfStack;
    tib->tib_ptib2 = tib2;
    tib->tib_version = 20;  // !!! FIXME
    tib->tib_ordinal = 79;  // !!! FIXME

    tib2->tib2_ultid = tid;
    tib2->tib2_ulpri = 512;
    tib2->tib2_version = 20;
    tib2->tib2_usMCCount = 0;
    tib2->tib2_fMCForceFlag = 0;
}

static uint16 setOs2Tib_lib2ine(uint8 *tibspace)
{
    pthread_setspecific(tlskey, (LxTIB *) tibspace);
    return 1;
}

static LxTIB *getOs2Tib_lib2ine(void)
{
    return (LxTIB *) pthread_getspecific(tlskey);
}

static void deinitOs2Tib_lib2ine(const uint16 selector)
{
    pthread_setspecific(tlskey, NULL);
}

static int findSelector_lib2ine(const uint32 addr, uint16 *outselector, uint16 *outoffset, int iscode)
{
    return 0;  // always fail. This is for 16-bit support.
}

static void freeSelector_lib2ine(const uint16 selector)
{
    // no-op. This is for 16-bit support.
}

static void *convert1616to32_lib2ine(const uint32 addr1616)
{
    return NULL;  // always fail. This is for 16-bit support.
}

static uint32 convert32to1616_lib2ine(void *addr32)
{
    return 0;  // always fail. This is for 16-bit support.
}

static LxModule *loadModule_lib2ine(const char *modname)
{
    return NULL;  // lx_loader will do OS/2 modules, we don't.
    // !!! FIXME: maybe this should dlopen() native modules instead?
}

static char *makeUnixPath_lib2ine(const char *os2path, uint32 *err)
{
    // this doesn't convert any OS/2 paths. For native apps, use native paths.
    char *retval = strdup(os2path);
    if (!retval) {
        *err = 8;  //ERROR_NOT_ENOUGH_MEMORY;
    }
    return retval;
}

static void *allocSegment_lib2ine(uint16 *selector, const int iscode)
{
    if (selector) *selector = 0xFFFF;
    return NULL;
}

static void freeSegment_lib2ine(const uint16 selector) {}

static void __attribute__((noreturn)) terminate_lib2ine(const uint32 exitcode)
{
    exit((int) exitcode);  // let static constructors and atexit() run.
}


LX_NATIVE_CONSTRUCTOR(lib2ine)
{
    if (pthread_key_create(&tlskey, NULL) != 0) {
        fprintf(stderr, "Couldn't create TLS key\n");
        abort();
    }

    memset(&GLoaderState, '\0', sizeof (GLoaderState));
    GLoaderState.dosExit = dosExit_lib2ine;
    GLoaderState.initOs2Tib = initOs2Tib_lib2ine;
    GLoaderState.setOs2Tib = setOs2Tib_lib2ine;
    GLoaderState.getOs2Tib = getOs2Tib_lib2ine;
    GLoaderState.deinitOs2Tib = deinitOs2Tib_lib2ine;
    GLoaderState.allocSegment = allocSegment_lib2ine;
    GLoaderState.freeSegment = freeSegment_lib2ine;
    GLoaderState.findSelector = findSelector_lib2ine;
    GLoaderState.freeSelector = freeSelector_lib2ine;
    GLoaderState.convert1616to32 = convert1616to32_lib2ine;
    GLoaderState.convert32to1616 = convert32to1616_lib2ine;
    GLoaderState.loadModule = loadModule_lib2ine;
    GLoaderState.makeUnixPath = makeUnixPath_lib2ine;
    GLoaderState.terminate = terminate_lib2ine;

    struct rlimit rlim;
    if (getrlimit(RLIMIT_STACK, &rlim) == -1) {
        rlim.rlim_cur = 8 * 1024 * 1024;  // oh well.
    }
    GLoaderState.initOs2Tib(GLoaderState.main_tibspace, &rlim, rlim.rlim_cur, 0);  // hopefully that's close enough on the stack address.
    GLoaderState.main_tib_selector = GLoaderState.setOs2Tib(GLoaderState.main_tibspace);

    if (getenv("TRACE_NATIVE")) {
        GLoaderState.trace_native = 1;
    }

    if (getenv("TRACE_EVENTS")) {
        GLoaderState.trace_events = 1;
    }

    GLoaderState.subprocess = (getenv("IS_2INE") != NULL);

    initPib();

    GLoaderState.running = 1;
}

LX_NATIVE_DESTRUCTOR(lib2ine)
{
    free(GLoaderState.pib.pib_pchenv);
    memset(&GLoaderState, '\0', sizeof (GLoaderState));
    pthread_key_delete(tlskey);
}


// end of lib2ine.c ...


