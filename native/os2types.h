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

typedef void VOID;
typedef VOID *PVOID;
typedef PVOID *PPVOID;
typedef char CHAR, *PCHAR;
typedef uint8_t UCHAR, *PUCHAR;
typedef int16_t SHORT, *PSHORT;
typedef uint16_t USHORT, *PUSHORT;
typedef int32_t LONG, *PLONG;
typedef uint32_t ULONG, *PULONG;
typedef uint32_t BOOL32, *PBOOL32;
typedef uint8_t BYTE, *PBYTE;

// !!! FIXME: HANDLE should either be 64-bits on x86_64, or we need to
// !!! FIXME:  refactor and mutex a bunch of stuff...if it has to be a 32-bit
// !!! FIXME:  int, it needs to be an index into a resizable array, but at the
// !!! FIXME:  native word size, these handles can just be pointers cast to ints.
typedef uint16_t SHANDLE, *PSHANDLE;
typedef uint32_t LHANDLE, *PLHANDLE;
typedef LHANDLE HANDLE, *PHANDLE;
typedef HANDLE HMODULE, *PHMODULE;
typedef HANDLE HFILE, *PHFILE;
typedef HANDLE HEV, *PHEV;
typedef HANDLE HMTX, *PHMTX;
typedef HANDLE PID, *PPID;
typedef HANDLE TID, *PTID;
typedef PCHAR PSZ;

#ifdef __cplusplus
}
#endif

#endif

// end of os2types.h ...

