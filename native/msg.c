#include <unistd.h>

#include "os2native.h"
#include "msg.h"

APIRET DosPutMessage(HFILE handle, ULONG msglen, PCHAR msg)
{
    write(handle, msg, msglen);
    return 0;
} // DosPutMessage

LX_NATIVE_MODULE_INIT()
    LX_NATIVE_EXPORT(DosPutMessage, 5)
LX_NATIVE_MODULE_INIT_END()

// end of msg.c ...

