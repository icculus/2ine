#ifndef _INCL_MSG_H_
#define _INCL_MSG_H_

#include "os2types.h"

#ifdef __cplusplus
extern "C" {
#endif

APIRET OS2API DosPutMessage(HFILE hfile, ULONG cbMsg, PCHAR pBuf);

#ifdef __cplusplus
}
#endif

#endif

// end of msg.h ...

