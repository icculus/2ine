#include "os2native.h"
#include "pmgpi.h"

BOOL GpiQueryTextBox(HPS hps, LONG lCount1, PCH pchString, LONG lCount2, PPOINTL aptlPoints)
{
    TRACE_NATIVE("GpiQueryTextBox(%u, %d, '%s', %d, %p)", (unsigned int) hps, (int) lCount1, pchString, (int) lCount2, aptlPoints);
    FIXME("write me");
    return 0;
} // GpiQueryTextBox

LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT(GpiQueryTextBox, 489)
LX_NATIVE_MODULE_INIT_END()

// end of pmgpi.c ...

