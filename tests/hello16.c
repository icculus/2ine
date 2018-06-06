// wcc test16.c -bt=os2 -fo=.obj -zq -od -ms -i="C:\WATCOM\h\os21x"
// wlink name test16 sys os2 op q file test16.obj

#define INCL_DOS
#include <os2.h>

int main(void)
{
    DosPutMessage(1, 32, "Hello from a 16-bit OS/2 .exe!\r\n");
    return 0;
}

// end of hello16.c ...

