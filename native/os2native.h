/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_OS2NATIVE_H_
#define _INCL_OS2NATIVE_H_

/* Unless forced off, build in support for OS/2 binaries (the LX export tables, 16-bit bridge code, etc) */
#ifndef LX_LEGACY
#define LX_LEGACY 1
#endif

#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 199309
#define _DEFAULT_SOURCE
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
#include "../lib2ine.h"

#if 1
#define TRACE_NATIVE(...) do { if (GLoaderState.trace_native) { fprintf(stderr, "2INE TRACE [%lu]: ", (unsigned long) pthread_self()); fprintf(stderr, __VA_ARGS__); fprintf(stderr, ";\n"); } } while (0)
#else
#define TRACE_NATIVE(...) do {} while (0)
#endif

#if 1
#define TRACE_EVENT(...) do { if (GLoaderState.trace_events) { fprintf(stderr, "2INE TRACE [%lu]: ", (unsigned long) pthread_self()); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)
#else
#define TRACE_EVENT(...) do {} while (0)
#endif

OS2EXPORT const LxExport * lxNativeModuleInit(uint32 *lx_num_exports);
OS2EXPORT void lxNativeModuleDeinit(void);

#define LX_NATIVE_MODULE_DEINIT(deinitcode) \
    void lxNativeModuleDeinit(void) { \
        deinitcode; \
    }

#define LX_NATIVE_MODULE_INIT(initcode) \
    const LxExport *lxNativeModuleInit(uint32 *lx_num_exports) { \
        initcode; \
        static const LxExport lx_native_exports[] = {

#define LX_NATIVE_EXPORT(fn, ord) { ord, #fn, fn, NULL }
#define LX_NATIVE_EXPORT_DIFFERENT_NAME(fn, fnname, ord) { ord, fnname, fn, NULL }

#define LX_NATIVE_MODULE_INIT_END() \
    }; \
    *lx_num_exports = sizeof (lx_native_exports) / sizeof (lx_native_exports[0]); \
    return lx_native_exports; \
}

// we store the TIB2 struct right after the TIB struct on the stack,
//  so get the TIB, then step forward to the TIB2.
#define LX_GETTIB() ((LxTIB *) GLoaderState.getOs2Tib())
#define LX_GETTIB2() ((LxTIB2 *) (LX_GETTIB() + 1))
#define LX_GETPOSTTIB() ((LxPostTIB *) (LX_GETTIB2() + 1))

#endif

// end of os2native.h ...

