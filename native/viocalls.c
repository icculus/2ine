#include "os2native16.h"
#include "viocalls.h"

LX_NATIVE_MODULE_16BIT_SUPPORT()
    LX_NATIVE_MODULE_16BIT_API(VioGetMode)
LX_NATIVE_MODULE_16BIT_SUPPORT_END()

// !!! FIXME:
#undef TRACE_NATIVE
#define TRACE_NATIVE(...) do { if (GLoaderState->trace_native) { fprintf(stderr, "2INE TRACE [%lu]: ", (unsigned long) pthread_self()); fprintf(stderr, __VA_ARGS__); fprintf(stderr, ";\n"); } } while (0)

LX_NATIVE_MODULE_DEINIT({
    LX_NATIVE_MODULE_DEINIT_16BIT_SUPPORT();
})

APIRET16 VioGetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio)
{
    TRACE_NATIVE("VioGetMode(%p, %u)", pvioModeInfo, (uint) hvio);
    FIXME("write me");
    return ERROR_INVALID_PARAMETER;
} // VioGetMode

static APIRET16 bridge16to32_VioGetMode(uint8 *args)
{
    const HVIO hvio = *((HVIO *) args); args += 2;
    PVIOMODEINFO pvmi = (PVIOMODEINFO) GLoaderState->convert1616to32(*((uint32*) args)); //args += 4;
    return VioGetMode(pvmi, hvio);
} // bridge16to32_VioGetMode

static int initViocalls(LxLoaderState *lx_state)
{
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT()
        LX_NATIVE_INIT_16BIT_BRIDGE(VioGetMode, 6)
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT_END()
    return 1;
} // initViocalls

LX_NATIVE_MODULE_INIT({ if (!initViocalls(lx_state)) return NULL; })
    LX_NATIVE_EXPORT16(VioGetMode, 21)
LX_NATIVE_MODULE_INIT_END()

// end of viocalls.c ...

