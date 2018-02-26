/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_QUECALLS_H_
#define _INCL_QUECALLS_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    PID pid;
    ULONG ulData;
} REQUESTDATA, *PREQUESTDATA;

enum
{
    QUE_FIFO = 0,
    QUE_LIFO = 1,
    QUE_PRIORITY = 2,
    QUE_NOCONVERT_ADDRESS = 0,
    QUE_CONVERT_ADDRESS = 4
};

OS2EXPORT APIRET OS2API DosCreateQueue(PHQUEUE phq, ULONG priority, PSZ pszName);
OS2EXPORT APIRET OS2API DosCloseQueue(HQUEUE hq);
OS2EXPORT APIRET OS2API DosOpenQueue(PPID ppid, PHQUEUE phq, PSZ pszName);
OS2EXPORT APIRET OS2API DosPeekQueue(HQUEUE hq, PREQUESTDATA pRequest, PULONG pcbData, PPVOID ppbuf, PULONG element, BOOL32 nowait, PBYTE ppriority, HEV hsem);
OS2EXPORT APIRET OS2API DosPurgeQueue(HQUEUE hq);
OS2EXPORT APIRET OS2API DosQueryQueue(HQUEUE hq, PULONG pcbEntries);
OS2EXPORT APIRET OS2API DosReadQueue(HQUEUE hq, PREQUESTDATA pRequest, PULONG pcbData, PPVOID ppbuf, ULONG element, BOOL32 wait, PBYTE ppriority, HEV hsem);
OS2EXPORT APIRET OS2API DosWriteQueue(HQUEUE hq, ULONG request, ULONG cbData, PVOID pbData, ULONG priority);

#ifdef __cplusplus
}
#endif

#endif

// end of quecalls.h ...

