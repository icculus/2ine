#define INCL_DOS
#define INCL_DOSERRORS
#include <stdio.h>
#include <stdlib.h>
#include <os2.h>

static PULONG GAddr = NULL;
static ULONG x = 0;

static void APIENTRY threadFunc(ULONG arg)
{
    const unsigned int t = (unsigned int) arg;
    DosSleep(t == 1 ? 500 : 1000);
    *GAddr = t * 100;
    //printf("thread %u set TLS value to %u\n", t, t * 100);
    //fflush(stdout);
    DosSleep(t == 1 ? 1000 : 50);
    x = *GAddr;
    //printf("thread %u sees TLS value of %u\n", t, (unsigned int) *GAddr);
    //fflush(stdout);
}

int main(void)
{
    TID tid = 0;

    if (DosAllocThreadLocalMemory(1, &GAddr) != NO_ERROR) {
        fprintf(stderr, "DosAllocLocalThreadMemory failed\n");
        return 1;
    }
    printf("the magic address is %p\n", GAddr);
    *GAddr = 10;
    printf("Main thread set TLS value to 10\n");
    fflush(stdout);

    if (DosCreateThread(&tid, threadFunc, 1, 0, 0xFFFF) != NO_ERROR) {
        fprintf(stderr, "DosCreateThread 1 failed\n");
        return 1;
    }
    if (DosCreateThread(&tid, threadFunc, 2, 0, 0xFFFF) != NO_ERROR) {
        fprintf(stderr, "DosCreateThread 2 failed\n");
        return 1;
    }

    DosSleep(4000);

    printf("Main thread sees TLS value of %u\n", (unsigned int) *GAddr);
    fflush(stdout);

    DosFreeThreadLocalMemory(GAddr);

    return 0;
}

