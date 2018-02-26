/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMGPI_H_
#define _INCL_PMGPI_H_

#include "os2types.h"
#include "pmwin.h"

#ifdef __cplusplus
extern "C" {
#endif

OS2EXPORT BOOL OS2API GpiQueryTextBox(HPS hps, LONG lCount1, PCH pchString, LONG lCount2, PPOINTL aptlPoints);

#ifdef __cplusplus
}
#endif

#endif

// end of pmgpi.h ...

