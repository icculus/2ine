/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native.h"
#include "sesmgr.h"

#include "sesmgr-lx.h"

APIRET DosStartSession(PSTARTDATA psd, PULONG pidSession, PPID ppid)
{
    TRACE_NATIVE("DosStartSession(%p, %p, %p)", psd, pidSession, ppid);
printf("'%s', '%s', '%s'\n", psd->PgmTitle, psd->PgmName, psd->IconFile);
    return ERROR_NOT_ENOUGH_MEMORY;
} // DosStartSession

// end of sesmgr.c ...

