#ifndef _INCL_OS2NATIVE_H_
#define _INCL_OS2NATIVE_H_

#define _POSIX_C_SOURCE 199309
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>

#include "os2types.h"
#include "os2errors.h"
#include "../lx_loader.h"

#if 0
#define TRACE_NATIVE(...) do { fprintf(stderr, "2INE TRACE [%lu]: ", (unsigned long) pthread_self()); fprintf(stderr, __VA_ARGS__); fprintf(stderr, ";\n"); } while (0)
#else
#define TRACE_NATIVE(...) do {} while (0)
#endif

OS2EXPORT const LxExport * lxNativeModuleInit(LxLoaderState *lx_state, uint32 *lx_num_exports);
void OS2EXPORT lxNativeModuleDeinit(void);

#define LX_NATIVE_MODULE_DEINIT(deinitcode) \
    void lxNativeModuleDeinit(void) { deinitcode; }

#define LX_NATIVE_MODULE_INIT(initcode) \
    const LxExport *lxNativeModuleInit(LxLoaderState *lx_state, uint32 *lx_num_exports) { \
        initcode; \
        static const LxExport lx_native_exports[] = {

#define LX_NATIVE_EXPORT(fn, ord) { ord, #fn, fn, NULL }

#define LX_NATIVE_MODULE_INIT_END() \
    }; \
    *lx_num_exports = sizeof (lx_native_exports) / sizeof (lx_native_exports[0]); \
    return lx_native_exports; \
}

#endif

// end of os2native.h ...

