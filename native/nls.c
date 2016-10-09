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

APIRET DosMapCase(ULONG cb, PCOUNTRYCODE pcc, PCHAR pch)
{
    // !!! FIXME: implement this for real.
    TRACE_NATIVE("DosMapCase(%u, %p, %p)", (uint) cb, pcc, pch);
    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;

    for (ULONG i = 0; i < cb; i++) {
        const CHAR ch = *pch;
        *(pch++) = ((ch >= 'A') && (ch <= 'Z')) ? ch - ('A' - 'a') : ch;
    } // for

    return NO_ERROR;
} // DosMapCase

LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT(DosQueryDBCSEnv, 6),
    LX_NATIVE_EXPORT(DosMapCase, 7)
LX_NATIVE_MODULE_INIT_END()

// end of nls.c ...

