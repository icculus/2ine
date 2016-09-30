#include <unistd.h>

#include "os2native.h"
#include "msg.h"

NATIVE_MODULE(msg);

APIRET DosPutMessage(HFILE handle, ULONG msglen, PCHAR msg)
{
    write(handle, msg, msglen);
    return 0;
} // DosPutMessage

NATIVE_REPLACEMENT_TABLE("msg")
    NATIVE_REPLACEMENT(DosPutMessage, 5)
END_NATIVE_REPLACEMENT_TABLE()

// end of msg.c ...

