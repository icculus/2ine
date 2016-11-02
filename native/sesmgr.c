#include "os2native.h"
#include "sesmgr.h"

LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT(DosStartSession, 37)
LX_NATIVE_MODULE_INIT_END()

APIRET DosStartSession(PSTARTDATA psd, PULONG pidSession, PPID ppid)
{
    TRACE_NATIVE("DosStartSession(%p, %p, %p)", psd, pidSession, ppid);
printf("'%s', '%s', '%s'\n", psd->PgmTitle, psd->PgmName, psd->IconFile);
    return ERROR_NOT_ENOUGH_MEMORY;
} // DosStartSession

// end of sesmgr.c ...

