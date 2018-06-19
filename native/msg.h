/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_MSG_H_
#define _INCL_MSG_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C" {
#endif

OS2EXPORT APIRET16 OS2API16 Dos16PutMessage(USHORT hfile, USHORT cbMsg, PCHAR pBuf) OS2APIINFO(ord=1,name=DOSPUTMESSAGE);
OS2EXPORT APIRET16 OS2API16 Dos16TrueGetMessage(PVOID pTable, USHORT cTable, PCHAR pData, USHORT cbBuf, USHORT msgnum, PCHAR pFilename, PUSHORT pcbMsg, PVOID msgseg) OS2APIINFO(ord=2,name=DOSTRUEGETMESSAGE);
OS2EXPORT APIRET OS2API DosPutMessage(HFILE hfile, ULONG cbMsg, PCHAR pBuf) OS2APIINFO(5);

#ifdef __cplusplus
}
#endif

#endif

// end of msg.h ...

