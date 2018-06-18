/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "kbdcalls.h"

#include "kbdcalls-lx.h"

APIRET16 KbdCharIn(PKBDKEYINFO pkbci, USHORT fWait, HKBD hkbd)
{
    TRACE_NATIVE("KbdCharIn(%p, %u, %u)", pkbci, fWait, hkbd);
    FIXME("this is just enough to survive 'press any key'");
    memset(pkbci, '\0', sizeof (*pkbci));
    pkbci->chChar = getchar();
    return NO_ERROR;
} // kbdCharIn

APIRET16 KBDGETSTATUS(PKBDKEYINFO pkbci, HKBD hkbd)
{
    TRACE_NATIVE("KbdGetStatus(%p, %u)", pkbci, hkbd);
    FIXME("stub");
    memset(pkbci, '\0', sizeof (*pkbci));
    return NO_ERROR;
}

APIRET16 KBDSETSTATUS(PKBDKEYINFO pkbci, HKBD hkbd)
{
    TRACE_NATIVE("KbdSetStatus(%p, %u)", pkbci, hkbd);
    FIXME("stub");
    return NO_ERROR;
}

APIRET16 KBDSTRINGIN(PCHAR pch, PSTRINGINBUF pchin, USHORT flag, HKBD hkbd)
{
    TRACE_NATIVE("KbdStringIn(%p, %p, %u, %u)", pch, pchin, flag, hkbd);
    if (!pch) return ERROR_INVALID_PARAMETER;
    int count = 0;
    char chr = getchar();
    while ((chr != '\r') && (chr != '\n') && (count < pchin->cb))
    {
        pch[count++] = chr;
        chr = getchar();
    }
    if (count < (pchin->cb - 1))
    {
        pch[count] = '\r';
    }
    if (pchin) pchin->cchIn = count;
    return NO_ERROR;
}

// end of kbdcalls.c ...

