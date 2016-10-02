#ifndef _INCL_DOSCALLS_H_
#define _INCL_DOSCALLS_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    EXIT_THREAD,
    EXIT_PROCESS
};

enum
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
};

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
} TIB, *PTIB;

typedef struct PIB
{
    uint32 pib_ulpid;
    uint32 pib_ulppid;
    HMODULE pib_hmte;
    char *pib_pchcmd;
    char *pib_pchenv;
    uint32 pib_flstatus;
    uint32 pib_ultype;
} PIB, *PPIB;

typedef VOID APIENTRY FNEXITLIST(ULONG);
typedef FNEXITLIST *PFNEXITLIST;

enum
{
    TC_EXIT,
    TC_HARDERROR,
    TC_TRAP,
    TC_KILLPROCESS,
    TC_EXCEPTION
};

enum
{
    EXLST_ADD = 1,
    EXLST_REMOVE,
    EXLST_EXIT
};

typedef void *PEXCEPTIONREGISTRATIONRECORD;  // !!! FIXME

// !!! FIXME: these should probably get sorted alphabetically and/or grouped
// !!! FIXME:  into areas of functionality, but for now, I'm just listing them
// !!! FIXME:  in the order I implemented them to get things running.

APIRET OS2API DosGetInfoBlocks(PTIB *pptib, PPIB *pppib);
APIRET OS2API DosQuerySysInfo(ULONG iStart, ULONG iLast, PVOID pBuf, ULONG cbBuf);
APIRET OS2API DosQueryModuleName(HMODULE hmod, ULONG cbName, PCHAR pch);
APIRET OS2API DosScanEnv(PSZ pszName, PSZ *ppszValue);
APIRET OS2API DosWrite(HFILE hFile, PVOID pBuffer, ULONG cbWrite, PULONG pcbActual);
VOID OS2API DosExit(ULONG action, ULONG result);
APIRET OS2API DosExitList(ULONG ordercode, PFNEXITLIST pfn);
APIRET OS2API DosCreateEventSem(PSZ pszName, PHEV phev, ULONG flAttr, BOOL32 fState);
APIRET OS2API DosCreateMutexSem(PSZ pszName, PHMTX phmtx, ULONG flAttr, BOOL32 fState);
APIRET OS2API DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD pERegRec);
ULONG OS2API DosFlatToSel(void);
APIRET OS2API DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes);
APIRET OS2API DosSetRelMaxFH(PLONG pcbReqCount, PULONG pcbCurMaxFH);
APIRET OS2API DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag);
APIRET OS2API DosSubSetMem(PVOID pbBase, ULONG flag, ULONG cb);
APIRET OS2API DosSubAllocMem(PVOID pbBase, PPVOID ppb, ULONG cb);
APIRET OS2API DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr);
APIRET OS2API DosSetMem(PVOID pb, ULONG cb, ULONG flag);

#ifdef __cplusplus
}
#endif

#endif

// end of doscalls.h ...

