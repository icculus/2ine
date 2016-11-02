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

APIRET DosQueryCtryInfo(ULONG cb, PCOUNTRYCODE pcc, PCOUNTRYINFO pci, PULONG pcbActual)
{
    // From the 4.5 toolkit docs: "If this area is too small to hold all of
    //  the available information, then as much information as possible is
    //  provided in the available space (in the order in which the data would
    //  appear)."  ...so we fill in a local struct and memcpy it at the end.
    COUNTRYINFO ci;
    memset(&ci, '\0', sizeof (ci));

    // From the 4.5 toolkit docs: "If the amount of data returned is not
    //  enough to fill the memory area provided by the caller, then the memory
    //  that is unaltered by the available data is zeroed out." ...so blank
    //  it all out now.
    memset(pci, '\0', cb);

    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;

    // !!! FIXME: this is just what OS/2 Warp 4.52 returns by default for my USA system.
    ci.country = 1;
    ci.codepage = 437;
    ci.fsDateFmt = 0;
    ci.szCurrency[0] = '$';
    ci.szThousandsSeparator[0] = ',';
    ci.szDecimal[0] = '.';
    ci.szDateSeparator[0] = '-';
    ci.szTimeSeparator[0] = ':';
    ci.fsCurrencyFmt = 0;;
    ci.cDecimalPlace = 2;
    ci.fsTimeFmt = 0;
    //USHORT abReserved1[2];
    ci.szDataSeparator[0] = ',';
    //USHORT abReserved2[5];

    if (pcbActual)
        *pcbActual = 38;  // OS/2 Warp 4.52 doesn't count the abReserved[5] at the end.

    return NO_ERROR;
} // DosQueryCtryInfo

LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT(DosQueryDBCSEnv, 6),
    LX_NATIVE_EXPORT(DosQueryCtryInfo, 5),
    LX_NATIVE_EXPORT(DosMapCase, 7)
LX_NATIVE_MODULE_INIT_END()

// end of nls.c ...

