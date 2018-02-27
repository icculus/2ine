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

// end of kbdcalls.c ...

