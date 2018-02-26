/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "kbdcalls.h"

APIRET16 KbdCharIn(PKBDKEYINFO pkbci, USHORT fWait, HKBD hkbd)
{
    TRACE_NATIVE("KbdCharIn(%p, %u, %u)", pkbci, fWait, hkbd);
    FIXME("this is just enough to survive 'press any key'");
    memset(pkbci, '\0', sizeof (*pkbci));
    pkbci->chChar = getchar();
    return NO_ERROR;
} // kbdCharIn

static APIRET16 bridge16to32_KbdCharIn(uint8 *args)
{
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(HKBD, hkbd);
    LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(USHORT, fWait);
    LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(PKBDKEYINFO, pkbci);
    return KbdCharIn(pkbci, fWait, hkbd);
} // bridge16to32_KbdCharIn

LX_NATIVE_MODULE_16BIT_SUPPORT()
    LX_NATIVE_MODULE_16BIT_API(KbdCharIn)
LX_NATIVE_MODULE_16BIT_SUPPORT_END()

LX_NATIVE_MODULE_DEINIT({
    LX_NATIVE_MODULE_DEINIT_16BIT_SUPPORT();
})

static int initKbdcalls(void)
{
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT()
        LX_NATIVE_INIT_16BIT_BRIDGE(KbdCharIn, 6)
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT_END()
    return 1;
} // initViocalls

LX_NATIVE_MODULE_INIT({ if (!initKbdcalls()) return NULL; })
    LX_NATIVE_EXPORT16(KbdCharIn, 4)
LX_NATIVE_MODULE_INIT_END()

// end of kbdcalls.c ...

