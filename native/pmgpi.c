/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native.h"
#include "pmgpi.h"

#include "pmgpi-lx.h"

BOOL GpiQueryTextBox(HPS hps, LONG lCount1, PCH pchString, LONG lCount2, PPOINTL aptlPoints)
{
    TRACE_NATIVE("GpiQueryTextBox(%u, %d, '%s', %d, %p)", (unsigned int) hps, (int) lCount1, pchString, (int) lCount2, aptlPoints);
    FIXME("write me");
    return 0;
} // GpiQueryTextBox

// end of pmgpi.c ...

