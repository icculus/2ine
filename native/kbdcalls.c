#include "os2native16.h"
#include "kbdcalls.h"

APIRET16 KbdCharIn(PKBDKEYINFO pkbci, USHORT fWait, HKBD hkbd)
{
    TRACE_NATIVE("KbdCharIn(%p, %u, %u)", pkbci, fWait, hkbd);
    FIXME("this is just enough to survive 'press any key'");
    getchar();
    return NO_ERROR;
} // kbdCharIn

static APIRET16 bridge16to32_KbdCharIn(uint8 *args)
{
    const HKBD hkbd = *((HKBD *) args); args += 2;
    const USHORT fWait = *((USHORT *) args); args += 2;
    //PKBDKEYINFO pkbci = (PKBDKEYINFO) GLoaderState->convert1616to32(*((uint32*) args)); //args += 4;
    return KbdCharIn(NULL/*FIXMEpkbci*/, fWait, hkbd);
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
        LX_NATIVE_INIT_16BIT_BRIDGE(KbdCharIn, 8)
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT_END()
    return 1;
} // initViocalls

LX_NATIVE_MODULE_INIT({ if (!initKbdcalls()) return NULL; })
    LX_NATIVE_EXPORT16(KbdCharIn, 4)
LX_NATIVE_MODULE_INIT_END()

// end of kbdcalls.c ...

