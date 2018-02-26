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

OS2EXPORT APIRET OS2API DosPutMessage(HFILE hfile, ULONG cbMsg, PCHAR pBuf);

#ifdef __cplusplus
}
#endif

#endif

// end of msg.h ...

