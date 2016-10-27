#ifndef _INCL_VIOCALLS_H_
#define _INCL_VIOCALLS_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct
{
    USHORT cb;
    UCHAR fbType;
    UCHAR color;
    USHORT col;
    USHORT row;
    USHORT hres;
    USHORT vres;
    UCHAR fmt_ID;
    UCHAR attrib;
    ULONG buf_addr;
    ULONG buf_length;
    ULONG full_length;
    ULONG partial_length;
    PCHAR ext_data_addr;
} VIOMODEINFO, *PVIOMODEINFO;
#pragma pack(pop)

enum
{
    VGMT_OTHER = 0x1,
    VGMT_GRAPHICS = 0x02,
    VGMT_DISABLEBURST = 0x04
};

APIRET16 OS2API16 VioGetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio);
APIRET16 OS2API16 VioGetCurPos(PUSHORT pusRow, PUSHORT pusColumn, HVIO hvio);

#ifdef __cplusplus
}
#endif

#endif

// end of viocalls.h ...

