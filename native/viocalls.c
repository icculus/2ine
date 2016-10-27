#include "os2native16.h"
#include "viocalls.h"

APIRET16 VioGetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio)
{
    TRACE_NATIVE("VioGetMode(%p, %u)", pvioModeInfo, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.
    else if (pvioModeInfo == NULL)
        return ERROR_VIO_INVALID_PARMS;
    else if (pvioModeInfo->cb != sizeof (*pvioModeInfo))
        return ERROR_VIO_INVALID_LENGTH;

    memset(pvioModeInfo, '\0', sizeof (*pvioModeInfo));
    pvioModeInfo->cb = sizeof (*pvioModeInfo);
    pvioModeInfo->fbType = VGMT_OTHER;
    pvioModeInfo->color = 8;  // bits?
    pvioModeInfo->col = 80;
    pvioModeInfo->row = 25;
    FIXME("I don't know what most of these fields do");
    //pvioModeInfo->hres = 640;
    //pvioModeInfo->vres = 480;
    //UCHAR fmt_ID;
    //UCHAR attrib;
    //ULONG buf_addr;
    //ULONG buf_length;
    //ULONG full_length;
    //ULONG partial_length;
    //PCHAR ext_data_addr;

    return NO_ERROR;
} // VioGetMode

static APIRET16 bridge16to32_VioGetMode(uint8 *args)
{
    const HVIO hvio = *((HVIO *) args); args += 2;
    PVIOMODEINFO pvmi = (PVIOMODEINFO) GLoaderState->convert1616to32(*((uint32*) args)); //args += 4;
    return VioGetMode(pvmi, hvio);
} // bridge16to32_VioGetMode


APIRET16 VioGetCurPos(PUSHORT pusRow, PUSHORT pusColumn, HVIO hvio)
{
    TRACE_NATIVE("VioGetCurPos(%p, %p, %u)", pusRow, pusColumn, (uint) hvio);

    if (hvio != 0)
        return ERROR_VIO_INVALID_HANDLE;  // !!! FIXME: can be non-zero when VioCreatePS() is implemented.

    FIXME("write me");
    if (pusRow)
        *pusRow = 0;
    if (pusColumn)
        *pusColumn = 0;

    return NO_ERROR;
} // VioGetCurPos

static APIRET16 bridge16to32_VioGetCurPos(uint8 *args)
{
    const HVIO hvio = *((HVIO *) args); args += 2;
    PUSHORT pusRow = GLoaderState->convert1616to32(*((uint32*) args)); args += 4;
    PUSHORT pusColumn = GLoaderState->convert1616to32(*((uint32*) args)); args += 4;
    return VioGetCurPos(pusRow, pusColumn, hvio);
} // bridge16to32_VioGetCurPos



LX_NATIVE_MODULE_16BIT_SUPPORT()
    LX_NATIVE_MODULE_16BIT_API(VioGetCurPos)
    LX_NATIVE_MODULE_16BIT_API(VioGetMode)
LX_NATIVE_MODULE_16BIT_SUPPORT_END()

LX_NATIVE_MODULE_DEINIT({
    LX_NATIVE_MODULE_DEINIT_16BIT_SUPPORT();
})

static int initViocalls(void)
{
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT()
        LX_NATIVE_INIT_16BIT_BRIDGE(VioGetCurPos, 6)
        LX_NATIVE_INIT_16BIT_BRIDGE(VioGetMode, 6)
    LX_NATIVE_MODULE_INIT_16BIT_SUPPORT_END()
    return 1;
} // initViocalls

LX_NATIVE_MODULE_INIT({ if (!initViocalls()) return NULL; })
    LX_NATIVE_EXPORT16(VioGetCurPos, 9),
    LX_NATIVE_EXPORT16(VioGetMode, 21)
LX_NATIVE_MODULE_INIT_END()

// end of viocalls.c ...

