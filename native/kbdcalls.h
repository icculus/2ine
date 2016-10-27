#ifndef _INCL_KBDCALLS_H_
#define _INCL_KBDCALLS_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 2)
typedef struct
{
    UCHAR chChar;
    UCHAR chScan;
    UCHAR fbStatus;
    UCHAR bNlsShift;
    USHORT fsState;
    ULONG time;
} KBDKEYINFO, *PKBDKEYINFO;
#pragma pack(pop)

APIRET16 OS2API16 KbdCharIn(PKBDKEYINFO pkbci, USHORT fWait, HKBD hkbd);

#ifdef __cplusplus
}
#endif

#endif

// end of kbdcalls.h ...

