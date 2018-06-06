/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "msg.h"

#include <unistd.h>

#include "msg-lx.h"

APIRET DosPutMessage(HFILE handle, ULONG msglen, PCHAR msg)
{
    TRACE_NATIVE("DosPutMessage(%u, %u, %p)", (uint) handle, (uint) msglen, msg);
    FIXME("really implement this");
    write(handle, msg, msglen);
    return NO_ERROR;
} // DosPutMessage

APIRET16 Dos16PutMessage(USHORT handle, USHORT msglen, PCHAR msg)
{
    TRACE_NATIVE("Dos16PutMessage(%u, %u, %p)", (uint) handle, (uint) msglen, msg);
    FIXME("really implement this");
    write(handle, msg, msglen);
    return NO_ERROR;
} // Dos16PutMessage

// end of msg.c ...

