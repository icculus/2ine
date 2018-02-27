/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

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

typedef struct
{
    USHORT yStart;
    USHORT cEnd;
    USHORT cx;
    USHORT attr;
} VIOCURSORINFO, *PVIOCURSORINFO;

OS2EXPORT APIRET16 OS2API16 VioGetMode(PVIOMODEINFO pvioModeInfo, HVIO hvio) OS2APIINFO(21);
OS2EXPORT APIRET16 OS2API16 VioGetCurPos(PUSHORT pusRow, PUSHORT pusColumn, HVIO hvio) OS2APIINFO(9);
OS2EXPORT APIRET16 OS2API16 VioGetBuf(PULONG pLVB, PUSHORT pcbLVB, HVIO hvio) OS2APIINFO(31);
OS2EXPORT APIRET16 OS2API16 VioGetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio) OS2APIINFO(27);
OS2EXPORT APIRET16 OS2API16 VioScrollUp(USHORT usTopRow, USHORT usLeftCol, USHORT usBotRow, USHORT usRightCol, USHORT cbLines, PBYTE pCell, HVIO hvio) OS2APIINFO(7);
OS2EXPORT APIRET16 OS2API16 VioSetCurPos(USHORT usRow, USHORT usColumn, HVIO hvio) OS2APIINFO(15);
OS2EXPORT APIRET16 OS2API16 VioSetCurType(PVIOCURSORINFO pvioCursorInfo, HVIO hvio) OS2APIINFO(32);
OS2EXPORT APIRET16 OS2API16 VioReadCellStr(PCH pchCellStr, PUSHORT pcb, USHORT usRow, USHORT usColumn, HVIO hvio) OS2APIINFO(24);
OS2EXPORT APIRET16 OS2API16 VioWrtCellStr(PCH pchCellStr, USHORT cb, USHORT usRow, USHORT usColumn, HVIO hvio) OS2APIINFO(10);
OS2EXPORT APIRET16 OS2API16 VioWrtCharStrAtt(PCH pch, USHORT cb, USHORT usRow, USHORT usColumn, PBYTE pAttr, HVIO hvio) OS2APIINFO(48);
OS2EXPORT APIRET16 OS2API16 VioWrtNCell(PBYTE pCell, USHORT cb, USHORT usRow, USHORT usColumn, HVIO hvio) OS2APIINFO(52);

#ifdef __cplusplus
}
#endif

#endif

// end of viocalls.h ...

