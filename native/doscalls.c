/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "doscalls.h"

#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "doscalls-lx.h"

static pthread_mutex_t GMutexDosCalls;

typedef struct ExitListItem
{
    PFNEXITLIST fn;
    uint32 priority;
    struct ExitListItem *next;
} ExitListItem;

static ExitListItem *GExitList = NULL;

// Flags returned for character devices from DosQueryHType()...
enum {
    DAW_STDIN  = (1 << 0),
    DAW_STDOUT = (1 << 1),
    DAW_NUL = (1 << 2),
    DAW_CLOCK = (1 << 3),
    DAW_SPEC = (1 << 4),
    DAW_ADD_ON = (1 << 5),
    DAW_GIOCTL  = (1 << 6),

    DAW_LEVEL0 = 0,
    DAW_LEVEL1 = (1 << 7),
    DAW_LEVEL2 = (1 << 8),
    DAW_LEVEL3 = (1 << 7) | (1 << 8),
    DAW_FCNLEV = (1 << 7) | (1 << 8) | (1 << 9),

    // ((1 << 10) is reserved, I think.)

    DAW_OPENCLOSE = (1 << 11),
    DAW_SHARE = (1 << 12),
    DAW_NONIBM = (1 << 13),
    DAW_IDC = (1 << 14),
    DAW_CHARACTER = (1 << 15)
};

typedef struct HFileInfo
{
    int fd;       // unix file descriptor.
    ULONG type;   // file, pipe, device, etc.
    ULONG attr;   // for character devices, DAW_*
    ULONG flags;  // OPEN_FLAGS_*
} HFileInfo;

static HFileInfo *HFiles = NULL;
static uint32 MaxHFiles = 0;

typedef struct Thread
{
    pthread_t thread;
    size_t stacklen;
    PFNTHREAD fn;
    ULONG fnarg;
    uint16 selector;
    struct Thread *prev;
    struct Thread *next;
} Thread;

static Thread *GDeadThreads = NULL;


typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    volatile int posted;
    volatile int waiting;
} EventSem;


typedef struct
{
    DIR *dirp;
    ULONG attr;
    ULONG level;
    char pattern[CCHMAXPATHCOMP];
} DirFinder;

static DirFinder GHDir1;

static GINFOSEG *ginfo;
static SEL ginfosel;
static LINFOSEG *linfo;
static SEL linfosel;

static HDIR *hdir16 = 0;

static int grabLock(pthread_mutex_t *lock)
{
    return (pthread_mutex_lock(lock) == 0);
} // grabLock

static void ungrabLock(pthread_mutex_t *lock)
{
    pthread_mutex_unlock(lock);
} // ungrabLock

static void timespecNowPlusMilliseconds(struct timespec *waittime, const ULONG ulTimeout)
{
    clock_gettime(CLOCK_REALTIME, waittime);
    waittime->tv_sec += ulTimeout / 1000;
    waittime->tv_nsec += (ulTimeout % 1000) * 1000000;
    if (waittime->tv_nsec >= 1000000000) {
        waittime->tv_sec++;
        waittime->tv_nsec -= 1000000000;
        assert(waittime->tv_nsec < 1000000000);
    } // if
} // timespecNowPlusMilliseconds

static void initHFileInfoFromUnixFd(const int fd, HFileInfo *info)
{
    struct stat statbuf;
    if ((fd != -1) && (fstat(fd, &statbuf) == 0)) {
        ULONG type = 0;
        switch (statbuf.st_mode & S_IFMT) {
            case S_IFREG:
                type = 0;
                break;
            case S_IFBLK:
            case S_IFCHR:
                type = 1;
                break;
            case S_IFIFO:
            case S_IFSOCK:
            default:
                type = 2;
                break;
        } // switch
        info->fd = fd;
        info->type = type;
        FIXME("What should this be?");
        info->attr = DAW_STDIN | DAW_STDOUT | DAW_LEVEL1 | DAW_CHARACTER;
        info->flags = 0;
    } else {
        info->fd = -1;
        info->type = 0;
        info->attr = 0;
        info->flags = 0;
    } // else
} // initHFileInfoFromUnixFd


APIRET DosGetInfoBlocks(PTIB *pptib, PPIB *pppib)
{
    TRACE_NATIVE("DosGetInfoBlocks(%p, %p)", pptib, pppib);

    if (pptib != NULL)
        *pptib = (PTIB) LX_GETTIB();

    if (pppib != NULL)
        *pppib = (PPIB) &GLoaderState.pib;

    return 0;
} // DosGetInfoBlocks

APIRET DosQuerySysInfo(ULONG first, ULONG last, PVOID _buf, ULONG buflen)
{
    TRACE_NATIVE("DosQuerySysInfo(%u, %u, %p, %u)", (uint) first, (uint) last, _buf, (uint) buflen);

    uint32 *buf = (uint32 *) _buf;
    if (last < first) return ERROR_INVALID_PARAMETER;
    if ( (buflen / sizeof (uint32)) < ((last - first) + 1) ) return ERROR_BUFFER_OVERFLOW;
    for (uint32 varid = first; varid <= last; varid++) {
        switch (varid) {
            case QSV_MAX_PATH_LENGTH: *(buf++) = PATH_MAX; break;
            case QSV_MAX_TEXT_SESSIONS: *(buf++) = 999999; break;
            case QSV_MAX_PM_SESSIONS: *(buf++) = 999999; break;
            case QSV_MAX_VDM_SESSIONS: *(buf++) = 0; break;
            case QSV_BOOT_DRIVE: *(buf++) = 3; break;  // "C:"
            case QSV_DYN_PRI_VARIATION: *(buf++) = 1; break;
            case QSV_MAX_WAIT: *(buf++) = 1; break;
            case QSV_MIN_SLICE: *(buf++) = 1; break;
            case QSV_MAX_SLICE: *(buf++) = 10; break;
            case QSV_PAGE_SIZE: *(buf++) = 4096; break;
            // !!! FIXME: change the version number in some way so apps can know this isn't actually OS/2.
            case QSV_VERSION_MAJOR: *(buf++) = 20; break;   // OS/2 Warp 4.0
            case QSV_VERSION_MINOR: *(buf++) = 40; break;   // OS/2 Warp 4.0
            case QSV_VERSION_REVISION: *(buf++) = 0; break; // OS/2 Warp 4.0

            case QSV_MS_COUNT: {
                static long startoffset = 0;
                if (startoffset == 0) {
                    struct timespec ts;
                    struct sysinfo info;
                    sysinfo(&info);
                    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
                    startoffset = info.uptime - ((long) ts.tv_sec);
                } // if

                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
                *(buf++) = (uint32) (((long) ts.tv_sec) - startoffset);
                break;
            } // case

            case QSV_TIME_LOW: *(buf++) = (uint32) time(NULL); break;
            case QSV_TIME_HIGH: *(buf++) = 0; break;  // !!! FIXME: 32-bit Linux time_t is 32-bits!

            case QSV_TOTPHYSMEM: {
                struct sysinfo info;
                sysinfo(&info);
                *(buf++) = (uint32) info.totalram;
                break;
            } // case

            case QSV_TOTRESMEM: {
                struct sysinfo info;
                sysinfo(&info);
                *(buf++) = (uint32) info.totalram - info.freeram;  // !!! FIXME: probably not right?
                break;
            } // case

            case QSV_TOTAVAILMEM: {
                struct sysinfo info;
                sysinfo(&info);
                *(buf++) = (uint32) info.freeram;
                break;
            } // case

            case QSV_MAXPRMEM: *(buf++) = 0x7FFFFFFF; break;  // !!! FIXME: should I list all 4 gigs? Also: what about machines with < 2 gigs?
            case QSV_MAXSHMEM: *(buf++) = 0x7FFFFFFF; break;  // !!! FIXME: should I list all 4 gigs? Also: what about machines with < 2 gigs?

            case QSV_TIMER_INTERVAL: *(buf++) = 10; break;
            case QSV_MAX_COMP_LENGTH: *(buf++) = NAME_MAX; break;
            case QSV_FOREGROUND_FS_SESSION: *(buf++) = 1; break;  // !!! FIXME
            case QSV_FOREGROUND_PROCESS: *(buf++) = 1; break;  // !!! FIXME

            default: return ERROR_INVALID_PARAMETER;
        } // switch
    } // for

    return NO_ERROR;
} // DosQuerySysInfo


APIRET DosQueryModuleName(HMODULE hmod, ULONG buflen, PCHAR buf)
{
    TRACE_NATIVE("DosQueryModuleName(%u, %u, %p)", (uint) hmod, (uint) buflen, buf);

    const LxModule *lxmod = (LxModule *) hmod;
    // !!! FIXME: error 6 ERROR_INVALID_HANDLE
    if (strlen(lxmod->os2path) >= buflen)
        return ERROR_BAD_LENGTH;
    strcpy(buf, lxmod->os2path);
    return NO_ERROR;
} // DosQueryModuleName


APIRET DosScanEnv(PSZ name, PSZ *outval)
{
    TRACE_NATIVE("DosScanEnv('%s', %p)", name, outval);

    char *env = GLoaderState.pib.pib_pchenv;
    const size_t len = strlen(name);
    while (*env) {
        if ((strncmp(env, name, len) == 0) && (env[len] == '=')) {
            *outval = env + len + 1;
            return NO_ERROR;
        } // if
        env += strlen(env) + 1;
    } // while

    return ERROR_ENVVAR_NOT_FOUND;
} // DosScanEnv


static int getHFileUnixDescriptor(HFILE h)
{
    grabLock(&GMutexDosCalls);
    const int fd = (h < MaxHFiles) ? HFiles[h].fd : -1;
    ungrabLock(&GMutexDosCalls);
    return fd;
} // getHFileUnixDescriptor

static APIRET DosWrite_implementation(HFILE h, PVOID buf, ULONG buflen, PULONG actual)
{
    const int fd = getHFileUnixDescriptor(h);
    if (fd == -1)
        return ERROR_INVALID_HANDLE;

    // !!! FIXME: writing to a terminal should probably convert CR/LF to LF.
    const int rc = write(fd, buf, buflen);
    if (rc < 0) {
        *actual = 0;
        return ERROR_DISK_FULL;  // !!! FIXME: map these errors.
    } // if

    *actual = (uint32) rc;
    return NO_ERROR;
} // DosWrite_implementation

APIRET DosWrite(HFILE h, PVOID buf, ULONG buflen, PULONG actual)
{
    TRACE_NATIVE("DosWrite(%u, %p, %u, %p)", (uint) h, buf, (uint) buflen, actual);
    return DosWrite_implementation(h, buf, buflen, actual);
} // DosWrite

static void runDosExitList(const uint32 why)
{
    ExitListItem *next = NULL;
    for (ExitListItem *item = GExitList; item; item = next) {
        // don't run any of these more than once:
        //  http://www.verycomputer.com/3_c1f6e02c06ed108e_1.htm
        PFNEXITLIST fn = item->fn;
        next = item->next;
        GExitList = next;
        free(item);
        //printf("calling dosexitlist item %p\n", fn); fflush(stdout);
        fn(why);
        //printf("dosexitlist item %p returned!\n", fn); fflush(stdout);
    } // for
} // runDosExitList

static VOID DosExit_implementation(ULONG action, ULONG exitcode)
{
    // !!! FIXME: what does a value other than 0 or 1 do here?
    if (action == EXIT_THREAD) {
        // !!! FIXME: If last thread: terminate process.
        FIXME("if last thread, treat as DosExit(1)");  // right now it will terminate process like exit(0), without running the exit list or reporting exitcode.
        pthread_exit(NULL);  // DosWaitThread doesn't report exit codes.
    } // if

    // terminate the process.
    runDosExitList(TC_EXIT);

    GLoaderState.terminate(exitcode);
} // DosExit_implementation

VOID DosExit(ULONG action, ULONG exitcode)
{
    TRACE_NATIVE("DosExit(%u, %u)", (uint) action, (uint) exitcode);
    DosExit_implementation(action, exitcode);
} // DosExit

APIRET DosExitList(ULONG ordercode, PFNEXITLIST fn)
{
    TRACE_NATIVE("DosExitList(%u, %p)", (uint) ordercode, fn);

    if (fn == NULL)
        return ERROR_INVALID_FUNCTION;

    const uint8 cmd = ordercode & 0xFF;
    const uint8 arg = (ordercode >> 8) & 0xFF;
    ExitListItem *prev = NULL;
    ExitListItem *item = NULL;

    // !!! FIXME: docs say this is illegal, but have to check what OS/2 actually does here.
    if ((cmd != EXLST_ADD) && (arg != 0))
        return ERROR_INVALID_DATA;

    switch (cmd) {
        case EXLST_ADD: {
            ExitListItem *newitem = (ExitListItem *) malloc(sizeof (ExitListItem));
            if (!newitem)
                return ERROR_NOT_ENOUGH_MEMORY;
            newitem->fn = fn;
            for (item = GExitList; item; item = item->next) {
                if (item->priority >= ((uint32) arg))
                    break;
                prev = item;
            } // for
            if (prev) {
                prev->next = newitem;
                newitem->next = item;
            } else {
                newitem->next = GExitList;
                GExitList = newitem;
            } // else
            return NO_ERROR;
        } // case

        case EXLST_REMOVE: {
            for (item = GExitList; item; item = item->next) {
                if (item->fn == fn) {
                    if (prev)
                        prev->next = item->next;
                    else
                        GExitList = item->next;
                    free(item);
                    return NO_ERROR;
                } // if
                prev = item;
            } // for
            return ERROR_INVALID_FUNCTION;  // !!! FIXME: yeah?
        } // case

        case EXLST_EXIT:
            return NO_ERROR;  // ... just treat this as a no-op, I guess...?

        default: return ERROR_INVALID_DATA;
    } // switch

    return NO_ERROR;
} // DosExitList

APIRET DosCreateEventSem(PSZ name, PHEV phev, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateEventSem('%s', %p, %u, %u)", name, phev, (uint) attr, (uint) state);

    if (name != NULL) {
        FIXME("implement named semaphores");
        return ERROR_NOT_ENOUGH_MEMORY;
    } // if

    if (attr & DC_SEM_SHARED) {
        FIXME("implement shared semaphores");
    } // if

    assert(sizeof (HEV) == sizeof (EventSem *));
    EventSem *sem = (EventSem *) malloc(sizeof (EventSem));
    if (sem == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    if (pthread_mutex_init(&sem->mutex, NULL) == -1) {
        free(sem);
        return ERROR_NOT_ENOUGH_MEMORY;
    } // if

    if (pthread_cond_init(&sem->condition, NULL) == -1) {
        pthread_mutex_destroy(&sem->mutex);
        free(sem);
        return ERROR_NOT_ENOUGH_MEMORY;
    } // if

    sem->posted = state ? 1 : 0;

    *phev = (HEV) sem;

    return NO_ERROR;
} // DosCreateEventSem

APIRET DosCreateMutexSem(PSZ name, PHMTX phmtx, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateMutexSem('%s', %p, %u, %u)", name, phmtx, (uint) attr, (uint) state);

    if (name != NULL) {
        FIXME("implement named mutexes");
        return ERROR_NOT_ENOUGH_MEMORY;
    } // if

    if (attr & DC_SEM_SHARED) {
        FIXME("implement shared mutexes");
    } // if

    assert(sizeof (HMTX) == sizeof (pthread_mutex_t *));
    pthread_mutex_t *mtx = (pthread_mutex_t *) malloc(sizeof (pthread_mutex_t));
    if (mtx == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    // OS/2 mutexes are recursive.
    pthread_mutexattr_t muxattr;
    pthread_mutexattr_init(&muxattr);
    pthread_mutexattr_settype(&muxattr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutexattr_setrobust(&muxattr, PTHREAD_MUTEX_ROBUST);

    const int rc = pthread_mutex_init(mtx, &muxattr);
    pthread_mutexattr_destroy(&muxattr);

    if (rc == -1) {
        free(mtx);
        return ERROR_NOT_ENOUGH_MEMORY;
    } // if

    if (state)
        pthread_mutex_lock(mtx);

    *phmtx = (HMTX) mtx;

    return NO_ERROR;
} // DosCreateMutexSem

// !!! FIXME: this is obviously not correct.
APIRET DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosSetExceptionHandler(%p)", rec);
    return NO_ERROR;
} // DosSetExceptionHandler

// !!! FIXME: this is obviously not correct.
APIRET DosUnsetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosUnsetExceptionHandler(%p)", rec);
    return NO_ERROR;
} // DosSetExceptionHandler

#if !LX_LEGACY
ULONG DosFlatToSel(VOID) { return 0; }
#else
ULONG _DosFlatToSel(PVOID ptr)
{
    TRACE_NATIVE("DosFlatToSel(%p)", ptr);
    return GLoaderState.convert32to1616(ptr);
} // _DosFlatToSel

// DosFlatToSel() passes its argument in %eax, so a little asm to bridge that...
// Note that this isn't syscall calling conventions at all; CSet/2 generates
//  thunking code that expects this function to preserve %ecx, so we'll just
//  go ahead and save a bunch of stuff, just in case.
__asm__ (
    ".globl DosFlatToSel  \n\t"
    ".type	DosFlatToSel, @function \n\t"
    "DosFlatToSel:  \n\t"
    "    pushl %ebx  \n\t"  // save off a bunch of stuff. Better safe than sorry.
    "    pushl %ecx  \n\t"
    "    pushl %edx  \n\t"
    "    pushl %edi  \n\t"
    "    pushl %esi  \n\t"
    "    pushl %eax  \n\t"  // the actual argument to DosFlatToSel.
    "    call _DosFlatToSel  \n\t"
    "    addl $4, %esp  \n\t"  // clear argument to DosFlatToSel.
    "    popl %esi  \n\t"
    "    popl %edi  \n\t"
    "    popl %edx  \n\t"  // save off a bunch of stuff.
    "    popl %ecx  \n\t"
    "    popl %ebx  \n\t"
    "    ret  \n\t"
	".size	_DosFlatToSel, .-_DosFlatToSel  \n\t"
);
#endif

APIRET DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes)
{
    TRACE_NATIVE("DosSetSignalExceptionFocus(%u, %p)", (uint) flag, pulTimes);

    if (flag == 0) {
        if (GLoaderState.main_module->signal_exception_focus_count == 0)
            return ERROR_ALREADY_RESET;
        GLoaderState.main_module->signal_exception_focus_count--;
    } else if (flag == 1) {
        if (GLoaderState.main_module->signal_exception_focus_count == 0xFFFFFFFF)
            return ERROR_NESTING_TOO_DEEP;
        GLoaderState.main_module->signal_exception_focus_count++;
    } else {
        // !!! FIXME: does OS/2 do something if flag != 0 or 1?
    } // else

    if (pulTimes)
        *pulTimes = GLoaderState.main_module->signal_exception_focus_count;

    // !!! FIXME: I guess enable/disable SIGINT handler here?

    return NO_ERROR;
} // DosSetSignalExceptionFocus

APIRET DosSetRelMaxFH(PLONG pincr, PULONG pcurrent)
{
    TRACE_NATIVE("DosSetRelMaxFH(%p, %p)", pincr, pcurrent);

    #if 0
    struct rlimit rlim;
    int rc = getrlimit(RLIMIT_NOFILE, &rlim);
    assert(rc == 0);  // this shouldn't fail...right?
    if (pcurrent)
        *pcurrent = (ULONG) rlim.rlim_cur;

    if (pincr) {
        rlim.rlim_cur = (rlim_t) (((LONG) rlim.rlim_cur) + *pincr);
        if (setrlimit(RLIMIT_NOFILE, &rlim) == 0) {
            if (pcurrent)
                *pcurrent = (ULONG) rlim.rlim_cur;
        } // if
    } // if
    #endif

    if (pincr != NULL) {
        const LONG incr = *pincr;
        // OS/2 API docs say reductions in file handles are advisory, so we
        //  ignore them outright. Fight me.
        if (incr > 0) {
            grabLock(&GMutexDosCalls);
            HFileInfo *info = (HFileInfo *) realloc(HFiles, sizeof (HFileInfo) * (MaxHFiles + incr));
            if (info != NULL) {
                HFiles = info;
                FIXME("this needs to initialize the end of the array, not the front");
                for (LONG i = 0; i < incr; i++, info++) {
                    info->fd = -1;
                    info->type = 0;
                    info->attr = 0;
                    info->flags = 0;
                } // for
                MaxHFiles += incr;
            } // if
            ungrabLock(&GMutexDosCalls);
        } // if
    } // if

    if (pcurrent != NULL) {
        grabLock(&GMutexDosCalls);
        *pcurrent = MaxHFiles;
        ungrabLock(&GMutexDosCalls);
    } // if

    return NO_ERROR;  // always returns NO_ERROR even if it fails.
} // DosSetRelMaxFH


APIRET DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag)
{
    TRACE_NATIVE("DosAllocMem(%p, %u, %u)", ppb, (uint) cb, (uint) flag);

    // !!! FIXME: this API is actually much more complicated than this.
    *ppb = calloc(1, cb);
    if (!*ppb)
        return ERROR_NOT_ENOUGH_MEMORY;
    return NO_ERROR;
} // DosAllocMem


APIRET DosSubSetMem(PVOID pbBase, ULONG flag, ULONG cb)
{
    TRACE_NATIVE("DosSubSetMem(%p, %u, %u)", pbBase, (uint) flag, (uint) cb);
    return NO_ERROR;  // !!! FIXME: obviously wrong
} // DosSubSetMem


APIRET DosSubAllocMem(PVOID pbBase, PPVOID ppb, ULONG cb)
{
    TRACE_NATIVE("DosSubAllocMem(%p, %p, %u)", pbBase, ppb, (uint) cb);
    *ppb = malloc(cb);  // !!! FIXME: obviously wrong
    if (!*ppb)
        return ERROR_NOT_ENOUGH_MEMORY;
    return NO_ERROR;
} // DosSubAllocMem

static APIRET DosQueryHType_implementation(HFILE hFile, PULONG pType, PULONG pAttr)
{
    APIRET retval = NO_ERROR;

    grabLock(&GMutexDosCalls);
    if ((hFile >= MaxHFiles) || (HFiles[hFile].fd == -1))
        retval = ERROR_INVALID_HANDLE;
    else {
        // OS/2 will dereference a NULL pointer here, but I can't do it...!!!
        if (pType) *pType = HFiles[hFile].type;
        if (pAttr) *pAttr = HFiles[hFile].attr;
    } // else
    ungrabLock(&GMutexDosCalls);

    return retval;
} // DosQueryHType_implementation

APIRET DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr)
{
    TRACE_NATIVE("DosQueryHType(%u, %p, %p)", (uint) hFile, pType, pAttr);
    return DosQueryHType_implementation(hFile, pType, pAttr);
} // DosQueryHType

APIRET DosSetMem(PVOID pb, ULONG cb, ULONG flag)
{
    TRACE_NATIVE("DosSetMem(%p, %u, %u)", pb, (uint) cb, (uint) flag);
    return NO_ERROR;  // !!! FIXME: obviously wrong.
} // DosSetMem

APIRET DosGetDateTime(PDATETIME pdt)
{
    TRACE_NATIVE("DosGetDateTime(%p)", pdt);

    // we can't use time() for this, since we need sub-second resolution.
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm ltm;
    localtime_r(&tv.tv_sec, &ltm);

    pdt->hours = ltm.tm_hour;
    pdt->minutes = ltm.tm_min;
    pdt->seconds = (ltm.tm_sec == 60) ? 59 : ltm.tm_sec;  // !!! FIXME: I assume OS/2 doesn't support leap seconds...
    pdt->hundredths = tv.tv_usec / 10000000;  // microseconds to hundreths.
    pdt->day = ltm.tm_mday;
    pdt->month = ltm.tm_mon + 1;
    pdt->year = ltm.tm_year + 1900;
    pdt->timezone = -(ltm.tm_gmtoff / 60);  // OS/2 is minutes west of GMT, Unix is seconds east. Convert and flip!
    pdt->weekday = ltm.tm_wday;
    
    return NO_ERROR;  // never returns failure.
} // DosGetDateTime

static char *makeUnixPath(const char *os2path, APIRET *err)
{
    return GLoaderState.makeUnixPath(os2path, (uint32 *) err);
} // makeUnixPath

static APIRET doDosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, LONGLONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2)
{
    int isReadOnly = 0;
    int flags = 0;
    const ULONG access_mask = OPEN_ACCESS_READONLY | OPEN_ACCESS_WRITEONLY | OPEN_ACCESS_READWRITE;
    switch (fsOpenMode & access_mask) {
        case OPEN_ACCESS_READONLY: isReadOnly = 1; flags |= O_RDONLY; break;
        case OPEN_ACCESS_WRITEONLY: flags |= O_WRONLY; break;
        case OPEN_ACCESS_READWRITE: flags |= O_RDWR; break;
        default: return ERROR_INVALID_PARAMETER;
    } // switch

    if (isReadOnly && (cbFile != 0))
        return ERROR_INVALID_PARAMETER;
    else if (ulAttribute & FILE_DIRECTORY)
        return ERROR_INVALID_PARAMETER;  // !!! FIXME: I assume you can't create a directory with DosOpen...right?
    else if (isReadOnly && ((fsOpenFlags & 0xF0) == OPEN_ACTION_CREATE_IF_NEW))
        return ERROR_INVALID_PARAMETER;  // !!! FIXME: is this invalid on OS/2?
    else if (isReadOnly && ((fsOpenFlags & 0x0F) == OPEN_ACTION_REPLACE_IF_EXISTS))
        return ERROR_INVALID_PARAMETER;  // !!! FIXME: is this invalid on OS/2?
    else if (fsOpenFlags & 0xfffffe00)  // bits (1<<8) through (1<<31) are reserved, must be zero.
        return ERROR_INVALID_PARAMETER;
    else if (fsOpenFlags & OPEN_FLAGS_DASD)  // no good is going to come of this.
        return ERROR_OPEN_FAILED;
    else if (fsOpenMode & 0xffff0000)  // bits (1<<16) through (1<<31) are reserved, must be zero.
        return ERROR_INVALID_PARAMETER;

    if (peaop2 != NULL) {
        FIXME("EAs not yet implemented");
        return ERROR_OPEN_FAILED;
    }

    switch (fsOpenFlags & 0xF0) {
        case OPEN_ACTION_FAIL_IF_NEW:
            break;  // just don't O_CREAT and you're fine.

        case OPEN_ACTION_CREATE_IF_NEW:
            flags |= O_CREAT | O_EXCL;  // the O_EXCL is intentional! see below.
            break;
    } // switch

    int mustNotExist = 0;
    int isExclusive = 0;
    int isReplacing = 0;

    switch (fsOpenFlags & 0x0F) {
        case OPEN_ACTION_FAIL_IF_EXISTS:
            if (isReadOnly)
                mustNotExist = 1;
            else {
                isExclusive = 1;
                flags |= O_CREAT | O_EXCL;
            } // else
            break;

        case OPEN_ACTION_OPEN_IF_EXISTS:
            break;  // nothing to do here.

        case OPEN_ACTION_REPLACE_IF_EXISTS:  // right now, we already failed above if readonly.
            isReplacing = 1;
            flags |= O_TRUNC;  // have the open call nuke it, and then we'll ftruncate to grow it if necessary.
            break;
    } // switch

    if (fsOpenMode & OPEN_FLAGS_WRITE_THROUGH)
        flags |= O_SYNC;  // !!! FIXME: this flag is supposed to drop when inherited by children, but fcntl() can't do that on Linux.

    if (fsOpenMode & OPEN_FLAGS_NOINHERIT)
        flags |= O_CLOEXEC;

    if (fsOpenMode & OPEN_FLAGS_FAIL_ON_ERROR)
        FIXME("need other file i/o APIs to do a system dialog box if this handle has i/o failures!");

    // O_DIRECT isn't supported on ZFS-on-Linux, so I'm already not on board,
    //  but also: do we care if programs intended to run on a system with a
    //  few megabytes of RAM--on an OS that didn't have a resizeable file
    //  cache!--writes into a multi-gigabyte machine's flexible cache? I don't!
    //if (fsOpenMode & OPEN_FLAGS_NO_CACHE)
    //    flags |= O_DIRECT;

    FIXME("There are fsOpenMode flags we currently ignore (like sharing support)");

    const mode_t mode = S_IRUSR | ((ulAttribute & FILE_READONLY) ? 0 : S_IWUSR);
    FIXME("Most of the file attributes don't make sense on Unix, but we could stuff them in EAs");

    HFileInfo *info = HFiles;
    uint32 i;

    if (!grabLock(&GMutexDosCalls))
        return ERROR_SYS_INTERNAL;

    for (i = 0; i < MaxHFiles; i++, info++) {
        if (info->fd == -1) {  // available?
            info->fd = -2;
            break;
        } // if
    } // for

    ungrabLock(&GMutexDosCalls);

    if (i == MaxHFiles) {
        return ERROR_TOO_MANY_OPEN_FILES;
    }

    const HFILE hf = i;

    APIRET err = NO_ERROR;
    char *unixpath = makeUnixPath(pszFileName, &err);
    if (!unixpath) {
        info->fd = -1;
        return err;
    } // if

    //if (strcmp(pszFileName, unixpath) != 0) { fprintf(stderr, "DosOpen: converted '%s' to '%s'\n", pszFileName, unixpath); }

    int fd = open(unixpath, flags, mode);

    // if failed trying exclusive-create because file already exists, but we
    //  didn't explicitly _need_ exclusivity, retry without O_EXCL. We always
    //  try O_EXCL at first when using O_CREAT, so we can tell atomically if
    //  we were the one that created the file, to satisfy pulAction.
    int existed = 0;
    if ((fd == -1) && (flags & (O_CREAT|O_EXCL)) && (errno == EEXIST) && !isExclusive) {
        existed = 1;
        fd = open(unixpath, flags & ~O_EXCL, mode);
    } else if (fd != -1) {
        existed = 1;
    } // else if
    free(unixpath);

    if (fd != -1) {
        if (mustNotExist) {
            close(fd);
            info->fd = -1;
            return ERROR_OPEN_FAILED;  // !!! FIXME: what error does OS/2 return for this?
        } else if ( (!existed || isReplacing) && (cbFile > 0) ) {
            if (ftruncate(info->fd, cbFile) == -1) {
                const int e = errno;
                close(fd);
                info->fd = -1;
                errno = e;  // let the switch below sort out the OS/2 error code.
            } // if
        } // else if
    } // if

    if (fd != -1) {
        if (isReplacing)
            FIXME("Replacing a file should delete all its EAs, too");
    } // if

    initHFileInfoFromUnixFd(fd, info);

    if (fd == -1) {
        switch (errno) {
            case EACCES: return ERROR_ACCESS_DENIED;
            case EDQUOT: return ERROR_CANNOT_MAKE;
            case EEXIST: return ERROR_CANNOT_MAKE;
            case EISDIR: return ERROR_ACCESS_DENIED;
            case EMFILE: return ERROR_TOO_MANY_OPEN_FILES;
            case ENFILE: return ERROR_TOO_MANY_OPEN_FILES;
            case ENAMETOOLONG: return ERROR_FILENAME_EXCED_RANGE;
            case ENOSPC: return ERROR_DISK_FULL;
            case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
            case ENOENT: return ERROR_FILE_NOT_FOUND;  // !!! FIXME: could be PATH_NOT_FOUND too, depending on circumstances.
            case ENOTDIR: return ERROR_PATH_NOT_FOUND;
            case EPERM: return ERROR_ACCESS_DENIED;
            case EROFS: return ERROR_ACCESS_DENIED;
            case ETXTBSY: return ERROR_ACCESS_DENIED;
            default: return ERROR_OPEN_FAILED;  // !!! FIXME: debug logging about missin errno case.
        } // switch
        __builtin_unreachable();
    } // if

    info->flags = fsOpenFlags;

    if (isReplacing)
        *pulAction = FILE_TRUNCATED;
    else if (existed)
        *pulAction = FILE_EXISTED;
    else
        *pulAction = FILE_CREATED;

    *pHf = hf;
    return NO_ERROR;
} // doDosOpen

APIRET DosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, ULONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosOpen('%s', %p, %p, %u, %u, %u, %u, %p)", pszFileName, pHf, pulAction, (uint) cbFile, (uint) ulAttribute, (uint) fsOpenFlags, (uint) fsOpenMode, peaop2);
    return doDosOpen(pszFileName, pHf, pulAction, (LONGLONG) cbFile, ulAttribute, fsOpenFlags, fsOpenMode, peaop2);
} // DosOpen

APIRET DosRequestMutexSem(HMTX hmtx, ULONG ulTimeout)
{
    TRACE_NATIVE("DosRequestMutexSem(%u, %u)", (uint) hmtx, (uint) ulTimeout);

    if (!hmtx)
        return ERROR_INVALID_HANDLE;

    pthread_mutex_t *mutex = (pthread_mutex_t *) hmtx;
    int rc;

    if (ulTimeout == SEM_IMMEDIATE_RETURN) {
        rc = pthread_mutex_trylock(mutex);
    } else if (ulTimeout == SEM_INDEFINITE_WAIT) {
        rc = pthread_mutex_lock(mutex);
    } else {
        struct timespec waittime;
        timespecNowPlusMilliseconds(&waittime, ulTimeout);
        rc = pthread_mutex_timedlock(mutex, &waittime);
    } // else

    switch (rc) {
        case 0: return NO_ERROR;
        case EAGAIN: return ERROR_TOO_MANY_SEM_REQUESTS;
        case EOWNERDEAD: return ERROR_SEM_OWNER_DIED;
        case ETIMEDOUT: return ERROR_TIMEOUT;
        default: break;
    } // switch

    return ERROR_INVALID_HANDLE;  // oh well.
} // DosRequestMutexSem

APIRET DosReleaseMutexSem(HMTX hmtx)
{
    TRACE_NATIVE("DosReleaseMutexSem(%u)", (uint) hmtx);

    if (!hmtx)
        return ERROR_INVALID_HANDLE;

    pthread_mutex_t *mutex = (pthread_mutex_t *) hmtx;
    const int rc = pthread_mutex_unlock(mutex);
    if (rc == 0)
        return NO_ERROR;
    else if (rc == EPERM)
        return ERROR_NOT_OWNER;

    return ERROR_INVALID_HANDLE;
} // DosReleaseMutexSem

static APIRET DosSetFilePtr_implementation(HFILE hFile, LONG ib, ULONG method, PULONG ibActual)
{
    int whence;
    switch (method) {
        case FILE_BEGIN: whence = SEEK_SET; break;
        case FILE_CURRENT: whence = SEEK_CUR; break;
        case FILE_END: whence = SEEK_END; break;
        default: return ERROR_INVALID_FUNCTION;
    } // switch

    const int fd = getHFileUnixDescriptor(hFile);
    if (fd == -1)
        return ERROR_INVALID_HANDLE;

    const off_t pos = lseek(fd, (off_t) ib, whence);
    if (pos == -1) {
        if (errno == EINVAL)
            return ERROR_NEGATIVE_SEEK;
        return ERROR_INVALID_FUNCTION;  // !!! FIXME: ?
    } // if

    if (ibActual)
        *ibActual = (ULONG) pos;

    return NO_ERROR;
} // DosSetFilePtr_implementation

APIRET DosSetFilePtr(HFILE hFile, LONG ib, ULONG method, PULONG ibActual)
{
    TRACE_NATIVE("DosSetFilePtr(%u, %d, %u, %p)", (uint) hFile, (int) ib, (uint) method, ibActual);
    return DosSetFilePtr_implementation(hFile, ib, method, ibActual);
} // DosSetFilePtr

static APIRET DosRead_implementation(HFILE hFile, PVOID pBuffer, ULONG cbRead, PULONG pcbActual)
{
    const int fd = getHFileUnixDescriptor(hFile);
    if (fd == -1)
        return ERROR_INVALID_HANDLE;

    const ssize_t br = read(fd, pBuffer, cbRead);
    if (br == -1)
        return ERROR_INVALID_FUNCTION;  // !!! FIXME: ?

    if (pcbActual)
        *pcbActual = (ULONG) br;

    return NO_ERROR;
} // DosRead_implementation

APIRET DosRead(HFILE hFile, PVOID pBuffer, ULONG cbRead, PULONG pcbActual)
{
    TRACE_NATIVE("DosRead(%u, %p, %u, %p)", (uint) hFile, pBuffer, (uint) cbRead, pcbActual);
    return DosRead_implementation(hFile, pBuffer, cbRead, pcbActual);
} // DosRead

static APIRET DosClose_implementation(HFILE hFile)
{
    const int fd = getHFileUnixDescriptor(hFile);
    if (fd == -1)
        return ERROR_INVALID_HANDLE;

    if (fd <= 2) { FIXME("remove me"); return NO_ERROR; } // for debugging purposes, remove me later.

    const int rc = close(fd);
    if (rc == -1)
        return ERROR_ACCESS_DENIED;  // !!! FIXME: ?

    grabLock(&GMutexDosCalls);
    HFiles[hFile].fd = -1;
    HFiles[hFile].type = 0;
    HFiles[hFile].attr = 0;
    ungrabLock(&GMutexDosCalls);

    return NO_ERROR;
} // DosClose_implementation

APIRET DosClose(HFILE hFile)
{
    TRACE_NATIVE("DosClose(%u)", (uint) hFile);
    return DosClose_implementation(hFile);
} // DosClose


APIRET DosEnterMustComplete(PULONG pulNesting)
{
    TRACE_NATIVE("DosEnterMustComplete(%p)", pulNesting);

    PTIB2 tib2 = (PTIB2) LX_GETTIB2();
    if (tib2->tib2_usMCCount == 0xFFFF) {
        return ERROR_NESTING_TOO_DEEP;
    } else if (tib2->tib2_usMCCount == 0) {
        FIXME("block signals here");
    }

    tib2->tib2_usMCCount++;

    if (pulNesting)
        *pulNesting = tib2->tib2_usMCCount;

    return NO_ERROR;
} // DosEnterMustComplete

APIRET DosExitMustComplete(PULONG pulNesting)
{
    TRACE_NATIVE("DosExitMustComplete(%p)", pulNesting);

    PTIB2 tib2 = (PTIB2) LX_GETTIB2();
    if (tib2->tib2_usMCCount == 0) {
        return ERROR_ALREADY_RESET;
    } else if (tib2->tib2_usMCCount == 1) {
        FIXME("unblock signals here");
    }

    tib2->tib2_usMCCount--;

    if (pulNesting)
        *pulNesting = tib2->tib2_usMCCount;

    return NO_ERROR;
} // DosExitMustComplete

static void unixTimeToOs2(const time_t unixtime, FDATE *os2date, FTIME *os2time)
{
    struct tm lt;
    localtime_r(&unixtime, &lt);
    os2date->day = (USHORT) lt.tm_mday;
    os2date->month = (USHORT) (lt.tm_mon + 1);
    os2date->year = (USHORT) (lt.tm_year - 80);  // wants years since 1980, apparently.
    os2time->twosecs = (USHORT) (lt.tm_sec / 2);
    os2time->minutes = (USHORT) lt.tm_min;
    os2time->hours = (USHORT) lt.tm_hour;
} // unixTimeToOs2

static inline void initFileCreationDateTime(FDATE *fdate, FTIME *ftime)
{
    // we don't store creation date on Unix. OS/2 zeroes out fields that aren't
    //  appropriate to FAT file systems, but I don't want to risk apps using
    //  zeroes to decide a file in on a FAT disk and reducing features,
    //  shrinking filenames to 8.3, etc.
    if (fdate) {
        fdate->day = 1;
        fdate->month = 1;
        fdate->year = 0;  // years since 1980, apparently.
    } // if

    if (ftime) {
        ftime->twosecs = 0;
        ftime->minutes = 0;
        ftime->hours = 0;
    } // if
} // initFileCreationDateTime

static APIRET queryFileInfoStandardFromStat(const struct stat *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FILESTATUS3 *st = (FILESTATUS3 *) pInfoBuf;
    memset(st, '\0', sizeof (*st));

    initFileCreationDateTime(&st->fdateCreation, &st->ftimeCreation);
    unixTimeToOs2(statbuf->st_atime, &st->fdateLastAccess, &st->ftimeLastAccess);
    unixTimeToOs2(statbuf->st_mtime, &st->fdateLastWrite, &st->ftimeLastWrite);

    st->cbFile = (ULONG) statbuf->st_size;
    st->cbFileAlloc = (ULONG) (statbuf->st_blocks * 512);

    if (S_ISDIR(statbuf->st_mode))
        st->attrFile |= FILE_DIRECTORY;
    if ((statbuf->st_mode & S_IWUSR) == 0)  // !!! FIXME: not accurate...?
        st->attrFile |= FILE_READONLY;

    return NO_ERROR;
} // queryFileInfoStandardFromStat

static APIRET queryPathInfoStandard(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof (FILESTATUS3))
        return ERROR_BUFFER_OVERFLOW;

    struct stat statbuf;
    if (stat(unixPath, &statbuf) == -1) {
        return ERROR_PATH_NOT_FOUND;  // !!! FIXME
    }

    return queryFileInfoStandardFromStat(&statbuf, pInfoBuf, cbInfoBuf);
} // queryPathInfoStandard

static APIRET queryPathInfoEaSize(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof (FILESTATUS4))
        return ERROR_BUFFER_OVERFLOW;
    const APIRET rc = queryPathInfoStandard(unixPath, pInfoBuf, cbInfoBuf);
    if (rc != NO_ERROR)
        return rc;

    FILESTATUS4 *st = (FILESTATUS4 *) pInfoBuf;
    FIXME("write me");
    st->cbList = 0;
    return NO_ERROR;
} // queryPathInfoEaSize

static APIRET queryPathInfoFullName(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    char *real = realpath(unixPath, NULL);
    if (real == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;  // !!! FIXME: no

    if ((strlen(real) + 3) < cbInfoBuf) {
        free(real);
        return ERROR_BUFFER_OVERFLOW;
    } // if

    char *dst = (char *) pInfoBuf;
    *(dst++) = 'C';
    *(dst++) = ':';
    strcpy(dst, real);
    free(real);

    while (*dst) {
        if (*dst == '/')
            *dst = '\\';
        dst++;
    } // while

    return NO_ERROR;
} // queryPathInfoFullName

static APIRET queryPathInfoEasFromList(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FIXME("write me");
    return ERROR_INVALID_LEVEL;
} // queryPathInfoEasFromList

static APIRET queryPathInfo(PSZ unixPath, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    switch (ulInfoLevel) {
        case FIL_STANDARD: return queryPathInfoStandard(unixPath, pInfoBuf, cbInfoBuf);
        case FIL_QUERYEASIZE: return queryPathInfoEaSize(unixPath, pInfoBuf, cbInfoBuf);
        case FIL_QUERYEASFROMLIST: return queryPathInfoEasFromList(unixPath, pInfoBuf, cbInfoBuf);

        // OS/2 has an undocumented info level, 7, that appears to return a case-corrected version
        //  of the path. (FIL_QUERYFULLNAME doesn't correct the case of what the app queries on
        //  OS/2). Since we have to case-correct for a Unix filesystem anyhow, we just use the
        //  usual FIL_QUERYFULLNAME to handle the undocumented level. Java uses this level.
        case 7:
        case FIL_QUERYFULLNAME: return queryPathInfoFullName(unixPath, pInfoBuf, cbInfoBuf);
        default: break;
    } // switch

    return ERROR_INVALID_LEVEL;
} // queryPathInfo

APIRET DosQueryPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    TRACE_NATIVE("DosQueryPathInfo('%s', %u, %p, %u)", pszPathName, (uint) ulInfoLevel, pInfoBuf, (uint) cbInfoBuf);

    APIRET err = NO_ERROR;
    char *unixPath = makeUnixPath(pszPathName, &err);
    if (!unixPath)
        return err;
    err = queryPathInfo(unixPath, ulInfoLevel, pInfoBuf, cbInfoBuf);
    free(unixPath);
    return err;
} // DosQueryPathInfo

static APIRET queryFileInfoStandard(const struct stat *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof (FILESTATUS3))
        return ERROR_BUFFER_OVERFLOW;
    return queryFileInfoStandardFromStat(statbuf, pInfoBuf, cbInfoBuf);
} // queryFileInfoStandard

static APIRET queryFileInfoEaSize(const struct stat *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof (FILESTATUS4))
        return ERROR_BUFFER_OVERFLOW;
    const APIRET rc = queryFileInfoStandard(statbuf, pInfoBuf, cbInfoBuf);
    if (rc != NO_ERROR)
        return rc;

    FILESTATUS4 *st = (FILESTATUS4 *) pInfoBuf;
    FIXME("write me");
    st->cbList = 0;
    return NO_ERROR;
} // queryFileInfoEaSize

static APIRET queryFileInfoEasFromList(const struct stat *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FIXME("write me");
    return ERROR_INVALID_LEVEL;
} // queryFileInfoEasFromList


APIRET DosQueryFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo, ULONG cbInfoBuf)
{
    TRACE_NATIVE("DosQueryFileInfo(%u, %u, %p, %u)", (uint) hf, (uint) ulInfoLevel, pInfo, (uint) cbInfoBuf);

    const int fd = getHFileUnixDescriptor(hf);
    if (fd == -1)
        return ERROR_INVALID_HANDLE;

    struct stat statbuf;
    if (fstat(fd, &statbuf) == -1)
        return ERROR_INVALID_HANDLE;  // !!! FIXME: ...?

    switch (ulInfoLevel) {
        case FIL_STANDARD: return queryFileInfoStandard(&statbuf, pInfo, cbInfoBuf);
        case FIL_QUERYEASIZE: return queryFileInfoEaSize(&statbuf, pInfo, cbInfoBuf);
        case FIL_QUERYEASFROMLIST: return queryFileInfoEasFromList(&statbuf, pInfo, cbInfoBuf);
        default: break;
    } // switch

    return ERROR_INVALID_LEVEL;
} // DosQueryFileInfo

static void os2ThreadCleanup(void *arg)
{
    Thread *thread = (Thread *) arg;
    GLoaderState.deinitOs2Tib(thread->selector);
    grabLock(&GMutexDosCalls);
    thread->prev = NULL;
    thread->next = GDeadThreads;
    GDeadThreads = thread;
    ungrabLock(&GMutexDosCalls);
} // os2ThreadCleanup

static void os2ThreadEntry2(uint8 *tibspace, Thread *thread)
{
    void *esp = NULL;  // close enough.
    GLoaderState.initOs2Tib(tibspace, &esp, thread->stacklen, (TID) thread);
    thread->selector = GLoaderState.setOs2Tib(tibspace);
    pthread_cleanup_push(os2ThreadCleanup, thread);
    thread->fn(thread->fnarg);
    pthread_cleanup_pop(1);
} // os2ThreadEntry2

static void *os2ThreadEntry(void *arg)
{
    // put our thread's TIB structs on the stack and call that the top of the stack.
    uint8 tibspace[LXTIBSIZE];
    memset(tibspace, '\0', LXTIBSIZE);  // make sure TLS slots are clear, etc.
    os2ThreadEntry2(tibspace, (Thread *) arg);
    return NULL;  // OS/2 threads don't return a value here.
} // os2ThreadEntry

APIRET DosCreateThread(PTID ptid, PFNTHREAD pfn, ULONG param, ULONG flag, ULONG cbStack)
{
    TRACE_NATIVE("DosCreateThread(%p, %p, %u, %u, %u)", ptid, pfn, (uint) param, (uint) flag, (uint) cbStack);

    if (flag & CREATE_SUSPENDED)
        FIXME("Can't start threads in suspended state");

    Thread *thread = (Thread *) malloc(sizeof (Thread));
    if (!thread)
        return ERROR_NOT_ENOUGH_MEMORY;
    memset(thread, '\0', sizeof (*thread));

    size_t stacksize = cbStack;
    if ((stacksize % 4096) != 0)
        stacksize += 4096 - (cbStack % 4096);

    stacksize += 4096;  // leave a page for the TIB and host system libs that use more stack.

    thread->stacklen = stacksize;
    thread->fn = pfn;
    thread->fnarg = param;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stacksize);
    const int rc = pthread_create(&thread->thread, &attr, os2ThreadEntry, thread);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        free(thread);
        return ERROR_MAX_THRDS_REACHED;  // I guess...?
    } // if

    if (ptid)
        *ptid = (TID) thread;

    return NO_ERROR;
} // DosCreateThread


static void __attribute__((noreturn)) execOs2Child(const char *exe, char * const *argv, char * const *envp)
{
    // !!! FIXME: wire up any other things we need here.
    execve(exe, argv, envp);
    fprintf(stderr, "Failed to exec child process: %s\n", strerror(errno));
    _exit(1);  // uh oh.
} // execOs2Child

static char *findExePath(const char *exe, APIRET *err)
{
    FIXME("eventually this needs to search the path and maybe launch native (non-OS/2) processes too");
    char *retval = strdup("/proc/self/exe");
    if (!retval)
        *err = ERROR_NOT_ENOUGH_MEMORY;
    return retval;
} // findExePath

static char **calcEnvp(const char *os2env, const char *exe, APIRET *err)
{
    char **retval = NULL;
    size_t len = 0;
    size_t idx = 0;

    const char *src = os2env;
    if (*src == '\0') {
        retval = (char **) malloc(sizeof (char *));
        if (retval)
            retval[0] = NULL;
        return retval;
    } // if

    while (1) {
        if (*(src++))
            len++;
        else {
            idx++;
            len += sizeof (char *) + 1;
            if (*src == '\0')
                break;
        } // else
    } // while

    len += sizeof (char *);  // NULL at the end.
    len += strlen(exe) + 16 + sizeof (char *);  // IS_2INE environment var.
    idx++;  // IS_2INE

    retval = (char **) malloc(len);
    if (!retval) {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    } // if

    char *dst = (char *) (retval + (idx + 1));
    char *start = dst;
    src = os2env;
    idx = 0;
    while (1) {
        const char ch = *(src++);
        *(dst++) = ch;
        if (!ch) {
            retval[idx++] = start;
            start = dst;
            if (*src == '\0')
                break;
        } // if
    } // while
    strcpy(dst, "IS_2INE=");
    dst += strlen(dst);
    strcpy(dst, exe);
    dst += strlen(dst) + 1;
    *(dst++) = '\0';
    retval[idx++] = start;

    retval[idx] = NULL;
    return retval;
} // calcEnvp

static char **calcArgv(const char *os2args, APIRET *err)
{
    const size_t argv0len = strlen(os2args);
    if (argv0len == 0) {
        *err = ERROR_BAD_ENVIRONMENT;
        return NULL;  // bad.
    } // if

    size_t len = 0;
    size_t idx = 1;

    const char *src = os2args + argv0len + 1;

    // this can overallocate a little, but that's alright.
    while (*src) {
        const char ch = *(src++);
        if ((ch == ' ') || (ch == '\t'))
            idx++;
        len++;
    } // while

    idx++;  // one more for NULL terminator element.

    len += argv0len + 1 + sizeof (char *);
    len += idx * (sizeof (char *) + 1);

    void *allocated = malloc(len);
    if (!allocated) {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    } // if
    char **retval = (char **) allocated;
    char *dst = (char *) (retval + idx + 1);

    idx = 0;

    retval[idx++] = dst;
    strcpy(dst, os2args);
    dst += argv0len + 1;

    src = os2args + argv0len + 1;
    while ((*src == ' ') || (*src == '\t'))
        src++;  // skip whitespace.

    const char *srcstart = src;

    while (*src) {
        const char ch = *(src++);
        if ((ch == ' ') || (ch == '\t')) {
            retval[idx++] = dst;
            const size_t cpylen = ((size_t) (src - srcstart)) - 1;
            memcpy(dst, srcstart, cpylen);
            dst += cpylen;
            *(dst++) = '\0';
            while ((*src == ' ') || (*src == '\t'))
                src++;
            srcstart = src;
        } else if (ch == '"') {
            srcstart++;
            while ((*src != '"') && (*src != '\0'))
                src++;
            retval[idx++] = dst;
            const size_t cpylen = (size_t) (src - srcstart);
            memcpy(dst, srcstart, cpylen);
            dst += cpylen;
            *(dst++) = '\0';
            if (*src == '"')
                src++;
            while ((*src == ' ') || (*src == '\t'))
                src++;
            srcstart = src;
        } // else if
    } // for

    const size_t cpylen = (size_t) (src - srcstart);
    if (cpylen) {
        retval[idx++] = dst;
        memcpy(dst, srcstart, cpylen);
        dst += cpylen;
        *(dst++) = '\0';
    } // if

    retval[idx] = NULL;
    return retval;
} // calcArgv

static void setProcessResultCode(PRESULTCODES pRes, const int status)
{
    if (!pRes)
        return;

    if (WIFEXITED(status)) {
        // !!! FIXME: OS/2 processes exit with a ULONG, and returns bottom 16-bits of it for codeResult. Unix only gives you 8, though!
        pRes->codeTerminate = TC_EXIT;
        pRes->codeResult = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        FIXME("These might not be exactly right");
        pRes->codeResult = 0;
        switch (WTERMSIG(status)) {
            case SIGTERM:
            case SIGKILL:
            case SIGINT:
            case SIGQUIT:
                pRes->codeTerminate = TC_KILLPROCESS;
                break;
            case SIGPIPE:
            case SIGABRT:
            pRes->codeTerminate = TC_HARDERROR;
                break;
            default:
                pRes->codeTerminate = TC_EXCEPTION;
                break;
        } // switch
    } // else if
} // setProcessResultCode

static APIRET DosExecPgm_implementation(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg, PSZ pEnv, PRESULTCODES pRes, PSZ pName)
{
    APIRET retval = NO_ERROR;

    if (pObjname)  // !!! FIXME: what does it want to put in here for failures?
        *pObjname = '\0';

    if ((execFlag == EXEC_TRACE) || (execFlag == EXEC_ASYNCRESULTDB)) { // sorry debuggers!
        FIXME("tried to launch a process for debugging!");
        return ERROR_ACCESS_DENIED;
    } // if

    if (execFlag == EXEC_LOAD) {
        FIXME("What is this, exactly?");
        return ERROR_GEN_FAILURE;
    } // if

    APIRET err;
    char *pNameUnix = makeUnixPath(pName, &err);
    if (!pNameUnix)
        return err;

    // calculate the args and environment first, so the child process is literally just calling execve().
    char *exe = findExePath(pName, &retval);
    if (!exe) {
        free(pNameUnix);
        return retval;
    } // if

    char **argv = calcArgv(pArg, &retval);
    if (!argv) {
        free(exe);
        free(pNameUnix);
        return retval;
    } // if

    char **envp = calcEnvp(pEnv, pNameUnix, &retval);
    if (!argv) {
        free(argv);
        free(exe);
        free(pNameUnix);
        return retval;
    } // if

    //printf("EXEC'ING! exe='%s'\n", exe);
    //for (char * const *ptr = argv; *ptr; ptr++) { printf(" ARGV[%d] = '%s'\n", (int) (ptr - argv), *ptr); }
    //for (char * const *ptr = envp; *ptr; ptr++) { printf(" ENVP[%d] = '%s'\n", (int) (ptr - envp), *ptr); }

    const int doubleFork = (execFlag == EXEC_ASYNC) || (execFlag == EXEC_BACKGROUND);
    int status = 0;
    pid_t pid = fork();
    if (pid == -1)  // failed?
        retval = ERROR_NO_PROC_SLOTS;

    else if (pid == 0) {  // we're the child.
        if (doubleFork) {  // fork again so we aren't a direct child of the original parent.
            pid = fork();
            if (pid == -1) {  // uhoh.
                fprintf(stderr, "Failed to double-fork child process for EXEC_ASYNC: %s\n", strerror(errno));
                _exit(1);
            } else if (pid != 0) {  // we're the parent
                _exit(0);  // just die now.
            }
            // we're the double-forked child.
            if (setsid() < 0) {
                fprintf(stderr, "Failed to setsid(): %s\n", strerror(errno));
                _exit(1);
            } // if
            // !!! FIXME: need to report pid to original parent now via a temporary pipe.
        } // if

        // run the new program!
        execOs2Child(exe, argv, envp);

    } else {  // we're the parent.
        switch (execFlag) {
            case EXEC_SYNC: {
                const pid_t rc = waitpid(pid, &status, 0);
                assert(rc == pid);  // !!! FIXME
                setProcessResultCode(pRes, status);
                break;
            } // case

            case EXEC_ASYNC:
            case EXEC_ASYNCRESULT:
            case EXEC_BACKGROUND:
                if (pRes) {
                    pRes->codeResult = 0;
                    pRes->codeTerminate = (ULONG) pid;
                } // if
                break;

            default:
                assert(!"shouldn't hit this code");
                retval = ERROR_INVALID_FUNCTION;
                break;
        } // switch
    } // else

    free(envp);
    free(argv);
    free(exe);

    return retval;
} // DosExecPgm_implementation

APIRET DosExecPgm(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg, PSZ pEnv, PRESULTCODES pRes, PSZ pName)
{
    TRACE_NATIVE("DosExecPgm('%s', %u, %u, '%s', '%s', %p, '%s')", pObjname, (uint) cbObjname, (uint) execFlag, pArg, pEnv, pRes, pName);
    return DosExecPgm_implementation(pObjname, cbObjname, execFlag, pArg, pEnv, pRes, pName);
} // DosExecPgm

APIRET DosResetEventSem(HEV hev, PULONG pulPostCt)
{
    TRACE_NATIVE("DosResetEventSem(%u, %p)", (uint) hev, pulPostCt);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    APIRET retval = NO_ERROR;
    ULONG count = 0;
    EventSem *sem = (EventSem *) hev;

    pthread_mutex_lock(&sem->mutex);
    count = sem->posted;
    if (!sem->posted)
        retval = ERROR_ALREADY_RESET;
    else
        sem->posted = 0;
    pthread_mutex_unlock(&sem->mutex);

    if (pulPostCt)
        *pulPostCt = count;

    return retval;
} // DosResetEventSem

APIRET DosPostEventSem(HEV hev)
{
    TRACE_NATIVE("DosPostEventSem(%u)", (uint) hev);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    APIRET retval = NO_ERROR;
    EventSem *sem = (EventSem *) hev;

    pthread_mutex_lock(&sem->mutex);
    if (sem->posted)
        retval = ERROR_ALREADY_POSTED;
    else {
        sem->posted = 1;
        pthread_cond_broadcast(&sem->condition);
    } // else
    pthread_mutex_unlock(&sem->mutex);

    return retval;
} // DosPostEventSem

APIRET DosWaitEventSem(HEV hev, ULONG ulTimeout)
{
    TRACE_NATIVE("DosWaitEventSem(%u, %u)", (uint) hev, (uint) ulTimeout);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    EventSem *sem = (EventSem *) hev;
    int posted = 0;

    if (ulTimeout == SEM_IMMEDIATE_RETURN) {
        pthread_mutex_lock(&sem->mutex);
        posted = sem->posted;
        pthread_mutex_unlock(&sem->mutex);
        return posted ? NO_ERROR : ERROR_TIMEOUT;
    } else if (ulTimeout == SEM_INDEFINITE_WAIT) {
        pthread_mutex_lock(&sem->mutex);
        sem->waiting++;
        while (1) {
            posted = sem->posted;
            if (!posted) {
                pthread_cond_wait(&sem->condition, &sem->mutex);
                posted = sem->posted;
            } // if

            if (posted) {
                sem->waiting--;
                pthread_mutex_unlock(&sem->mutex);
                return NO_ERROR;
            } // if

            // spurious wakeup? Block again.
        } // while
    } else {  // wait for X milliseconds.
        struct timespec waittime;
        timespecNowPlusMilliseconds(&waittime, ulTimeout);

        pthread_mutex_lock(&sem->mutex);
        sem->waiting++;
        while (1) {
            int rc = 0;
            posted = sem->posted;
            if (!posted) {
                rc = pthread_cond_timedwait(&sem->condition, &sem->mutex, &waittime);
                posted = sem->posted;
            } // if

            if ((posted) || (rc == ETIMEDOUT)) {
                sem->waiting--;
                pthread_mutex_unlock(&sem->mutex);
                if (posted)
                    return NO_ERROR;  // we're good.
                else
                    return ERROR_TIMEOUT;
            } // if

            // otherwise, try again...spurious wakeup?
        } // while
    } // else

    return NO_ERROR;
} // DosWaitEventSem

APIRET DosQueryEventSem(HEV hev, PULONG pulPostCt)
{
    TRACE_NATIVE("DosQueryEventSem(%u, %p)", (uint) hev, pulPostCt);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    EventSem *sem = (EventSem *) hev;
    pthread_mutex_lock(&sem->mutex);
    const ULONG count = (ULONG) sem->posted;
    pthread_mutex_unlock(&sem->mutex);

    if (pulPostCt)
        *pulPostCt = count;

    return NO_ERROR;
} // DosQueryEventSem

APIRET DosCloseEventSem(HEV hev)
{
    TRACE_NATIVE("DosCloseEventSem(%u)", (uint) hev);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    EventSem *sem = (EventSem *) hev;

    pthread_mutex_lock(&sem->mutex);
    const int waiting = sem->waiting;
    pthread_mutex_unlock(&sem->mutex);

    if (waiting > 0)
        return ERROR_SEM_BUSY;

    pthread_mutex_destroy(&sem->mutex);
    pthread_cond_destroy(&sem->condition);
    free(sem);

    return NO_ERROR;
} // DosCloseEventSem

APIRET DosFreeMem(PVOID pb)
{
    TRACE_NATIVE("DosFreeMem(%p)", pb);

    // !!! FIXME: this API is actually much more complicated than this.
    free(pb);
    return NO_ERROR;
} // DosFreeMem

APIRET DosWaitChild(ULONG action, ULONG option, PRESULTCODES pres, PPID ppid, PID pid)
{
    TRACE_NATIVE("DosWaitChild(%u, %u, %p, %p, %u)", (uint) action, (uint) option, pres, ppid, (uint) pid);

    if (action == DCWA_PROCESSTREE) {
        // we need a wait to wait on the whole tree (including trees of a
        // terminated child we already waited on, which the API reference
        // lists as an example.
        FIXME("implement me");
        return ERROR_INVALID_PARAMETER;
    } // if

    pid_t unixpid;
    if (action == DCWA_PROCESS)
        unixpid = (pid == 0) ? (pid_t) -1 : (pid_t) pid;

    int waitoptions;
    if (option == DCWW_WAIT) {
        waitoptions = 0;
    } else if (option == DCWW_NOWAIT) {
        waitoptions = WNOHANG;
    } else {
        return ERROR_INVALID_PARAMETER;
    } // else

    int status = 0;
    pid_t rc;
    while (1) {
        rc = waitpid(unixpid, &status, waitoptions);
        if (rc == 0) {  // WNOHANG and no children were done.
            assert(option == DCWW_NOWAIT);
            return ERROR_CHILD_NOT_COMPLETE;
        } else if (rc == -1) {
            if (errno == EINTR)
                continue;  // try again, I guess.
            else if (errno == ECHILD)
                return (pid == 0) ? ERROR_WAIT_NO_CHILDREN : ERROR_INVALID_PROCID;
        } else {
            break;  // we're good.
        } // else

        assert(!"Shouldn't hit this code");
        return ERROR_CHILD_NOT_COMPLETE;
    } // while

    if (ppid)
        *ppid = (PID) rc;

    setProcessResultCode(pres, status);

    return NO_ERROR;
} // DosWaitChild

APIRET DosWaitThread(PTID ptid, ULONG option)
{
    TRACE_NATIVE("DosWaitThread(%p, %u)", ptid, (uint) option);

    Thread *thread = (Thread *) *ptid;
    void *exitcode = NULL;

    if ((option != DCWW_WAIT) && (option != DCWW_NOWAIT))
        return ERROR_INVALID_PARAMETER;

    if (thread == NULL) {   // check our dead thread list for anything.
        while (1) {
            grabLock(&GMutexDosCalls);
            if (GDeadThreads) {
                thread = GDeadThreads;
                GDeadThreads = thread->next;
                if (GDeadThreads)
                    GDeadThreads->prev = NULL;
                assert(!thread->prev);
            } // if
            ungrabLock(&GMutexDosCalls);

            if (thread) {
                pthread_join(thread->thread, &exitcode);  // reap it.
                *ptid = (TID) thread;
                free(thread);
                return NO_ERROR;
            } // if

            if (option == DCWW_NOWAIT)
                return ERROR_THREAD_NOT_TERMINATED;

            usleep(10000);  // sleep then try again, for DCWW_WAIT.
        } // while

        assert(!"shouldn't hit this code");
        return ERROR_THREAD_NOT_TERMINATED; // !!! FIXME
    } // if

    // codepath if we were waiting for a specific thread.
    int rc;
    if (option == DCWW_NOWAIT) {
        rc = pthread_tryjoin_np(thread->thread, &exitcode);
    } else {  //if (option == DCWW_WAIT)
        rc = pthread_join(thread->thread, &exitcode);
    } // else

    if (rc == EBUSY)
        return ERROR_THREAD_NOT_TERMINATED;
    else if (rc != 0)
        return ERROR_INVALID_THREADID; // oh well.

    grabLock(&GMutexDosCalls);
    if (thread->prev)
        thread->prev->next = thread->next;
    else
        GDeadThreads = thread->next;

    if (thread->next)
        thread->next->prev = thread->prev;
    ungrabLock(&GMutexDosCalls);

    free(thread);
    return NO_ERROR;
} // DosWaitThread

APIRET DosSleep(ULONG msec)
{
    TRACE_NATIVE("DosSleep(%u)", (uint) msec);

    if (msec == 0)
        sched_yield();
    else
        usleep(msec * 1000);
    return NO_ERROR;
} // DosSleep

APIRET DosSubFreeMem(PVOID pbBase, PVOID pb, ULONG cb)
{
    TRACE_NATIVE("DosSubFreeMem(%p, %p, %u)", pbBase, pb, (uint) cb);
    free(pb);  // !!! FIXME: obviously wrong
    return NO_ERROR;
} // DosSubAllocMem

static APIRET DosDelete_implementation(PSZ pszFile)
{
    APIRET err;
    if (!pszFile)
        return ERROR_INVALID_PARAMETER;
    char *unixpath = makeUnixPath(pszFile, &err);
    if (!unixpath)
        return err;

    if (unlink(unixpath) == -1) {
        switch (errno) {
            case EACCES: return ERROR_ACCESS_DENIED;
            case EBUSY: return ERROR_ACCESS_DENIED;
            case EISDIR: return ERROR_ACCESS_DENIED;
            case ENAMETOOLONG: return ERROR_FILENAME_EXCED_RANGE;
            case ENOENT: return ERROR_FILE_NOT_FOUND;  // !!! FIXME: could be PATH_NOT_FOUND too, depending on circumstances.
            case ENOTDIR: return ERROR_PATH_NOT_FOUND;
            case EPERM: return ERROR_ACCESS_DENIED;
            case EROFS: return ERROR_ACCESS_DENIED;
            case ETXTBSY: return ERROR_ACCESS_DENIED;
            default: return ERROR_INVALID_PARAMETER;  // !!! FIXME: debug logging about missin errno case.
        } // switch
        __builtin_unreachable();
    } // if

    return NO_ERROR;
} // DosDelete_implementation

APIRET DosDelete(PSZ pszFile)
{
    TRACE_NATIVE("DosDelete('%s')", pszFile);
    return DosDelete_implementation(pszFile);
} // DosDelete


APIRET DosQueryCurrentDir(ULONG disknum, PBYTE pBuf, PULONG pcbBuf)
{
    TRACE_NATIVE("DosQueryCurrentDir(%u, %p, %p)", (uint) disknum, pBuf, pcbBuf);

    if ((disknum != 0) && (disknum != 3))  // C:  (or "current")
        return ERROR_INVALID_DRIVE;

    // !!! FIXME: this is lazy.
    char *cwd = getcwd(NULL, 0);
    if (!cwd)
        return ERROR_NOT_ENOUGH_MEMORY;  // this doesn't ever return this error on OS/2.

    const size_t len = strlen(cwd);
    if ((len + 1) > *pcbBuf) {
        *pcbBuf = len + 1;
        free(cwd);
        return ERROR_BUFFER_OVERFLOW;
    } // if

    char *ptr = (char *) pBuf;

    char *src = cwd;
    while (*src == '/')
        src++;  // skip initial '/' char.

    while (*src) {
        const char ch = *src;
        *(ptr++) = ((ch == '/') ? '\\' : ch);
        src++;
    } // while

    *ptr = '\0';

    free(cwd);

    return NO_ERROR;
} // DosQueryCurrentDir

APIRET DosSetPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf, ULONG flOptions)
{
    TRACE_NATIVE("DosSetPathInfo('%s', %u, %p, %u, %u)", pszPathName, (uint) ulInfoLevel, pInfoBuf, (uint) cbInfoBuf, (uint) flOptions);
    FIXME("write me");
    return NO_ERROR;
} // DosSetPathInfo

APIRET DosQueryModuleHandle(PSZ pszModname, PHMODULE phmod)
{
    TRACE_NATIVE("DosQueryModuleHandle('%s', %p)", pszModname, phmod);
    grabLock(&GMutexDosCalls);
    LxModule *lxmod;
    for (lxmod = GLoaderState.loaded_modules; lxmod; lxmod = lxmod->next) {
        if (strcasecmp(lxmod->name, pszModname) == 0)
            break;
    } // for
    ungrabLock(&GMutexDosCalls);

    if (phmod)
        *phmod = (HMODULE) lxmod;

    return lxmod ? NO_ERROR : ERROR_INVALID_NAME;
} // DosQueryModuleHandle

APIRET DosQueryProcAddr(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN* ppfn)
{
    TRACE_NATIVE("DosQueryProcAddr(%u, %u, '%s', %p)", (uint) hmod, (uint) ordinal, pszName, ppfn);

    const LxModule *lxmod = (LxModule *) hmod;
    const LxExport *exports = lxmod->exports;
    const uint32 num_exports = lxmod->num_exports;

    if (ordinal > 65533) {
        return ERROR_INVALID_ORDINAL;  // according to the docs.
    } else if (ordinal != 0) {
        for (uint32 i = 0; i < num_exports; i++, exports++) {
            if (exports->ordinal == ordinal) {
                if (ppfn)
                    *ppfn = (PFN) exports->addr;
                return NO_ERROR;
            } // if
        } // for
        return ERROR_INVALID_ORDINAL;
    } else {
        // the docs explicitly say that you can't do name lookups in
        //  the DOSCALLS module, but whatever, I allow it.
        for (uint32 i = 0; i < num_exports; i++, exports++) {
            if (exports->name && (strcmp(exports->name, pszName) == 0)) {
                if (ppfn)
                    *ppfn = (PFN) exports->addr;
                return NO_ERROR;
            } // if
        } // for
        return ERROR_INVALID_NAME;
    } // else

    __builtin_unreachable();
} // DosQueryProcAddr

APIRET DosQueryCp(ULONG cb, PULONG arCP, PULONG pcCP)
{
    TRACE_NATIVE("DosQueryCp(%u, %p, %p)", (uint) cb, arCP, pcCP);

    FIXME("This is mostly a stub");

    if (cb < sizeof (*arCP) * 2)
        return ERROR_CPLIST_TOO_SMALL;

    // 437 == United States codepage.
    arCP[0] = 437;  // current process codepage
    arCP[1] = 437;  // prepared system codepage

    if (pcCP)
        *pcCP = sizeof (*arCP) * 2;

    return NO_ERROR;
} // DosQueryCp

APIRET DosOpenL(PSZ pszFileName, PHFILE pHf, PULONG pulAction, LONGLONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosOpenL('%s', %p, %p, %llu, %u, %u, %u, %p)", pszFileName, pHf, pulAction, (unsigned long long) cbFile, (uint) ulAttribute, (uint) fsOpenFlags, (uint) fsOpenMode, peaop2);
    return doDosOpen(pszFileName, pHf, pulAction, cbFile, ulAttribute, fsOpenFlags, fsOpenMode, peaop2);
} // DosOpenL

static inline ULONG findNextSizeNeeded(const ULONG ulInfoLevel, const ULONG pcFileNames)
{
    switch (ulInfoLevel) {
        case -1: return (sizeof (FILEFINDBUF) - CCHMAXPATHCOMP) * pcFileNames;
        case FIL_STANDARD: return (sizeof (FILEFINDBUF3) - CCHMAXPATHCOMP) * pcFileNames;
        case FIL_QUERYEASIZE: return (sizeof (FILEFINDBUF4) - CCHMAXPATHCOMP) * pcFileNames;
        case FIL_QUERYEASFROMLIST: FIXME("write me"); return 0xFFFFFFFF;
        default: assert(!"Shouldn't hit this."); return 0xFFFFFFFF;
    } // switch
    __builtin_unreachable();
} // findNextSizeNeeded;

static int wildcardMatch(const char *str, const char *pattern)
{
    // !!! FIXME: the way OS/2 matches files is sort of complex, and I'm not sure this is 100% right.
    int ext = 0;
    while (1) {
        const char pat = *(pattern++);
        if (pat == '\0') {
            return (*str == '\0') ? 1 : 0;
        } else if (pat == '?') {
            if (*str)  // also matches end of string, but don't advance.
                str++;
        } else if ((pat == '.') && (!ext)) {
            ext = 1;
            if (*str == '.')
                str++;
            else if (*str != '\0')
                return 0;
        } else if (pat == '*') {
            const char next = *pattern;
            while (*str && (*str != next))
                str++;
        } else {
            const char ch = *str;
            const char a = ((ch >= 'A') && (ch <= 'Z')) ? (ch - ('A' - 'a')) : ch;
            const char b = ((pat >= 'A') && (pat <= 'Z')) ? (pat - ('A' - 'a')) : pat;
            if (a == b)
                str++;
            else
                return 0;
        } // else
    } // while

    __builtin_unreachable();
} // wildcardMatch

static int findNextOne(DirFinder *dirf, PVOID *ppfindbuf, PULONG pcbBuf, int *err)
{
    const ULONG attr = dirf->attr;
    const ULONG musthave = (attr & 0xFF00) >> 8;
    const ULONG mayhave = (attr & 0x00FF);

    // these attributes mean nothing on Unix, so if they are Must Haves, return nothing.
    if (musthave & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED))
        return 0;

    struct dirent *dent;
    const int fd = dirfd(dirf->dirp);
    const char *pattern = dirf->pattern;

    while ((dent = readdir(dirf->dirp)) != NULL) {
        const char *name = dent->d_name;
        // !!! FIXME: OS/2 doesn't enumerate ".." for the drive root!
        //if (strcmp(name, "..") == 0) { if drive_root continue; }

        const size_t namelen = strlen(name);
        if (namelen >= CCHMAXPATHCOMP) continue;
        if (namelen + findNextSizeNeeded(dirf->level, 1) >= *pcbBuf) continue; // FIXME??

        if (!wildcardMatch(name, pattern)) continue;

        // we stat(), not lstat(), because OS/2 doesn't understand symlinks,
        //  so we pretend the real file/dir is there. This could cause
        //  problems with broken links and infinite loops, though.
        //  For lstat-like behaviour, if we change our minds, the last arg
        //  of fstatat() needs the AT_SYMLINK_NOFOLLOW flag.
        struct stat statbuf;
        if (fstatat(fd, name, &statbuf, 0) == -1) continue;

        const int isfile = S_ISREG(statbuf.st_mode);
        const int isdir = S_ISDIR(statbuf.st_mode);
        if (!isfile && !isdir) continue;  // OS/2 didn't have fifos, devs, sockets, etc.
        assert(isfile != isdir);

        if (isdir) {
            if ((mayhave & FILE_DIRECTORY) == 0) continue;
        } else {
            if (musthave & FILE_DIRECTORY) continue;
        } // else

        const int readonly = ((statbuf.st_mode & S_IWUSR) == 0);  // !!! FIXME: not accurate...?
        if (readonly) {
            if ((mayhave & FILE_READONLY) == 0) continue;
        } else {
            if (musthave & FILE_READONLY) continue;
        } // else


        // okay! Definitely take this one!
        switch (dirf->level) {
            case -1: {
                PFILEFINDBUF st = (PFILEFINDBUF) *ppfindbuf;
                initFileCreationDateTime(&st->fdateCreation, &st->ftimeCreation);
                unixTimeToOs2(statbuf.st_atime, &st->fdateLastAccess, &st->ftimeLastAccess);
                unixTimeToOs2(statbuf.st_mtime, &st->fdateLastWrite, &st->ftimeLastWrite);
                st->cbFile = (ULONG) statbuf.st_size;  // !!! FIXME: > 2gig files?
                st->cbFileAlloc = (ULONG) (statbuf.st_blocks * 512);
                if (isdir) st->attrFile |= FILE_DIRECTORY;
                if (readonly) st->attrFile |= FILE_READONLY;
                st->cchName = (UCHAR) namelen;
                strcpy(st->achName, name);
                return 1;
            } // case

            case FIL_STANDARD: {
                PFILEFINDBUF3 st = (PFILEFINDBUF3) *ppfindbuf;
                memset(st, '\0', sizeof (*st));
                *ppfindbuf = st + 1;
                *pcbBuf -= sizeof (*st);
                st->oNextEntryOffset = sizeof (*st);
                initFileCreationDateTime(&st->fdateCreation, &st->ftimeCreation);
                unixTimeToOs2(statbuf.st_atime, &st->fdateLastAccess, &st->ftimeLastAccess);
                unixTimeToOs2(statbuf.st_mtime, &st->fdateLastWrite, &st->ftimeLastWrite);
                st->cbFile = (ULONG) statbuf.st_size;  // !!! FIXME: > 2gig files?
                st->cbFileAlloc = (ULONG) (statbuf.st_blocks * 512);
                if (isdir) st->attrFile |= FILE_DIRECTORY;
                if (readonly) st->attrFile |= FILE_READONLY;
                st->cchName = (UCHAR) namelen;
                strcpy(st->achName, name);
                return 1;
            } // case

            case FIL_QUERYEASIZE: {
                PFILEFINDBUF4 st = (PFILEFINDBUF4) *ppfindbuf;
                memset(st, '\0', sizeof (*st));
                *ppfindbuf = st + 1;
                *pcbBuf -= sizeof (*st);
                st->oNextEntryOffset = sizeof (*st);
                initFileCreationDateTime(&st->fdateCreation, &st->ftimeCreation);
                unixTimeToOs2(statbuf.st_atime, &st->fdateLastAccess, &st->ftimeLastAccess);
                unixTimeToOs2(statbuf.st_mtime, &st->fdateLastWrite, &st->ftimeLastWrite);
                st->cbFile = (ULONG) statbuf.st_size;  // !!! FIXME: > 2gig files?
                st->cbFileAlloc = (ULONG) (statbuf.st_blocks * 512);
                if (isdir) st->attrFile |= FILE_DIRECTORY;
                if (readonly) st->attrFile |= FILE_READONLY;
                st->cbList = 0;  FIXME("write me: EA support");
                st->cchName = (UCHAR) namelen;
                strcpy(st->achName, name);
                return 1;
            } // case

            case FIL_QUERYEASFROMLIST:
                FIXME("write me");
                break;
        } // switch
    } // while

    return 0;  // found nothing else that matches our needs.
} // findNextOne
    

static APIRET findNext(DirFinder *dirf, PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames)
{
    if (!dirf->dirp) {
        *pcFileNames = 0;
        return ERROR_NO_MORE_FILES;
    } // if

    const ULONG maxents = *pcFileNames;
    ULONG i;
    int err = NO_ERROR;
    memset(pfindbuf, 0, cbBuf);
    for (i = 0; i < maxents; i++) {
        if (!findNextOne(dirf, &pfindbuf, &cbBuf, &err))
            break;
    } // for

    *pcFileNames = i;

    if ((i == 0) && (maxents > 0)) {
        closedir(dirf->dirp);
        dirf->dirp = NULL;
        err = ERROR_NO_MORE_FILES;
    } // if

    return err;
} // findNext

APIRET DosFindFirst(PSZ pszFileSpec, PHDIR phdir, ULONG flAttribute, PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames, ULONG ulInfoLevel)
{
    TRACE_NATIVE("DosFindFirst('%s', %p, %u, %p, %u, %p, %u)", pszFileSpec, phdir, (uint) flAttribute, pfindbuf, (uint) cbBuf, pcFileNames, (uint) ulInfoLevel);

    if (flAttribute & 0xffffc8c8)  // reserved bits that must be zero.
        return ERROR_INVALID_PARAMETER;
    else if (cbBuf > 0xFFFF)   // !!! FIXME: Control Program API Reference says this fails if > 64k, although that was obviously a 16-bit limitation.
        return ERROR_INVALID_PARAMETER;
    else if ((ulInfoLevel != FIL_STANDARD) && (ulInfoLevel != FIL_QUERYEASIZE) && (ulInfoLevel != FIL_QUERYEASFROMLIST) && (ulInfoLevel != -1)) // -1 for 16 call
        return ERROR_INVALID_PARAMETER;
    else if (!pcFileNames)
        return ERROR_INVALID_PARAMETER;
    else if (!pfindbuf && (*pcFileNames != 0))
        return ERROR_INVALID_PARAMETER;
    else if (!phdir)
        return ERROR_INVALID_PARAMETER;
    else if (!*phdir)
        return ERROR_INVALID_HANDLE;
    else if (cbBuf < findNextSizeNeeded(ulInfoLevel, *pcFileNames))
        return ERROR_BUFFER_OVERFLOW;

    if ((ulInfoLevel != FIL_STANDARD) && (ulInfoLevel != -1)) {
        FIXME("implement extended attribute support");
        return ERROR_INVALID_PARAMETER;
    } // if

    if (flAttribute == FILE_NORMAL)
        flAttribute = FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED;  // most of the "may-have" bits.

    APIRET err = NO_ERROR;
    char *path = makeUnixPath(pszFileSpec, &err);
    if (!path)
        return err;

    char *pattern = strrchr(path, '/');
    if (pattern)
        *(pattern++) = '\0';
    else
        pattern = path;

    if (strlen(pattern) >= CCHMAXPATHCOMP) {
        free(path);
        return ERROR_FILENAME_EXCED_RANGE;
    } // if

    DirFinder *dirf = NULL;
    if (*phdir == HDIR_SYSTEM) {
        dirf = &GHDir1;
    } else if (*phdir == HDIR_CREATE) {
        dirf = (DirFinder *) malloc(sizeof (DirFinder));
        if (!dirf) {
            free(path);
            return ERROR_NOT_ENOUGH_MEMORY;
        } // if
        memset(dirf, '\0', sizeof (*dirf));
    } else {
        dirf = (DirFinder *) *phdir;
    } // else

    DIR *dirp = opendir((path == pattern) ? "." : path);
    if (!dirp) {
        const int rc = errno;
        if (*phdir == HDIR_CREATE)
            free(dirf);
        free(path);
        switch (rc) {
            case EACCES: return ERROR_ACCESS_DENIED;
            case EMFILE: return ERROR_NO_MORE_SEARCH_HANDLES;
            case ENFILE: return ERROR_NO_MORE_SEARCH_HANDLES;
            case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
            case ENOENT: return ERROR_PATH_NOT_FOUND;  // !!! FIXME: should this be FILE_NOT_FOUND if the parent dir exists...?
            case ENAMETOOLONG: return ERROR_FILENAME_EXCED_RANGE;
            default: break;
        } // switch
        return ERROR_PATH_NOT_FOUND;  // oh well.
    } // if

    if (dirf->dirp)
        closedir(dirf->dirp);

    dirf->dirp = dirp;
    dirf->attr = flAttribute;
    dirf->level = ulInfoLevel;
    strcpy(dirf->pattern, pattern);
    free(path);

    err = findNext(dirf, pfindbuf, cbBuf, pcFileNames);

    // From the Control Program API reference:
    //  "Any nonzero return code, except ERROR_EAS_DIDNT_FIT, indicates that no handle has been allocated. This includes such nonerror
    //  indicators as ERROR_NO_MORE_FILES."
    if ((err != NO_ERROR) && (err != ERROR_EAS_DIDNT_FIT)) {
        closedir(dirf->dirp);
        dirf->dirp = NULL;
        if (*phdir == HDIR_CREATE)
            free(dirf);
        return err;
    } // if

    if (err == ERROR_NO_MORE_FILES)
        return ERROR_FILE_NOT_FOUND;
    *phdir = (HDIR) dirf;
    return NO_ERROR;
} // DosFindFirst

APIRET DosFindNext(HDIR hDir, PVOID pfindbuf, ULONG cbfindbuf, PULONG pcFilenames)
{
    TRACE_NATIVE("DosFindNext(%u, %p, %u, %p)", (uint) hDir, pfindbuf, (uint) cbfindbuf, pcFilenames);

    if ((hDir == 0) || (hDir == HDIR_SYSTEM) || (hDir == HDIR_CREATE))
        return ERROR_INVALID_HANDLE;
    else if (cbfindbuf > 0xFFFF)   // !!! FIXME: Control Program API Reference says this fails if > 64k, although that was obviously a 16-bit limitation.
        return ERROR_INVALID_PARAMETER;
    else if (!pcFilenames)
        return ERROR_INVALID_PARAMETER;
    else if (!pfindbuf && (*pcFilenames != 0))
        return ERROR_INVALID_PARAMETER;
    else if (cbfindbuf < findNextSizeNeeded(((DirFinder *) hDir)->level, *pcFilenames))
        return ERROR_BUFFER_OVERFLOW;
    return findNext((DirFinder *) hDir, pfindbuf, cbfindbuf, pcFilenames);
} // DosFindNext

APIRET DosFindClose(HDIR hDir)
{
    TRACE_NATIVE("DosFindClose(%u)", (uint) hDir);

    if ((hDir == 0) || (hDir == HDIR_SYSTEM) || (hDir == HDIR_CREATE))
        return ERROR_INVALID_HANDLE;

    DirFinder *dirf = (DirFinder *) hDir;
    if (dirf->dirp)
        closedir(dirf->dirp);

    if (dirf == &GHDir1)
        memset(dirf, '\0', sizeof (*dirf));
    else
        free(dirf);

    return NO_ERROR;
} // DosFindClose

APIRET DosQueryCurrentDisk(PULONG pdisknum, PULONG plogical)
{
    TRACE_NATIVE("DosQueryCurrentDisk(%p, %p)", pdisknum, plogical);

    // we only offer a "C:" drive at the moment.
    if (pdisknum)
        *pdisknum = 3;  // C:
    if (plogical)
        *plogical = (1 << 2);  // just C:
    return NO_ERROR;
} // DosQueryCurrentDisk

APIRET DosDevConfig(PVOID pdevinfo, ULONG item)
{
    TRACE_NATIVE("DosDevConfig(%p, %u)", pdevinfo, (uint) item);

    if (pdevinfo == NULL)
        return ERROR_INVALID_PARAMETER;

    BYTE *info = (BYTE *) pdevinfo;
    switch (item) {
        case DEVINFO_PRINTER: *info = 0; break; // we don't report any printers.
        case DEVINFO_RS232: *info = 0; break;   // we don't report any serial ports.
        case DEVINFO_FLOPPY: *info = 0; break;  // we don't report any floppies.
        case DEVINFO_COPROCESSOR: *info = 1; break;  // We could check for this, but c'mon.
        case DEVINFO_SUBMODEL: *info = 1; break; // OS/2 Warp 4 under Vmware returns this, but I don't know what this means.
        case DEVINFO_MODEL: *info = 252; break;  // OS/2 Warp 4 under Vmware returns this, but I don't know what this means.
        case DEVINFO_ADAPTER: *info = 1; break;  // sure, you have a real display from the last 30+ years.
        default: return ERROR_INVALID_PARAMETER;
    } // switch

    return NO_ERROR;
} // DosDevConfig

APIRET DosLoadModule(PSZ pszName, ULONG cbName, PSZ pszModname, PHMODULE phmod)
{
    TRACE_NATIVE("DosLoadModule(%p, %u, '%s', %p)", pszName, (uint) cbName, pszModname, phmod);

    FIXME("improve this");
    *pszName = 0;

    FIXME("there's no mutex on this global state at the moment!");
    LxModule *lxmod = GLoaderState.loadModule(pszModname);
    if (!lxmod)
        return ERROR_BAD_FORMAT;

    *phmod = (HMODULE) lxmod;
    return NO_ERROR;
} // DosLoadModule


static APIRET resetOneBuffer(const int fd)
{
    if (fsync(fd) == -1) {
        switch (errno) {
            case EBADF: return ERROR_INVALID_HANDLE;
            case EIO: return ERROR_ACCESS_DENIED;
            case EROFS: return ERROR_ACCESS_DENIED;
            case EINVAL: return ERROR_INVALID_HANDLE;
            default: break;
        } // switch
        return ERROR_INVALID_HANDLE;
    } // if

    return NO_ERROR;
} // resetOneBuffer


APIRET DosResetBuffer(HFILE hFile)
{
    TRACE_NATIVE("DosResetBuffer(%u)", (uint) hFile);

    if (hFile == 0xFFFFFFFF) {  // flush all files.
        // !!! FIXME: this is holding the lock the whole time...
        APIRET err = NO_ERROR;
        grabLock(&GMutexDosCalls);
        for (uint32 i = 0; i < MaxHFiles; i++) {
            const int fd = HFiles[hFile].fd;
            if (fd != -1) {
                const APIRET thiserr = resetOneBuffer(fd);
                if (err == NO_ERROR)
                    err = thiserr;
            } // if
        } // for
        ungrabLock(&GMutexDosCalls);
        return err;
    } // if

    // flush just one file.
    const int fd = getHFileUnixDescriptor(hFile);
    if (fd == -1)
        return ERROR_INVALID_HANDLE;
    return resetOneBuffer(fd);
} // DosResetBuffer

APIRET DosQueryAppType(PSZ pszName, PULONG pFlags)
{
    TRACE_NATIVE("DosQueryAppType('%s', %p)", pszName, pFlags);

    if (!pFlags)
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *path = makeUnixPath(pszName, &err);
    if (!path)
        return err;

    const int fd = open(path, O_RDONLY, 0);
    free(path);
    if (fd == -1) {
        switch (errno) {  // !!! FIXME: copy/paste, put this in a function.
            case EACCES: return ERROR_ACCESS_DENIED;
            case EISDIR: return ERROR_ACCESS_DENIED;
            case EMFILE: return ERROR_TOO_MANY_OPEN_FILES;
            case ENFILE: return ERROR_TOO_MANY_OPEN_FILES;
            case ENAMETOOLONG: return ERROR_FILENAME_EXCED_RANGE;
            case ENOSPC: return ERROR_DISK_FULL;
            case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
            case ENOENT: return ERROR_FILE_NOT_FOUND;  // !!! FIXME: could be PATH_NOT_FOUND too, depending on circumstances.
            case ENOTDIR: return ERROR_PATH_NOT_FOUND;
            case EPERM: return ERROR_ACCESS_DENIED;
            case EROFS: return ERROR_ACCESS_DENIED;
            case ETXTBSY: return ERROR_ACCESS_DENIED;
            default: break;
        } // switch
        return ERROR_OPEN_FAILED;  // !!! FIXME: debug logging about missing errno case.
    } // if

    // !!! FIXME: I guess we should parse NE/LE files here too?

    uint32 lxoffset = 0;
    LxHeader lx;
    uint8 mz[2];
    if (read(fd, mz, 2) != 2) goto queryapptype_iofailed;
    if ((mz[0] != 'M') || (mz[1] != 'Z')) goto queryapptype_iofailed;
    if (lseek(fd, 0x3C, SEEK_SET) == -1) goto queryapptype_iofailed;
    if (read(fd, &lxoffset, sizeof (lxoffset)) != sizeof (lxoffset)) goto queryapptype_iofailed;
    if (lseek(fd, lxoffset, SEEK_SET) == -1) goto queryapptype_iofailed;
    if (read(fd, &lx, sizeof (lx)) != sizeof (lx)) goto queryapptype_iofailed;
    close(fd);

    if ((lx.magic_l != 'L') || (lx.magic_x != 'X')) return ERROR_INVALID_EXE_SIGNATURE;
    if (lx.module_flags & 0x2000) return ERROR_EXE_MARKED_INVALID;  // "not loadable" ... I guess this is it?

    const uint32 module_type = lx.module_flags & 0x00038000;

    ULONG flags = 0;

    // !!! FIXME: can you be both compat and incompat?
    if (lx.module_flags & 0x100) flags |= FAPPTYP_NOTWINDOWCOMPAT;
    if (lx.module_flags & 0x200) flags |= FAPPTYP_WINDOWCOMPAT;
    if (lx.module_flags & 0x300) flags |= FAPPTYP_WINDOWAPI;
    // !!! FIXME: FAPPTYP_BOUND = 0x0008
    if (module_type & 0x8000) flags |= FAPPTYP_DLL;
    if (module_type & 0x20000) flags |= FAPPTYP_PHYSDRV;
    if (module_type & 0x28000) flags |= FAPPTYP_VIRTDRV;
    if (module_type & 0x18000) flags |= FAPPTYP_PROTDLL;

    // !!! FIXME: FAPPTYP_DOS = 0x0020
    // !!! FIXME: FAPPTYP_WINDOWSREAL = 0x0200
    // !!! FIXME: FAPPTYP_WINDOWSPROT = 0x0400
    // !!! FIXME: FAPPTYP_WINDOWSPROT31 =0x1000

    flags |= FAPPTYP_32BIT;  // !!! FIXME: currently always true.

    return NO_ERROR;

queryapptype_iofailed:
    close(fd);
    return ERROR_INVALID_EXE_SIGNATURE;
} // DosQueryAppType

static uint32 *initTLSPage(void)
{
    const int pagesize = getpagesize();

    void *addr = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == ((void *) MAP_FAILED))
        return NULL;

    // Fill in the page with a debug info string.
    char *dst = (char *) addr;
    const char *str = "This is the fake OS/2 TLS page. ";
    assert(strlen(str) == 32);
    for (int i = 0; i < 127; i++, dst += 32)
        strcpy(dst, str);
    memcpy(dst, str, 31);
    dst[31] = '\0';

    if (mprotect(addr, pagesize, PROT_NONE) == -1) {
        munmap(addr, pagesize);
        return NULL;
    } // if

    printf("allocated magic OS/2 TLS page at %p\n", addr); fflush(stdout);

    return (uint32 *) addr;
} // initTLSPage


APIRET DosAllocThreadLocalMemory(ULONG cb, PULONG *p)
{
    TRACE_NATIVE("DosAllocThreadLocalMemory(%u, %p)", (uint) cb, p);

    if (cb > 8)  // this is a limitation in OS/2. This whole API is weird.
        return ERROR_INVALID_PARAMETER;
    else if (!p)
        return ERROR_INVALID_PARAMETER;

    APIRET retval = NO_ERROR;
    uint32 mask = 0xFF >> (8 - cb);

    grabLock(&GMutexDosCalls);

    // this is probably expensive to do with the mutex held, but honestly,
    //  is there a lot of thread contention at the point where your app is
    //  allocating TLS?
    if (GLoaderState.tlspage == NULL) {
        if ((GLoaderState.tlspage = initTLSPage()) == NULL) {
            ungrabLock(&GMutexDosCalls);
            return ERROR_NOT_ENOUGH_MEMORY;
        } // if
    } // if

    int i;
    for (i = 0; i < 32; i++, mask <<= 1) {
        if (((GLoaderState.tlsmask ^ mask) & mask) == mask)
            break;
    } // for

    if (i == 32)
        retval = ERROR_NOT_ENOUGH_MEMORY; // there are only 32 slots, couldn't find anything contiguous.
    else {
        assert(GLoaderState.tlsallocs[i] == 0);
        GLoaderState.tlsallocs[i] = (uint8) cb;
        GLoaderState.tlsmask |= mask;
        *p = GLoaderState.tlspage + i;
        printf("allocated %u OS/2 TLS slot%s at %p\n", (uint) cb, (cb == 1) ? "" : "s", *p); fflush(stdout);
    } // else
        
    ungrabLock(&GMutexDosCalls);

    return retval;
} // DosAllocThreadLocalMemory

APIRET DosFreeThreadLocalMemory(ULONG *p)
{
    TRACE_NATIVE("DosFreeThreadLocalMemory(%p)", p);

    APIRET retval = ERROR_INVALID_PARAMETER;

    grabLock(&GMutexDosCalls);

    if (GLoaderState.tlspage) {
        const uint32 slot = (uint32) (p - GLoaderState.tlspage);
        if (slot < 32) {
            const uint8 slots = GLoaderState.tlsallocs[slot];
            if (slots > 0) {
                const uint32 mask = ((uint32) (0xFF >> (8 - slots))) << slot;
                assert((GLoaderState.tlsmask & mask) == mask);
                GLoaderState.tlsmask &= ~mask;
                printf("freed %u OS/2 TLS slot%s at %p\n", (uint) slots, (slots == 1) ? "" : "s", p); fflush(stdout);
                GLoaderState.tlsallocs[slot] = 0;
                retval = NO_ERROR;
            } // if
        } // if
    } // if

    ungrabLock(&GMutexDosCalls);

    return retval;
} // DosFreeThreadLocalMemory

APIRET DosQueryFHState(HFILE hFile, PULONG pMode)
{
    TRACE_NATIVE("DosQueryFHState(%u, %p)", (uint) hFile, pMode);

    APIRET retval = NO_ERROR;
    ULONG tmp = 0;
    if (!pMode)
        pMode = &tmp;

    grabLock(&GMutexDosCalls);
    if ((hFile < MaxHFiles) && (HFiles[hFile].fd != -1))
        *pMode = HFiles[hFile].flags;
    else
        retval = ERROR_INVALID_HANDLE;
    ungrabLock(&GMutexDosCalls);

    return retval;
} // DosQueryFHState

APIRET DosQueryHeaderInfo(HMODULE hmod, ULONG ulIndex, PVOID pvBuffer, ULONG cbBuffer, ULONG ulSubFunction)
{
    TRACE_NATIVE("DosQueryHeaderInfo(%u, %u, %p, %u, %u)", (uint) hmod, (uint) ulIndex, pvBuffer, (uint) cbBuffer, (uint) ulSubFunction);

    switch (ulSubFunction) {
        //case QHINF_EXEINFO:
        //case QHINF_READRSRCTBL:
        //case QHINF_READFILE:

        case QHINF_LIBPATHLENGTH:
            if (cbBuffer < sizeof (ULONG))
                return ERROR_BUFFER_OVERFLOW;
            *((ULONG *) pvBuffer) = GLoaderState.libpathlen;
            return NO_ERROR;

        case QHINF_LIBPATH:
            if (cbBuffer < GLoaderState.libpathlen)
                return ERROR_BUFFER_OVERFLOW;
            strcpy((char *) pvBuffer, GLoaderState.libpath);
            return NO_ERROR;

        //case QHINF_FIXENTRY:
        //case QHINF_STE:
        //case QHINF_MAPSEL:

        default: FIXME("I don't know what this query wants"); break;
    } // switch

    return ERROR_INVALID_PARAMETER;
} // DosQueryHeaderInfo

APIRET DosQueryExtLIBPATH(PSZ pszExtLIBPATH, ULONG flags)
{
    TRACE_NATIVE("DosQueryExtLIBPATH('%s', %u)", pszExtLIBPATH, (uint) flags);

    // !!! FIXME: this is mostly a stub for now.

    if ((flags != BEGIN_LIBPATH) && (flags != END_LIBPATH))
        return ERROR_INVALID_PARAMETER;

    if (pszExtLIBPATH)
        *pszExtLIBPATH = '\0';

    return NO_ERROR;
} // DosQueryExtLIBPATH

APIRET DosSetMaxFH(ULONG cFH)
{
    grabLock(&GMutexDosCalls);

    if (cFH < MaxHFiles) {
        ungrabLock(&GMutexDosCalls);
        return ERROR_INVALID_PARAMETER;  // strictly speaking, we could shrink, but I'm not doing it.
    } // if

    if (cFH == MaxHFiles) {
        ungrabLock(&GMutexDosCalls);
        return NO_ERROR;
    } // if

    HFileInfo *info = (HFileInfo *) realloc(HFiles, sizeof (HFileInfo) * (cFH));
    if (info != NULL) {
        HFiles = info;
        info += MaxHFiles;
        for (ULONG i = MaxHFiles; i < cFH; i++, info++) {
            info->fd = -1;
            info->type = 0;
            info->attr = 0;
            info->flags = 0;
        } // for
        MaxHFiles = cFH;
    } // if

    ungrabLock(&GMutexDosCalls);

    return NO_ERROR;
} // DosSetMaxFH

APIRET DosQuerySysState(ULONG func, ULONG arg1, ULONG pid, ULONG _res_, PVOID buf, ULONG bufsz)
{
    TRACE_NATIVE("DosQuerySysState(%u, %u, %u, %u, %p, %u)", (uint) func, (uint) arg1, (uint) pid, (uint) _res_, buf, (uint) bufsz);
    FIXME("implement me");
    return ERROR_INVALID_PARAMETER;
} // DosQuerySysState

APIRET DosR3ExitAddr(void)
{
    TRACE_NATIVE("DosR3ExitAddr()");
    FIXME("I have no idea what this is");  // but...JAVA USES IT.
    return ERROR_INVALID_PARAMETER;
} // DosR3ExitAddr

APIRET DosQueryThreadContext(TID tid, ULONG level, PCONTEXTRECORD pcxt)
{
    TRACE_NATIVE("DosQueryThreadContext(%u, %u, %p)", (uint) tid, (uint) level, pcxt);
    FIXME("Need to be able to suspend threads first");
    return ERROR_INVALID_PARAMETER;
} // DosQueryThreadContext

#if !LX_LEGACY
ULONG DosSelToFlat(VOID) { return 0; }
#else
ULONG _DosSelToFlat(void *ptr)
{
    TRACE_NATIVE("DosSelToFlat(%p)", ptr);
    return (ULONG) GLoaderState.convert1616to32((uint32) ptr);
} // _DosSelToFlat

// DosSelToFlat() passes its argument in %eax, so a little asm to bridge that...
__asm__ (
    // !!! FIXME: this needs to protect more registers.
    ".globl DosSelToFlat  \n\t"
    ".type	DosSelToFlat, @function \n\t"
    "DosSelToFlat:  \n\t"
    "    pushl %eax  \n\t"
    "    call _DosSelToFlat  \n\t"
    "    addl $4, %esp  \n\t"
    "    ret  \n\t"
	".size	_DosSelToFlat, .-_DosSelToFlat  \n\t"
);
#endif

static inline int trySpinLock(int *lock)
{
    return (__sync_lock_test_and_set(lock, 1) == 0);
} // trySpinLock

APIRET16 DosSemRequest(PHSEM16 sem, LONG ms)
{
    TRACE_NATIVE("DosSemRequest(%p, %u)", sem, (uint) ms);
    if (ms == 0) {
        return !trySpinLock(sem) ? ERROR_TIMEOUT : NO_ERROR;
    } else if (ms < 0) {
        while (!trySpinLock(sem))
            usleep(1000);
        return NO_ERROR;
    }

    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    const uint64 end = (((uint64) ts.tv_sec) * 1000) + (((uint64) ts.tv_nsec) / 1000000) + ms;

    while (1) {
        if (trySpinLock(sem))
            return NO_ERROR;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        const uint64 now = (((uint64) ts.tv_sec) * 1000) + (((uint64) ts.tv_nsec) / 1000000);
        if (now >= end)
            return ERROR_TIMEOUT;
        usleep(1000);
    } // while

    return NO_ERROR;
} // DosSemRequest

APIRET16 DosSemClear(PHSEM16 sem)
{
    TRACE_NATIVE("DosSemClear(%p)", sem);
    __sync_lock_release(sem);
    return NO_ERROR;
} // DosSemClear

APIRET16 DosSemWait(PHSEM16 sem, LONG ms)
{
    TRACE_NATIVE("DosSemWait(%p, %u)", sem, (uint) ms);
    if (ms == 0) {
        return __sync_bool_compare_and_swap(sem, 0, 1) ? NO_ERROR : ERROR_TIMEOUT;
    } else if (ms < 0) {
        while (!__sync_bool_compare_and_swap(sem, 0, 1))
            usleep(1000);
        return NO_ERROR;
    }

    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    const uint64 end = (((uint64) ts.tv_sec) * 1000) + (((uint64) ts.tv_nsec) / 1000000) + ms;

    while (1) {
        if (__sync_bool_compare_and_swap(sem, 0, 1))
            return NO_ERROR;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        const uint64 now = (((uint64) ts.tv_sec) * 1000) + (((uint64) ts.tv_nsec) / 1000000);
        if (now >= end)
            return ERROR_TIMEOUT;
        usleep(1000);
    } // while

    return NO_ERROR;
} // DosSemWait

APIRET16 DosSemSet(PHSEM16 sem)
{
    TRACE_NATIVE("DosSemSet(%p)", sem);
    __sync_bool_compare_and_swap(sem, 1, 0);
    return NO_ERROR;
} // DosSemSet

APIRET DosCloseMutexSem(HMTX hmtx)
{
    TRACE_NATIVE("DosCloseMutexSem(%u)", (uint) hmtx);

    if (hmtx) {
        pthread_mutex_t *mutex = (pthread_mutex_t *) hmtx;
        const int rc = pthread_mutex_destroy(mutex);
        if (rc == EBUSY) {
            return ERROR_SEM_BUSY;
        } else if (rc == 0) {
            free(mutex);
            return NO_ERROR;
        } // else if
    } // if

    return ERROR_INVALID_HANDLE;
} // DosCloseMutexSem

APIRET DosSetProcessCp(ULONG cp)
{
    TRACE_NATIVE("DosSetProcessCp(%u)", (unsigned int) cp);
    if (cp != 437) {  // United States
        FIXME("non-US codepages unsupported at the moment");
        return ERROR_INVALID_CODE_PAGE;
    }
    return NO_ERROR;
} // DosSetProcessCp

APIRET DosQueryFSAttach(PSZ pszDeviceName, ULONG ulOrdinal, ULONG ulFSAInfoLevel, PFSQBUFFER2 pfsqb, PULONG pcbBuffLength)
{
    TRACE_NATIVE("DosQueryFSAttach('%s', %u, %u, %p, %p)", pszDeviceName, (unsigned int) ulOrdinal, (unsigned int) ulFSAInfoLevel, pfsqb, pcbBuffLength);
    return ERROR_INVALID_FUNCTION;
} // DosQueryFSAttach

APIRET DosSetFileSize(HFILE h, ULONG len)
{
    TRACE_NATIVE("DosSetFileSize(%u, %u)", (unsigned int) h, (unsigned int) len);
    const int fd = getHFileUnixDescriptor(h);
    if (fd == -1)
        return ERROR_INVALID_HANDLE;

    if (ftruncate(fd, len) == -1) {
        FIXME("Set a better error based on errno");
        return ERROR_DISK_FULL;
    }
    return NO_ERROR;
} // DosSetFileSize

static APIRET DosDupHandle_implementation(HFILE hFile, PHFILE pHfile)
{
    if (*pHfile == -1) {
        HFileInfo *info = HFiles;
        uint32 i;

        if (!grabLock(&GMutexDosCalls))
            return ERROR_SYS_INTERNAL;

        for (i = 0; i < MaxHFiles; i++, info++) {
            if (info->fd == -1) {  // available?
                    info->fd = -2;
                    break;
            } // if
        } // for

        ungrabLock(&GMutexDosCalls);

        if (i == MaxHFiles) {
            return ERROR_TOO_MANY_OPEN_FILES;
        }
        memcpy(&HFiles[i], &HFiles[hFile], sizeof(HFileInfo));
        HFiles[i].fd = dup(HFiles[hFile].fd);
        *pHfile = i;
    } else {
        if (HFiles[*pHfile].fd != -1)
            return ERROR_INVALID_TARGET_HANDLE;
        memcpy(&HFiles[*pHfile], &HFiles[hFile], sizeof(HFileInfo));
        HFiles[*pHfile].fd = dup(HFiles[hFile].fd);
    }
    return NO_ERROR;
} // DosDupHandle_implementation

APIRET DosDupHandle(HFILE hFile, PHFILE pHfile)
{
    TRACE_NATIVE("DosDupHandle(%u, %p)", hFile, pHfile);
    return DosDupHandle_implementation(hFile, pHfile);
} // DosDupHandle

APIRET16 Dos16GetVersion(PUSHORT pver)
{
    TRACE_NATIVE("Dos16GetVersion(%p)", pver);
    uint8 *bytes = (uint8 *) pver;
    FIXME("better version here?");
    bytes[0] = 2;
    bytes[1] = 4;
    return NO_ERROR;
} // Dos16GetVersion

APIRET16 Dos16GetMachineMode(PBYTE pmode)
{
    TRACE_NATIVE("Dos16GetMachineMode(%p)", pmode);
    *pmode = 1;  // 1==OS/2 mode (as opposed to zero for DOS mode).
    return NO_ERROR;
} // Dos16GetMachineMode

APIRET16 Dos16GetHugeShift(PUSHORT pcount)
{
    TRACE_NATIVE("Dos16GetHugeShift(%p)", pcount);
    *pcount = 0;  FIXME("maybe?");
    return NO_ERROR;
} // Dos16GetHugeShift

APIRET16 Dos16GetPID(PPIDINFO16 ppidinfo)
{
    TRACE_NATIVE("Dos16GetPID(%p)", ppidinfo);
    PTIB2 tib2 = (PTIB2) LX_GETTIB2();

    // note that these happen to fit Linux default PID ranges in 2018, which
    //  tend to be 32768, but can be much higher by writing to
    //  /proc/sys/kernel/pid_max
    assert(GLoaderState.pib.pib_ulpid <= 0xFFFF);
    assert(GLoaderState.pib.pib_ulppid <= 0xFFFF);
    assert(tib2->tib2_ultid <= 0xFFFF);

    ppidinfo->pid = (USHORT) GLoaderState.pib.pib_ulpid;
    ppidinfo->tid = (USHORT) tib2->tib2_ultid;
    ppidinfo->pidParent = (USHORT) GLoaderState.pib.pib_ulppid;
    return NO_ERROR;
} // Dos16GetPID

VOID Dos16Exit(USHORT action, USHORT exitcode)
{
    TRACE_NATIVE("Dos16Exit(%u, %u)", (uint) action, (uint) exitcode);
    DosExit_implementation(action, exitcode);
} // Dos16Exit

APIRET16 Dos16QHandType(USHORT handle, PUSHORT ptype, PUSHORT pflags)
{
    TRACE_NATIVE("Dos16QHandType(%u, %p, %p)", (uint) handle, ptype, pflags);
    ULONG ultype, ulattr;
    const APIRET rc = DosQueryHType_implementation((HFILE) handle, &ultype, &ulattr);
    if (rc == NO_ERROR) {
        if (ptype) *ptype = (USHORT) ultype;
        if (pflags) *pflags = (USHORT) ulattr;
    } // if
    return (APIRET16) rc;
} // Dos16QHandType

APIRET16 Dos16AllocSeg(USHORT size, PUSHORT psel, USHORT flags)
{
    TRACE_NATIVE("Dos16AllocSeg(%u, %p, %u)", (uint) size, psel, (uint) flags);
    if (psel == NULL) {
        return ERROR_INVALID_PARAMETER;
    } else if ((flags & 0xFFF0) != 0) {  // these bits are reserved and must be zero.
        return ERROR_INVALID_PARAMETER;
    }

    // we don't care about the size, we just allocate a whole 64k segment. Oh well.
    uint16 sel = 0xFFFF;
    //void *addr = GLoaderState.allocSegment(&sel, 1);  // we mark these as code in case they want to do self-modifying magic, etc.
    void *addr = GLoaderState.allocSegment(&sel, 0);
    if (!addr) {
        assert(sel == 0xFFFF);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    assert(sel != 0xFFFF);

    if (flags & (1 << 0)) {
        FIXME("segment is meant to be shareable through Dos16GiveSeg");
    }

    if (flags & (1 << 1)) {
        FIXME("segment is meant to be shareable through Dos16GetSeg");
    }

    // documentation says this is a selector (specifically: a segment only on
    //  DOS in the family mode API), but it looks like OS/2 wants a segment too.
    //*psel = (USHORT) sel;
    *psel = (USHORT) ((sel << 3) | 7);
    return NO_ERROR;
} // Dos16AllocSeg

APIRET16 Dos16ReallocSeg(USHORT size, USHORT sel)
{
    TRACE_NATIVE("Dos16ReallocSeg(%u, %u)", (uint) size, (uint) sel);

    // we don't ever discard segments, and we always allocate the full 64k,
    //  so we don't have to worry about resizing an existing segment.
    //  But there are sharing concerns, etc.
    FIXME("this implementation is severely lacking");

    // (we should probably make sure the selector is valid...)
    return NO_ERROR;
} // Dos16ReallocSeg

APIRET16 Dos16FreeSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16FreeSeg(%u)", (uint) sel);
    FIXME("this needs to deal with reference counting and shared segments");
    // documentation says this is a selector (specifically: a segment only on
    //  DOS in the family mode API), but it looks like OS/2 wants a segment too.
    GLoaderState.freeSegment(sel >> 3);
    return NO_ERROR;
} // Dos16FreeSeg

APIRET16 Dos16GetCp(USHORT cb, PUSHORT arCP, PUSHORT pcCP)
{
    TRACE_NATIVE("Dos16GetCp(%u, %p, %p)", (uint) cb, arCP, pcCP);

    FIXME("This is mostly a stub");
    FIXME("...and mostly copy/paste from DosQueryCp()...");

    if (cb < sizeof (*arCP) * 2)
        return ERROR_CPLIST_TOO_SMALL;

    // 437 == United States codepage.
    arCP[0] = 437;  // current process codepage
    arCP[1] = 437;  // prepared system codepage

    if (pcCP)
        *pcCP = sizeof (*arCP) * 2;

    return NO_ERROR;
} // Dos16GetCp

APIRET16 Dos16GetEnv(PUSHORT psel, PUSHORT pcmdoffset)
{
    TRACE_NATIVE("Dos16GetEnv(%p, %p)", psel, pcmdoffset);
    const uint32 cmd1616 = GLoaderState.convert32to1616(GLoaderState.pib.pib_pchcmd);
    if (pcmdoffset) *pcmdoffset = (USHORT) (cmd1616 & 0xFFFF);
    if (psel) *psel = (USHORT) ((cmd1616 >> 16) & 0xFFFF);
    return NO_ERROR;
} // Dos16GetEnv

APIRET16 Dos16Write(USHORT h, PVOID buf, USHORT buflen, PUSHORT actual)
{
    TRACE_NATIVE("Dos16Write(%u, %p, %u, %p)", (uint) h, buf, (uint) buflen, actual);
    ULONG ulactual = actual ? *actual : 0;
    APIRET rc = DosWrite_implementation(h, buf, buflen, &ulactual);
    if (actual) *actual = (USHORT) ulactual;
    return (APIRET16) rc;
} // Dos16Write

APIRET16 Dos16SetVec(USHORT vecnum, PFN routine, PVOID prevaddress)
{
   TRACE_NATIVE("Dos16SetVec(%u, %p, %p)", (uint) vecnum, routine, prevaddress);
   FIXME("do something");
   if (prevaddress) *(PFN *)prevaddress = 0;
   return NO_ERROR;
} 

APIRET16 Dos16SetSigHandler(PFN routine, PVOID prevaddress, PUSHORT prevaction, USHORT action, USHORT signumber)
{
   TRACE_NATIVE("Dos16SetSigHandler(%p, %p, %p, %u, %u)", routine, prevaddress, prevaction, action, signumber);
   FIXME("do something");
   if (prevaddress) *(PFN *)prevaddress = 0;
   if (prevaction) *prevaction = 0;
   return NO_ERROR;
} 

APIRET16 Dos16GetInfoSeg(PSEL globalseg, PSEL localseg)
{
    TRACE_NATIVE("Dos16GetInfoSeg(%p, %p)", globalseg, localseg);
    if (globalseg) *globalseg = ginfosel << 3 | 7;
    if (localseg) *localseg = linfosel << 3 | 7;
    return NO_ERROR;
}

APIRET16 Dos16QCurDisk(PUSHORT drivenum, PULONG drivemap)
{
    TRACE_NATIVE("Dos16QCurDisk(%p, %p)", drivenum, drivemap);
    if (drivenum) *drivenum = 3; // FIXME: C: only
    if (drivemap) *drivemap = 1 << 2;
    return NO_ERROR;
}

APIRET16 Dos16QCurDir(USHORT drivenum, PBYTE dirpath, PUSHORT dirpathlen)
{
    TRACE_NATIVE("Dos16QCurDir(%d, %p, %p)", drivenum, dirpath, dirpathlen);
    if (!dirpathlen || !dirpath) return ERROR_INVALID_PARAMETER; // ???
    int rlen, len = *dirpathlen;
    char dirtmp[PATH_MAX];
    getcwd(dirtmp, PATH_MAX);
    rlen = strlen(dirtmp);
    if (dirpathlen) *dirpathlen = rlen + 1;
    if (rlen >= len) {
        *dirpathlen = rlen + 1;
        return ERROR_BUFFER_OVERFLOW;
    }
    char *pch = strchr(dirtmp, '/');
    while (pch) {
        *pch = '\\';
        pch = strchr(pch, '/');
    }
 
    strcpy((char *)dirpath, dirtmp + 1);
    return NO_ERROR;
}

APIRET16 Dos16HoldSignal(USHORT action)
{
    TRACE_NATIVE("Dos16HoldSignal(%d)", action);
    FIXME("stub");
    return NO_ERROR;
}

APIRET16 Dos16QFsInfo(USHORT drivenum, USHORT fslevel, PBYTE infobuf, USHORT infobufsize)
{
    TRACE_NATIVE("Dos16QFsInfo(%d, %d, %p, %d)", drivenum, fslevel, infobuf, infobufsize);
    const char label[] = "linux";
    if (fslevel == 1)
        FIXME("fslevel 1");
    else if (fslevel == 2)
    {
        infobuf[4] = sizeof(label);
        strcpy((char *)&infobuf[5], label);
    }
    return NO_ERROR;
}

APIRET16 Dos16QFileMode(PCHAR filepath, PUSHORT attr, ULONG res)
{
    TRACE_NATIVE("Dos16QFileMode('%s', %p, %d)", filepath, attr, res);
    APIRET err;
    char *path = makeUnixPath(filepath, &err);
    if (!path) return err;
    struct stat sbuf;
    int ret = stat(path, &sbuf);
    free(path);
    if (ret) return ERROR_FILE_NOT_FOUND;
    *attr = 0;
    if (S_ISDIR(sbuf.st_mode))
        *attr |= (1 << 4);
    return NO_ERROR;
}

APIRET16 Dos16Open(PSZ pszFileName, PUSHORT pHf, PULONG pulAction, ULONG cbFile, USHORT ulAttribute, USHORT fsOpenFlags, USHORT fsOpenMode, PEAOP2 peaop2)
{
    TRACE_NATIVE("Dos16Open('%s', %p, %p, %u, %u, %u, %u, %p)", pszFileName, pHf, pulAction, (uint) cbFile, (uint) ulAttribute, (uint) fsOpenFlags, (uint) fsOpenMode, peaop2);
    HFILE hf;
    APIRET ret = doDosOpen(pszFileName, &hf, pulAction, (LONGLONG) cbFile, ulAttribute, fsOpenFlags, fsOpenMode, peaop2);
    *pHf = hf;
    return ret;
} // DosOpen

APIRET16 Dos16FindFirst(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute, PVOID pfindbuf, USHORT cbBuf, PUSHORT pcFileNames, ULONG res)
{
    if (!hdir16)
        hdir16 = calloc(10, sizeof(HDIR)); // 10 dir handles
    ULONG hdir32 = *phdir;
    ULONG cfn = *pcFileNames;
    APIRET ret = DosFindFirst(pszFileSpec, &hdir32, flAttribute, pfindbuf, cbBuf, &cfn, -1);
    if (ret && (ret != ERROR_EAS_DIDNT_FIT))
        return ret;
    int i;
    for (i = 0; i < 10; i++)
    {
        if(!hdir16[i])
            break;
    }
    if (i == 10)
        return ERROR_NO_MORE_FILES;
    hdir16[i] = hdir32;
    *phdir = i;
    *pcFileNames = cfn;
    return ret;
}

APIRET16 Dos16ChDir(PSZ pszDir, ULONG res)
{
    TRACE_NATIVE("Dos16ChDir('%s', %u)", pszDir, res);
    APIRET err;
    char *path = makeUnixPath(pszDir, &err);
    if (!path) return err;
    int ret = chdir(path);
    free(path);
    if (ret && (errno == ENOENT)) return ERROR_PATH_NOT_FOUND;
    else if (ret) return ERROR_ACCESS_DENIED;
    return NO_ERROR;
}

APIRET16 Dos16Close(USHORT hFile)
{
    TRACE_NATIVE("Dos16Close(%u)", (uint) hFile);
    return DosClose_implementation(hFile);
} // Dos16Close

APIRET16 Dos16FindNext(USHORT hDir, PVOID pfindbuf, USHORT cbfindbuf, PUSHORT pcFilenames)
{
    APIRET ret;
    ULONG pcfn = *pcFilenames;
    ret = DosFindNext(hdir16[hDir], pfindbuf, cbfindbuf, &pcfn);
    *pcFilenames = pcfn;
    return ret;
}

APIRET16 Dos16FindClose(USHORT hDir)
{
    if (hDir > 10)
        return ERROR_INVALID_HANDLE;
    HDIR hdir32 = hdir16[hDir];
    hdir16[hDir] = 0;
    return DosFindClose(hdir32);
}
 
APIRET16 Dos16Delete(PSZ pszFile, ULONG res)
{
    TRACE_NATIVE("Dos16Delete('%s', %u)", pszFile, (uint) res);
    return DosDelete_implementation(pszFile);
} // Dos16Delete

APIRET16 Dos16DupHandle(USHORT hFile, PUSHORT pHfile)
{
    TRACE_NATIVE("Dos16DupHandle(%u, %p)", (uint) hFile, pHfile);
    HFILE phf = *(int16_t *)pHfile;
    APIRET ret = DosDupHandle_implementation(hFile, &phf);
    *pHfile = phf;
    return ret;
} // Dos16DupHandle

APIRET16 Dos16Read(USHORT hFile, PVOID pBuffer, USHORT cbRead, PUSHORT pcbActual)
{
    TRACE_NATIVE("Dos16Read(%u, %p, %u, %p)", (uint) hFile, pBuffer, (uint) cbRead, pcbActual);
    ULONG actual;
    APIRET ret = DosRead_implementation(hFile, pBuffer, cbRead, &actual);
    *pcbActual = (USHORT) actual;
    return ret;
} // Dos16Read

APIRET16 Dos16ExecPgm(PCHAR pObjname, SHORT cbObjname, USHORT execFlag, PSZ pArg, PSZ pEnv, PRESULTCODES16 pRes, PSZ pName)
{
    TRACE_NATIVE("Dos16ExecPgm('%s', %d, %u, '%s', '%s', %p, '%s')", pObjname, (int) cbObjname, (uint) execFlag, pArg, pEnv, pRes, pName);
    RESULTCODES res;
    const APIRET ret = DosExecPgm_implementation(pObjname, cbObjname, execFlag, pArg, pEnv, &res, pName);
    pRes->codeTerminate = (USHORT) res.codeTerminate;
    pRes->codeResult = (USHORT) res.codeResult;
    return ret;
} // Dos16ExecPgm

APIRET16 Dos16CWait(USHORT action, USHORT option, PRESULTCODES16 pres, PUSHORT ppid, USHORT pid)
{
    TRACE_NATIVE("Dos16CWait(%u, %u, %p, %p, %u)", (uint) action, (uint) option, pres, ppid, (uint) pid);
    RESULTCODES res;
    PID cpid;
    APIRET ret = DosWaitChild(action, option, &res, &cpid, pid);
    pres->codeTerminate = res.codeTerminate;
    pres->codeResult = res.codeResult;
    *ppid = cpid;
    return ret;
} // Dos16CWait

APIRET16 Dos16ChgFilePtr(USHORT handle, LONG distance, USHORT whence, PULONG newoffset)
{
    TRACE_NATIVE("Dos16ChgFlePtr(%u, %u, %u, %p)", (uint) handle, (uint) distance, (uint) whence, newoffset);
    return (APIRET16) DosSetFilePtr_implementation(handle, distance, whence, newoffset);
} // Dos16ChgFilePtr


LX_NATIVE_CONSTRUCTOR(doscalls)
{
    GLoaderState.dosExit = DosExit;

    if (pthread_mutex_init(&GMutexDosCalls, NULL) == -1) {
        fprintf(stderr, "pthread_mutex_init failed!\n");
        abort();
    } // if

    MaxHFiles = 20;  // seems to be OS/2's default.
    HFiles = (HFileInfo *) malloc(sizeof (HFileInfo) * MaxHFiles);
    if (!HFiles) {
        pthread_mutex_destroy(&GMutexDosCalls);
        fprintf(stderr, "Out of memory!\n");
        abort();
    } // if

    HFileInfo *info = HFiles;
    for (uint32 i = 0; i < MaxHFiles; i++, info++) {
        info->fd = -1;
        info->type = 0;
        info->attr = 0;
        info->flags = 0;
    } // for

    // launching a Hello World program from CMD.EXE seems to inherit several
    //  file handles. 0, 1, 2 seem to map to stdin, stdout, stderr (and will
    //  be character devices (maybe CON?) by default, unless you redirect
    //  to a file in which case they're physical files, and using '|' in
    //  CMD.EXE will make handle 1 into a Pipe, of course.
    // Handles, 4, 6 and 9 were also in use (all character devices, attributes
    //  51585, 51392, 51392), but I don't know what these are, if they are
    //  inherited from CMD.EXE or supplied by OS/2 for whatever purpose.
    //  For now, we just wire up stdio.

    FIXME("We can iterate /proc/self/fd to get all currently-open files");
    for (int i = 0; i <= 2; i++) {
        initHFileInfoFromUnixFd(i, &HFiles[i]);
    } // for

    GLoaderState.allocSegment(&ginfosel, 0);
    GLoaderState.allocSegment(&linfosel, 0);
    ginfo = (GINFOSEG *)GLoaderState.convert1616to32(ginfosel << 19);
    linfo = (LINFOSEG *)GLoaderState.convert1616to32(linfosel << 19);
    memset(ginfo, 0, sizeof(GINFOSEG));
    memset(linfo, 0, sizeof(LINFOSEG));
    ginfo->time = time(0);
    struct tm *gtime = gmtime((time_t *)&ginfo->time);
    ginfo->hour = gtime->tm_hour;
    ginfo->minutes = gtime->tm_min;
    ginfo->seconds = gtime->tm_sec;
    ginfo->timezone = 0;
    ginfo->cusecTimerInterval = 100;
    ginfo->day = gtime->tm_mday;
    ginfo->month = gtime->tm_mon;
    ginfo->year = gtime->tm_year;
    ginfo->weekday = gtime->tm_wday;
    ginfo->uchMajorVersion = 20;
    ginfo->uchMinorVersion = 40;
    ginfo->sgCurrent = 1;
    ginfo->sgMax = 99;
    ginfo->cHugeShift = 0;
    ginfo->fProtectModeOnly = 1;
    ginfo->pidForeground = GLoaderState.pib.pib_ulpid;
    ginfo->csecMaxWait = 1;
    ginfo->cmsecMinSlice = 1;
    ginfo->cmsecMaxSlice = 10;
    ginfo->bootdrive = 3;
    ginfo->csgWindowableVioMax = 99;
    ginfo->csgPMMax = 99;
    linfo->pidCurrent = GLoaderState.pib.pib_ulpid;
    linfo->pidParent = GLoaderState.pib.pib_ulppid;
}

LX_NATIVE_DESTRUCTOR(doscalls)
{
    GLoaderState.dosExit = NULL;

    ExitListItem *next = GExitList;
    GExitList = NULL;

    for (ExitListItem *item = next; item; item = next) {
        next = item->next;
        free(item);
    } // for

    for (uint32 i = 0; i < MaxHFiles; i++) {
        if (HFiles[i].fd > 2)  // don't close -1, or any stdio handles.
            close(HFiles[i].fd);
    } // for
    free(HFiles);
    HFiles = NULL;
    MaxHFiles = 0;

    GLoaderState.freeSelector(ginfosel);
    GLoaderState.freeSelector(linfosel);
    pthread_mutex_destroy(&GMutexDosCalls);
}

// end of doscalls.c ...

