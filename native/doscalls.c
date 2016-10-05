#define _POSIX_C_SOURCE 199309
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>
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
    LX_NATIVE_EXPORT(DosExit, 234),
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

    // !!! FIXME: this is seriously incomplete.
    if (pptib != NULL) {
        static __thread TIB tib;
        static __thread TIB2 tib2;
        if (tib.tib_ptib2 == NULL) {
            // new thread, initialize this TIB.
            const LxModule *lxmod = GLoaderState->main_module;
            tib.tib_ptib2 = &tib2;
            tib.tib_pstack = (void *) ((size_t) lxmod->esp);
            tib.tib_pstacklimit = (void *) (((size_t) lxmod->esp) - lxmod->mmaps[lxmod->lx.esp_object - 1].size);
        } // if
        *pptib = &tib;
    } // if

    if (pppib != NULL) {
        static PIB pib;
        if (pib.pib_ulpid == 0) {
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

// end of doscalls.c ...

