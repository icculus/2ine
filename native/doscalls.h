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

#pragma pack(push, 1)
typedef struct TIB2
{
    ULONG tib2_ultid;
    ULONG tib2_ulpri;
    ULONG tib2_version;
    USHORT tib2_usMCCount;
    USHORT tib2_fMCForceFlag;
} TIB2, *PTIB2;
#pragma pack(pop)

typedef struct TIB
{
    PVOID tib_pexchain;
    PVOID tib_pstack;
    PVOID tib_pstacklimit;
    PTIB2 tib_ptib2;
    ULONG tib_version;
    ULONG tib_ordinal;
} TIB, *PTIB;

typedef struct PIB
{
    ULONG pib_ulpid;
    ULONG pib_ulppid;
    HMODULE pib_hmte;
    PCHAR pib_pchcmd;
    PCHAR pib_pchenv;
    ULONG pib_flstatus;
    ULONG pib_ultype;
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

typedef struct
{
    UCHAR   hours;
    UCHAR   minutes;
    UCHAR   seconds;
    UCHAR   hundredths;
    UCHAR   day;
    UCHAR   month;
    USHORT  year;
    SHORT   timezone;
    UCHAR   weekday;
} DATETIME, *PDATETIME;


typedef void *PEAOP2; //  !!! FIXME

enum
{
    FILE_EXISTED = 1,
    FILE_CREATED,
    FILE_TRUNCATED
};

enum
{
    FILE_OPEN = 0x01,
    FILE_TRUNCATE = 0x02,
    FILE_CREATE = 0x10
};

enum
{
    FILE_NORMAL = 0x00,
    FILE_READONLY = 0x01,
    FILE_HIDDEN = 0x02,
    FILE_SYSTEM = 0x04,
    FILE_DIRECTORY = 0x10,
    FILE_ARCHIVED = 0x20
};

enum
{
    OPEN_ACTION_FAIL_IF_EXISTS = 0x0000,
    OPEN_ACTION_OPEN_IF_EXISTS = 0x0001,
    OPEN_ACTION_REPLACE_IF_EXISTS = 0x0002,
    OPEN_ACTION_FAIL_IF_NEW = 0x0000,
    OPEN_ACTION_CREATE_IF_NEW = 0x0010,
};

enum
{
    OPEN_ACCESS_READONLY = 0x0000,
    OPEN_ACCESS_WRITEONLY = 0x0001,
    OPEN_ACCESS_READWRITE = 0x0002,
    OPEN_SHARE_DENYREADWRITE = 0x0010,
    OPEN_SHARE_DENYWRITE = 0x0020,
    OPEN_SHARE_DENYREAD = 0x0030,
    OPEN_SHARE_DENYNONE = 0x0040,
    OPEN_FLAGS_NOINHERIT = 0x0080,
    OPEN_FLAGS_NO_LOCALITY = 0x0000,
    OPEN_FLAGS_SEQUENTIAL = 0x0100,
    OPEN_FLAGS_RANDOM = 0x0200,
    OPEN_FLAGS_RANDOMSEQUENTIAL = 0x0300,
    OPEN_FLAGS_NO_CACHE = 0x1000,
    OPEN_FLAGS_FAIL_ON_ERROR = 0x2000,
    OPEN_FLAGS_WRITE_THROUGH = 0x4000,
    OPEN_FLAGS_DASD = 0x8000,
    OPEN_FLAGS_NONSPOOLED = 0x00040000,
    OPEN_FLAGS_PROTECTED_HANDLE = 0x40000000
};

enum
{
    FILE_BEGIN,
    FILE_CURRENT,
    FILE_END
};

enum
{
    FIL_STANDARD = 1,
    FIL_QUERYEASIZE = 2,
    FIL_QUERYEASFROMLIST = 3,
    FIL_QUERYFULLNAME = 5
};

typedef struct
{
    USHORT day : 5;
    USHORT month : 4;
    USHORT year : 7;
} FDATE, *PFDATE;

typedef struct
{
    USHORT twosecs : 5;
    USHORT minutes : 6;
    USHORT hours : 5;
} FTIME, *PFTIME;

typedef struct
{
    FDATE fdateCreation;
    FTIME ftimeCreation;
    FDATE fdateLastAccess;
    FTIME ftimeLastAccess;
    FDATE fdateLastWrite;
    FTIME ftimeLastWrite;
    ULONG cbFile;
    ULONG cbFileAlloc;
    ULONG attrFile;
} FILESTATUS3, *PFILESTATUS3;

typedef struct
{
    FDATE fdateCreation;
    FTIME ftimeCreation;
    FDATE fdateLastAccess;
    FTIME ftimeLastAccess;
    FDATE fdateLastWrite;
    FTIME ftimeLastWrite;
    ULONG cbFile;
    ULONG cbFileAlloc;
    ULONG attrFile;
    ULONG cbList;
} FILESTATUS4, *PFILESTATUS4;

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
ULONG OS2API DosFlatToSel(PVOID ptr);
APIRET OS2API DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes);
APIRET OS2API DosSetRelMaxFH(PLONG pcbReqCount, PULONG pcbCurMaxFH);
APIRET OS2API DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag);
APIRET OS2API DosSubSetMem(PVOID pbBase, ULONG flag, ULONG cb);
APIRET OS2API DosSubAllocMem(PVOID pbBase, PPVOID ppb, ULONG cb);
APIRET OS2API DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr);
APIRET OS2API DosSetMem(PVOID pb, ULONG cb, ULONG flag);
APIRET OS2API DosGetDateTime(PDATETIME pdt);
APIRET OS2API DosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, ULONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2);
APIRET OS2API DosRequestMutexSem(HMTX hmtx, ULONG ulTimeout);
APIRET OS2API DosReleaseMutexSem(HMTX hmtx);
APIRET OS2API DosSetFilePtr(HFILE hFile, LONG ib, ULONG method, PULONG ibActual);
APIRET OS2API DosRead(HFILE hFile, PVOID pBuffer, ULONG cbRead, PULONG pcbActual);
APIRET OS2API DosClose(HFILE hFile);
APIRET OS2API DosEnterMustComplete(PULONG pulNesting);
APIRET OS2API DosExitMustComplete(PULONG pulNesting);
APIRET OS2API DosQueryPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf);
APIRET OS2API DosQueryFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo, ULONG cbInfoBuf);

#ifdef __cplusplus
}
#endif

#endif

// end of doscalls.h ...

