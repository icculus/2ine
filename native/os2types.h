#ifndef _INCL_OS2TYPES_H_
#define _INCL_OS2TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

#ifndef OS2EXPORT
#define OS2EXPORT __attribute__((visibility("default")))
#endif

#ifndef OS2API
#define OS2API APIENTRY OS2EXPORT
#endif

typedef uint32_t APIRET;
typedef uint16_t APIRET16;
typedef uint32_t APIRET32;

#define VOID void
typedef uint32_t BOOL32;
typedef int32_t LONG, *PLONG;
typedef uint32_t ULONG, *PULONG;
typedef char CHAR, *PCHAR;
typedef PCHAR PSZ;
typedef VOID *PVOID;
typedef PVOID *PPVOID;
typedef uint32_t HMODULE, *PHMODULE;
typedef uint32_t HFILE, *PHFILE;
typedef uint32_t HEV, *PHEV;
typedef uint32_t HMTX, *PHMTX;

#ifdef __cplusplus
}
#endif

#endif

// end of os2types.h ...

