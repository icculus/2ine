/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "sesmgr.h"

#include "sesmgr-lx.h"

APIRET DosStartSession(PSTARTDATA psd, PULONG pidSession, PPID ppid)
{
    TRACE_NATIVE("DosStartSession(%p, %p, %p)", psd, pidSession, ppid);
printf("'%s', '%s', '%s'\n", psd->PgmTitle, psd->PgmName, psd->IconFile);
    return ERROR_NOT_ENOUGH_MEMORY;
} // DosStartSession

OS2EXPORT APIRET16 OS2API16 Dos16SMSetTitle(PCHAR title)
{
    TRACE_NATIVE("Dos16SMSetTitle(%p)", title);
    return NO_ERROR;
}	

// end of sesmgr.c ...

