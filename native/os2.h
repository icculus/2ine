#ifndef _INCL_OS2_H_
#define _INCL_OS2_H_

// !!! FIXME: the official toolkit headers have all sorts of tapdancing with
//   "#ifdef INCL_DOSPROCESS" and "#define INCL_DOSINCLUDED" and stuff.
// !!! FIXME: naturally, these filenames don't match either, but this isn't
// !!! FIXME:  meant to be a drop-in replacement. At least: not at the moment.

#include "os2types.h"
#include "os2errors.h"
#include "doscalls.h"
#include "kbdcalls.h"
#include "viocalls.h"
#include "quecalls.h"
#include "msg.h"
#include "nls.h"
#include "sesmgr.h"
#include "pmgpi.h"
#include "tcpip32.h"

#endif

// end of os2.h ...
