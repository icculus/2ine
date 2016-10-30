#include "os2native.h"
#include "quecalls.h"


APIRET DosCreateQueue(PHQUEUE phq, ULONG priority, PSZ pszName)
{
    TRACE_NATIVE("DosCreateQueue(%p, %u, '%s')", phq, priority, pszName);
    FIXME("write me");
    return ERROR_QUE_NO_MEMORY;
} // DosCreateQueue

APIRET DosCloseQueue(HQUEUE hq)
{
    TRACE_NATIVE("DosCloseQueue(%u)", (uint) hq);
    FIXME("write me");
    return ERROR_QUE_INVALID_HANDLE;
} // DosCloseQueue

APIRET DosOpenQueue(PPID ppid, PHQUEUE phq, PSZ pszName)
{
    TRACE_NATIVE("DosOpenQueue(%p, %p, '%s')", ppid, phq, pszName);
    FIXME("write me");
    return ERROR_QUE_NO_MEMORY;
} // DosOpenQueue

APIRET DosPeekQueue(HQUEUE hq, PREQUESTDATA pRequest, PULONG pcbData, PPVOID ppbuf, PULONG element, BOOL32 nowait, PBYTE ppriority, HEV hsem)
{
    TRACE_NATIVE("DosPeekQueue(%u, %p, %p, %p, %p, %u, %p, %u)", (uint) hq, pRequest, pcbData, ppbuf, element, (uint) nowait, ppriority, (uint) hsem);
    FIXME("write me");
    return ERROR_QUE_INVALID_HANDLE;
} // DosPeekQueue

APIRET DosPurgeQueue(HQUEUE hq)
{
    TRACE_NATIVE("DosPurgeQueue(%u)", (uint) hq);
    FIXME("write me");
    return ERROR_QUE_INVALID_HANDLE;
} // DosCloseQueue

APIRET DosQueryQueue(HQUEUE hq, PULONG pcbEntries)
{
    TRACE_NATIVE("DosQueryQueue(%u, %p)", (uint) hq, pcbEntries);
    FIXME("write me");
    return ERROR_QUE_INVALID_HANDLE;
} // DosQueryQueue

APIRET DosReadQueue(HQUEUE hq, PREQUESTDATA pRequest, PULONG pcbData, PPVOID ppbuf, ULONG element, BOOL32 wait, PBYTE ppriority, HEV hsem)
{
    TRACE_NATIVE("DosReadQueue(%u, %p, %p, %p, %u, %u, %p, %u)", (uint) hq, pRequest, pcbData, ppbuf, (uint) element, (uint) wait, ppriority, (uint) hsem);
    FIXME("write me");
    return ERROR_QUE_INVALID_HANDLE;
} // DosReadQueue

APIRET DosWriteQueue(HQUEUE hq, ULONG request, ULONG cbData, PVOID pbData, ULONG priority)
{
    TRACE_NATIVE("DosWriteQueue(%u)", (uint) hq);
    FIXME("write me");
    return ERROR_QUE_INVALID_HANDLE;
} // DosWriteQueue


LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT(DosReadQueue, 9),
    LX_NATIVE_EXPORT(DosPurgeQueue, 10),
    LX_NATIVE_EXPORT(DosCloseQueue, 11),
    LX_NATIVE_EXPORT(DosQueryQueue, 12),
    LX_NATIVE_EXPORT(DosPeekQueue, 13),
    LX_NATIVE_EXPORT(DosWriteQueue, 14),
    LX_NATIVE_EXPORT(DosOpenQueue, 15),
    LX_NATIVE_EXPORT(DosCreateQueue, 16)
LX_NATIVE_MODULE_INIT_END()

// end of quecalls.c ...

