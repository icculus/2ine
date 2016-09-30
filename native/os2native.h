#ifndef _INCL_NATIVE_H_
#define _INCL_NATIVE_H_

#include <stdint.h>
#include <stdlib.h>

#include "os2types.h"
#include "os2errors.h"
#include "../lx_loader.h"

#if 1
#define TRACE_NATIVE(...) do { printf("TRACE: "); printf(__VA_ARGS__); printf(";\n"); } while (0)
#else
#define TRACE_NATIVE(...) do {} while (0)
#endif

// !!! FIXME: this is nasty for several reasons.
#define NATIVE_MODULE(modname) \
    static LxLoaderState *GLoaderState = NULL

#define NATIVE_REPLACEMENT_TABLE(modname) \
    LxModule *loadNativeLxModule(LxLoaderState *state) { \
        GLoaderState = state; \
        LxModule *retval = (LxModule *) malloc(sizeof (LxModule)); \
        if (!retval) goto loadnative_failed; \
        memset(retval, '\0', sizeof (LxModule)); \
        retval->refcount = 1; \
        strcpy(retval->name, modname); \

#define NATIVE_REPLACEMENT(fn, ord) { \
        void *ptr = realloc(retval->exported_names, (retval->num_names+1) * sizeof (LxExportedName)); \
        if (!ptr) { goto loadnative_failed; } \
        retval->exported_names = (LxExportedName *) ptr; \
        strcpy(retval->exported_names[retval->num_names].name, #fn); \
        retval->exported_names[retval->num_names].addr = (uint32) ((size_t) fn); \
        retval->num_names++; \
        ptr = realloc(retval->exported_ordinals, (retval->num_ordinals+1) * sizeof (LxExportedOrdinal)); \
        if (!ptr) { goto loadnative_failed; } \
        retval->exported_ordinals = (LxExportedOrdinal *) ptr; \
        retval->exported_ordinals[retval->num_ordinals].ordinal = ord; \
        retval->exported_ordinals[retval->num_ordinals].addr = (uint32) ((size_t) fn); \
        retval->num_ordinals++; \
        }

#define END_NATIVE_REPLACEMENT_TABLE() \
        return retval; \
        \
    loadnative_failed: \
        if (retval) { \
            free(retval->exported_ordinals); \
            free(retval->exported_names); \
            free(retval); \
        } \
        return NULL; \
    }

#endif

// end of native.h ...

