/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <assert.h>
#include <dlfcn.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <ucontext.h>

// 16-bit selector kernel nonsense...
#include <sys/syscall.h>
#include <sys/types.h>
#include <asm/ldt.h>

#include "lib2ine.h"

// !!! FIXME: move this into an lx_common.c file.
static int sanityCheckLxModule(const uint8 *exe, const uint32 exelen)
{
    if (sizeof (LxHeader) >= exelen) {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const LxHeader *lx = (const LxHeader *) exe;
    if ((lx->byte_order != 0) || (lx->word_order != 0)) {
        fprintf(stderr, "Module is not little-endian!\n");
        return 0;
    }

    if (lx->lx_version != 0) {
        fprintf(stderr, "Module is unknown LX version (%u)\n", (uint) lx->lx_version);
        return 0;
    }

    if (lx->cpu_type > 3) { // 1==286, 2==386, 3==486
        fprintf(stderr, "Module needs unknown CPU type (%u)\n", (uint) lx->cpu_type);
        return 0;
    }

    if (lx->os_type != 1) { // 1==OS/2, others: dos4, windows, win386, unknown.
        fprintf(stderr, "Module needs unknown OS type (%u)\n", (uint) lx->os_type);
        return 0;
    }

    if (lx->page_size != 4096) {
        fprintf(stderr, "Module page size isn't 4096 (%u)\n", (uint) lx->page_size);
        return 0;
    }

    if (lx->module_flags & 0x2000) {
        fprintf(stderr, "Module is flagged as not-loadable\n");
        return 0;
    }

    // !!! FIXME: check if EIP and ESP are non-zero vs per-process library bits, etc.

    return 1;
} // sanityCheckLxModule

static int sanityCheckNeModule(const uint8 *exe, const uint32 exelen)
{
    if (sizeof (NeHeader) >= exelen) {
        fprintf(stderr, "not an OS/2 EXE\n");
        return 0;
    }

    const NeHeader *ne = (const NeHeader *) exe;
    if (ne->exe_type > 1) {
        fprintf(stderr, "Not an OS/2 NE module file (exe_type is %d, not 1)\n", (int) ne->exe_type);
        return 0;
    }

    return 1;
} // sanityCheckNeModule

static int sanityCheckModule(uint8 **_exe, uint32 *_exelen, int *_is_lx)
{
    if (*_exelen < 62) {
        fprintf(stderr, "not an OS/2 module\n");
        return 0;
    }
    const uint32 header_offset = *((uint32 *) (*_exe + 0x3C));
    //printf("header offset is %u\n", (uint) header_offset);

    *_exe += header_offset;  // skip the DOS stub, etc.
    *_exelen -= header_offset;

    const uint8 *magic = *_exe;

    if ((magic[0] == 'L') && (magic[1] == 'X')) {
        *_is_lx = 1;
        return sanityCheckLxModule(*_exe, *_exelen);
    } else if ((magic[0] == 'N') && (magic[1] == 'E')) {
        *_is_lx = 0;
        return sanityCheckNeModule(*_exe, *_exelen);
    }

    fprintf(stderr, "not an OS/2 module\n");
    return 0;
} // sanityCheckModule


static char *makeOS2Path(const char *fname)
{
    char *full = realpath(fname, NULL);
    if (!full)
        return NULL;

    char *retval = (char *) malloc(strlen(full) + 3);
    if (!retval)
        return NULL;

    retval[0] = 'C';
    retval[1] = ':';
    strcpy(retval + 2, full);
    free(full);
    for (char *ptr = retval + 2; *ptr; ptr++) {
        if (*ptr == '/')
            *ptr = '\\';
    } // for

    return retval;
} // makeOS2Path

// based on case-insensitive search code from PhysicsFS:
//    https://icculus.org/physfs/
//  It's also zlib-licensed, plus I wrote it.  :)  --ryan.
// !!! FIXME: this doesn't work as-is for UTF-8 case folding, since string
// !!! FIXNE:  length can change!
static int locateOneElement(char *buf)
{
    if (access(buf, F_OK) == 0)
        return 1;  // quick rejection: exists in current case.

    DIR *dirp;
    char *ptr = strrchr(buf, '/');  // find entry at end of path.
    if (ptr == NULL) {
        dirp = opendir(".");
        ptr = buf;
    } else if (ptr == buf) {
        dirp = opendir("/");
    } else {
        *ptr = '\0';
        dirp = opendir(buf);
        *ptr = '/';
        ptr++;  // point past dirsep to entry itself.
    } // else

    for (struct dirent *dent = readdir(dirp); dent; dent = readdir(dirp)) {
        if (strcasecmp(dent->d_name, ptr) == 0) {
            strcpy(ptr, dent->d_name); // found a match. Overwrite with this case.
            closedir(dirp);
            return 1;
        } // if
    } // for

    // no match at all...
    closedir(dirp);
    return 0;
} // locateOneElement

static int locatePathCaseInsensitive(char *buf)
{
    int rc;
    char *ptr = buf;

    if (*ptr == '\0')
        return 0;  // Uh...I guess that's success?

    if (access(buf, F_OK) == 0)
        return 0;  // quick rejection: exists in current case.

    while ( (ptr = strchr(ptr + 1, '/')) != NULL ) {
        *ptr = '\0';  // block this path section off
        rc = locateOneElement(buf);
        *ptr = '/'; // restore path separator
        if (!rc)
            return -2;  // missing element in path.
    } // while

    // check final element...
    return locateOneElement(buf) ? 0 : -1;
} // locatePathCaseInsensitive

static char *lxMakeUnixPath(const char *os2path, uint32 *err)
{
    if ((strcasecmp(os2path, "NUL") == 0) || (strcasecmp(os2path, "\\DEV\\NUL") == 0))
        os2path = "/dev/null";
    // !!! FIXME: emulate other OS/2 device names (CON, etc).
    //else if (strcasecmp(os2path, "CON") == 0)
    else {
        char drive = os2path[0];
        if ((drive >= 'a') && (drive <= 'z'))
            drive += 'A' - 'a';

        if ((drive >= 'A') && (drive <= 'Z')) {
            if (os2path[1] == ':') {  // it's a drive letter.
                if (drive == 'C')
                    os2path += 2;  // skip "C:" if it's there.
                else {
                    *err = 26; //ERROR_NOT_DOS_DISK;
                    return NULL;
                } // else
            } // if
        } // if
    } // else

    const size_t len = strlen(os2path);
    char *retval = (char *) malloc(len + 1);
    if (!retval) {
        *err = 8;  //ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    } // else

    strcpy(retval, os2path);

    for (char *ptr = strchr(retval, '\\'); ptr; ptr = strchr(ptr + 1, '\\'))
        *ptr = '/';  // convert to Unix-style path separators.

    locatePathCaseInsensitive(retval);

    return retval;
} // lxMakeUnixPath


// So exepack2 sometimes copies between pieces of the destination buffer,
//  but you can't memmove() since you actually want it to copy the data as
//  it changes in case of overlap. But to prevent debugging tools from
//  complaining about overlapping memcpy()'s, we just do a simple for-loop.
static void linearmove(uint8 *dst, const uint8 *src, uint8 len)
{
    while (len--) {
        *dst = *src;
        dst++;
        src++;
    }
} // linearmove

/* this algorithm is from lxlite 138u. */
static int decompressExePack2(uint8 *dst, const uint32 dstlen, const uint8 *src, const uint32 srclen)
{
    sint32 sOf = 0;
    sint32 dOf = 0;
    sint32 bOf = 0;
    uint8 b1 = 0;
    uint8 b2 = 0;

    #define SRCAVAIL(n) ((sOf + (n)) <= srclen)
    #define DSTAVAIL(n) ((dOf + (n)) <= dstlen)

    do {
        if (!SRCAVAIL(1))
            break;

        b1 = src[sOf];
        switch (b1 & 3) {
            case 0:
                if (b1 == 0) {
                    if (SRCAVAIL(2)) {
                        if (src[sOf + 1] == 0) {
                            sOf += 2;
                            break;
                        } else if (SRCAVAIL(3) && DSTAVAIL(src[sOf + 1])) {
                            memset(dst + dOf, src[sOf + 2], src[sOf + 1]);
                            sOf += 3;
                            dOf += src[sOf - 2];
                        } else {
                            return 0;
                        }
                    } else {
                        return 0;
                    }
                } else if (SRCAVAIL((b1 >> 2) + 1) && DSTAVAIL(b1 >> 2)) {
                    memcpy(dst + dOf, src + (sOf + 1), b1 >> 2);
                    dOf += b1 >> 2;
                    sOf += (b1 >> 2) + 1;
                } else {
                    return 0;
                }
                break;

            case 1:
                if (!SRCAVAIL(2))
                    return 0;
                bOf = (*((const uint16 *) (src + sOf))) >> 7;
                b2 = ((b1 >> 4) & 7) + 3;
                b1 = ((b1 >> 2) & 3);
                sOf += 2;
                if (SRCAVAIL(b1) && DSTAVAIL(b1 + b2) && ((dOf + b1 - bOf) >= 0)) {
                    memcpy(dst + dOf, src + sOf, b1);
                    dOf += b1;
                    sOf += b1;
                    linearmove(dst + dOf, dst + (dOf - bOf), b2);
                    dOf += b2;
                } else {
                    return 0;
                } // else
                break;

            case 2:
                if (!SRCAVAIL(2))
                    return 0;
                bOf = (*((const uint16 *) (src + sOf))) >> 4;
                b1 = ((b1 >> 2) & 3) + 3;
                if (DSTAVAIL(b1) && (dOf - bOf >= 0)) {
                    linearmove(dst + dOf, dst + (dOf - bOf), b1);
                    dOf += b1;
                    sOf += 2;
                } else {
                    return 0;
                } // else
                break;

            case 3:
                if (!SRCAVAIL(3))
                    return 0;
                b2 = ((*((const uint16 *) (src + sOf))) >> 6) & 0x3F;
                b1 = (src[sOf] >> 2) & 0x0F;
                bOf = (*((const uint16 *) (src + (sOf + 1)))) >> 4;
                sOf += 3;
                if (SRCAVAIL(b1) && DSTAVAIL(b1 + b2) && ((dOf + b1 - bOf) >= 0))
                {
                    memcpy(dst + dOf, src + sOf, b1);
                    dOf += b1;
                    sOf += b1;
                    linearmove(dst + dOf, dst + (dOf - bOf), b2);
                    dOf += b2;
                } else {
                    return 0;
                } // else
                break;
        } // switch
    } while (dOf < dstlen);
    #undef SRCAVAIL
    #undef DSTAVAIL

    // pad out the rest of the page with zeroes.
    if ((dstlen - dOf) > 0)
        memset(dst + dOf, '\0', dstlen - dOf);

    return 1;
} // decompressExePack2

static int decompressIterated(uint8 *dst, uint32 dstlen, const uint8 *src, uint32 srclen)
{
    while (srclen) {
        if (srclen < 4)
            return 0;
        const uint16 iterations = *((uint16 *) src); src += 2;
        const uint16 len = *((uint16 *) src); src += 2;
        srclen -= 4;
        if (dstlen < (iterations * len))
            return 0;
        else if (srclen < len)
            return 0;

        for (uint16 i = 0; i < iterations; i++) {
            memcpy(dst, src, len);
            dst += len;
            dstlen -= len;
        } // for

        src += len;
        srclen -= len;
    } // while

    // pad out the rest of the page with zeroes.
    if (dstlen > 0)
        memset(dst, '\0', dstlen);

    return 1;
} // decompressIterated

static inline uint16 lxSelectorToSegment(const uint16 selector)
{
    return (selector << 3) | 7;
} // lxSelectorToSegment

// !!! FIXME: mutex this
static int allocateSelector(const uint16 selector, const int pages, const uint32 addr, const unsigned int contents, const int is32bit)
{
    assert(selector < LX_MAX_LDT_SLOTS);

    if (GLoaderState.ldt[selector])
        return 0;  // already in use.

    //const int expand_down = (contents == MODIFY_LDT_CONTENTS_STACK);

    struct user_desc entry;
    entry.entry_number = (unsigned int) selector;
    entry.base_addr = (unsigned int) addr; //(expand_down ? (addr + 0x10000) : addr);
    entry.limit = pages; //expand_down ? 0 : 16;
    entry.seg_32bit = is32bit;
    entry.contents = contents;
    entry.read_exec_only = 0;
    entry.limit_in_pages = 1;
    entry.seg_not_present = 0;
    entry.useable = 1;
    if (syscall(SYS_modify_ldt, 1, &entry, sizeof (entry)) != 0)
        return 0;

    GLoaderState.ldt[selector] = addr;
    return 1;
} // allocateSelector

// !!! FIXME: mutex this
static int lxFindSelector(const uint32 _addr, uint16 *outselector, uint16 *outoffset, int iscode)
{
    uint32 addr = _addr;
    if (addr < 4096)
        return 0;   // we won't map the NULL page.

    const uint32 *ldt = GLoaderState.ldt;

    int available = -1;
    int preferred = -1;

    // optimize for the case where we need a selector that happens to be in tiled memory,
    //  since it's fast to look up.
    if (addr < (1024 * 1024 * 512)) {
        const uint16 idx = (uint16) (addr >> 16);
        const uint32 tile = ldt[idx];
        if (tile == 0) {
            preferred = available = idx;  // we can use this piece.
        } else if ((tile <= addr) && ((tile + 0x10000) > addr)) {
            *outselector = idx;
            *outoffset = (uint16) (addr - tile);
            //printf("SELECTOR: found tiled selector 0x%X for address %p\n", (uint) idx, (void *) addr);
            return 1;  // already allocated to this address.
        } // if
    } // if

    for (int i = 0; i < LX_MAX_LDT_SLOTS; i++) {
        const uint32 tile = ldt[i];
        if (tile == 0)
            available = i;
        else if ((tile <= addr) && ((tile + 0x10000) > addr)) {
            *outselector = (uint16) i;
            *outoffset = (uint16) (addr - tile);
            //printf("SELECTOR: found existing selector 0x%X for address %p\n", (uint) i, (void *) addr);
            return 1;  // already allocated to this address.
        } // else if
    } // for

    // nothing allocated to this address so far. Try to allocate something.
    if (available == -1) {
        fprintf(stderr, "Uhoh, we've run out of LDT selectors! Probably about to crash...\n"); fflush(stderr);
        return 0;  // uh oh, out of selectors!
    } // if

    // decide if there is code or data mapped here. If there's an OS/2 API to
    //  change page permissions, we'll need to update the mapping and our state.
    for (LxModule *lxmod = GLoaderState.loaded_modules; (iscode == -1) && lxmod; lxmod = lxmod->next) {
        LxMmaps *mmaps = lxmod->mmaps;
        for (uint32 i = 0; i < lxmod->num_mmaps; i++, mmaps++) {
            const size_t lo = (size_t) mmaps->addr;
            const size_t hi = lo + mmaps->size;
            if ((addr >= lo) && (addr <= hi)) {
                iscode = ((mmaps->prot & PROT_EXEC) != 0);
                break;
            } // if
        } // for
    } // for

    if (iscode == -1) {
        iscode = 0;
    }

    const uint32 diff = addr % 0x10000;
    addr -= diff;   // make sure we start on a 64k border.

    const uint16 selector = (uint16) (preferred != -1) ? preferred : available;
    //printf("setting up LDT mapping for %s at selector %u\n", iscode ? "code" : "data", (unsigned int) selector);

    if (!allocateSelector(selector, 16, addr, iscode ? MODIFY_LDT_CONTENTS_CODE : MODIFY_LDT_CONTENTS_DATA, 0)) {
        fprintf(stderr, "Uhoh, we've failed to allocate LDT selector %u! Probably about to crash...\n", (uint) selector); fflush(stderr);
        return 0;
    } // if

    //printf("SELECTOR: allocated selector 0x%X for address %p\n", (uint) selector, (void *) addr);
    *outselector = selector;
    *outoffset = (uint16) diff;
    return 1;
} // lxFindSelector

// !!! FIXME: mutex this
static void lxFreeSelector(const uint16 selector)
{
    assert(selector < LX_MAX_LDT_SLOTS);

    if (!GLoaderState.ldt[selector])
        return;  // already free.

    struct user_desc entry;
    memset(&entry, '\0', sizeof (entry));
    entry.entry_number = (unsigned int) selector;
    entry.read_exec_only = 1;
    entry.seg_not_present = 1;
    if (syscall(SYS_modify_ldt, 1, &entry, sizeof (entry)) != 0)
        return;  // oh well.

    GLoaderState.ldt[selector] = 0;
} // lxFreeSelector

static void *lxConvert1616to32(const uint32 addr1616)
{
    if (addr1616 == 0)
        return NULL;

    const uint16 selector = (uint16) (addr1616 >> 19);  // slide segment down, and shift out control bits.
    const uint16 offset = (uint16) (addr1616 % 0x10000);  // all our LDT segments start at 64k boundaries (at the moment!).
    //printf("lxConvert1616to32: 0x%X -> %p\n", (uint) addr1616, (void *) (size_t) (GLoaderState.ldt[selector] + offset));
    assert(GLoaderState.ldt[selector] != 0);
    return (void *) (size_t) (GLoaderState.ldt[selector] + offset);
} // lxConvert1616to32

static uint32 lxConvert32to1616(void *addr32)
{
    if (addr32 == NULL)
        return 0;

    uint16 selector = 0;
    uint16 offset = 0;
    if (!lxFindSelector((uint32) addr32, &selector, &offset, -1)) {
        fprintf(stderr, "Uhoh, ran out of LDT entries?!\n");
        return 0;  // oh well, crash, probably.
    } // if

    //printf("selector=0x%X, segment=0x%X\n", (uint) selector, (uint) lxSelectorToSegment(selector));
    return (((uint32)lxSelectorToSegment(selector)) << 16) | ((uint32) offset);
} // lxConvert32to1616

static inline void *lxConvertSegmentOffsetto32(const uint16 seg, const uint16 off)
{
    return lxConvert1616to32((((uint32) seg) << 16) | ((uint32) off));
} // lxConvertSegmentOffsetto32

// EMX (and probably many other things) occasionally has to call a 16-bit
//  system API, and assumes its stack is tiled in the LDT; it'll just shift
//  the stack pointer and use it as a stack segment for the 16-bit call
//  without calling DosFlatToSel(). So we tile the main thread's stack and
//  pray that covers it. If we have to tile _every_ thread's stack, we can do
//  that later.
// !!! FIXME: if we do this for secondary thread stacks, we'll need to mutex this.
static void initOs2StackSegments(uint32 addr, uint32 stacklen, const int deinit)
{
    //printf("base == %p, stacklen == %u\n", (void*)addr, (uint) stacklen);
    const uint32 diff = addr % 0x10000;
    addr -= diff;   // make sure we start on a 64k border.
    stacklen += diff;   // make sure we start on a 64k border.

    // We fill in LDT tiles for the entire stack (EMX, etc, assume this will work).
    if (stacklen % 0x10000)  // pad this out to 64k
        stacklen += 0x10000 - (stacklen % 0x10000);

    // !!! FIXME: do we have to allocate these backwards? (stack grows down).
    while (stacklen) {
        //printf("Allocating selector 0x%X for stack %p ... \n", (uint) (addr >> 16), (void *) addr);
        if (deinit) {
            lxFreeSelector((uint16) (addr >> 16));
        } else {
            if (!allocateSelector((uint16) (addr >> 16), 16, addr, MODIFY_LDT_CONTENTS_DATA, 0)) {
                FIXME("uhoh, couldn't set up an LDT entry for a stack segment! Might crash later!");
            } // if
        } // else
        stacklen -= 0x10000;
        addr += 0x10000;
    } // while
} // initOs2StackSegments

// OS/2 threads keep their Thread Information Block at FS:0x0000, so we have
//  to ask the Linux kernel to screw around with 16-bit selectors on our
//  behalf so we don't crash out when apps try to access it directly.
// OS/2 provides a C-callable API to obtain the (32-bit linear!) TIB address
//  without going directly to the FS register, but lots of programs (including
//  the EMX runtime) touch the register directly, so we have to deal with it.
// You must call this once for each thread that will go into LX land, from
//  that thread, as soon as possible after starting.
static uint16 lxSetOs2Tib(uint8 *tibspace)
{
    // !!! FIXME: I barely know what I'm doing here, this could all be wrong.
    struct user_desc entry;
    entry.entry_number = -1;
    entry.base_addr = (unsigned int) ((size_t)tibspace);
    entry.limit = LXTIBSIZE;
    entry.seg_32bit = 1;
    entry.contents = MODIFY_LDT_CONTENTS_DATA;
    entry.read_exec_only = 0;
    entry.limit_in_pages = 0;
    entry.seg_not_present = 0;
    entry.useable = 1;
    const long rc = syscall(SYS_set_thread_area, &entry);
    assert(rc == 0);  FIXME("this can legit fail, though!");

    // The "<< 3 | 3" makes this a GDT selector at ring 3 permissions.
    //  If this did "| 7" instead of "| 3", it'd be an LDT selector.
    //  Use lxFindSelector() or allocateSelector() for LDT entries, though!
    const unsigned int segment = (entry.entry_number << 3) | 3;
    __asm__ __volatile__ ( "movw %%ax, %%fs  \n\t" : : "a" (segment) );
    return (uint16) entry.entry_number;
} // lxSetOs2Tib

static LxTIB2 *lxGetOs2Tib2(void)
{
    // just read the FS register, since we have to stick it there anyhow...
    LxTIB2 *ptib2;
    __asm__ __volatile__ ( "movl %%fs:0xC, %0  \n\t" : "=r" (ptib2) );
    return ptib2;
} // lxGetTib2

static LxTIB *lxGetOs2Tib(void)
{
    // we store the TIB2 struct right after the TIB struct on the stack,
    //  so get the TIB2's linear address from %fs:0xC, then step back
    //  to the TIB's linear address.
    uint8 *ptib2 = (uint8 *) lxGetOs2Tib2();
    return (LxTIB *) (ptib2 - sizeof (LxTIB));
} // lxGetOs2Tib

static void lxDeinitOs2Tib(const uint16 selector)
{
    // !!! FIXME: I barely know what I'm doing here, this could all be wrong.
    struct user_desc entry;
    memset(&entry, '\0', sizeof (entry));
    entry.entry_number = selector;
    entry.read_exec_only = 1;
    entry.seg_not_present = 1;

    const long rc = syscall(SYS_set_thread_area, &entry);
    assert(rc == 0);  FIXME("this can legit fail, though!");
} // lsDeinitOs2Tib

static void freeLxModule(LxModule *lxmod);

static __attribute__((noreturn)) void lxTerminate(const uint32 exitcode)
{
    // free the actual .exe
    freeLxModule(GLoaderState.main_module);

    // clear out anything that is still loaded...
    // presumably everything in the loaded_modules list is sorted in order
    //  of dependency, since we prepend the newest loads to the front of the
    //  list, and anything a load depends on gets listed before the dependent
    //  module, pushing it futher down.
    // if this doesn't work out, maybe just run everything's termination code
    //  and free everything after there's nothing left to run.
    while (GLoaderState.loaded_modules) {
        LxModule *lxmod = GLoaderState.loaded_modules;
        lxmod->refcount = 1;  // force it to free now.
        freeLxModule(lxmod);
    } // while

    free(GLoaderState.ldt);
    GLoaderState.ldt = NULL;

    // OS/2's docs say this only keeps the lower 16 bits of exitcode.
    // !!! FIXME: ...but Unix only keeps the lowest 8 bits. Will have to
    // !!! FIXME:  tapdance to pass larger values back to OS/2 parent processes.
    if (exitcode > 255)
        FIXME("deal with process exit codes > 255. We clamped this one!");

    _exit((int) (exitcode & 0xFF));
} // lxTerminate

static __attribute__((noreturn)) void endLxProcess(const uint32 exitcode)
{
    if (GLoaderState.dosExit)
        GLoaderState.dosExit(1, exitcode);  // let exit lists run. Should call lxTerminate!
    lxTerminate(exitcode);  // just in case.
} // endLxProcess

static void *lxAllocSegment(uint16 *selector, const int iscode)
{
    // These are meant to be 64k segments mapped for use by 16-bit code, which means we
    //  need them under 512 megabytes so their segments can convert directly to a linear
    //  address with some bit twiddling.
    static size_t baseaddr = 136 * 1024 * 1024;  // just start at a random low address that (hopefully) doesn't overlap anything.
    const uint32 segmentsize = 0x10000;
    void *segment = mmap((void *) baseaddr, segmentsize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (segment == ((void *) MAP_FAILED)) {
        FIXME("This could be more robust.");
        fprintf(stderr, "Failed to mmap a 16-bit friendly memory segment.\n");
        return NULL;
    }

    baseaddr += segmentsize;
    uint16 offset = 0xFFFF;
    *selector = 0xFFFF;
    lxFindSelector((uint32) segment, selector, &offset, iscode);
    assert(*selector != 0xFFFF);
    assert(offset == 0);
    //printf("lxAllocSegment: addr=%p, selector=%x\n", segment, (uint) *selector);
    return segment;
} // lxAllocSegment

static void lxFreeSegment(const uint16 selector)
{
    const uint32 addr32 = GLoaderState.ldt[selector];
    if (addr32) {
        void *addr = (void *) ((size_t) addr32);
        //printf("lxFreeSegment: addr=%p, selector=%x\n", addr, (uint) selector);
        munmap(addr, 0x10000);
        lxFreeSelector(selector);
    } // if
} // lxFreeSegment

static void missingEntryPointCalled(const char *module, const char *entry)
{
    fflush(stdout);
    fflush(stderr);
    fprintf(stderr, "\n\nMissing entry point '%s' in module '%s'! Aborting.\n\n\n", entry, module);
    //STUBBED("output backtrace");
    fflush(stderr);
    lxTerminate(1);
} // missingEntryPointCalled

static void *generateMissingTrampoline(const char *_module, const char *_entry)
{
    static void *page = NULL;
    static uint32 pageused = 0;
    static uint32 pagesize = 0;

    if (pagesize == 0)
        pagesize = getpagesize();

    if ((!page) || ((pagesize - pageused) < 32))
    {
        if (page)
            mprotect(page, pagesize, PROT_READ | PROT_EXEC);
        page = mmap(NULL, pagesize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        pageused = 0;
    } // if

    void *trampoline = page + pageused;
    char *ptr = (char *) trampoline;
    char *module = strdup(_module);
    char *entry = strdup(_entry);

    *(ptr++) = 0x55;  // pushl %ebp
    *(ptr++) = 0x89;  // movl %esp,%ebp
    *(ptr++) = 0xE5;  //   ...movl %esp,%ebp
    *(ptr++) = 0x68;  // pushl immediate
    memcpy(ptr, &entry, sizeof (char *));
    ptr += sizeof (uint32);
    *(ptr++) = 0x68;  // pushl immediate
    memcpy(ptr, &module, sizeof (char *));
    ptr += sizeof (uint32);
    *(ptr++) = 0xB8;  // movl immediate to %eax
    const void *fn = missingEntryPointCalled;
    memcpy(ptr, &fn, sizeof (void *));
    ptr += sizeof (void *);
    *(ptr++) = 0xFF;  // call absolute in %eax.
    *(ptr++) = 0xD0;  //   ...call absolute in %eax.

    const uint32 trampoline_len = (uint32) (ptr - ((char *) trampoline));
    assert(trampoline_len <= 32);
    pageused += trampoline_len;

    if (pageused % 4)  // keep these aligned to 32 bits.
        pageused += (4 - (pageused % 4));

    //printf("Generated trampoline %p for module '%s' export '%s'\n", trampoline, module, entry);

    return trampoline;
} // generateMissingTrampoline

static void *generateMissingTrampoline16(const char *_module, const char *_entry, const LxExport **_lxexp)
{
    static void *segment = NULL;
    static uint32 segmentused = 0;
    const uint32 segmentsize = 0x10000;
    static uint16 selector = 0xFFFF;

    if ((!segment) || ((segmentsize - segmentused) < 64))
    {
        if (segment)
            mprotect(segment, segmentsize, PROT_READ | PROT_EXEC);
        segment = lxAllocSegment(&selector, 1);
        assert(segment != NULL);
        assert(selector != 0xFFFF);
        segmentused = 0;
    } // if

    void *trampoline = segment + segmentused;
    char *ptr = (char *) trampoline;
    char *module = strdup(_module);
    char *entry = strdup(_entry);

    // USE16
    *(ptr++) = 0x89;  /* mov cx,sp... */
    *(ptr++) = 0xE1;  /*  ...mov cx,sp */
    *(ptr++) = 0x66;  /* jmp dword 0x7788:0x33332222... */
    *(ptr++) = 0xEA;  /*  ...jmp dword 0x7788:0x33332222 */
    const uint32 jmp32addr = (uint32) (ptr + 6);
    memcpy(ptr, &jmp32addr, 4); ptr += 4;
    memcpy(ptr, &GLoaderState.original_cs, 2); ptr += 2;

    // USE32
    *(ptr++) = 0x66;  /* mov ax,ss... */
    *(ptr++) = 0x8C;  /*  ...mov ax,ss */
    *(ptr++) = 0xD0;  /*  ...mov ax,ss */
    *(ptr++) = 0x66;  /* shr ax,byte 0x3... */
    *(ptr++) = 0xC1;  /*  ...shr ax,byte 0x3 */
    *(ptr++) = 0xE8;  /*  ...shr ax,byte 0x3 */
    *(ptr++) = 0x03;  /*  ...shr ax,byte 0x3 */
    *(ptr++) = 0xC1;  /* shl eax,byte 0x10... */
    *(ptr++) = 0xE0;  /*  ...shl eax,byte 0x10 */
    *(ptr++) = 0x10;  /*  ...shl eax,byte 0x10 */
    *(ptr++) = 0x66;  /* mov ax,cx... */
    *(ptr++) = 0x89;  /*  ...mov ax,cx */
    *(ptr++) = 0xC8;  /*  ...mov ax,cx */
    *(ptr++) = 0x66;  /* mov cx,0xabcd... */
    *(ptr++) = 0xB9;  /*  ...mov cx,0xabcd */
    memcpy(ptr, &GLoaderState.original_ss, 2); ptr += 2;
    *(ptr++) = 0x8E;  /* mov ss,ecx... */
    *(ptr++) = 0xD1;  /*  ...mov ss,ecx */
    *(ptr++) = 0x89;  /* mov esp,eax... */
    *(ptr++) = 0xC4;  /*  ...mov esp,eax */

    if (GLoaderState.original_ss != GLoaderState.original_ds) {
        *(ptr++) = 0x66;  /* mov cx,0x8888... */
        *(ptr++) = 0xB9;  /*  ...mov cx,0x8888 */
        memcpy(ptr, &GLoaderState.original_ds, 2); ptr += 2;
    }
    *(ptr++) = 0x8E;  /* mov ds,ecx... */
    *(ptr++) = 0xD9;  /*  ...mov ds,ecx */

    if (GLoaderState.original_es != GLoaderState.original_ds) {
        *(ptr++) = 0x66;  /* mov cx,0x8888... */
        *(ptr++) = 0xB9;  /*  ...mov cx,0x8888 */
        memcpy(ptr, &GLoaderState.original_es, 2); ptr += 2;
    }
    *(ptr++) = 0x8E;  /* mov es,ecx... */
    *(ptr++) = 0xC1;  /*  ...mov es,ecx */

    // okay, CPU is in a sane state again, call the trampoline. Don't bother cleaning up.
    *(ptr++) = 0x68;  // pushl immediate
    memcpy(ptr, &entry, sizeof (char *));
    ptr += sizeof (uint32);
    *(ptr++) = 0x68;  // pushl immediate
    memcpy(ptr, &module, sizeof (char *));
    ptr += sizeof (uint32);
    *(ptr++) = 0xB8;  // movl immediate to %eax

    const void *fn = missingEntryPointCalled;
    memcpy(ptr, &fn, sizeof (void *));
    ptr += sizeof (void *);
    *(ptr++) = 0xFF;  // call absolute in %eax.
    *(ptr++) = 0xD0;  //   ...call absolute in %eax.
    // (and never return.)

    const uint32 trampoline_len = (uint32) (ptr - ((char *) trampoline));
    assert(trampoline_len <= 64);
    segmentused += trampoline_len;

    if (segmentused % 4)  // keep these aligned to 32 bits.
        segmentused += (4 - (segmentused % 4));

    //printf("Generated trampoline %p for module '%s' 16-bit export '%s'\n", trampoline, module, entry);

    // this hack is not thread safe and only works at all because we don't store this long-term.
    static LxExport lxexp;
    static LxMmaps lxmmap;
    lxexp.addr = trampoline;
    lxexp.object = &lxmmap;
    lxmmap.mapped = lxmmap.addr = segment;
    lxmmap.size = 0x10000;
    lxmmap.alias = selector;
    *_lxexp = &lxexp;

    return trampoline;
} // generateMissingTrampoline16

static __attribute__((noreturn)) void runLxModule(LxModule *lxmod)
{
    uint8 *stack = (uint8 *) ((size_t) lxmod->esp);

    // ...and you pass it the pointer to argv0. This is (at least as far as the docs suggest) appended to the environment table.
    //fprintf(stderr, "jumping into LX land for exe '%s'...! eip=%p esp=%p\n", lxmod->name, (void *) lxmod->eip, stack); fflush(stderr);

    GLoaderState.running = 1;

    __asm__ __volatile__ (
        "movl %%esi,%%esp  \n\t"  // use the OS/2 process's stack.
        "pushl %%eax       \n\t"  // cmd
        "pushl %%ecx       \n\t"  // env
        "pushl $0          \n\t"  // reserved
        "pushl %%edx       \n\t"  // module handle
        "leal 1f,%%eax     \n\t"  // address that entry point should return to.
        "pushl %%eax       \n\t"
        "pushl %%edi       \n\t"  // the OS/2 process entry point (we'll "ret" to it instead of jmp, so stack and registers are all correct).
        "xorl %%eax,%%eax  \n\t"
        "xorl %%ebx,%%ebx  \n\t"
        "xorl %%ecx,%%ecx  \n\t"
        "xorl %%edx,%%edx  \n\t"
        "xorl %%esi,%%esi  \n\t"
        "xorl %%edi,%%edi  \n\t"
        "xorl %%ebp,%%ebp  \n\t"
        "ret               \n\t"  // go to OS/2 land!
        "1:                \n\t"  //  ...and return here.
        "pushl %%eax       \n\t"  // push exit code from OS/2 app.
        "call endLxProcess \n\t"  // never returns.
        // If we returned here, %eax has the exit code from the app.
            : // no outputs.
            : "a" (GLoaderState.pib.pib_pchcmd),
              "c" (GLoaderState.pib.pib_pchenv),
              "d" (lxmod), "S" (stack), "D" (lxmod->eip)
            : "memory"
    );

    (void) endLxProcess;  // make compiler happy.

    __builtin_unreachable();
} // runLxModule

static __attribute__((noreturn)) void runNeModule(LxModule *lxmod)
{
    //fprintf(stderr, "jumping into NE land for exe '%s'...! cs:ip=%X:%X (%p) ss:sp=%X:%X (%p)\n", lxmod->name, (uint) (lxmod->eip >> 16), (uint) (lxmod->eip & 0xFFFF), lxConvert1616to32(lxmod->eip), (uint) (lxmod->esp >> 16), (uint) (lxmod->esp & 0xFFFF), lxConvert1616to32(lxmod->esp)); fflush(stderr);

    GLoaderState.running = 1;

    // According to https://github.com/open-watcom/open-watcom-v2/blob/master/bld/clib/startup/c/maino16.c ,
    //  The stack at startup should have, pushed in this order: cmdline offset, env segment, far* to top of stack, far* to bottom of stack.
    // Microsoft C 5.1 wants things in registers (maybe Watcom does too, elsewhere, and I'm reading it wrong?),
    //  so we do both.
    const uint16 stacksize = (uint16) GLoaderState.mainstacksize;
    const uint16 ss = (lxmod->esp >> 16) & 0xFFFF;  // stack segment
    const uint32 cmd1616 = lxConvert32to1616(GLoaderState.pib.pib_pchcmd);
    const uint16 cmdlineoffset = cmd1616 & 0xFFFF;
    const uint16 envseg = (cmd1616 >> 16) & 0xFFFF;

    uint8 *stack = (uint8 *) lxConvert1616to32(lxmod->esp);
    //printf("stack is at 32=%p 16=%x:%x\n", stack, (uint) ss, (uint) (lxmod->esp & 0xFFFF));
    stack -= 2; *((uint16 *) stack) = cmdlineoffset;  // cmdline offset
    stack -= 2; *((uint16 *) stack) = envseg;  // env segment
    stack -= 2; *((uint16 *) stack) = lxmod->esp & 0xFFFF;  // top of stack
    stack -= 2; *((uint16 *) stack) = ss;
    stack -= 2; *((uint16 *) stack) = (lxmod->esp & 0xFFFF) - stacksize;  // bottom of stack
    stack -= 2; *((uint16 *) stack) = ss;  // bottom of stack

    uint16 selector = 0xFFFF;
    void *segment = lxAllocSegment(&selector, 1);
    assert(segment != NULL);
    assert(selector != 0xFFFF);
    char *ptr = (char *) segment;

/*
; instructions are in Intel syntax here, not AT&T.
USE32
JMP WORD 0xAAAA:0xBBBB  ; jump into 16-bit land (the next instruction!).

USE16
MOV AX,0x1111     ; set stack segment
MOV SS,AX
MOV SP,0x2222     ; set stack pointer
MOV AX,0x3333     ; set data segment
MOV DS,AX
XOR AX,AX         ; set extra segment to zero.
MOV ES,AX
MOV AX,0x4444     ; AX=env segment
MOV BX,0x5555     ; BX=cmdline offset
MOV CX,0x6666     ; CX=size of auto data segment
XOR DX,DX         ; clear remaining general registers.
XOR SI,SI
XOR DI,DI
XOR BP,BP
JMP 0x6666:0x7777 ; jump into actual 16-bit entry point.
INT 0x3           ; if it returns here, crash and burn.
*/

    // USE32
    *(ptr++) = 0x66;  /* jmp word 0xaaaa:0xbbbb... */
    *(ptr++) = 0xEA;  /*  ...jmp word 0xaaaa:0xbbbb */
    const uint16 jmp16offset = 6;
    memcpy(ptr, &jmp16offset, 2); ptr += 2;
    const uint16 jmp16segment = lxSelectorToSegment(selector);
    memcpy(ptr, &jmp16segment, 2); ptr += 2;

    // USE16
    *(ptr++) = 0xB8;  /* mov ax,0x1111... */
    memcpy(ptr, &ss, 2); ptr += 2;
    *(ptr++) = 0x8E;  /* mov ss,ax... */
    *(ptr++) = 0xD0;  /*  ...mov ss,ax */
    *(ptr++) = 0xBC;  /* mov sp,0x2222... */
    const uint16 sp = (uint16) (((size_t) stack) & 0xFFFF);  // top of stack offset
    memcpy(ptr, &sp, 2); ptr += 2;
    *(ptr++) = 0xB8;  /* mov ax,0x3333... */
    const uint16 ds = lxmod->header.ne.auto_data_segment ? lxSelectorToSegment(lxmod->mmaps[lxmod->header.ne.auto_data_segment-1].alias) : ss;  // data segment
    memcpy(ptr, &ds, 2); ptr += 2;
    *(ptr++) = 0x8E;  /* mov ds,ax... */
    *(ptr++) = 0xD8;  /*  ...mov ds,ax */
    *(ptr++) = 0x31;  /* xor ax,ax... */
    *(ptr++) = 0xC0;  /*  ...xor ax,ax */
    *(ptr++) = 0x8E;  /* mov es,ax... */
    *(ptr++) = 0xC0;  /*  ...mov es,ax */
    *(ptr++) = 0xB8;  /* mov ax,0x4444... */
    memcpy(ptr, &envseg, 2); ptr += 2;
    *(ptr++) = 0xBB;  /* mov bx,0x5555... */
    memcpy(ptr, &cmdlineoffset, 2); ptr += 2;
    *(ptr++) = 0xB9;  /* mov cx,0x6666... */
    const uint16 autodatasize = lxmod->autodatasize;
    memcpy(ptr, &autodatasize, 2); ptr += 2;
    *(ptr++) = 0x31;  /* xor dx,dx... */
    *(ptr++) = 0xD2;  /*  ...xor dx,dx */
    *(ptr++) = 0x31;  /* xor si,si... */
    *(ptr++) = 0xF6;  /*  ...xor si,si */
    *(ptr++) = 0x31;  /* xor di,di... */
    *(ptr++) = 0xFF;  /*  ...xor di,di */
    *(ptr++) = 0x31;  /* xor bp,bp... */
    *(ptr++) = 0xED;  /*  ...xor bp,bp */
    *(ptr++) = 0xEA;  /* jmp 0x6666:0x7777... */
    const uint16 ip = (uint16) (lxmod->eip & 0xFFFF);
    memcpy(ptr, &ip, 2); ptr += 2;
    const uint16 cs = (uint16) ((lxmod->eip >> 16) & 0xFFFF);
    memcpy(ptr, &cs, 2); ptr += 2;
    *(ptr++) = 0xCD;  /* int 0x3... */
    *(ptr++) = 0x03;  /*  ...int 0x3 */

    __asm__ __volatile__("jmp *%%eax\n\t" : /* no outputs */ : "a" (segment) : "memory");

    __builtin_unreachable();
} // runNeModule

static __attribute__((noreturn)) void runModule(LxModule *lxmod)
{
    if (lxmod->is_lx) {
        runLxModule(lxmod);
    } else {
        runNeModule(lxmod);
    }
    __builtin_unreachable();
} // runModule

static void runLxLibraryInitOrTerm(LxModule *lxmod, const int isTermination)
{
    uint8 *stack = NULL;

    // force us over to OS/2 main module's stack if we aren't already on it.
    if (!GLoaderState.running)
        stack = (uint8 *) ((size_t) GLoaderState.main_module->esp);

    //fprintf(stderr, "jumping into LX land to %s library '%s'...! eip=%p esp=%p\n", isTermination ? "terminate" : "initialize", lxmod->name, (void *) lxmod->eip, stack); fflush(stderr);

    __asm__ __volatile__ (
        "pushal            \n\t"  // save all the current registers.
        "pushfl            \n\t"  // save all the current flags.
        "movl %%esp,%%ecx  \n\t"  // save our stack to a temporary register.
        "testl %%esi,%%esi \n\t"  // force the OS/2 process's stack?
        "je 1f             \n\t"  // if 0, nope, we're already good.
        "movl %%esi,%%esp  \n\t"  // use the OS/2 process's stack.
        "1:                \n\t"  //
        "pushl %%ecx       \n\t"  // save original stack pointer for real.
        "pushl %%eax       \n\t"  // isTermination
        "pushl %%edx       \n\t"  // library module handle
        "leal 2f,%%eax     \n\t"  // address that entry point should return to.
        "pushl %%eax       \n\t"
        "pushl %%edi       \n\t"  // the OS/2 library entry point (we'll "ret" to it instead of jmp, so stack and registers are all correct).
        "xorl %%eax,%%eax  \n\t"
        "xorl %%ebx,%%ebx  \n\t"
        "xorl %%ecx,%%ecx  \n\t"
        "xorl %%edx,%%edx  \n\t"
        "xorl %%esi,%%esi  \n\t"
        "xorl %%edi,%%edi  \n\t"
        "xorl %%ebp,%%ebp  \n\t"
        "ret               \n\t"  // go to OS/2 land!
        "2:                \n\t"  //  ...and return here.
        "addl $8,%%esp     \n\t"  // drop arguments to entry point.
        "popl %%esp        \n\t"  // restore native process stack now.
        "popfl             \n\t"  // restore our original flags.
        "popal             \n\t"  // restore our original registers.
            : // no outputs
            : "a" (isTermination), "d" (lxmod), "S" (stack), "D" (lxmod->eip)
            : "memory"
    );

    //fprintf(stderr, "...survived time in LX land!\n"); fflush(stderr);

    // !!! FIXME: this entry point returns a result...do we abort if it reports error?
    // !!! FIXME: (actually, DosLoadModule() can report that failure. Abort if (GLoaderState.running == 0), though!)
} // runLxLibraryInitOrTerm

static void runLxLibraryInit(LxModule *lxmod)
{
    // we don't check the LIBINIT flag, because if the EIP fields are valid
    //  but this bit is missing, we're supposed to do "global" initialization,
    //  which I guess means it runs once for all processes on the system
    //  instead of once per process...but we aren't really a full OS/2 system
    //  so maybe it's okay to do global init in isolation...? We'll cross that
    //  bridge when we come to it, I guess. Either way: this is running here,
    //  flag or not.
    if (1) { //if (lxmod->module_flags & 0x4) {  // LIBINIT flag
        if (lxmod->header.lx.eip_object != 0)
            runLxLibraryInitOrTerm(lxmod, 0);
    } // if
} // runLxLibraryInit

static void runLxLibraryTerm(LxModule *lxmod)
{
    // See notes about global initialization in runLxLibraryInit(); it
    //  applies to deinit here, too.
    if (1) { //if (lxmod->module_flags & 0x40000000) {  // LIBTERM flag
        if (lxmod->header.lx.eip_object != 0)
            runLxLibraryInitOrTerm(lxmod, 1);
    } // if
} // runLxLibraryTerm

static void runNeLibraryInit(LxModule *lxmod)
{
    FIXME("write me");
} // runNeLibraryInit

static void runNeLibraryTerm(LxModule *lxmod)
{
    FIXME("write me");
} // runNeLibraryTerm

static void runLibraryTerm(LxModule *lxmod)
{
    if (lxmod->is_lx) {
        runLxLibraryTerm(lxmod);
    } else {
        runNeLibraryTerm(lxmod);
    }
} // runLibraryTerm

static void freeLxModule(LxModule *lxmod)
{
    if (!lxmod)
        return;

    //printf("freeing %s module '%s' (starting refcount=%d) ...\n", lxmod->nativelib ? "native" : lxmod->is_lx ? "LX" : "NE", lxmod->name, (int) lxmod->refcount); fflush(stdout);

    // !!! FIXME: mutex from here
    lxmod->refcount--;
    //fprintf(stderr, "unref'd module '%s' to %u\n", lxmod->name, (uint) lxmod->refcount);
    if (lxmod->refcount > 0)
        return;  // something is still using it.

    if ((lxmod->initialized) && (lxmod != GLoaderState.main_module)) {
        if (!lxmod->nativelib) {
            runLibraryTerm(lxmod);
        } else {
            LxNativeModuleDeinitEntryPoint fn = (LxNativeModuleDeinitEntryPoint) dlsym(lxmod->nativelib, "lxNativeModuleDeinit");
            if (fn)
                fn();
        } // else
    } // if

    if (lxmod->next)
        lxmod->next->prev = lxmod->prev;

    if (lxmod->prev)
        lxmod->prev->next = lxmod->next;

    if (lxmod == GLoaderState.loaded_modules)
        GLoaderState.loaded_modules = lxmod->next;

    if (GLoaderState.main_module == lxmod) {
        GLoaderState.main_module = NULL;
        LxMmaps *lxmmap = lxmod->is_lx ?
            &lxmod->mmaps[lxmod->header.lx.esp_object - 1] :
            &lxmod->mmaps[lxmod->header.ne.reg_ss - 1];
        const uint32 stackbase = (uint32) ((size_t)lxmmap->addr);
        initOs2StackSegments(stackbase, lxmmap->size, 1);
        lxmmap->mapped = NULL;  // don't unmap the stack we're probably using!
    } // if
    // !!! FIXME: mutex to here

    for (uint32 i = lxmod->num_dependencies; i > 0; i--) {
        freeLxModule(lxmod->dependencies[i-1]);
    } // for
    free(lxmod->dependencies);

    for (uint32 i = 0; i < lxmod->num_mmaps; i++) {
        if (lxmod->mmaps[i].alias != 0xFFFF)
            lxFreeSelector(lxmod->mmaps[i].alias);
        if (lxmod->mmaps[i].mapped)
            munmap(lxmod->mmaps[i].mapped, lxmod->mmaps[i].size);
    } // for
    free(lxmod->mmaps);    

    if (lxmod->nativelib)
        dlclose(lxmod->nativelib);
    else {
        for (uint32 i = 0; i < lxmod->num_exports; i++)
            free((void *) lxmod->exports[i].name);
        free((void *) lxmod->exports);
    } // else

    //printf("Freed module '%s'\n", lxmod->name); fflush(stdout);
    free(lxmod);
} // freeLxModule

static LxModule *loadModuleByModuleNameInternal(const char *modname, const int dependency_tree_depth);

static void *getModuleProcAddrByOrdinal(const LxModule *module, const uint32 ordinal, const LxExport **_lxexp, const int want16bit, const int native)
{
    //printf("lookup module == '%s', ordinal == %u\n", module->name, (uint) ordinal);

    const LxExport *lxexp = module->exports;
    for (uint32 i = 0; i < module->num_exports; i++, lxexp++) {
        if (lxexp->ordinal == ordinal) {
            if (_lxexp)
                *_lxexp = lxexp;
            void *retval = lxexp->addr;
            if (native && lxexp->object && lxexp->object->alias != 0xFFFF)  // 16-bit bridge? this is actually a void**, due to some macro salsa.  :/
                retval = *((void**) retval);
            return retval;
        } // if
    } // for

    if (_lxexp)
        *_lxexp = NULL;

    #if 1
    char entry[128];
    snprintf(entry, sizeof (entry), "ordinal #%u", (uint) ordinal);
    return want16bit ? generateMissingTrampoline16(module->name, entry, _lxexp) : generateMissingTrampoline(module->name, entry);
    #else
    return NULL;
    #endif
} // getModuleProcAddrByOrdinal

static void *getModuleProcAddrByName(const LxModule *module, const char *name, const LxExport **_lxexp, const int want16bit)
{
    //printf("lookup module == '%s', name == '%s'\n", module->name, name);

    const LxExport *lxexp = module->exports;
    for (uint32 i = 0; i < module->num_exports; i++, lxexp++) {
        if (strcmp(lxexp->name, name) == 0) {
            if (_lxexp)
                *_lxexp = lxexp;
            void *retval = lxexp->addr;
            if (lxexp->object && lxexp->object->alias != 0xFFFF)  // 16-bit bridge? this is actually a void**, due to some macro salsa.  :/
                retval = *((void**) retval);
            return retval;
        } // if
    } // for

    if (_lxexp)
        *_lxexp = NULL;

    #if 1
    return want16bit ? generateMissingTrampoline16(module->name, name, _lxexp) : generateMissingTrampoline(module->name, name);
    #else
    return NULL;
    #endif
} // getModuleProcAddrByName

static void doFixup(uint8 *page, const sint16 offset, const uint32 finalval, const uint16 finalval2, const uint32 finalsize)
{
    #if 0
    if (finalsize == 6) {
        printf("fixing up %p to 0x%X:0x%X (6 bytes)...\n", page + offset, (uint) finalval2, (uint) finalval);
    } else {
        printf("fixing up %p to 0x%X (%u bytes)...\n", page + offset, (uint) finalval, (uint) finalsize);
    } // else
    fflush(stdout);
    #endif

    switch (finalsize) {
        case 1: { uint8 *dst = (uint8 *) (page + offset); *dst = (uint8) finalval; } break;
        case 2: { uint16 *dst = (uint16 *) (page + offset); *dst = (uint16) finalval; } break;
        case 4: { uint32 *dst = (uint32 *) (page + offset); *dst = (uint32) finalval; } break;
        case 6: {
            uint32 *dst1 = (uint32 *) (page + offset);
            *dst1 = (uint32) finalval;
            uint16 *dst2 = (uint16 *) (page + offset + 4);
            *dst2 = (uint16) finalval2;
            break;
        } // case
        default:
            fprintf(stderr, "BUG! Unexpected fixup final size of %u\n", (uint) finalsize);
            exit(1);
    } // switch
} // doFixup

static void fixupPage(const uint8 *exe, LxModule *lxmod, const LxObjectTableEntry *obj, const uint32 pagenum, uint8 *page)
{
    assert(lxmod->is_lx);  // this should only be called from the LX loader.
    const LxHeader *lx = &lxmod->header.lx;
//const LxObjectTableEntry *origobj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset));
    const uint32 *fixuppage = (((const uint32 *) (exe + lx->fixup_page_table_offset)) + ((obj->page_table_index - 1) + pagenum));
    const uint32 fixupoffset = *fixuppage;
    const uint32 fixuplen = fixuppage[1] - fixuppage[0];
    const uint8 *fixup = (exe + lx->fixup_record_table_offset) + fixupoffset;
    const uint8 *fixupend = fixup + fixuplen;
//int record = 0;
    while (fixup < fixupend) {
//record++; printf("FIXUP obj #%u page #%u record #: %d\n", (uint) (obj - origobj), (uint) pagenum, record);
        const uint8 srctype = *(fixup++);
        const uint8 fixupflags = *(fixup++);
        uint8 srclist_count = 0;
        sint16 srcoffset = 0;

        uint32 finalval = 0;
        uint16 finalval2 = 0;
        uint32 finalsize = 0;
        int allow_fixup_to_alias = 0;

        switch (srctype & 0xF) {
            case 0x0:  // byte fixup
                finalsize = 1;
                break;
            case 0x2:  // 16-bit selector fixup
                finalsize = 2;
                allow_fixup_to_alias = 1;
                break;
            case 0x5:  // 16-bit offset fixup
                finalsize = 2;
                break;
            case 0x3:  // 16:16 pointer fixup
                finalsize = 4;
                allow_fixup_to_alias = 1;
                break;
            case 0x7:  // 32-bit offset fixup
            case 0x8:  // 32-bit self-relative offset fixup
                finalsize = 4;
                break;
            case 0x6:  // 16:32 pointer fixup
                finalval2 = GLoaderState.original_cs;
                finalsize = 6;
                allow_fixup_to_alias = 1;
                break;
        } // switch

        const int fixup_to_alias = ((srctype & 0x10) != 0);
        if (fixup_to_alias) {  // fixup to alias flag.
            if (!allow_fixup_to_alias)
                fprintf(stderr, "WARNING: Bogus fixup srctype (%u) with fixup-to-alias flag!\n", (uint) (srctype & 0xF));
        } // if

        if (srctype & 0x20) { // contains a source list
            srclist_count = *(fixup++);
        } else {
            srcoffset = *((sint16 *) fixup);
            fixup += 2;
        } // else

        switch (fixupflags & 0x3) {
            case 0x0: { // Internal fixup record
                uint16 objectid = 0;
                if (fixupflags & 0x40) { // 16 bit value
                    objectid = *((uint16 *) fixup); fixup += 2;
                } else {
                    objectid = (uint16) *(fixup++);
                } // else

                uint32 targetoffset = 0;

                if ((srctype & 0xF) != 2) {  // not used for 16-bit selector fixups.
                    if (fixupflags & 0x10) {  // 32-bit target offset
                        targetoffset = *((uint32 *) fixup); fixup += 4;
                    } else {  // 16-bit target offset
                        targetoffset = (uint32) (*((uint16 *) fixup)); fixup += 2;
                    } // else
                } // if

                if (!fixup_to_alias) {
                    const uint32 base = (uint32) (size_t) lxmod->mmaps[objectid - 1].addr;
                    finalval = base + targetoffset;
                } else {
                    if (lxmod->mmaps[objectid - 1].alias == 0xFFFF) {
                        fprintf(stderr, "uhoh, need a 16-bit alias fixup, but object has no 16:16 alias!\n");
                    } else {
                        const uint16 segment = lxSelectorToSegment(lxmod->mmaps[objectid - 1].alias);
                        switch (srctype & 0xF) {
                            case 0x2: finalval = segment; break; // 16-bit selector fixup?
                            case 0x3: finalval = (((uint32) segment) << 16) | targetoffset; break; // 16:16 pointer fixup
                            case 0x6: finalval = targetoffset; finalval2 = segment; break; // 16:32 pointer fixup
                            default: assert(!"shouldn't hit this code"); break;
                        } // switch
                    } // else
                } // else
                break;
            } // case

            case 0x1: { // Import by ordinal fixup record
                uint16 moduleid = 0;  // module ordinal
                if (fixupflags & 0x40) { // 16 bit value
                    moduleid = *((uint16 *) fixup); fixup += 2;
                } else {
                    moduleid = (uint16) *(fixup++);
                } // else

                uint32 importid = 0;  // import ordinal
                if (fixupflags & 0x80) { // 8 bit value
                    importid = (uint32) *(fixup++);
                } else if (fixupflags & 0x10) {  // 32-bit value
                    importid = *((uint32 *) fixup); fixup += 4;
                } else {  // 16-bit value
                    importid = (uint32) *((uint16 *) fixup); fixup += 2;
                } // else

                const LxExport *lxexp = NULL;
                if (moduleid == 0) {
                    fprintf(stderr, "uhoh, looking for module ordinal 0, which is illegal.\n");
                } else if (moduleid > lx->num_import_mod_entries) {
                    fprintf(stderr, "uhoh, looking for module ordinal %u, but only %u available.\n", (uint) moduleid, (uint) lx->num_import_mod_entries);
                } else {
                    //printf("import-by-ordinal fixup: module=%u import=%u\n", (uint) moduleid, (uint) importid);
                    finalval = (uint32) (size_t) getModuleProcAddrByOrdinal(lxmod->dependencies[moduleid-1], importid, &lxexp, fixup_to_alias, 1);
                } // else

                if (fixup_to_alias) {
                    if (!lxexp || !lxexp->object || (lxexp->object->alias == 0xFFFF)) {
                        fprintf(stderr, "uhoh, couldn't find a selector for a fixup-to-alias address!\n");
                    } else {
                        const uint16 segment = lxSelectorToSegment(lxexp->object->alias);
                        switch (srctype & 0xF) {
                            case 0x2: finalval = segment; break; // 16-bit selector fixup?  !!! FIXME:
                            case 0x3: finalval = (((uint32) segment) << 16) | (finalval - ((uint32)lxexp->object->addr)); break; // 16:16 pointer fixup
                            case 0x6: finalval -= ((uint32)lxexp->object->addr); finalval2 = segment; break; // 16:32 pointer fixup
                            default: assert(!"shouldn't hit this code"); break;
                        } // switch
                    } // else
                } // else
                break;
            } // case

            case 0x2: { // Import by name fixup record
                uint16 moduleid = 0;  // module ordinal
                if (fixupflags & 0x40) { // 16 bit value
                    moduleid = *((uint16 *) fixup); fixup += 2;
                } else {
                    moduleid = (uint16) *(fixup++);
                } // else

                uint32 name_offset = 0;
                if (fixupflags & 0x10) {  // 32-bit value
                    name_offset = *((uint32 *) fixup); fixup += 4;
                } else {  // 16-bit value
                    name_offset = *((uint16 *) fixup); fixup += 2;
                } // else

                const LxExport *lxexp = NULL;
                if (moduleid == 0) {
                    fprintf(stderr, "uhoh, looking for module ordinal 0, which is illegal.\n");
                } else if (moduleid > lx->num_import_mod_entries) {
                    fprintf(stderr, "uhoh, looking for module ordinal %u, but only %u available.\n", (uint) moduleid, (uint) lx->num_import_mod_entries);
                } else {
                    const uint8 *import_name = (exe + lx->import_proc_table_offset) + name_offset;
                    char name[128];
                    const uint8 namelen = *(import_name++) & 0x7F;  // the top bit is reserved.
                    memcpy(name, import_name, namelen);
                    name[namelen] = '\0';
                    //printf("import-by-name fixup: module=%u import='%s'\n", (uint) moduleid, name);
                    finalval = (uint32) (size_t) getModuleProcAddrByName(lxmod->dependencies[moduleid-1], name, &lxexp, fixup_to_alias);
                } // else

                if (fixup_to_alias) {
                    if (!lxexp || !lxexp->object || (lxexp->object->alias == 0xFFFF)) {
                        fprintf(stderr, "uhoh, couldn't find a selector for a fixup-to-alias address!\n");
                    } else {
                        const uint16 segment = lxSelectorToSegment(lxexp->object->alias);
                        switch (srctype & 0xF) {
                            case 0x2: finalval = segment; break; // 16-bit selector fixup?  !!! FIXME:
                            case 0x3: finalval = (((uint32) segment) << 16) | (finalval - ((uint32)lxexp->object->addr)); break; // 16:16 pointer fixup
                            case 0x6: finalval -= ((uint32)lxexp->object->addr); finalval2 = segment; break; // 16:32 pointer fixup
                            default: assert(!"shouldn't hit this code"); break;
                        } // switch
                    } // else
                } // else
                break;
            } // case

            case 0x3: { // Internal entry table fixup record
                FIXME("FIXUP 0x3 WRITE ME");
                assert(!"write me");
                _exit(1);
                break;
            } // case
        } // switch

        if (fixupflags & 0x4) {  // Has additive.
            uint32 additive = 0;
            if (fixupflags & 0x20) { // 32-bit value
                additive = *((uint32 *) fixup); fixup += 4;
            } else {  // 16-bit value
                additive = (uint32) *((uint16 *) fixup);
                fixup += 2;
            } // else
            finalval += additive;
        } // if

        if (srctype & 0x20) {  // source list
            for (uint8 i = 0; i < srclist_count; i++) {
                const sint16 offset = *((sint16 *) fixup); fixup += 2;
                if ((srctype & 0xF) == 0x08) {  // self-relative fixup?
                    assert(finalval2 == 0);
                    doFixup(page, offset, finalval - (((uint32)page)+offset+4), 0, finalsize);
                } else {
                    doFixup(page, offset, finalval, finalval2, finalsize);
                } // else
            } // for
        } else {
            if ((srctype & 0xF) == 0x08) {  // self-relative fixup?
                assert(finalval2 == 0);
                doFixup(page, srcoffset, finalval - (((uint32)page)+srcoffset+4), 0, finalsize);
            } else {
                doFixup(page, srcoffset, finalval, finalval2, finalsize);
            } // else
        } // else
    } // while
} // fixupPage

static int loadLxNameTable(LxModule *lxmod, const uint8 *name_table)
{
    for (uint32 i = 0; *name_table; i++) {
        const uint8 namelen = *(name_table++);
        const uint8 *nameptr = name_table;
        name_table += namelen;
        const uint16 ordinal = *((const uint16 *) name_table); name_table += 2;

        if (ordinal == 0)
            continue;  // skip this one.

        LxExport *export = (LxExport *) lxmod->exports;
        const uint32 num_exports = lxmod->num_exports;
        for (uint32 i = 0; i < num_exports; i++, export++) {
            if (export->ordinal == ordinal) {
                char *name = (char *) malloc(namelen + 1);
                if (!name) {
                    fprintf(stderr, "Out of memory!\n");
                    return 0;
                } // if
                memcpy(name, nameptr, namelen);
                name[namelen] = '\0';
                export->name = name;
                break;
            } // if
        } // for
    } // for

    return 1;
} // loadLxNameTable


// This is how I understand this mess, which I could be wrong about.
// Apparently 32-bit ring-3 code on OS/2 uses code segment 0x5B, even though
//  generally 32-bit apps don't think about their _segment_ because they have
//  a linear address space. This is true on Linux, too, and the kernel
//  hardcodes a segment for user-space code (0x23 at the moment, at least for
//  x86-32 code running on an x86-64 kernel. It's a different hardcoded number
//  on x86-32 kernels, too).
// Code (from IBM!) exists in the wild that hardcodes references to the OS/2
//  code segment, which means if the linear address space isn't on that
//  segment, programs will crash if they explicitly reference it.
// This is hard to map around on Linux because we can't meaningfully mess with
//  the GDT in a flexible way.
// In theory, the only place we should ever see a hardcoded reference to
//  CS 0x5B is in a 16-bit thunk that is trying to jump back to 32-bit mode.
//  16-bit code wouldn't otherwise reference it, being 16-bit, and 32-bit
//  code wouldn't need to explicitly reference it.
//  Even still, most tools (EMX, etc) don't do this. I couldn't find any
//  mention that OS/2 guarantees this code segment will exist, just that it
//  seems to map 32-bit apps there and it's probably normal since Linux uses
//  roughly the same approach. If it had ever changed, it would definitely
//  break some programs.
// So we go through object pages that are marked as 16:16 and EXEC, and look
//  for things that look like absolute jmps to code segment 0x5B, and patch
//  them up to the host's actual linear address code segment. This is nasty,
//  but oh well.
static void fixupLinearCodeSegmentReferences(uint8 *addr, size_t size)
{
    // in lieu of writing a full x86 instruction decoder, we just look for
    //  0xEA (JMP LARGE FAR PTR ADDR), skip 4 bytes (the 32-bit offset), and
    //  see if the next two bytes are 0x5B 0x00 (the 16-bit segment). This is
    //  wrong but "good enough" for now.
    while (size >= 7) {
        size--;
        if (*(addr++) == 0xEA) {
            if ((addr[4] == 0x5B) && (addr[5] == 0x00)) {
                //printf("Patching out code segment 0x5B at %p\n", addr+4);
                memcpy(&addr[4], &GLoaderState.original_cs, 2);
                size -= 6;
                addr += 6;
            } // if
        } // if
    } // while
} // fixupLinearCodeSegmentReferences

/* LX ("Linear Executable") modules are 32-bit binaries that can contain 16-bit segments. */
// !!! FIXME: break up this function.
static LxModule *loadLxModule(const char *fname, const uint8 *origexe, uint8 *exe, uint32 exelen, int dependency_tree_depth)
{
    const LxHeader *lx = (const LxHeader *) exe;
    const uint32 module_type = lx->module_flags & 0x00038000;

    if ((module_type != 0x0000) && (module_type != 0x8000)) {
        fprintf(stderr, "This is not a standard .exe or .dll (maybe device driver?)\n");
        goto loadlx_failed;
    } else if (lx->module_flags & 0x2000) {
        fprintf(stderr, "This module is marked as not loadable.\n");
        goto loadlx_failed;
    } // if

    const int isDLL = (module_type == 0x8000);

    if (isDLL && !GLoaderState.main_module) {
        fprintf(stderr, "uhoh, need to load an .exe before a .dll!\n");
        goto loadlx_failed;
    } else if (!isDLL && GLoaderState.main_module) {
        fprintf(stderr, "uhoh, loading an .exe after already loading one!\n");
        goto loadlx_failed;
    } // if else if

    LxModule *retval = (LxModule *) malloc(sizeof (LxModule));
    if (!retval) {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    } // if
    memset(retval, '\0', sizeof (*retval));
    retval->is_lx = 1;
    retval->refcount = 1;
    retval->dependencies = (LxModule **) malloc(sizeof (LxModule *) * lx->num_import_mod_entries);
    retval->mmaps = (LxMmaps *) malloc(sizeof (LxMmaps) * lx->module_num_objects);
    if (!retval->dependencies || !retval->mmaps) {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    } // if
    memset(retval->dependencies, '\0', sizeof (LxModule *) * lx->num_import_mod_entries);
    memset(retval->mmaps, '\0', sizeof (LxMmaps) * lx->module_num_objects);
    retval->num_dependencies = lx->num_import_mod_entries;
    retval->num_mmaps = lx->module_num_objects;
    for (int i = 0; i < retval->num_mmaps; i++)
        retval->mmaps[i].alias = 0xFFFF;

    memcpy(&retval->header.lx, lx, sizeof (*lx));

    if (lx->resident_name_table_offset) {
        const uint8 *name_table = exe + lx->resident_name_table_offset;
        const uint8 namelen = *(name_table++);
        char *ptr = retval->name;
        for (uint32 i = 0; i < namelen; i++) {
            const char ch = *(name_table++);
            *(ptr++) = ((ch >= 'a') && (ch <= 'z')) ? (ch + ('A' - 'a')) : ch;
        } // for
        *ptr = '\0';
    } // if

    if (!isDLL) {
        GLoaderState.main_module = retval;
        GLoaderState.pib.pib_hmte = retval;
    } // else if

    const char *modname = retval->name;
    //printf("ref'd new module '%s' to %u\n", modname, 1);

    // !!! FIXME: apparently OS/2 does 1024, but they're not loading the module into RAM each time.
    // !!! FIXME: the spec mentions the 1024 thing for "forwarder" records in the entry table,
    // !!! FIXME:  specifically. _Can_ two DLLs depend on each other? We'll have to load both and
    // !!! FIXME:  then fix them up without recursing.
    if (++dependency_tree_depth > 32) {
        fprintf(stderr, "Likely circular dependency in module '%s'\n", modname);
            goto loadlx_failed;
    } // if

    int uses_aliases = 0;
    const LxObjectTableEntry *obj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++) {
        if (obj->object_flags & 0x8)  // !!! FIXME: resource object; ignore this until resource support is written, later.
            continue;

        uint32 vsize = obj->virtual_size;
        if ((vsize % lx->page_size) != 0)
             vsize += lx->page_size - (vsize % lx->page_size);

        const int needs_1616_alias = ((obj->object_flags & 0x1000) != 0);  // Object needs a 16:16 alias.
        if (needs_1616_alias) {
            if (vsize > 0x10000) {
                fprintf(stderr, "Module '%s' object #%u needs a 16:16 alias, but is > 64k\n", modname, (uint) (i+1));
                goto loadlx_failed;  // I guess this is an error...?
            } // if
            uses_aliases = 1;

            if (isDLL)  // (specifically: if !MAP_FIXED)
                vsize += 0x10000;  // make sure we can align to a 64k boundary.
        } // if

        const int mmapflags = MAP_ANONYMOUS | MAP_PRIVATE | (isDLL ? 0 : MAP_FIXED);
        void *base = isDLL ? NULL : (void *) ((size_t) obj->reloc_base_addr);
        void *mmapaddr = mmap(base, vsize, PROT_READ|PROT_WRITE, mmapflags, -1, 0);
        // we'll mprotect() these pages to the proper permissions later.

        //fprintf(stderr, "%s lxobj #%u mmap(%p, %u, %c%c%c, ANON|PRIVATE%s, -1, 0) == %p\n", retval->name, (uint) i, base, (uint) vsize, (obj->object_flags & 0x1) ? 'R' : '-', (obj->object_flags & 0x2) ? 'W' : '-', (obj->object_flags & 0x4) ? 'X' : '-', isDLL ? "" : "|FIXED", mmapaddr);

        if (mmapaddr == ((void *) MAP_FAILED)) {
            fprintf(stderr, "mmap(%p, %u, RW-, ANON|PRIVATE%s, -1, 0) failed (%d): %s\n",
                    base, (uint) vsize, isDLL ? "" : "|FIXED", errno, strerror(errno));
            goto loadlx_failed;
        } // if

        retval->mmaps[i].mapped = mmapaddr;
        retval->mmaps[i].size = vsize;

        // force objects with a 16:16 alias to a 64k boundary.
        size_t adjust = (size_t) mmapaddr;
        if (needs_1616_alias && ((adjust % 0x10000) != 0)) {
            assert(isDLL);  // this must be fixed to a specific address, hopefully handled this...?
            const size_t diff = 0x10000 - (adjust % 0x10000);
            vsize -= diff;
            adjust += diff;
            mmapaddr = (void *) adjust;
        } // if

        FIXME("Can we just unmap the adjusted pieces?");  // instead of keeping an extra pointer and wasted space...
        retval->mmaps[i].addr = mmapaddr;

        const LxObjectPageTableEntry *objpage = ((const LxObjectPageTableEntry *) (exe + lx->object_page_table_offset));
        objpage += obj->page_table_index - 1;

        uint8 *dst = (uint8 *) mmapaddr;
        const uint32 numPageTableEntries = obj->num_page_table_entries;
        for (uint32 pagenum = 0; pagenum < numPageTableEntries; pagenum++, objpage++) {
            const uint8 *src;
            switch (objpage->flags) {
                case 0x00:  // preload
                    src = origexe + lx->data_pages_offset + (objpage->page_data_offset << lx->page_offset_shift);
                    memcpy(dst, src, objpage->data_size);
                    if (objpage->data_size < lx->page_size) {
                        memset(dst + objpage->data_size, '\0', lx->page_size - objpage->data_size);
                    }
                    break;

                case 0x01:  // iterated data.
                    // LX format docs say object_iter_pages_offset must be zero or equal to data_pages_offset for OS/2 2.0. So just use data_pages_offset here.
                    //src = origexe + lx->object_iter_pages_offset + (objpage->page_data_offset << lx->page_offset_shift);
                    src = origexe + lx->data_pages_offset + (objpage->page_data_offset << lx->page_offset_shift);
                    if (!decompressIterated(dst, lx->page_size, src, objpage->data_size)) {
                        fprintf(stderr, "Failed to decompress iterated object page (corrupt file or bug).\n");
                        goto loadlx_failed;
                    } // if
                    break;

                case 0x02: // INVALID, just zero it out.
                case 0x03: // ZEROFILL, just zero it out.
                    memset(dst, '\0', lx->page_size);
                    break;

                case 0x05:  // exepack2
                    src = origexe + lx->data_pages_offset + (objpage->page_data_offset << lx->page_offset_shift);
                    if (!decompressExePack2(dst, lx->page_size, src, objpage->data_size)) {
                        fprintf(stderr, "Failed to decompress object page (corrupt file or bug).\n");
                        goto loadlx_failed;
                    } // if
                    break;

                case 0x08: // !!! FIXME: this is listed in lxlite, presumably this is exepack3 or something. Maybe this was new to Warp4, like exepack2 was new to Warp3. Pull this algorithm in from lxlite later.
                default:
                    fprintf(stderr, "Don't know how to load an object page of type %u!\n", (uint) objpage->flags);
                    goto loadlx_failed;
            } // switch

            dst += lx->page_size;
        } // for

        // any bytes at the end of this object that we didn't initialize? Zero them out.
        const uint32 remain = vsize - ((uint32) (dst - ((uint8 *) mmapaddr)));
        if (remain)
            memset(dst, '\0', remain);
    } // for

    // All the pages we need from the EXE are loaded into the appropriate spots in memory.
    //printf("mmap()'d everything we need!\n");

    retval->eip = lx->eip;
    if (lx->eip_object != 0) {
        const uint32 base = (uint32) ((size_t)retval->mmaps[lx->eip_object - 1].addr);
        retval->eip += base;
    } // if

    // !!! FIXME: esp==0 means something special for programs (and is ignored for library init).
    // !!! FIXME: "A zero value in this field indicates that the stack pointer is to be initialized to the highest address/offset in the object"
    if (!isDLL) {
        assert(lx->esp_object != 0);
        const LxMmaps *lxmmap = &retval->mmaps[lx->esp_object - 1];
        const uint32 stackbase = (uint32) ((size_t)lxmmap->addr);
        const uint32 stacksize = (uint32) lxmmap->size;
        retval->esp = lx->esp + stackbase;
        GLoaderState.mainstacksize = stacksize;

        // This needs to be set up now, so it's available to any library
        //  init code that runs in LX land.
        GLoaderState.initOs2Tib(GLoaderState.main_tibspace, (void *) ((size_t) retval->esp), stacksize, 0);
        GLoaderState.main_tib_selector = GLoaderState.setOs2Tib(GLoaderState.main_tibspace);
        initOs2StackSegments(stackbase, stacksize, 0);
    } // if

    // now we have the stack tiled--and it got the specific selectors it
    //  needed--let's start making 16:16 aliases for the appropriate objects.
    if (uses_aliases) {
        obj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset));
        for (uint32 i = 0; i < lx->module_num_objects; i++, obj++) {
            if ((obj->object_flags & 0x1000) == 0)
                continue;  // Object doesn't need a 16:16 alias, skip it.
            uint16 offset = 0;
            const int iscode = (obj->object_flags & 0x4) ? 1 : 0;
            if (!lxFindSelector((uint32) (size_t) retval->mmaps[i].addr, &retval->mmaps[i].alias, &offset, iscode)) {
                fprintf(stderr, "Ran out of LDT entries getting a 16:16 alias for '%s' object #%u!\n", modname, (uint) (i + 1));
                goto loadlx_failed;
            } // if
            assert(offset == 0);

            if (obj->object_flags & 0x4) {  // 16:16 segment with code.
                fixupLinearCodeSegmentReferences((uint8 *) (size_t) retval->mmaps[i].mapped, retval->mmaps[i].size);
            }
        } // for
    } // if

    // Set up our exports...
    uint32 total_ordinals = 0;
    const uint8 *entryptr = exe + lx->entry_table_offset;
    while (*entryptr) {  /* end field has a value of zero. */
        const uint8 numentries = *(entryptr++);  /* number of entries in this bundle */
        const uint8 bundletype = *(entryptr++) & ~0x80;
        if (bundletype != 0x00) {
            total_ordinals += numentries;
            entryptr += 2 + ((bundletype == 0x01) ? 3 : 5) * numentries;
        } // if
    } // while

    LxExport *lxexp = (LxExport *) malloc(sizeof (LxExport) * total_ordinals);
    if (!lxexp) {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    } // if
    memset(lxexp, '\0', sizeof (LxExport) * total_ordinals);
    retval->exports = lxexp;

    uint32 ordinal = 1;
    entryptr = exe + lx->entry_table_offset;
    while (*entryptr) {  /* end field has a value of zero. */
        const uint8 numentries = *(entryptr++);  /* number of entries in this bundle */
        const uint8 bundletype = *(entryptr++) & ~0x80;
        uint16 objidx = 0;

        switch (bundletype) {
            case 0x00: // UNUSED
                ordinal += numentries;
                break;

            case 0x01: // 16BIT
                objidx = *((const uint16 *) entryptr) - 1;
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++) {
                    entryptr++;
                    lxexp->ordinal = ordinal++;
                    lxexp->addr = ((uint8 *) retval->mmaps[objidx].addr) + *((const uint16 *) entryptr);
                    lxexp->object = &retval->mmaps[objidx];
                    lxexp++;
                    entryptr += 2;
                } // for
                break;

            case 0x03: // 32BIT
                objidx = *((const uint16 *) entryptr) - 1;
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++) {
                    entryptr++;
                    lxexp->ordinal = ordinal++;
                    lxexp->addr = ((uint8 *) retval->mmaps[objidx].addr) + *((const uint32 *) entryptr);
                    lxexp->object = &retval->mmaps[objidx];
                    lxexp++;
                    entryptr += 4;
                }
                break;

            case 0x02: // 286CALLGATE
            case 0x04: // FORWARDER
                fprintf(stderr, "WRITE ME %s:%d\n", __FILE__, __LINE__);
                goto loadlx_failed;

            default:
                fprintf(stderr, "UNKNOWN ENTRY TYPE (%u)\n\n", (uint) bundletype);
                goto loadlx_failed;
        } // switch
    } // while

    retval->num_exports = (uint32) (lxexp - retval->exports);

    // Now load named entry points.
    if (lx->resident_name_table_offset) {
        if (!loadLxNameTable(retval, exe + lx->resident_name_table_offset))
            goto loadlx_failed;
    } // if

    if (lx->non_resident_name_table_offset && lx->non_resident_name_table_len) {
        if (!loadLxNameTable(retval, origexe + lx->non_resident_name_table_offset))
            goto loadlx_failed;
    } // if

    // Load other dependencies of this module.
    const uint8 *import_modules_table = exe + lx->import_module_table_offset;
    for (uint32 i = 0; i < lx->num_import_mod_entries; i++) {
        const uint8 namelen = *(import_modules_table++);
        if (namelen > 127) {
            fprintf(stderr, "Import module %u name is > 127 chars (%u). Corrupt file or bug!\n", (uint) (i + 1), (uint) namelen);
            goto loadlx_failed;
        }
        char name[128];
        memcpy(name, import_modules_table, namelen);
        import_modules_table += namelen;
        name[namelen] = '\0';
        retval->dependencies[i] = loadModuleByModuleNameInternal(name, dependency_tree_depth);
        if (!retval->dependencies[i]) {
            fprintf(stderr, "Failed to load dependency '%s' for module '%s'\n", name, modname);
            goto loadlx_failed;
        } // if
    } // for

    // Run through again and do all the fixups...
    obj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++) {
        if (obj->object_flags & 0x8)  // !!! FIXME: resource object; ignore this until resource support is written, later.
            continue;

        uint8 *dst = (uint8 *) retval->mmaps[i].addr;
        const uint32 numPageTableEntries = obj->num_page_table_entries;
        for (uint32 pagenum = 0; pagenum < numPageTableEntries; pagenum++) {
            fixupPage(exe, retval, obj, pagenum, dst);
            dst += lx->page_size;
        } // for

        // !!! FIXME: hack to nop out some 16-bit code in emx.dll startup...
        if ((i == 1) && (strcasecmp(modname, "EMX") == 0)) {
            // This is the 16-bit signal handler installer. nop it out.
            uint8 *ptr = ((uint8 *) retval->mmaps[1].addr) + 28596;
            for (uint32 j = 0; j < 37; j++)
                *(ptr++) = 0x90; // nop
        } // if

        // !!! FIXME: hack for Java that fails to mark a code page as executable
        // !!! FIXME:  (on 386 machines, read implies execute, so this error could go unnoticed).
        uint32 object_flags = obj->object_flags;
        if ((i == 4) && (strcasecmp(modname, "HPI") == 0)) {
            fprintf(stderr, "fixed page permissions to workaround bug in Java DLL\n");
            object_flags |= 0x4;
        } // if

        // Now set all the pages of this object to the proper final permissions...
        const int prot = ((object_flags & 0x1) ? PROT_READ : 0) |
                         ((object_flags & 0x2) ? PROT_WRITE : 0) |
                         ((object_flags & 0x4) ? PROT_EXEC : 0);

        retval->mmaps[i].prot = prot;

        if (mprotect(retval->mmaps[i].mapped, retval->mmaps[i].size, prot) == -1) {
            fprintf(stderr, "mprotect(%p, %u, %s%s%s, ANON|PRIVATE|FIXED, -1, 0) failed (%d): %s\n",
                    retval->mmaps[i].addr, (uint) retval->mmaps[i].size,
                    (prot&PROT_READ) ? "R" : "-",
                    (prot&PROT_WRITE) ? "W" : "-",
                    (prot&PROT_EXEC) ? "X" : "-",
                    errno, strerror(errno));
            goto loadlx_failed;
        } // if
    } // for

    retval->os2path = makeOS2Path(fname);
    if (!retval->os2path)
        goto loadlx_failed;

    if (!isDLL) {
        retval->initialized = 1;
    } else {
        // call library init code...
        assert(GLoaderState.main_module != NULL);
        assert(GLoaderState.main_module != retval);
        runLxLibraryInit(retval);

        retval->initialized = 1;

        // module is ready to use, put it in the loaded list.
        // !!! FIXME: mutex this
        if (GLoaderState.loaded_modules) {
            retval->next = GLoaderState.loaded_modules;
            GLoaderState.loaded_modules->prev = retval;
        } // if
        GLoaderState.loaded_modules = retval;
    } // if

    return retval;

loadlx_failed:
    freeLxModule(retval);
    return NULL;
} // loadLxModule


static int fixupNeSegment(const NeSegmentTableEntry *seg, LxModule *lxmod, uint8 *dst, const uint32 sector_shift, const uint8 *origexe, uint8 *exe, uint32 exelen)
{
    assert(!lxmod->is_lx);  // this should only be called from the NE loader.

    if ((seg->segment_flags & 0x100) == 0) {
        return 1;  // no fixups here.
    } // if

    const uint8 *segptr = origexe + (seg->offset << sector_shift);
    const uint8 *fixupptr = segptr + (seg->size ? seg->size : 0xFFFF);
    const uint32 num_fixups = (uint32) *((const uint16 *) fixupptr); fixupptr += 2;

    for (uint32 i = 0; i < num_fixups; i++) {
        const uint8 srctype = *(fixupptr++);
        const uint8 flags = *(fixupptr++);
        uint32 srcchain_offset = (uint32) *((const uint16 *) fixupptr); fixupptr += 2;
        const uint8 *targetptr = fixupptr; fixupptr += 4;
        const int additive = (flags & 0x4) != 0;
        // confusingly, what we write to is the "source" and where we read from is the "target."
        uint32 target = 0;

        switch (flags & 0x3) {
            case 0: { // INTERNALREF
                const LxExport *lxexp = NULL;
                const uint8 segment = targetptr[0];
                //const uint8 reserved = targetptr[1];
                const uint16 offset = *((const uint16 *) &targetptr[2]);
                if (segment == 0xFF) {  // this is like IMPORTORDINAL with ourselves as the module.
                    void *addr = getModuleProcAddrByOrdinal(lxmod, offset, &lxexp, 1, 0);
                    if (addr) {
                        target = lxConvert32to1616(addr);
                    }
                } else {
                    target = (lxSelectorToSegment(lxmod->mmaps[segment-1].alias) << 16) | offset;
                }
            }
            break;

            case 1: { // IMPORTORDINAL
                const uint16 module = ((const uint16 *) targetptr)[0];
                const uint16 ordinal = ((const uint16 *) targetptr)[1];
                const LxExport *lxexp = NULL;
                void *addr = getModuleProcAddrByOrdinal(lxmod->dependencies[module-1], ordinal, &lxexp, 1, 1);
                if (addr) {
                    target = lxConvert32to1616(addr);
                }
            }
            break;

            case 2: { // IMPORTNAME
                const uint16 module = ((const uint16 *) targetptr)[0];
                const uint16 offset = ((const uint16 *) targetptr)[1];
                const LxExport *lxexp = NULL;
                const uint8 *import_name = (exe + lxmod->header.ne.imported_names_table_offset) + offset;
                char name[256];
                const uint8 namelen = *(import_name++);
                memcpy(name, import_name, namelen);
                name[namelen] = '\0';
                void *addr = getModuleProcAddrByName(lxmod->dependencies[module-1], name, &lxexp, 1);
                if (addr) {
                    target = lxConvert32to1616(addr);
                }
            }
            break;

            case 3: {
                //const uint16 type = ((const uint16 *) targetptr)[0];
                // I think these are for FPU emulation, but we don't care in modern times.
                continue;  // just skip this fixup.
            }
            break;

            default:
                fprintf(stderr, "unknown fixup target type %u\n", (uint) (flags & 0x3));
                break;
        } // switch

        if (additive) {
            uint16 *source = (uint16 *) (dst + srcchain_offset);
            switch (srctype & 0xF) {
                case 0: *((uint8 *) source) += target & 0xFF; break; // LOBYTE
                case 2: *source += target >> 16; break; // SEGMENT
                case 3: source[0] += target & 0xFFFF; source[1] += target >> 16; break; // FAR_ADDR  ... this adds to the offset, not segment.
                case 5: *source += target & 0xFFFF; break; // OFFSET
                default: fprintf(stderr, "unknown fixup type %u\n", (uint) (srctype & 0xF)); break;
            } // switch
        } else {
            while (srcchain_offset < seg->size) {  // strictly, this should end with 0xFFFF, but we drop out for any out-of-bounds index.
                uint16 *source = (uint16 *) (dst + srcchain_offset);
                srcchain_offset = *source;
                switch (srctype & 0xF) {
                    case 0: *((uint8 *) source) = target & 0xFF; break; // LOBYTE
                    case 2: *source = target >> 16; break; // SEGMENT
                    case 3: source[0] = target & 0xFFFF; source[1] = target >> 16; break; // FAR_ADDR
                    case 5: *source = target & 0xFFFF; break; // OFFSET
                    default: fprintf(stderr, "unknown fixup type %u\n", (uint) (srctype & 0xF)); break;
                } // switch
            } // while
        } // else
    } // for

    return 1;
} // fixupNeSegment

// !!! FIXME: this code isn't _exactly_ like the LX loader, but it's close enough that there's a _ton_ of copy/paste.
// NE ("New Executable") modules are 16-bit binaries.
static LxModule *loadNeModule(const char *fname, const uint8 *origexe, uint8 *exe, uint32 exelen, int dependency_tree_depth)
{
    FIXME("All of this needs bounds checking and corruption testing");
    const NeHeader *ne = (const NeHeader *) exe;
    const uint16 module_type = ne->module_flags & 0x8000;

    if (ne->module_flags & 0x2000) {  // NOTLOADABLE
        fprintf(stderr, "This module is marked as not loadable.\n");
        goto loadne_failed;
    } // if

    const int isDLL = (module_type == 0x8000);

    if (isDLL && !GLoaderState.main_module) {
        fprintf(stderr, "uhoh, need to load an .exe before a .dll!\n");
        goto loadne_failed;
    } else if (!isDLL && GLoaderState.main_module) {
        fprintf(stderr, "uhoh, loading an .exe after already loading one!\n");
        goto loadne_failed;
    }

    // we load an "LxModule" even for NE binaries. Sorry!!
    LxModule *retval = (LxModule *) malloc(sizeof (LxModule));
    if (!retval) {
        fprintf(stderr, "Out of memory!\n");
        goto loadne_failed;
    } // if
    memset(retval, '\0', sizeof (*retval));
    retval->is_lx = 0;
    retval->refcount = 1;
    retval->dependencies = (LxModule **) malloc(sizeof (LxModule *) * ne->num_module_ref_table_entries);
    retval->mmaps = (LxMmaps *) malloc(sizeof (LxMmaps) * ne->num_segment_table_entries);
    if (!retval->dependencies || !retval->mmaps) {
        fprintf(stderr, "Out of memory!\n");
        goto loadne_failed;
    } // if
    memset(retval->dependencies, '\0', sizeof (LxModule *) * ne->num_module_ref_table_entries);
    memset(retval->mmaps, '\0', sizeof (LxMmaps) * ne->num_segment_table_entries);
    retval->num_dependencies = ne->num_module_ref_table_entries;
    retval->num_mmaps = ne->num_segment_table_entries;
    for (int i = 0; i < retval->num_mmaps; i++)
        retval->mmaps[i].alias = 0xFFFF;

    memcpy(&retval->header.ne, ne, sizeof (*ne));

    if (ne->resident_name_table_offset) {
        const uint8 *name_table = exe + ne->resident_name_table_offset;
        const uint8 namelen = *(name_table++);
        char *ptr = retval->name;
        for (uint32 i = 0; i < namelen; i++) {
            const char ch = *(name_table++);
            *(ptr++) = ((ch >= 'a') && (ch <= 'z')) ? (ch + ('A' - 'a')) : ch;
        } // for
        *ptr = '\0';
    } // if

    if (!isDLL) {
        GLoaderState.main_module = retval;
        GLoaderState.pib.pib_hmte = retval;
    } // else if

    const char *modname = retval->name;
    //printf("ref'd new module '%s' to %u\n", modname, 1);

    // !!! FIXME: apparently OS/2 does 1024, but they're not loading the module into RAM each time.
    // !!! FIXME: the spec mentions the 1024 thing for "forwarder" records in the entry table,
    // !!! FIXME:  specifically. _Can_ two DLLs depend on each other? We'll have to load both and
    // !!! FIXME:  then fix them up without recursing.
    if (++dependency_tree_depth > 32) {
        fprintf(stderr, "Likely circular dependency in module '%s'\n", modname);
        goto loadne_failed;
    } // if

    if ((!ne->reg_ss) || (ne->reg_ss > ne->num_segment_table_entries)) {
        fprintf(stderr, "Bogus reg_ss in module '%s'\n", modname);
        goto loadne_failed;
    }

    FIXME("What do we do if ne->NOAUTODATA doesn't agree with ne->auto_data_segment?");
    const NeSegmentTableEntry *seg = (const NeSegmentTableEntry *) (exe + ne->segment_table_offset);
    const NeSegmentTableEntry *autodataseg = ne->auto_data_segment ? (seg + ne->auto_data_segment) : NULL;
    if (autodataseg) {
        retval->autodatasize = autodataseg->size;
    }

    for (uint32 i = 0; i < ne->num_segment_table_entries; i++, seg++) {
        // for 16-bit binaries, we just allocate entire 64k segments regardless of
        //  what the segment table entry asks for; it's not like it's a big expense in
        //  modern times.
        const uint32 vsize = 0x10000;
        // currently we ignore the movable flag, since we just allocate whatever, whereever, but if we _need_ specific segments, we'll have to revisit this.
        //const int movable = (seg->segment_flags & 0x10) != 0;
        const int iscode = ((seg->segment_flags & 0x7) == 0) ? 1 : 0;
        const int isiterated = (seg->segment_flags & 0x8) ? 1 : 0;
        uint16 selector = 0xFFFF;
        void *mmapaddr = lxAllocSegment(&selector, iscode);
        //printf("%s neobj #%u lxAllocSegment(%d) == %p (selector=0x%X)\n", retval->name, (uint) i, iscode, mmapaddr, (uint) selector);

        if (mmapaddr == NULL) {
            fprintf(stderr, "lxAllocSegment(%d) failed\n", iscode);
            goto loadne_failed;
        } else if (selector == 0xFFFF) {
            munmap(mmapaddr, vsize);
            mmapaddr = NULL;
            fprintf(stderr, "Failed to get a selector for a segment\n");
            goto loadne_failed;
        }

        retval->mmaps[i].mapped = mmapaddr;
        retval->mmaps[i].size = vsize;
        retval->mmaps[i].addr = mmapaddr;
        retval->mmaps[i].alias = selector;

        uint8 *dst = (uint8 *) mmapaddr;
        const size_t segfiledataoffset = seg->offset;
        const size_t segfiledatalen = seg->size ? seg->size : 0x10000;
        assert(segfiledatalen <= vsize);
        const uint8 *src = origexe + (segfiledataoffset << ne->sector_alignment_shift_count);

        if (isiterated) {
            if (!decompressIterated(dst, vsize, src, segfiledatalen)) {
                fprintf(stderr, "Failed to decompress iterated segment (corrupt file or bug).\n");
                goto loadne_failed;
            }
        } else {
            size_t leftover = vsize;
            if (segfiledataoffset) {
                memcpy(dst, src, segfiledatalen);
                char fname[16]; snprintf(fname, sizeof (fname), "seg%d.bin", (int) i+1); FILE *io = fopen(fname, "wb"); if (io) { fwrite(dst, segfiledatalen, 1, io); fclose(io); }
                leftover -= segfiledatalen;
                dst += segfiledatalen;
            }
            if (leftover) {
                memset(dst, '\0', leftover);
            }
        }
    } // for

    // All the pages we need from the EXE are loaded into the appropriate spots in memory.
    //printf("mmap()'d everything we need!\n");

    retval->eip = (lxSelectorToSegment(retval->mmaps[ne->reg_cs-1].alias) << 16) | ne->reg_ip;

    if (!isDLL) {
        /* "If SS equals the automatic data segment and SP equals
            zero, the stack pointer is set to the top of the automatic data
            segment just below the additional heap area." */
        uint16 sp = ne->reg_sp;
        if ((ne->reg_ss == ne->auto_data_segment) && (sp == 0)) {
            if (!autodataseg) {
                goto loadne_failed;
            }
            sp = autodataseg->size + ne->stack_size;
        }

        retval->esp = (lxSelectorToSegment(retval->mmaps[ne->reg_ss-1].alias) << 16) | (sp ? sp : 0xFFFF);

        uint16 stacksize = ne->stack_size;
        if (stacksize == 0) {
            stacksize = sp;
            if (sp && (ne->reg_ss == ne->auto_data_segment)) {
                stacksize -= autodataseg->size;
            }
        }
        GLoaderState.mainstacksize = stacksize;

        // This needs to be set up now, so it's available to any library
        //  init code that runs in LX land.
        void *topofstack = (void *) (((size_t) retval->mmaps[ne->reg_ss-1].addr) + sp);
        GLoaderState.initOs2Tib(GLoaderState.main_tibspace, topofstack, stacksize, 0);
        GLoaderState.main_tib_selector = GLoaderState.setOs2Tib(GLoaderState.main_tibspace);
    } // if

    // Set up our exports...
    if (ne->entry_table_size > 0) {
        uint32 total_ordinals = 0;
        const uint8 *entryptr = exe + ne->entry_table_offset;
        while (*entryptr) {  /* end field has a value of zero. */
            const uint8 numentries = *(entryptr++);  /* number of entries in this bundle */
            const uint8 bundletype = *(entryptr++);
            total_ordinals += numentries;
            if (bundletype != 0x00) {
                entryptr += ((bundletype == 0xFF) ? 6 : 3) * numentries;
            } // if
        } // while

        if (total_ordinals > 0) {
            LxExport *lxexp = (LxExport *) malloc(sizeof (LxExport) * total_ordinals);
            if (!lxexp) {
                fprintf(stderr, "Out of memory!\n");
                goto loadne_failed;
            } // if
            memset(lxexp, '\0', sizeof (LxExport) * total_ordinals);
            retval->exports = lxexp;

            uint32 ordinal = 1;
            entryptr = exe + ne->entry_table_offset;
            while (*entryptr) {  /* end field has a value of zero. */
                const uint8 numentries = *(entryptr++);  /* number of entries in this bundle */
                const uint8 bundletype = *(entryptr++);

                switch (bundletype) {
                    case 0x00: // UNUSED
                        ordinal += numentries;
                        break;

                    case 0xFF: // MOVABLE
                        for (uint8 i = 0; i < numentries; i++) {
                            const uint8 flags = *(entryptr++); FIXME("deal with flags"); (void) flags;
                            /*const uint16 int3f = *((const uint16 *) entryptr);*/ entryptr += 2;
                            const uint8 segment = (*(entryptr++)) - 1;
                            const uint16 offset = *((const uint16 *) entryptr); entryptr += 2;
                            lxexp->ordinal = ordinal++;
                            lxexp->addr = ((uint8 *) retval->mmaps[segment].addr) + offset;
                            lxexp->object = &retval->mmaps[segment];
                            lxexp++;
                        } // for
                        break;

                    default:
                        for (uint8 i = 0; i < numentries; i++) {
                            const uint8 segment = bundletype - 1;
                            const uint8 flags = *(entryptr++); FIXME("deal with flags"); (void) flags;
                            const uint16 offset = *((const uint16 *) entryptr); entryptr += 2;
                            lxexp->ordinal = ordinal++;
                            lxexp->addr = ((uint8 *) retval->mmaps[segment].addr) + offset;
                            lxexp->object = &retval->mmaps[segment];
                            lxexp++;
                        } // for
                        break;
                } // switch
            } // while

            retval->num_exports = (uint32) (lxexp - retval->exports);
        } // if
    } // if

    // Now load named entry points.
    if (ne->resident_name_table_offset) {
        if (!loadLxNameTable(retval, exe + ne->resident_name_table_offset))
            goto loadne_failed;
    } // if

    if (ne->non_resident_name_table_offset && ne->non_resident_name_table_size) {
        if (!loadLxNameTable(retval, origexe + ne->non_resident_name_table_offset))
            goto loadne_failed;
    } // if

    // Load other dependencies of this module.
    const uint16 *import_modules_table = (const uint16 *) (exe + ne->module_reference_table_offset);
    for (uint32 i = 0; i < ne->num_module_ref_table_entries; i++) {
        const uint8 *nameptr = (exe + ne->imported_names_table_offset) + *(import_modules_table++);
        const uint8 namelen = *(nameptr++);
        char name[256];
        memcpy(name, nameptr, namelen);
        name[namelen] = '\0';
        retval->dependencies[i] = loadModuleByModuleNameInternal(name, dependency_tree_depth);
        if (!retval->dependencies[i]) {
            fprintf(stderr, "Failed to load dependency '%s' for module '%s'\n", name, modname);
            goto loadne_failed;
        } // if
    } // for

    // Run through again and do all the fixups...
    seg = (const NeSegmentTableEntry *) (exe + ne->segment_table_offset);
    for (uint32 i = 0; i < ne->num_segment_table_entries; i++, seg++) {
        if (!fixupNeSegment(seg, retval, (uint8 *) retval->mmaps[i].addr, ne->sector_alignment_shift_count, origexe, exe, exelen)) {
            goto loadne_failed;
        }

        // Now set all the pages of this object to the proper final permissions...
        const int iscode = ((seg->segment_flags & 0x7) == 0) ? 1 : 0;
        const int preload = (seg->segment_flags & 0x40) ? 1 : 0;  // PRELOAD implies read-only, according to spec.
        const int prot = ((preload && !iscode) ? 0 : PROT_WRITE) | (iscode ? PROT_EXEC : 0) | PROT_READ;
        retval->mmaps[i].prot = prot;

        //printf("mprotect(%p, %u, %s%s%s);\n", retval->mmaps[i].mapped, (uint) retval->mmaps[i].size, (prot&PROT_READ) ? "R" : "-", (prot&PROT_WRITE) ? "W" : "-", (prot&PROT_EXEC) ? "X" : "-");

        if (mprotect(retval->mmaps[i].mapped, retval->mmaps[i].size, prot) == -1) {
            fprintf(stderr, "mprotect(%p, %u, %s%s%s) failed (%d): %s\n",
                    retval->mmaps[i].mapped, (uint) retval->mmaps[i].size,
                    (prot&PROT_READ) ? "R" : "-",
                    (prot&PROT_WRITE) ? "W" : "-",
                    (prot&PROT_EXEC) ? "X" : "-",
                    errno, strerror(errno));
            goto loadne_failed;
        } // if
    } // for

    retval->os2path = makeOS2Path(fname);
    if (!retval->os2path)
        goto loadne_failed;

    if (!isDLL) {
        retval->initialized = 1;
    } else {
        // call library init code...
        assert(GLoaderState.main_module != NULL);
        assert(GLoaderState.main_module != retval);
        runNeLibraryInit(retval);

        retval->initialized = 1;

        // module is ready to use, put it in the loaded list.
        // !!! FIXME: mutex this
        if (GLoaderState.loaded_modules) {
            retval->next = GLoaderState.loaded_modules;
            GLoaderState.loaded_modules->prev = retval;
        } // if
        GLoaderState.loaded_modules = retval;
    } // if

    return retval;

loadne_failed:
    freeLxModule(retval);
    return NULL;
} // loadNeModule

static LxModule *loadModule(const char *fname, uint8 *exe, uint32 exelen, int dependency_tree_depth)
{
    //printf("loadModule('%s')\n", fname); fflush(stdout);
    //const uint32 origexelen = exelen;
    const uint8 *origexe = exe;
    int is_lx = 0;

    if (!sanityCheckModule(&exe, &exelen, &is_lx)) {
        return NULL;
    }

    return is_lx ?
        loadLxModule(fname, origexe, exe, exelen, dependency_tree_depth) :
        loadNeModule(fname, origexe, exe, exelen, dependency_tree_depth);
} // loadModule


static LxModule *loadModuleByPathInternal(const char *fname, const int dependency_tree_depth)
{
    const char *what = NULL; (void) what;
    uint8 *module = NULL;
    uint32 modulelen = 0;
    FILE *io = NULL;

    // !!! FIXME: locate correct file (case-insensitive checking, convert '\\' to '/' etc)...

    #define LOADFAIL(x) { what = x; goto loadmod_failed; }

    if ((io = fopen(fname, "rb")) == NULL) LOADFAIL("open");
    if (fseek(io, 0, SEEK_END) < 0) LOADFAIL("seek");
    modulelen = ftell(io);
    if ((module = (uint8 *) malloc(modulelen)) == NULL) LOADFAIL("malloc");
    rewind(io);
    if (fread(module, modulelen, 1, io) != 1) LOADFAIL("read");
    fclose(io);

    #undef LOADFAIL

    LxModule *retval = loadModule(fname, module, modulelen, dependency_tree_depth);
    free(module);
    return retval;

loadmod_failed:
    fprintf(stderr, "%s failure on '%s: %s'\n", what, fname, strerror(errno));
    if (io)
        fclose(io);
    free(module);
    return NULL;
} // loadModuleByPathInternal

static inline LxModule *loadModuleByPath(const char *fname)
{
    return loadModuleByPathInternal(fname, 0);
} // loadModuleByPath

static LxModule *loadNativeModule(const char *fname, const char *modname)
{
    LxModule *retval = (LxModule *) malloc(sizeof (LxModule));
    void *lib = NULL;
    LxNativeModuleInitEntryPoint fn = NULL;
    const LxExport *exports = NULL;
    uint32 num_exports = 0;
    char *os2path = makeOS2Path(fname);

    if (!retval || !modname || !os2path)
        goto loadnative_failed;
        
    memset(retval, '\0', sizeof(*retval));

    // !!! FIXME: mutex this.
    lib = dlopen(fname, RTLD_LOCAL | RTLD_NOW);
    if (lib == NULL) {
        fprintf(stderr, "dlopen('%s') failed: %s\n", fname, dlerror());
        goto loadnative_failed;
    } // if

    fn = (LxNativeModuleInitEntryPoint) dlsym(lib, "lxNativeModuleInit");
    if (!fn)
        goto loadnative_failed;

    exports = fn(&num_exports);
    if (!exports)
        goto loadnative_failed;
    retval->refcount = 1;
    strcpy(retval->name, modname);
    retval->nativelib = lib;
    retval->os2path = os2path;
    retval->exports = exports;
    retval->num_exports = num_exports;
    retval->initialized = 1;

    //fprintf(stderr, "Loaded native module '%s' (%u exports).\n", fname, (uint) num_exports);

    return retval;

loadnative_failed:
    if (lib) dlclose(lib);
    free(os2path);
    free(retval);
    return NULL;
} // loadNativeModule

static LxModule *loadModuleByModuleNameInternal(const char *modname, const int dependency_tree_depth)
{
    // !!! FIXME: mutex this
    for (LxModule *i = GLoaderState.loaded_modules; i != NULL; i = i->next) {
        if (strcasecmp(i->name, modname) == 0) {
            i->refcount++;
            //printf("ref'd module '%s' to %u\n", i->name, (uint) i->refcount);
            return i;
        } // if
    } // for

    // !!! FIXME: decide the right path to the file, or if it's a native replacement library.
    char fname[256];
    snprintf(fname, sizeof (fname), "%s.dll", modname);
    locatePathCaseInsensitive(fname);

    LxModule *retval = NULL;
    if (access(fname, F_OK) == 0)
        retval = loadModuleByPathInternal(fname, dependency_tree_depth);

    if (!retval) {
        snprintf(fname, sizeof (fname), "./lib%s.so", modname);
        for (char *ptr = fname; *ptr; ptr++) {
            *ptr = (((*ptr >= 'A') && (*ptr <= 'Z')) ? (*ptr - ('A' - 'a')) : *ptr);
        } // for
        retval = loadNativeModule(fname, modname);

        if (retval != NULL) {
            // module is ready to use, put it in the loaded list.
            // !!! FIXME: mutex this
            if (GLoaderState.loaded_modules) {
                retval->next = GLoaderState.loaded_modules;
                GLoaderState.loaded_modules->prev = retval;
            } // if
            GLoaderState.loaded_modules = retval;
        } // if
    } // if
    return retval;
} // loadModuleByModuleNameInternal

static LxModule *loadModuleByModuleName(const char *modname)
{
    return loadModuleByModuleNameInternal(modname, 0);
} // loadModuleByModuleName

static LxModule *loadModuleByPathOrModuleName(const char *modname)
{
    LxModule *retval = NULL;
    if (!strchr(modname, '/') && !strchr(modname, '\\')) {
        char *str = (char *) alloca(strlen(modname) + 1);
        if (str) {
            strcpy(str, modname);
            char *ptr = strrchr(str, '.');
            if (ptr) {
                *ptr = '\0';
            }
            retval = loadModuleByModuleName(str);
        } // if
    } else {
        uint32 err = 0;
        char *path = lxMakeUnixPath(modname, &err);
        if (!path)
            return NULL;
        retval = loadModuleByPath(path);
        free(path);
    } // else
    return retval;
} // loadModuleByPathOrModuleName

static __attribute__((noreturn)) void handleThreadLocalStorageAccess(const int slot, ucontext_t *uctx)
{
    greg_t *gregs = uctx->uc_mcontext.gregs;

    // use the segfaulting thread's FS register, so we can get its TIB2 pointer.
    LxTIB2 *ptib2 = NULL;
    __asm__ __volatile__ (
        "pushw %%fs            \n\t"
        "movw %%ax, %%fs       \n\t"
        "movl %%fs:0xC, %%eax  \n\t"
        "popw %%fs             \n\t"
            : "=a" (ptib2)
            : "eax" (gregs[REG_FS])
    );

    // The thread's TLS data is stored in LxPostTIB, right after its TIB2 struct.
    LxPostTIB *posttib = (LxPostTIB *) (ptib2 + 1);
    uint32 *tls = posttib->tls;
    tls += slot;

    //printf("We wanted to access thread %p TLS slot %d (currently holds %u)\n", tls - slot, slot, (uint) *tls);

    static const int x86RegisterToUContextEnum[] = {
        REG_EAX, REG_ECX, REG_EDX, REG_EBX, REG_ESP, REG_EBP, REG_ESI, REG_EDI
    };

    // this is a hack; we'll want a much more serious x86 instruction decoder before long.
    int handled = 1;
    uint8 *eip = (void *) (size_t) gregs[REG_EIP];  // program counter at point of segfault.
    switch (eip[0]) {
        case 0xC7:  // mov imm16/32 -> r/m
            //printf("setting TLS slot %d to imm %u.\n", slot, (uint) *((uint32 *) (eip + 2)));
            *tls = *((uint32 *) (eip + 2));  // !!! FIXME: verify it's a imm32, not 16
            gregs[REG_EIP] += 6;
            break;

        case 0x89:  // mov r -> r/m
            //printf("setting TLS slot %d to reg %d (%u).\n", slot, x86RegisterToUContextEnum[eip[1] >> 3], (uint) gregs[x86RegisterToUContextEnum[eip[1] >> 3]]);
            *tls = (uint32) gregs[x86RegisterToUContextEnum[eip[1] >> 3]];
            gregs[REG_EIP] += 2;
            break;

        case 0x8B:  // mov r/m -> r
            //printf("setting reg %d to TLS slot %d (%u).\n", (uint) x86RegisterToUContextEnum[eip[1] >> 3], slot, *tls);
            gregs[x86RegisterToUContextEnum[eip[1] >> 3]] = (greg_t) *tls;
            gregs[REG_EIP] += 2;
            break;

        default:
            handled = 0;
            break;
    } // switch

    if (!handled) {
        fprintf(stderr, "Oh no, unhandled opcode 0x%X at %p accessing TLS register! File a bug!\n", (uint) eip[0], eip);
    } else {
        // drop out of signal handler to (hopefully) next instruction in the app,
        //  as if it accessed the TLS slot normally and none of this ever happened.
        //printf("TLS access handler jumping back into app at %p...\n", (void *) gregs[REG_EIP]); fflush(stdout);
        setcontext(uctx);
        fprintf(stderr, "panic: setcontext() failed in the TLS access handler! Aborting! (%s)\n", strerror(errno));
    } // else

    fflush(stderr);
    abort();
} // handleThreadLocalStorageAccess

static void segfault_catcher(int sig, siginfo_t *info, void *ctx)
{
    ucontext_t *uctx = (ucontext_t *) ctx;
    const uint32 *addr = (const uint32 *) info->si_addr;
    const uint32 *tlspage = GLoaderState.tlspage;

    if (tlspage && (addr >= tlspage))
    {
        // was the app accessing one of the OS/2 TLS slots?
        const int slot = (int) (addr - tlspage);
        if (slot < 32)
            handleThreadLocalStorageAccess(slot, uctx);
    } // if

    static int faults = 0;
    faults++;
    switch (faults) {
        // !!! FIXME: case #1 should be a much more detailed crash dump.
        case 1: fprintf(stderr, "SIGSEGV at addr=%p (eip=%p)\n", addr, (void *) ((ucontext_t *) ctx)->uc_mcontext.gregs[REG_EIP]); break;
        case 2: write(2, "SIGSEGV, aborting.\n", 19); break;
        default: break;
    } // switch

    abort();  // cash out.
} // segfault_catcher

static int installSignalHandlers(void)
{
    struct sigaction action;

    memset(&action, '\0', sizeof (action));
    action.sa_sigaction = segfault_catcher;
    action.sa_flags = SA_NODEFER | SA_SIGINFO;

    if (sigaction(SIGSEGV, &action, NULL) == -1) {
        fprintf(stderr, "Couldn't install SIGSEGV handler! (%s)\n", strerror(errno));
        return 0;
    } // if

    return 1;
} // installSignalHandlers

int main(int argc, char **argv, char **envp)
{
    if (argc < 2) {
        fprintf(stderr, "USAGE: %s <program.exe> [...programargs...]\n", argv[0]);
        return 1;
    }

    GLoaderState.ldt = (uint32 *) calloc(LX_MAX_LDT_SLOTS, sizeof (*GLoaderState.ldt));
    if (!GLoaderState.ldt) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    // cleanup some defaults lib2ine set up, since we'll set it up a different way.
    GLoaderState.using_lx_loader = 1;
    GLoaderState.running = 0;
    GLoaderState.deinitOs2Tib(GLoaderState.main_tib_selector);
    GLoaderState.main_tib_selector = 0;

    if (!installSignalHandlers())
        return 1;

    unsigned int segment = 0;
    __asm__ __volatile__ ( "movw %%cs, %%ax  \n\t" : "=a" (segment) );
    GLoaderState.original_cs = segment;
    __asm__ __volatile__ ( "movw %%ds, %%ax  \n\t" : "=a" (segment) );
    GLoaderState.original_ds = segment;
    __asm__ __volatile__ ( "movw %%es, %%ax  \n\t" : "=a" (segment) );
    GLoaderState.original_es = segment;
    __asm__ __volatile__ ( "movw %%ss, %%ax  \n\t" : "=a" (segment) );
    GLoaderState.original_ss = segment;

    GLoaderState.setOs2Tib = lxSetOs2Tib;
    GLoaderState.getOs2Tib = lxGetOs2Tib;
    GLoaderState.deinitOs2Tib = lxDeinitOs2Tib;
    GLoaderState.allocSegment = lxAllocSegment;
    GLoaderState.freeSegment = lxFreeSegment;
    GLoaderState.findSelector = lxFindSelector;
    GLoaderState.freeSelector = lxFreeSelector;
    GLoaderState.convert1616to32 = lxConvert1616to32;
    GLoaderState.convert32to1616 = lxConvert32to1616;
    GLoaderState.loadModule = loadModuleByPathOrModuleName;
    GLoaderState.makeUnixPath = lxMakeUnixPath;
    GLoaderState.terminate = lxTerminate;

    const char *modulename = GLoaderState.subprocess ? getenv("IS_2INE") : argv[1];
    LxModule *lxmod = loadModuleByPath(modulename);
    if (lxmod != NULL)
        runModule(lxmod);

    return 1;
} // main

// end of lx_loader.c ...

