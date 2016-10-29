#ifndef _INCL_OS2NATIVE16_H_
#define _INCL_OS2NATIVE16_H_

#include "os2native.h"

// !!! FIXME: _lots_ of macro salsa in here.

#define LX_NATIVE_MODULE_16BIT_SUPPORT() \
    static LxMmaps obj16;

// These are the 16-bit entry points, which are exported to the LX loader
//  (not when linking directly to this shared library).
#define LX_NATIVE_MODULE_16BIT_API(fn) \
    static void *fn##16 = NULL;

#define LX_NATIVE_MODULE_16BIT_SUPPORT_END()

#define LX_NATIVE_MODULE_DEINIT_16BIT_SUPPORT() \
    if ((obj16.alias != 0xFFFF) && (GLoaderState)) { \
        GLoaderState->freeSelector(obj16.alias); \
    } \
    if (obj16.mapped != NULL) { \
        munmap(obj16.mapped, obj16.size); \
    } \
    obj16.mapped = obj16.addr = NULL; \
    obj16.size = 0; \
    obj16.alias = 0xFFFF; \

#define LX_NATIVE_MODULE_INIT_16BIT_SUPPORT() \
    obj16.mapped = obj16.addr = NULL; \
    obj16.size = 0; \
    obj16.alias = 0xFFFF; \
    \
    const size_t vsize = 0x10000 * 2; \
    void *mmapaddr = mmap(NULL, vsize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0); \
    if (mmapaddr == ((void *) MAP_FAILED)) { \
        fprintf(stderr, "mmap(NULL, 0x20000, RW-, ANON|PRIVATE, -1, 0) failed (%d): %s\n", errno, strerror(errno)); \
        return 0; \
    } \
    \
    obj16.mapped = mmapaddr; \
    obj16.size = vsize; \
    \
    /* force objects with a 16:16 alias to a 64k boundary. */ \
    size_t adjust = (size_t) mmapaddr; \
    if ((adjust % 0x10000) != 0) { \
        const size_t diff = 0x10000 - (adjust % 0x10000); \
        adjust += diff; \
        mmapaddr = (void *) adjust; \
    } \
    obj16.addr = mmapaddr; \
    \
    uint16 offset = 0; \
    if (!GLoaderState->findSelector((uint32) obj16.addr, &obj16.alias, &offset)) { \
        fprintf(stderr, "couldn't find a selector for 16-bit entry points!\n"); \
        munmap(obj16.mapped, vsize); \
        obj16.mapped = obj16.addr = NULL; \
        obj16.size = 0; \
        obj16.alias = 0xFFFF; \
        return 0; \
    } \
    assert(offset == 0); \
    \
    uint8 *ptr = (uint8 *) mmapaddr;


/* This is the original assembly code, for use with NASM.
USE16  ; start in 16-bit code where our 16-bit caller lands.
MOV BX, SS ; save off ss:sp
SHL EBX, 16  ; scootch over!
MOV BX, SP  ; save off ss:sp

; our stack is tiled, so we can easily find the linear address of it.
MOV AX, SS  ; move the stack segment into the low word.
SHR AX, 3  ; shift out the control bits from the segment.
SHL EAX, 16  ; move the remaining selector bits to the high word of %eax
MOV AX, SP  ; move the stack offset into the low word. Now %eax has the linear stack pointer.

JMP DWORD 0x7788:0x33332222  ; jmp into the 32-bit code segment (the next instruction!).

USE32
MOV CX, 0xABCD  ; original linear stack segment from lx_loader's main().
MOV SS, CX
MOV ESP, EAX  ; and the same stack pointer, but linear.
ADD EAX, 4  ; %eax now points to original function arguments on the stack.

PUSH EBX  ; save original ss:sp to stack.
PUSH DS  ; save off the caller's data segment.
MOV CX, 0x8888  ; restore our linear data segment.
MOV DS, CX

PUSH EAX  ; make this the sole argument to the bridge function.
MOV EAX, 0x55555555  ; absolute address of our 32-bit bridging function in C.
CALL [EAX]  ; call our 32-bit bridging function in C.
; don't touch EAX anymore, it has the return value now!
ADD ESP, 4  ; dump our function argument.

POP DS  ; get back our 16-bit data segment.

; Restore 16:16 stack.  !!! FIXME: can use LSS if we figure out prefix and DS politics.
POP BX  ; Get our original ss back.
POP CX  ; Get our original sp back.

JMP WORD 0xAAAA:0xBBBB  ; back into 16-bit land (the next instruction!).

USE16
MOV SS, CX
MOV SP, BX
RETF 0x22   ; ...and back to the (far) caller, clearing the args (Pascal calling convention!) with retval in AX.
*/

#define LX_NATIVE_INIT_16BIT_BRIDGE(fn, argbytes) { \
    fn##16 = ptr; \
    \
    /* instructions are in Intel syntax here, not AT&T. */ \
    /* USE16 */ \
    *(ptr++) = 0x8C;  /* mov bx,ss... */ \
    *(ptr++) = 0xD3;  /*  ...mov bx,ss */ \
    *(ptr++) = 0x66;  /* shl ebx,byte 0x10... */ \
    *(ptr++) = 0xC1;  /*  ...shl ebx,byte 0x10 */ \
    *(ptr++) = 0xE3;  /*  ...shl ebx,byte 0x10 */ \
    *(ptr++) = 0x10;  /*  ...shl ebx,byte 0x10 */ \
    *(ptr++) = 0x89;  /* mov bx,sp... */ \
    *(ptr++) = 0xE3;  /*  ...mov bx,sp */ \
    *(ptr++) = 0x8C;  /* mov ax,ss... */ \
    *(ptr++) = 0xD0;  /*  ...mov ax,ss */ \
    *(ptr++) = 0xC1;  /* shr ax,byte 0x3... */ \
    *(ptr++) = 0xE8;  /*  ...shr ax,byte 0x3 */ \
    *(ptr++) = 0x03;  /*  ...shr ax,byte 0x3 */ \
    *(ptr++) = 0x66;  /* shl eax,byte 0x10... */ \
    *(ptr++) = 0xC1;  /*  ...shl eax,byte 0x10 */ \
    *(ptr++) = 0xE0;  /*  ...shl eax,byte 0x10 */ \
    *(ptr++) = 0x10;  /*  ...shl eax,byte 0x10 */ \
    *(ptr++) = 0x89;  /* mov ax,sp... */ \
    *(ptr++) = 0xE0;  /*  ...mov ax,sp */ \
    *(ptr++) = 0x66;  /* jmp dword 0x7788:0x33332222... */ \
    *(ptr++) = 0xEA;  /*  ...jmp dword 0x7788:0x33332222 */ \
    const uint32 jmp32addr = (uint32) (ptr + 6); \
    memcpy(ptr, &jmp32addr, 4); ptr += 4; \
    memcpy(ptr, &GLoaderState->original_cs, 2); ptr += 2; \
    \
    /* USE32 */ \
    *(ptr++) = 0x66;  /* mov cx,0xabcd... */ \
    *(ptr++) = 0xB9;  /*  ...mov cx,0xabcd */ \
    memcpy(ptr, &GLoaderState->original_ss, 2); ptr += 2; \
    *(ptr++) = 0x8E;  /* mov ss,ecx... */ \
    *(ptr++) = 0xD1;  /*  ...mov ss,ecx */ \
    *(ptr++) = 0x89;  /* mov esp,eax... */ \
    *(ptr++) = 0xC4;  /*  ...mov esp,eax */ \
    *(ptr++) = 0x83;  /* add eax,byte +0x4... */ \
    *(ptr++) = 0xC0;  /*  ...add eax,byte +0x4 */ \
    *(ptr++) = 0x04;  /*  ...add eax,byte +0x4 */ \
    *(ptr++) = 0x53;  /* push ebx */ \
    *(ptr++) = 0x1E;  /* push ds */ \
    *(ptr++) = 0x66;  /* mov cx,0x8888... */ \
    *(ptr++) = 0xB9;  /*  ...mov cx,0x8888 */ \
    memcpy(ptr, &GLoaderState->original_ds, 2); ptr += 2; \
    *(ptr++) = 0x8E;  /* mov ds,ecx... */ \
    *(ptr++) = 0xD9;  /*  ...mov ds,ecx */ \
    *(ptr++) = 0x50;  /* push eax */ \
    *(ptr++) = 0xB8;  /* mov eax,0x55555555... */ \
    const uint32 callbridgeaddr = (uint32) bridge16to32_##fn; \
    memcpy(ptr, &callbridgeaddr, 4); ptr += 4; \
    *(ptr++) = 0xFF;  /* call dword [eax]... */ \
    *(ptr++) = 0xD0;  /*  ...call dword [eax] */ \
    *(ptr++) = 0x83;  /* add esp,byte +0x4... */ \
    *(ptr++) = 0xC4;  /*  ...add esp,byte +0x4 */ \
    *(ptr++) = 0x04;  /*  ...add esp,byte +0x4 */ \
    *(ptr++) = 0x1F;  /* pop ds */ \
    *(ptr++) = 0x66;  /* pop bx... */ \
    *(ptr++) = 0x5B;  /*  ...pop bx */ \
    *(ptr++) = 0x66;  /* pop cx... */ \
    *(ptr++) = 0x59;  /*  ...pop cx */ \
    *(ptr++) = 0x66;  /* jmp word 0xaaaa:0xbbbb... */ \
    *(ptr++) = 0xEA;  /*  ...jmp word 0xaaaa:0xbbbb */ \
    const uint16 jmp16offset = (uint16) ((((uint32)ptr) - ((uint32)mmapaddr))+4); \
    memcpy(ptr, &jmp16offset, 2); ptr += 2; \
    const uint16 jmp16segment = (obj16.alias << 3) | 7; \
    memcpy(ptr, &jmp16segment, 2); ptr += 2; \
    \
    /* USE16 */ \
    *(ptr++) = 0x8E;  /* mov ss,cx... */ \
    *(ptr++) = 0xD1;  /*  ...mov ss,cx */ \
    *(ptr++) = 0x89;  /* mov sp,bx... */ \
    *(ptr++) = 0xDC;  /*  ...mov sp,bx */ \
    *(ptr++) = 0xCA;  /* retf 0x22... */ \
    const uint16 argbytecount = argbytes; \
    memcpy(ptr, &argbytecount, 2); ptr += 2; \
}

#define LX_NATIVE_MODULE_INIT_16BIT_SUPPORT_END() { \
    assert((((uint32)ptr) - ((uint32)mmapaddr)) < 0x10000);  /* don't be more than 64k. */ \
    if (mprotect(obj16.mapped, vsize, PROT_READ | PROT_EXEC) == -1) { \
        fprintf(stderr, "mprotect() failed for 16-bit bridge code!\n"); \
        munmap(obj16.mapped, vsize); \
        GLoaderState->freeSelector(obj16.alias); \
        obj16.mapped = obj16.addr = NULL; \
        obj16.size = 0; \
        obj16.alias = 0xFFFF; \
        return 0; \
    } \
}

#define LX_NATIVE_MODULE_16BIT_BRIDGE_ARG(typ, var) \
    const typ var = *((typ *) args); args += sizeof (typ)

#define LX_NATIVE_MODULE_16BIT_BRIDGE_PTRARG(typ, var) \
    typ var = (typ) GLoaderState->convert1616to32(*((uint32 *) args)); args += sizeof (uint32)

#define LX_NATIVE_EXPORT16(fn, ord) { ord, #fn, &fn##16, &obj16 }

#endif

// end of os2native16.h ...

