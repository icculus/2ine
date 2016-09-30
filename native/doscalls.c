#define _POSIX_C_SOURCE 199309
#include "native.h"
#include <time.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

NATIVE_MODULE(doscalls);

typedef LxModule *HMODULE;
typedef uint32 HFILE;
typedef uint32 HEV;
typedef uint32 HMTX;

typedef enum
{
    QSV_MAX_PATH_LENGTH = 1,
    QSV_MAX_TEXT_SESSIONS,
    QSV_MAX_PM_SESSIONS,
    QSV_MAX_VDM_SESSIONS,
    QSV_BOOT_DRIVE,
    QSV_DYN_PRI_VARIATION,
    QSV_MAX_WAIT,
    QSV_MIN_SLICE,
    QSV_MAX_SLICE,
    QSV_PAGE_SIZE,
    QSV_VERSION_MAJOR,
    QSV_VERSION_MINOR,
    QSV_VERSION_REVISION,
    QSV_MS_COUNT,
    QSV_TIME_LOW,
    QSV_TIME_HIGH,
    QSV_TOTPHYSMEM,
    QSV_TOTRESMEM,
    QSV_TOTAVAILMEM,
    QSV_MAXPRMEM,
    QSV_MAXSHMEM,
    QSV_TIMER_INTERVAL,
    QSV_MAX_COMP_LENGTH,
    QSV_FOREGROUND_FS_SESSION,
    QSV_FOREGROUND_PROCESS
} QuerySysInfoVariable;

typedef struct TIB2
{
    uint32 tib2_ultid;
    uint32 tib2_ulpri;
    uint32 tib2_version;
    uint16 tib2_usMCCount;
    uint16 tib2_fMCForceFlag;
} TIB2;

typedef struct TIB
{
    void *tib_pexchain;
    void *tib_pstack;
    void *tib_pstacklimit;
    TIB2 *tib_ptib2;
    uint32 tib_version;
    uint32 tib_ordinal;
} TIB;

typedef struct PIB
{
    uint32 pib_ulpid;
    uint32 pib_ulppid;
    HMODULE pib_hmte;
    char *pib_pchcmd;
    char *pib_pchenv;
    uint32 pib_flstatus;
    uint32 pib_ultype;
} PIB;

typedef void(*ExitListFn)(uint32 why);

typedef struct ExitListItem
{
    ExitListFn fn;
    uint32 priority;
    struct ExitListItem *next;
} ExitListItem;

static ExitListItem *GExitList = NULL;

static APIRET DosGetInfoBlocks(TIB **pptib, PIB **pppib)
{
printf("DosGetInfoBlocks\n");
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

static APIRET DosQuerySysInfo(uint32 first, uint32 last, void *_buf, uint32 buflen)
{
    uint32 *buf = (uint32 *) _buf;
printf("DosQuerySysInfo(%u, %u, %p, %u);\n", (unsigned int) first, (unsigned int) last, buf, (unsigned int) buflen);
    if (last < first) return 87;  // ERROR_INVALID_PARAMETER
    if ( (buflen / sizeof (uint32)) < ((last - first) + 1) ) return 111; // ERROR_BUFFER_OVERFLOW
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

            default: return 87;  // ERROR_INVALID_PARAMETER
        } // switch
    } // for

    return 0;  // NO_ERROR
} // DosQuerySysInfo


static APIRET DosQueryModuleName(HMODULE hmod, uint32 buflen, char *buf)
{
    const LxModule *lxmod = (LxModule *) hmod;
printf("DosQueryModuleName ('%s')\n", lxmod->os2path);
    // !!! FIXME: error 6 ERROR_INVALID_HANDLE
    if (strlen(lxmod->os2path) <= buflen)
        return 24;  // ERROR_BAD_LENGTH
    strcpy(buf, lxmod->os2path);
    return 0;  // NO_ERROR
} // DosQueryModuleName

static APIRET DosScanEnv(char *name, char **outval)
{
printf("DosScanEnv('%s')\n", name);
    char *env = GLoaderState->main_module->env;
    const size_t len = strlen(name);
    while (*env) {
        if ((strncmp(env, name, len) == 0) && (env[len] == '=')) {
            *outval = env + len + 1;
            return 0;  // NO_ERROR
        } // if
        env += strlen(env) + 1;
    } // while

    return 203;  // ERROR_ENVVAR_NOT_FOUND
} // DosScanEnv

static APIRET DosWrite(HFILE h, void *buf, uint32 buflen, uint32 *actual)
{
    // !!! FIXME: writing to a terminal should convert CR/LF to LF.
printf("DosWrite(%u, %p, %u, %p)\n", (unsigned int) h, buf, (unsigned int) buflen, actual);
    // OS/2 appears to use 0, 1, 2 for stdin, stdout, stderr, like Unix! Hooray!
    const int rc = write((int) h, buf, buflen);
    if (rc < 0)
        return 112;  // ERROR_DISK_FULL  !!! FIXME: map these errors.

    *actual = (uint32) rc;
    return 0;  // NO_ERROR
} // DosWrite

static void runDosExitList(const uint32 why)
{
    ExitListItem *next = NULL;
    for (ExitListItem *item = GExitList; item; item = next) {
        // don't run any of these more than once:
        //  http://www.verycomputer.com/3_c1f6e02c06ed108e_1.htm
        ExitListFn fn = item->fn;
        next = item->next;
        GExitList = next;
        free(item);
        fn(why);
    } // for
} // runDosExitList

static void DosExit(uint32 action, uint32 exitcode)
{
printf("DosExit(%u, %u)\n", (unsigned int) action, (unsigned int) exitcode);
    // !!! FIXME: what does a value other than 0 or 1 do here?
    if (action == 0) { // EXIT_THREAD
        // !!! FIXME: terminate thread. If last thread: terminate process.
        fprintf(stderr, "FIXME: DosExit(0) should terminate thread, not process.\n");
        fflush(stderr);
    } // if

    // terminate the process.
    runDosExitList(0);  // 0 == TC_EXIT

    // !!! FIXME: finalize OS/2 DLLs before killing the process?

    // OS/2's docs say this only keeps the lower 16 bits of exitcode.
    // !!! FIXME: ...but Unix only keeps the lowest 8 bits. Will have to
    // !!! FIXME:  tapdance to pass larger values back to OS/2 parent processes.
    exit((int) (exitcode & 0xFFFF));
} // DosExit

static APIRET DosExitList(uint32 ordercode, ExitListFn fn)
{
printf("DosExitList(%u, %p)\n", (unsigned int) ordercode, fn);
    if (fn == NULL)
        return 1;  // ERROR_INVALID_FUNCTION

    const uint8 cmd = ordercode & 0xFF;
    const uint8 arg = (ordercode >> 8) & 0xFF;
    ExitListItem *prev = NULL;
    ExitListItem *item = NULL;

    // !!! FIXME: docs say this is illegal, but have to check what OS/2 actually does here.
    if ((cmd != 1) && (arg != 0))
        return 13;  // ERROR_INVALID_DATA

    switch (cmd) {
        case 1: {  // EXLST_ADD
            ExitListItem *newitem = (ExitListItem *) malloc(sizeof (ExitListItem));
            if (!newitem)
                return 8;  // ERROR_NOT_ENOUGH_MEMORY
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
            return 0;  // NO_ERROR
        } // case

        case 2: {  // EXLST_REMOVE
            for (item = GExitList; item; item = item->next) {
                if (item->fn == fn) {
                    if (prev)
                        prev->next = item->next;
                    else
                        GExitList = item->next;
                    free(item);
                    return 0;  // NO_ERROR
                } // if
                prev = item;
            } // for
            return 1;  // ERROR_INVALID_FUNCTION  !!! FIXME: yeah?
        } // case

        case 3:  // EXLST_EXIT
            return 0;  // NO_ERROR ... just treat this as a no-op, I guess...?

        default: return 13;  // ERROR_INVALID_DATA
    } // switch

    return 0;  // NO_ERROR
} // DosExitList

static APIRET DosCreateEventSem(char *name, HEV *phev, uint32 attr, uint32 state)
{
    printf("DosCreateEventSem('%s', %p, %u, %u);\n", name, phev, (unsigned int) attr, (unsigned int) state);
    // !!! FIXME: write me
    return 0;  // NO_ERROR
} // DosCreateEventSem

static APIRET DosCreateMutexSem(char *name, HMTX *phmtx, uint32 attr, uint32 state)
{
    printf("DosCreateMutexSem('%s', %p, %u, %u);\n", name, phmtx, (unsigned int) attr, (unsigned int) state);
    // !!! FIXME: write me
    return 0;  // NO_ERROR
} // DosCreateMutexSem

// !!! FIXME: this is obviously not correct.
static APIRET DosSetExceptionHandler(void *rec)
{
    printf("DosSetExceptionHandler(%p);\n", rec);
    return 0;  // NO_ERROR
} // DosSetExceptionHandler

static uint32 DosFlatToSel(void)
{
    // this actually passes the arg in eax instead of the stack.
    uint32 eax = 0;
    __asm__ __volatile__ ("" : "=a" (eax));
    printf("DosFlatToSel(%p);\n", (void *) (size_t) eax);
    return 0x12345678;  // !!! FIXME
} // DosFlatToSel

static APIRET DosSetSignalExceptionFocus(uint32 flag, uint32 *pulTimes)
{
    printf("DosSetSignalExceptionFocus(%u, %p);\n", (unsigned int) flag, pulTimes);
    if (flag == 0) {
        if (GLoaderState->main_module->signal_exception_focus_count == 0)
            return 300;  // ERROR_ALREADY_RESET
        GLoaderState->main_module->signal_exception_focus_count--;
    } else if (flag == 1) {
        if (GLoaderState->main_module->signal_exception_focus_count == 0xFFFFFFFF)
            return 650;  // ERROR_NESTING_TOO_DEEP
        GLoaderState->main_module->signal_exception_focus_count++;
    } else {
        // !!! FIXME: does OS/2 do something if flag != 0 or 1?
    } // else

    if (pulTimes)
        *pulTimes = GLoaderState->main_module->signal_exception_focus_count;

    // !!! FIXME: I guess enable/disable SIGINT handler here?

    return 0;  // NO_ERROR
} // DosSetSignalExceptionFocus


NATIVE_REPLACEMENT_TABLE("doscalls")
    NATIVE_REPLACEMENT(DosScanEnv, 227)
    NATIVE_REPLACEMENT(DosExit, 234)
    NATIVE_REPLACEMENT(DosWrite, 282)
    NATIVE_REPLACEMENT(DosExitList, 296)
    NATIVE_REPLACEMENT(DosGetInfoBlocks, 312)
    NATIVE_REPLACEMENT(DosQueryModuleName, 320)
    NATIVE_REPLACEMENT(DosCreateEventSem, 324)
    NATIVE_REPLACEMENT(DosCreateMutexSem, 331)
    NATIVE_REPLACEMENT(DosQuerySysInfo, 348)
    NATIVE_REPLACEMENT(DosSetExceptionHandler, 354)
    NATIVE_REPLACEMENT(DosSetSignalExceptionFocus, 378)
    NATIVE_REPLACEMENT(DosFlatToSel, 425)
END_NATIVE_REPLACEMENT_TABLE()

// end of doscalls.c ...

