/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "msg.h"

#include <unistd.h>

#include "msg-lx.h"

APIRET DosPutMessage(HFILE handle, ULONG msglen, PCHAR msg)
{
    TRACE_NATIVE("DosPutMessage(%u, %u, %p)", (uint) handle, (uint) msglen, msg);
    FIXME("really implement this");
    write(handle, msg, msglen);
    return NO_ERROR;
} // DosPutMessage

APIRET16 Dos16PutMessage(USHORT handle, USHORT msglen, PCHAR msg)
{
    TRACE_NATIVE("Dos16PutMessage(%u, %u, %p)", (uint) handle, (uint) msglen, msg);
    FIXME("really implement this");
    write(handle, msg, msglen);
    return NO_ERROR;
} // Dos16PutMessage

APIRET16 Dos16TrueGetMessage(PVOID pTable, USHORT cTable, PCHAR pData, USHORT cbBuf, USHORT msgnum, PCHAR pFilename, PUSHORT pcbMsg, PVOID msgseg)
{
    TRACE_NATIVE("Dos16TrueGetMessage(%p, %u, %p, %u, %u, %p, %p, %p)", pTable, cTable, pData, cbBuf, msgnum, pFilename, pcbMsg, msgseg);
    void *msgdat = msgseg;
    int offset = *(uint16_t *)(msgdat + 14);
    offset = *(uint16_t *)(msgdat + offset + 4);
    int count = *(uint16_t *)(msgdat + offset + 3);
    uint16_t *msgarr = (uint16_t *)(msgdat + offset + 5);
    int i, j = 0;
    for (i = 0; i < count; i++)
    {
        if(*(uint16_t *)(msgdat + msgarr[i]) == msgnum)
            break;
    }
    if (i == count) return ERROR_MR_MID_NOT_FOUND;
    int msglen = *(PUSHORT)(msgdat + msgarr[i] + 2);
    char *substr, *msg = (char *)(msgdat + msgarr[i] + 4);
    int sub;
    if (msg[0] == 'E')
    {
        j = 8;
        if (cbBuf > 8)
            sprintf(pData, "SYS%04d:", msgnum);
    }
    for (i = 1; (i < msglen) && (j < cbBuf); i++, j++)
    {
        if (msg[i] == '%')
        {
            sub = msg[++i] - '1';
            if ((sub < 0) || (sub >= cTable)) return ERROR_MR_INV_MSGF_FORMAT;
            substr = GLoaderState.convert1616to32(((uint32_t *)pTable)[sub]);
            strcpy(&pData[j], substr);
            j += strlen(substr) - 1;
        }
        else
            pData[j] = msg[i];
    }
    *pcbMsg = j;
    return NO_ERROR;
}

APIRET DosTrueGetMessage(PVOID msgseg, PVOID pTable, ULONG cTable, PCHAR pData, ULONG cbBuf, ULONG msgnum, PCHAR pFilename, PULONG pcbMsg)
{
    TRACE_NATIVE("DosTrueGetMessage(%p, %p, %u, %p, %u, %u, '%s', %p)", msgseg, pTable, cTable, pData, cbBuf, msgnum, pFilename, pcbMsg);
    FIXME("really implement this");
    const char *msgstr = "SYSXXX: DosTrueGetMessage is not yet implemented.";
    ULONG rc = 0;
    if (pData) {
        rc = (ULONG) snprintf(pData, cbBuf, "%s", msgstr);
        if (rc >= cbBuf) {
            rc = cbBuf;
        }
    }
    if (pcbMsg) *pcbMsg = rc;
    return ERROR_MR_MID_NOT_FOUND;
}

// end of msg.c ...

