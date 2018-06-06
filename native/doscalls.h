/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_DOSCALLS_H_
#define _INCL_DOSCALLS_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CCHMAXPATH 260
#define CCHMAXPATHCOMP 256

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

typedef struct TIB
{
    PVOID tib_pexchain;
    PVOID tib_pstack;
    PVOID tib_pstacklimit;
    PTIB2 tib_ptib2;
    ULONG tib_version;
    ULONG tib_ordinal;
} TIB, *PTIB;
#pragma pack(pop)

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

#pragma pack(push, 1)
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
#pragma pack(pop)

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

enum
{
    CREATE_READY,
    CREATE_SUSPENDED
};

enum
{
    STACK_SPARSE = 0,
    STACK_COMMITTED = 2
};

typedef VOID APIENTRY FNTHREAD(ULONG);
typedef FNTHREAD *PFNTHREAD;

typedef struct
{
    ULONG codeTerminate;
    ULONG codeResult;
} RESULTCODES, *PRESULTCODES;

enum
{
    EXEC_SYNC,
    EXEC_ASYNC,
    EXEC_ASYNCRESULT,
    EXEC_TRACE,
    EXEC_BACKGROUND,
    EXEC_LOAD,
    EXEC_ASYNCRESULTDB
};


enum
{
    DC_SEM_SHARED = 1
};

enum
{
    DCMW_WAIT_ANY = (1<<1),
    DCMW_WAIT_ALL = (1<<2)
};

enum
{
    SEM_INDEFINITE_WAIT = -1,
    SEM_IMMEDIATE_RETURN = 0
};

enum
{
    DCWW_WAIT,
    DCWW_NOWAIT
};

enum
{
    DCWA_PROCESS,
    DCWA_PROCESSTREE
};

enum
{
   DSPI_WRTTHRU = 0x10,
};

enum
{
    HDIR_CREATE = -1,
    HDIR_SYSTEM = 1
};

enum
{
   MUST_HAVE_READONLY = ( (FILE_READONLY << 8) | FILE_READONLY ),
   MUST_HAVE_HIDDEN = ( (FILE_HIDDEN << 8) | FILE_HIDDEN ),
   MUST_HAVE_SYSTEM = ( (FILE_SYSTEM << 8) | FILE_SYSTEM),
   MUST_HAVE_DIRECTORY = ( (FILE_DIRECTORY << 8) | FILE_DIRECTORY ),
   MUST_HAVE_ARCHIVED = ( (FILE_ARCHIVED  << 8) | FILE_ARCHIVED  )
};

typedef struct
{
    ULONG oNextEntryOffset;
    FDATE fdateCreation;
    FTIME ftimeCreation;
    FDATE fdateLastAccess;
    FTIME ftimeLastAccess;
    FDATE fdateLastWrite;
    FTIME ftimeLastWrite;
    ULONG cbFile;
    ULONG cbFileAlloc;
    ULONG attrFile;
    UCHAR cchName;
    CHAR achName[CCHMAXPATHCOMP];
} FILEFINDBUF3, *PFILEFINDBUF3;

typedef struct
{
    ULONG oNextEntryOffset;
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
    UCHAR cchName;
    CHAR achName[CCHMAXPATHCOMP];
} FILEFINDBUF4, *PFILEFINDBUF4;

typedef struct
{
    USHORT iType;
    USHORT cbName;
    USHORT cbFSDName;
    USHORT cbFSAData;
    UCHAR szName[1];
    UCHAR szFSDName[1];
    UCHAR rgFSAData[1];
} FSQBUFFER2, *PFSQBUFFER2;

enum
{
    DEVINFO_PRINTER,
    DEVINFO_RS232,
    DEVINFO_FLOPPY,
    DEVINFO_COPROCESSOR,
    DEVINFO_SUBMODEL,
    DEVINFO_MODEL,
    DEVINFO_ADAPTER
};

enum
{
    FAPPTYP_NOTSPEC = 0x0000,
    FAPPTYP_NOTWINDOWCOMPAT = 0x0001,
    FAPPTYP_WINDOWCOMPAT = 0x0002,
    FAPPTYP_WINDOWAPI = 0x0003,
    FAPPTYP_BOUND = 0x0008,
    FAPPTYP_DLL = 0x0010,
    FAPPTYP_DOS = 0x0020,
    FAPPTYP_PHYSDRV = 0x0040,
    FAPPTYP_VIRTDRV = 0x0080,
    FAPPTYP_PROTDLL = 0x0100,
    FAPPTYP_WINDOWSREAL = 0x0200,
    FAPPTYP_WINDOWSPROT = 0x0400,
    FAPPTYP_WINDOWSPROT31 =0x1000,
    FAPPTYP_32BIT = 0x4000
};

enum
{
    BEGIN_LIBPATH = 1,
    END_LIBPATH = 2
};

typedef void *PCONTEXTRECORD;  // !!! FIXME


// DosQueryHeaderInfo() is an undocumented OS/2 API that is exported from DOSCALLS. Java uses it.
//  This isn't mentioned in the docs or the SDK (beyond the ordinal being listed). I got the basic details
//  from Odin32, which lists it in their os2newapi.h header.
enum
{
    QHINF_EXEINFO = 1,
    QHINF_READRSRCTBL,
    QHINF_READFILE,
    QHINF_LIBPATHLENGTH,
    QHINF_LIBPATH,
    QHINF_FIXENTRY,
    QHINF_STE,
    QHINF_MAPSEL
};

OS2EXPORT APIRET OS2API DosQueryHeaderInfo(HMODULE hmod, ULONG ulIndex, PVOID pvBuffer, ULONG cbBuffer, ULONG ulSubFunction) OS2APIINFO(582);

// This is also undocumented (thanks, EDM/2!). Of course, Java uses it.
OS2EXPORT APIRET OS2API DosQuerySysState(ULONG func, ULONG arg1, ULONG pid, ULONG _res_, PVOID buf, ULONG bufsz) OS2APIINFO(368);

// This is also undocumented (no idea about this at all, including function params). Of course, Java uses it.
OS2EXPORT APIRET OS2API DosR3ExitAddr(void) OS2APIINFO(553);

// These are 16-bit APIs that aren't in the 4.5 toolkit headers. Yeah, Java uses them! Winging it.
typedef int HSEM16, *PHSEM16;  // !!! FIXME: int? Not short?
OS2EXPORT APIRET16 OS2API16 DosSemRequest(PHSEM16 sem, LONG ms) OS2APIINFO(140);
OS2EXPORT APIRET16 OS2API16 DosSemClear(PHSEM16 sem) OS2APIINFO(141);
OS2EXPORT APIRET16 OS2API16 DosSemWait(PHSEM16 sem, LONG ms) OS2APIINFO(142);
OS2EXPORT APIRET16 OS2API16 DosSemSet(PHSEM16 sem) OS2APIINFO(143);


// These are only meant to be available to 16-bit binaries, some were removed in OS/2 2.0's SDK.
OS2EXPORT APIRET16 OS2API16 Dos16GetVersion(PUSHORT pver) OS2APIINFO(92);
OS2EXPORT APIRET16 OS2API16 Dos16GetMachineMode(PBYTE pmode) OS2APIINFO(49);
OS2EXPORT APIRET16 OS2API16 Dos16GetHugeShift(PUSHORT pcount) OS2APIINFO(41);
OS2EXPORT VOID OS2API16 Dos16Exit(USHORT action, USHORT result) OS2APIINFO(5);

typedef struct {
    PID pid;
    TID tid;
    PID pidParent;
} PIDINFO, *PPIDINFO;
OS2EXPORT APIRET16 OS2API16 Dos16GetPID(PPIDINFO ppidinfo) OS2APIINFO(94);


// !!! FIXME: these should probably get sorted alphabetically and/or grouped
// !!! FIXME:  into areas of functionality, but for now, I'm just listing them
// !!! FIXME:  in the order I implemented them to get things running.

OS2EXPORT APIRET OS2API DosGetInfoBlocks(PTIB *pptib, PPIB *pppib) OS2APIINFO(312);
OS2EXPORT APIRET OS2API DosQuerySysInfo(ULONG iStart, ULONG iLast, PVOID pBuf, ULONG cbBuf) OS2APIINFO(348);
OS2EXPORT APIRET OS2API DosQueryModuleName(HMODULE hmod, ULONG cbName, PCHAR pch) OS2APIINFO(320);
OS2EXPORT APIRET OS2API DosScanEnv(PSZ pszName, PSZ *ppszValue) OS2APIINFO(227);
OS2EXPORT APIRET OS2API DosWrite(HFILE hFile, PVOID pBuffer, ULONG cbWrite, PULONG pcbActual) OS2APIINFO(282);
OS2EXPORT VOID OS2API DosExit(ULONG action, ULONG result) OS2APIINFO(234);
OS2EXPORT APIRET OS2API DosExitList(ULONG ordercode, PFNEXITLIST pfn) OS2APIINFO(296);
OS2EXPORT APIRET OS2API DosCreateEventSem(PSZ pszName, PHEV phev, ULONG flAttr, BOOL32 fState) OS2APIINFO(324);
OS2EXPORT APIRET OS2API DosCreateMutexSem(PSZ pszName, PHMTX phmtx, ULONG flAttr, BOOL32 fState) OS2APIINFO(331);
OS2EXPORT APIRET OS2API DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD pERegRec) OS2APIINFO(354);
OS2EXPORT APIRET OS2API DosUnsetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD pERegRec) OS2APIINFO(355);
OS2EXPORT ULONG OS2API DosFlatToSel(VOID) OS2APIINFO(425);
OS2EXPORT APIRET OS2API DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes) OS2APIINFO(378);
OS2EXPORT APIRET OS2API DosSetRelMaxFH(PLONG pcbReqCount, PULONG pcbCurMaxFH) OS2APIINFO(382);
OS2EXPORT APIRET OS2API DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag) OS2APIINFO(299);
OS2EXPORT APIRET OS2API DosSubSetMem(PVOID pbBase, ULONG flag, ULONG cb) OS2APIINFO(344);
OS2EXPORT APIRET OS2API DosSubAllocMem(PVOID pbBase, PPVOID ppb, ULONG cb) OS2APIINFO(345);
OS2EXPORT APIRET OS2API DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr) OS2APIINFO(224);
OS2EXPORT APIRET OS2API DosSetMem(PVOID pb, ULONG cb, ULONG flag) OS2APIINFO(305);
OS2EXPORT APIRET OS2API DosGetDateTime(PDATETIME pdt) OS2APIINFO(230);
OS2EXPORT APIRET OS2API DosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, ULONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2) OS2APIINFO(273);
OS2EXPORT APIRET OS2API DosRequestMutexSem(HMTX hmtx, ULONG ulTimeout) OS2APIINFO(334);
OS2EXPORT APIRET OS2API DosReleaseMutexSem(HMTX hmtx) OS2APIINFO(335);
OS2EXPORT APIRET OS2API DosSetFilePtr(HFILE hFile, LONG ib, ULONG method, PULONG ibActual) OS2APIINFO(256);
OS2EXPORT APIRET OS2API DosRead(HFILE hFile, PVOID pBuffer, ULONG cbRead, PULONG pcbActual) OS2APIINFO(281);
OS2EXPORT APIRET OS2API DosClose(HFILE hFile) OS2APIINFO(257);
OS2EXPORT APIRET OS2API DosEnterMustComplete(PULONG pulNesting) OS2APIINFO(380);
OS2EXPORT APIRET OS2API DosExitMustComplete(PULONG pulNesting) OS2APIINFO(381);
OS2EXPORT APIRET OS2API DosQueryPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf) OS2APIINFO(223);
OS2EXPORT APIRET OS2API DosQueryFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo, ULONG cbInfoBuf) OS2APIINFO(279);
OS2EXPORT APIRET OS2API DosCreateThread(PTID ptid, PFNTHREAD pfn, ULONG param, ULONG flag, ULONG cbStack) OS2APIINFO(311);
OS2EXPORT APIRET OS2API DosExecPgm(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg, PSZ pEnv, PRESULTCODES pRes, PSZ pName) OS2APIINFO(283);
OS2EXPORT APIRET OS2API DosResetEventSem(HEV hev, PULONG pulPostCt) OS2APIINFO(327);
OS2EXPORT APIRET OS2API DosPostEventSem(HEV hev) OS2APIINFO(328);
OS2EXPORT APIRET OS2API DosCloseEventSem(HEV hev) OS2APIINFO(326);
OS2EXPORT APIRET OS2API DosWaitEventSem(HEV hev, ULONG ulTimeout) OS2APIINFO(329);
OS2EXPORT APIRET OS2API DosQueryEventSem(HEV hev, PULONG pulPostCt) OS2APIINFO(330);
OS2EXPORT APIRET OS2API DosFreeMem(PVOID pb) OS2APIINFO(304);
OS2EXPORT APIRET OS2API DosWaitChild(ULONG action, ULONG option, PRESULTCODES pres, PPID ppid, PID pid) OS2APIINFO(280);
OS2EXPORT APIRET OS2API DosWaitThread(PTID ptid, ULONG option) OS2APIINFO(349);
OS2EXPORT APIRET OS2API DosSleep(ULONG msec) OS2APIINFO(229);
OS2EXPORT APIRET OS2API DosSubFreeMem(PVOID pbBase, PVOID pb, ULONG cb) OS2APIINFO(346);
OS2EXPORT APIRET OS2API DosDelete(PSZ pszFile) OS2APIINFO(259);
OS2EXPORT APIRET OS2API DosQueryCurrentDir(ULONG disknum, PBYTE pBuf, PULONG pcbBuf) OS2APIINFO(274);
OS2EXPORT APIRET OS2API DosSetPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf, ULONG flOptions) OS2APIINFO(219);
OS2EXPORT APIRET OS2API DosQueryModuleHandle(PSZ pszModname, PHMODULE phmod) OS2APIINFO(319);
OS2EXPORT APIRET OS2API DosQueryProcAddr(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN* ppfn) OS2APIINFO(321);
OS2EXPORT APIRET OS2API DosQueryCp(ULONG cb, PULONG arCP, PULONG pcCP) OS2APIINFO(291);
OS2EXPORT APIRET OS2API DosOpenL(PSZ pszFileName, PHFILE pHf, PULONG pulAction, LONGLONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2) OS2APIINFO(981);
OS2EXPORT APIRET OS2API DosFindFirst(PSZ pszFileSpec, PHDIR phdir, ULONG flAttribute, PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames, ULONG ulInfoLevel) OS2APIINFO(264);
OS2EXPORT APIRET OS2API DosFindNext(HDIR hDir, PVOID pfindbuf, ULONG cbfindbuf, PULONG pcFilenames) OS2APIINFO(265);
OS2EXPORT APIRET OS2API DosFindClose(HDIR hDir) OS2APIINFO(263);
OS2EXPORT APIRET OS2API DosQueryCurrentDisk(PULONG pdisknum, PULONG plogical) OS2APIINFO(275);
OS2EXPORT APIRET OS2API DosDevConfig(PVOID pdevinfo, ULONG item) OS2APIINFO(231);
OS2EXPORT APIRET OS2API DosLoadModule(PSZ pszName, ULONG cbName, PSZ pszModname, PHMODULE phmod) OS2APIINFO(318);
OS2EXPORT APIRET OS2API DosResetBuffer(HFILE hFile) OS2APIINFO(254);
OS2EXPORT APIRET OS2API DosQueryAppType(PSZ pszName, PULONG pFlags) OS2APIINFO(323);
OS2EXPORT APIRET OS2API DosAllocThreadLocalMemory(ULONG cb, PULONG *p) OS2APIINFO(454);
OS2EXPORT APIRET OS2API DosFreeThreadLocalMemory(ULONG *p) OS2APIINFO(455);
OS2EXPORT APIRET OS2API DosQueryFHState(HFILE hFile, PULONG pMode) OS2APIINFO(276);
OS2EXPORT APIRET OS2API DosQueryExtLIBPATH(PSZ pszExtLIBPATH, ULONG flags) OS2APIINFO(874);
OS2EXPORT APIRET OS2API DosSetMaxFH(ULONG cFH) OS2APIINFO(209);
OS2EXPORT APIRET OS2API DosQueryThreadContext(TID tid, ULONG level, PCONTEXTRECORD pcxt) OS2APIINFO(877);
OS2EXPORT ULONG OS2API DosSelToFlat(VOID) OS2APIINFO(426);
OS2EXPORT APIRET OS2API DosCloseMutexSem(HMTX hmtx) OS2APIINFO(333);
OS2EXPORT APIRET OS2API DosSetProcessCp(ULONG cp) OS2APIINFO(289);
OS2EXPORT APIRET OS2API DosQueryFSAttach(PSZ pszDeviceName, ULONG ulOrdinal, ULONG ulFSAInfoLevel, PFSQBUFFER2 pfsqb, PULONG pcbBuffLength) OS2APIINFO(277);
OS2EXPORT APIRET OS2API DosSetFileSize(HFILE hFile, ULONG cbSize) OS2APIINFO(272);

#ifdef __cplusplus
}
#endif

#endif

// end of doscalls.h ...

