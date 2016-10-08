#define _POSIX_C_SOURCE 199309
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "os2native.h"
#include "doscalls.h"

static LxLoaderState *GLoaderState = NULL;

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
    int fd;
    ULONG type;
    ULONG attr;
} HFileInfo;

static HFileInfo *HFiles = NULL;
static uint32 MaxHFiles = 0;

LX_NATIVE_MODULE_DEINIT({
    ExitListItem *next = GExitList;
    GExitList = NULL;
    for (ExitListItem *item = next; item; item = next) {
        next = item->next;
        free(item);
    } // for
    free(HFiles);
    HFiles = NULL;
    MaxHFiles = 0;
    GLoaderState = NULL;
})

static int initDoscalls(LxLoaderState *lx_state)
{
    GLoaderState = lx_state;
    MaxHFiles = 20;  // seems to be OS/2's default.
    HFiles = (HFileInfo *) malloc(sizeof (HFileInfo) * MaxHFiles);
    if (!HFiles) {
        fprintf(stderr, "Out of memory!\n");
        return 0;
    } // if

    HFileInfo *info = HFiles;
    for (uint32 i = 0; i < MaxHFiles; i++, info++) {
        info->fd = -1;
        info->type = 0;
        info->attr = 0;
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

    for (int i = 0; i <= 2; i++) {
        HFiles[i].fd = i;
        HFiles[i].type = 1;  // !!! FIXME: could be a pipe or file.
        HFiles[i].attr = DAW_STDIN | DAW_STDOUT | DAW_LEVEL1 | DAW_CHARACTER;
    } // for

    return 1;
} // initDoscalls

LX_NATIVE_MODULE_INIT({ if (!initDoscalls(lx_state)) return NULL; })
    LX_NATIVE_EXPORT(DosQueryHType, 224),
    LX_NATIVE_EXPORT(DosScanEnv, 227),
    LX_NATIVE_EXPORT(DosGetDateTime, 230),
    LX_NATIVE_EXPORT(DosExit, 234),
    LX_NATIVE_EXPORT(DosOpen, 273),
    LX_NATIVE_EXPORT(DosWrite, 282),
    LX_NATIVE_EXPORT(DosExitList, 296),
    LX_NATIVE_EXPORT(DosAllocMem, 299),
    LX_NATIVE_EXPORT(DosSetMem, 305),
    LX_NATIVE_EXPORT(DosGetInfoBlocks, 312),
    LX_NATIVE_EXPORT(DosQueryModuleName, 320),
    LX_NATIVE_EXPORT(DosCreateEventSem, 324),
    LX_NATIVE_EXPORT(DosCreateMutexSem, 331),
    LX_NATIVE_EXPORT(DosSubSetMem, 344),
    LX_NATIVE_EXPORT(DosSubAllocMem, 345),
    LX_NATIVE_EXPORT(DosQuerySysInfo, 348),
    LX_NATIVE_EXPORT(DosSetExceptionHandler, 354),
    LX_NATIVE_EXPORT(DosSetSignalExceptionFocus, 378),
    LX_NATIVE_EXPORT(DosSetRelMaxFH, 382),
    LX_NATIVE_EXPORT(DosFlatToSel, 425)
LX_NATIVE_MODULE_INIT_END()


APIRET DosGetInfoBlocks(PTIB *pptib, PPIB *pppib)
{
    TRACE_NATIVE("DosGetInfoBlocks(%p, %p)", pptib, pppib);

    if (pptib != NULL) {
        // just read the FS register, since we have to stick it there anyhow...
        uint8 *ptib2;
        __asm__ __volatile__ ( "movl %%fs:0xC, %0  \n\t" : "=r" (ptib2) );
        // we store the TIB2 struct right after the TIB struct on the stack,
        //  so get the TIB2's linear address from %fs:0xC, then step back
        //  to the TIB's linear address.
        *pptib = (PTIB) (ptib2 - sizeof (TIB));
    } // if

    if (pppib != NULL) {
        static PIB pib;
        if (pib.pib_ulpid == 0) {
            // !!! FIXME: this is seriously incomplete.
            pib.pib_hmte = (HMODULE) GLoaderState->main_module;
            pib.pib_ulpid = (uint32) getpid();
            pib.pib_ulppid = (uint32) getppid();
            pib.pib_pchcmd = GLoaderState->main_module->cmd;
            pib.pib_pchenv = GLoaderState->main_module->env;
        } // if
        *pppib = &pib;
    } // if

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
    if (strlen(lxmod->os2path) <= buflen)
        return ERROR_BAD_LENGTH;
    strcpy(buf, lxmod->os2path);
    return NO_ERROR;
} // DosQueryModuleName


APIRET DosScanEnv(PSZ name, PSZ *outval)
{
    TRACE_NATIVE("DosScanEnv('%s', %p)", name, outval);

    char *env = GLoaderState->main_module->env;
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

APIRET DosWrite(HFILE h, PVOID buf, ULONG buflen, PULONG actual)
{
    TRACE_NATIVE("DosWrite(%u, %p, %u, %p)", (uint) h, buf, (uint) buflen, actual);

    // !!! FIXME: writing to a terminal should probably convert CR/LF to LF.

    if ((h >= MaxHFiles) || (HFiles[h].fd == -1))
        return ERROR_INVALID_HANDLE;

    const int rc = write(HFiles[h].fd, buf, buflen);
    if (rc < 0) {
        *actual = 0;
        return ERROR_DISK_FULL;  // !!! FIXME: map these errors.
    } // if

    *actual = (uint32) rc;
    return NO_ERROR;
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
        fn(why);
    } // for
} // runDosExitList

VOID DosExit(ULONG action, ULONG exitcode)
{
    TRACE_NATIVE("DosExit(%u, %u)", (uint) action, (uint) exitcode);

    // !!! FIXME: what does a value other than 0 or 1 do here?
    if (action == EXIT_THREAD) {
        // !!! FIXME: terminate thread. If last thread: terminate process.
        fprintf(stderr, "FIXME: DosExit(0) should terminate thread, not process.\n");
        fflush(stderr);
    } // if

    // terminate the process.
    runDosExitList(TC_EXIT);

    // !!! FIXME: finalize OS/2 DLLs before killing the process?

    // OS/2's docs say this only keeps the lower 16 bits of exitcode.
    // !!! FIXME: ...but Unix only keeps the lowest 8 bits. Will have to
    // !!! FIXME:  tapdance to pass larger values back to OS/2 parent processes.
    exit((int) (exitcode & 0xFFFF));
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
    if ((cmd != 1) && (arg != 0))
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
    // !!! FIXME: write me
    return NO_ERROR;
} // DosCreateEventSem

APIRET DosCreateMutexSem(PSZ name, PHMTX phmtx, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateMutexSem('%s', %p, %u, %u)", name, phmtx, (uint) attr, (uint) state);
    // !!! FIXME: write me
    return NO_ERROR;
} // DosCreateMutexSem

// !!! FIXME: this is obviously not correct.
APIRET DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosSetExceptionHandler(%p)", rec);
    return NO_ERROR;
} // DosSetExceptionHandler

ULONG DosFlatToSel(void)
{
    // this actually passes the arg in eax instead of the stack.
    uint32 eax = 0;
    __asm__ __volatile__ ("" : "=a" (eax));
    TRACE_NATIVE("DosFlatToSel(%p)", (void *) (size_t) eax);
    return (ULONG) 0x12345678;  // !!! FIXME
} // DosFlatToSel

APIRET DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes)
{
    TRACE_NATIVE("DosSetSignalExceptionFocus(%u, %p)", (uint) flag, pulTimes);

    if (flag == 0) {
        if (GLoaderState->main_module->signal_exception_focus_count == 0)
            return ERROR_ALREADY_RESET;
        GLoaderState->main_module->signal_exception_focus_count--;
    } else if (flag == 1) {
        if (GLoaderState->main_module->signal_exception_focus_count == 0xFFFFFFFF)
            return ERROR_NESTING_TOO_DEEP;
        GLoaderState->main_module->signal_exception_focus_count++;
    } else {
        // !!! FIXME: does OS/2 do something if flag != 0 or 1?
    } // else

    if (pulTimes)
        *pulTimes = GLoaderState->main_module->signal_exception_focus_count;

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
            HFileInfo *info = (HFileInfo *) realloc(HFiles, sizeof (HFileInfo) * (MaxHFiles + incr));
            if (info != NULL) {
                HFiles = info;
                for (LONG i = 0; i < incr; i++, info++) {
                    info->fd = -1;
                    info->type = 0;
                    info->attr = 0;
                } // for
                MaxHFiles += incr;
            } // if
        } // if
    } // if

    if (pcurrent != NULL) {
        *pcurrent = MaxHFiles;
    } // if

    return NO_ERROR;  // always returns NO_ERROR even if it fails.
} // DosSetRelMaxFH


APIRET DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag)
{
    TRACE_NATIVE("DosAllocMem(%p, %u, %u)", ppb, (uint) cb, (uint) flag);

    // !!! FIXME: this API is actually much more complicated than this.
    *ppb = malloc(cb);
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

APIRET DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr)
{
    TRACE_NATIVE("DosQueryHType(%u, %p, %p)", (uint) hFile, pType, pAttr);

    if ((hFile >= MaxHFiles) || (HFiles[hFile].fd == -1))
        return ERROR_INVALID_HANDLE;

    // OS/2 will dereference a NULL pointer here, but I can't do it...!!!
    if (pType) *pType = HFiles[hFile].type;
    if (pAttr) *pAttr = HFiles[hFile].attr;

    return NO_ERROR;
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


// based on case-insensitive search code from PhysicsFS:
//    https://icculus.org/physfs/
//  It's also zlib-licensed, plus I wrote it.  :)  --ryan.
// !!! FIXME: this doesn't work as-is for UTF-8 case folding, since string
// !!! FIXNE:  length can change!
static int locateOneElement(char *buf)
{
    if (access(buf, F_OK))
        return 1;  // quick rejection: exists in current case.

    DIR *dirp;
    char *ptr = strrchr(buf, '/');  // find entry at end of path.
    if (ptr == NULL) {
        dirp = opendir("/");
        ptr = buf;
    } else {
        *ptr = '\0';
        dirp = opendir(buf);
        *ptr = '/';
        ptr++;  // point past dirsep to entry itself.
    } // else

    for (struct dirent *dent = readdir(dirp); dent; dent = readdir(dirp)) {
        if (strcasecmp(dent->d_name, ptr) == 0) {
            strcpy(ptr, dent->d_name); // found a match. Overwrite with this case.
            closedir(dirp);
            return 1;
        } // if
    } // for

    // no match at all...
    closedir(dirp);
    return 0;
} // locateOneElement

int locatePathCaseInsensitive(char *buf)
{
    int rc;
    char *ptr = buf;

    if (*ptr == '\0')
        return 0;  // Uh...I guess that's success?

    if (access(buf, F_OK))
        return 0;  // quick rejection: exists in current case.

    while ( (ptr = strchr(ptr + 1, '/')) != NULL ) {
        *ptr = '\0';  // block this path section off
        rc = locateOneElement(buf);
        *ptr = '/'; // restore path separator
        if (!rc)
            return -2;  // missing element in path.
    } // while

    // check final element...
    return locateOneElement(buf) ? 0 : -1;
} // locatePathCaseInsensitive


static char *makeUnixPath(const char *os2path, APIRET *err)
{
    if ((strcasecmp(os2path, "NUL") == 0) || (strcasecmp(os2path, "\\DEV\\NUL") == 0))
        os2path = "/dev/null";
    // !!! FIXME: emulate other OS/2 device names (CON, etc).
    //else if (strcasecmp(os2path, "CON") == 0)
    else {
        char drive = os2path[0];
        if ((drive >= 'a') && (drive <= 'z'))
            drive += 'A' - 'a';

        if ((drive >= 'A') && (drive <= 'Z')) {
            if (os2path[1] == ':') {  // it's a drive letter.
                if (drive == 'C')
                    os2path += 2;  // skip "C:" if it's there.
                else {
                    *err = ERROR_NOT_DOS_DISK;
                    return NULL;
                } // else
            } // if
        } // if
    } // else

    const size_t len = strlen(os2path);
    char *retval = (char *) malloc(len + 1);
    if (!retval) {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    } // else

    strcpy(retval, os2path);

    for (char *ptr = strchr(retval, '\\'); ptr; ptr = strchr(ptr + 1, '\\'))
        *ptr = '/';  // convert to Unix-style path separators.

    locatePathCaseInsensitive(retval);

    return retval;
} // makeUnixPath

APIRET DosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, ULONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosOpen('%s', %p, %p, %u, %u, %u, %u, %p)", pszFileName, pHf, pulAction, (uint) cbFile, (uint) ulAttribute, (uint) fsOpenFlags, (uint) fsOpenMode, peaop2);

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

    // !!! FIXME: mutex this.
    for (i = 0; i < MaxHFiles; i++, info++) {
        if (info->fd == -1)  // available?
            break;
    } // for

    if (i == MaxHFiles)
        return ERROR_TOO_MANY_OPEN_FILES;

    const HFILE hf = i;

    APIRET err = NO_ERROR;
    char *unixpath = makeUnixPath(pszFileName, &err);
    if (!unixpath) {
        info->fd = -1;
        return err;
    } // if

    printf("DosOpen: converted '%s' to '%s'\n", pszFileName, unixpath);

    info->fd = open(unixpath, flags, mode);

    // if failed trying exclusive-create because file already exists, but we
    //  didn't explicitly _need_ exclusivity, retry without O_EXCL. We always
    //  try O_EXCL at first when using O_CREAT, so we can tell atomically if
    //  we were the one that created the file, to satisfy pulAction.
    int existed = 0;
    if ((info->fd == -1) && (flags & (O_CREAT|O_EXCL)) && (errno == EEXIST) && !isExclusive) {
        existed = 1;
        info->fd = open(unixpath, flags & ~O_EXCL, mode);
    } else if (info->fd != -1) {
        existed = 1;
    } // else if
    free(unixpath);

    if (info->fd != -1) {
        if (mustNotExist) {
            close(info->fd);
            info->fd = -1;
            return ERROR_OPEN_FAILED;  // !!! FIXME: what error does OS/2 return for this?
        } else if ( (!existed || isReplacing) && (cbFile > 0) ) {
            if (ftruncate(info->fd, cbFile) == -1) {
                const int e = errno;
                close(info->fd);
                info->fd = -1;
                errno = e;  // let the switch below sort out the OS/2 error code.
            } // if
        } // else if
    } // if

    if (info->fd != -1) {
        if (isReplacing)
            FIXME("Replacing a file should delete all its EAs, too");
    } // if

    if (info->fd == -1) {
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

    if (isReplacing)
        *pulAction = FILE_TRUNCATED;
    else if (existed)
        *pulAction = FILE_EXISTED;
    else
        *pulAction = FILE_CREATED;

    *pHf = hf;
    return NO_ERROR;
} // DosOpen

// end of doscalls.c ...

