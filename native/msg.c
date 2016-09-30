#include "native.h"

NATIVE_MODULE(msg);

static uint32 DosPutMessage(uint32 handle, uint32 msglen, const char *msg)
{
    // !!! FIXME: this isn't right, but good enough for now.
    fwrite(msg, msglen, 1, stdout);
    fflush(stdout);
    return 0;
} // DosPutMessage

NATIVE_REPLACEMENT_TABLE("msg")
    NATIVE_REPLACEMENT(DosPutMessage, 5)
END_NATIVE_REPLACEMENT_TABLE()

// end of msg.c ...

