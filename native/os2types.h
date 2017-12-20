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
#define OS2API APIENTRY
#endif

// Note that we never export actual 16-bit APIs from these headers, even if
//  we have them marked as OS2API16. This is just for documentation's sake at
//  the moment. If you compile against these headers and link against an
//  OS2API16 function, they will be exist and operate in a linear address
//  space. If you call them from an LX executable, though, the system assumes
//  you're calling in from 16-bit code and exports something that cleans up
//  the details behind the scenes.
// For developing against this API directly on Linux, you should assume this
//  works like OS/2's PowerPC port, and the same 16-bit APIs you used on x86
//  were made 32-bit clean by default for the new platform.
#ifndef OS2API16
#define OS2API16 OS2API
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
typedef int64_t LONGLONG, *PLONGLONG;
typedef uint64_t ULONGLONG, *PULONGLONG;
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
typedef HANDLE HDIR, *PHDIR;
typedef HANDLE HEV, *PHEV;
typedef HANDLE HMTX, *PHMTX;
typedef HANDLE HQUEUE, *PHQUEUE;
typedef HANDLE PID, *PPID;
typedef HANDLE TID, *PTID;
typedef SHANDLE HVIO, *PHVIO;
typedef SHANDLE HKBD, *PHKBD;
typedef PCHAR PSZ;
typedef PCHAR PCH;

typedef int (APIENTRY *PFN)(void);

#ifdef __cplusplus
}
#endif

#endif

// end of os2types.h ...

