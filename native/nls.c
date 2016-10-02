#include "os2native.h"
#include "nls.h"

APIRET DosQueryDBCSEnv(ULONG buflen, PCOUNTRYCODE pcc, PCHAR buf)
{
    // !!! FIXME: implement this for real.
    TRACE_NATIVE("DosQueryDBCSEnv(%u, %p, %p)", (uint) buflen, pcc, buf);
    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;
    memset(buf, '\0', buflen);
    return NO_ERROR;
} // DosQueryDBCSEnv

LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT(DosQueryDBCSEnv, 6)
LX_NATIVE_MODULE_INIT_END()

// end of nls.c ...

