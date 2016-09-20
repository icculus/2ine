#define _GNU_SOURCE 1

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <assert.h>
#include "lx_loader.h"

// !!! FIXME: move this into an lx_common.c file.
static int sanityCheckExe(uint8 **_exe, uint32 *_exelen)
{
    if (*_exelen < 196) {
        fprintf(stderr, "not an OS/2 LX EXE\n");
        return 0;
    }
    const uint32 header_offset = *((uint32 *) (*_exe + 0x3C));
    //printf("header offset is %u\n", (unsigned int) header_offset);
    if ((header_offset + sizeof (LxHeader)) >= *_exelen) {
        fprintf(stderr, "not an OS/2 LX EXE\n");
        return 0;
    }

    *_exe += header_offset;  // skip the DOS stub, etc.
    *_exelen -= header_offset;

    const LxHeader *lx = (const LxHeader *) *_exe;

    if ((lx->magic_l != 'L') || (lx->magic_x != 'X')) {
        fprintf(stderr, "not an OS/2 LX EXE\n");
        return 0;
    }

    if ((lx->byte_order != 0) || (lx->word_order != 0)) {
        fprintf(stderr, "Program is not little-endian!\n");
        return 0;
    }

    if (lx->lx_version != 0) {
        fprintf(stderr, "Program is unknown LX EXE version (%u)\n", (unsigned int) lx->lx_version);
        return 0;
    }

    if (lx->cpu_type > 3) { // 1==286, 2==386, 3==486
        fprintf(stderr, "Program needs unknown CPU type (%u)\n", (unsigned int) lx->cpu_type);
        return 0;
    }

    if (lx->os_type != 1) { // 1==OS/2, others: dos4, windows, win386, unknown.
        fprintf(stderr, "Program needs unknown OS type (%u)\n", (unsigned int) lx->os_type);
        return 0;
    }

    if (lx->page_size != 4096) {
        fprintf(stderr, "Program page size isn't 4096 (%u)\n", (unsigned int) lx->page_size);
        return 0;
    }

    // !!! FIXME: check if EIP and ESP are non-zero vs per-process library bits, etc.

    return 1;
} // sanityCheckExe

/* this algorithm is from lxlite 138u. */
static int decompressExePack2(uint8 *dst, const uint32 dstlen, const uint8 *src, const uint32 srclen)
{
    uint32 sOf = 0;
    uint32 dOf = 0;
    uint32 bOf = 0;
    uint32 b1 = 0;
    uint32 b2 = 0;

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
                if (SRCAVAIL(b1) && DSTAVAIL(b1 + b2) && (dOf + b1 - bOf >= 0)) {
                    memcpy(dst + dOf, src + sOf, b1);
                    dOf += b1;
                    sOf += b1;
                    memmove(dst + dOf, dst + (dOf - bOf), b2);
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
                    memmove(dst + dOf, dst + (dOf - bOf), b1);
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
                if (SRCAVAIL(b1) && DSTAVAIL(b1 + b2) && (dOf + b1 - bOf >= 0))
                {
                    memcpy(dst + dOf, src + sOf, b1);
                    dOf += b1;
                    sOf += b1;
                    memmove(dst + dOf, dst + (dOf - bOf), b2);
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
    memset(dst + dOf, '\0', dstlen - dOf);
    return 1;
} // decompressExePack2

void missing_ordinal_called(const uint32 ordinal)
{
    fflush(stdout);
    fflush(stderr);
    fprintf(stderr, "\n\nMissing ordinal '%u' called!\n", ordinal);
    fprintf(stderr, "Aborting.\n\n\n");
    //STUBBED("output backtrace");
    fflush(stderr);
    _exit(1);
} // missing_ordinal_called

static uint32 DosPutMessage(uint32 handle, uint32 msglen, const char *msg)
{
    // !!! FIXME: this isn't right, but good enough for now.
    fwrite(msg, msglen, 1, stdout);
    fflush(stdout);
    return 0;
} // DosPutMessage

static uint32 getModuleProcAddrByOrdinal(const uint16 moduleid, const uint32 importid)
{
    // !!! FIXME: write me for real.
    if (moduleid != 1) {  // this is MSG in hello.exe, for testing purposes.
        fprintf(stderr, "uhoh, looking for module ordinal that isn't #1.\n");
        return 0;
    } // if

    //fprintf(stderr, "importid == %u\n", (unsigned int) importid);

    static void *page = NULL;
    static uint32 pageused = 0;
    static uint32 pagesize = 0;
    if (pagesize == 0)
        pagesize = getpagesize();

    if ((!page) || ((pagesize - pageused) < 32))
    {
        if (page)
            mprotect(page, pagesize, PROT_READ | PROT_EXEC);
        page = valloc(pagesize);
        mprotect(page, pagesize, PROT_READ | PROT_WRITE | PROT_EXEC);
        pageused = 0;
    } // if

    if (importid == 5)  // !!! FIXME: this is DosPutMessage in the MSG module.
        return (uint32) (size_t) DosPutMessage;

    void *trampoline = page + pageused;
    char *ptr = (char *) trampoline;
    const uint32 ordinal = importid;

    *(ptr++) = 0x55;  // pushl %ebp
    *(ptr++) = 0x89;  // movl %esp,%ebp
    *(ptr++) = 0xE5;  //   ...movl %esp,%ebp
    *(ptr++) = 0x68;  // pushl immediate
    memcpy(ptr, &ordinal, sizeof (uint32));
    ptr += sizeof (uint32);
    *(ptr++) = 0xB8;  // movl immediate to %eax
    const void *fn = missing_ordinal_called;
    memcpy(ptr, &fn, sizeof (void *));
    ptr += sizeof (void *);
    *(ptr++) = 0xFF;  // call absolute in %eax.
    *(ptr++) = 0xD0;  //   ...call absolute in %eax.

    const uint32 trampoline_len = (uint32) (ptr - ((char *) trampoline));
    assert(trampoline_len <= 32);
    pageused += trampoline_len;

    if (pageused % 4)  // keep these aligned to 32 bits.
        pageused += (4 - (pageused % 4));

    return (uint32) (size_t) trampoline;
} // getModuleProcAddrByOrdinal


static void doFixup(uint8 *page, const uint16 offset, const uint32 finalval, const uint16 finalval2, const uint32 finalsize)
{
    #if 0
    if (finalsize == 6) {
        printf("fixing up %p to to 0x%X:0x%X (6 bytes)...\n", page + offset, (unsigned int) finalval2, (unsigned int) finalval);
    } else {
        printf("fixing up %p to to 0x%X (%u bytes)...\n", page + offset, (unsigned int) finalval, (unsigned int) finalsize);
    } // else
    fflush(stdout);
    #endif

    switch (finalsize) {
        case 1: { uint8 *dst = (uint8 *) (page + offset); *dst = (uint8) finalval; } break;
        case 2: { uint16 *dst = (uint16 *) (page + offset); *dst = (uint16) finalval; } break;
        case 4: { uint32 *dst = (uint32 *) (page + offset); *dst = (uint32) finalval; } break;
        case 6: {
            uint16 *dst1 = (uint16 *) (page + offset);
            *dst1 = (uint16) finalval2;
            uint32 *dst2 = (uint32 *) (page + offset + 2);
            *dst2 = (uint32) finalval;
            break;
        } // case
        default:
            fprintf(stderr, "BUG! Unexpected fixup final size of %u\n", (unsigned int) finalsize);
            exit(1);
    } // switch
} // doFixup

static void fixupPage(const LxHeader *lx, const LxObjectTableEntry *obj, uint8 *page)
{
    const uint8 *exe = (const uint8 *) lx;   // this is the start of the LX EXE (past the DOS stub, etc).
    const uint32 *fixuppage = (((const uint32 *) (exe + lx->fixup_page_table_offset)) + (obj->page_table_index - 1));
    const uint32 fixupoffset = *fixuppage;
    const uint32 fixuplen = fixuppage[1] - fixuppage[0];
    const uint8 *fixup = (exe + lx->fixup_record_table_offset) + fixupoffset;
    const uint8 *fixupend = fixup + fixuplen;

    while (fixup < fixupend) {
        const uint8 srctype = *(fixup++);
        const uint8 fixupflags = *(fixup++);
        uint8 srclist_count = 0;
        uint16 srcoffset = 0;

        uint32 finalval = 0;
        uint16 finalval2 = 0;
        uint32 finalsize = 0;

        switch (srctype & 0xF) {
            case 0x0:  // byte fixup
                finalsize = 1;
                break;
            case 0x2:  // 16-bit selector fixup
            case 0x5:  // 16-bit offset fixup
                finalsize = 2;
                break;
            case 0x3:  // 16:16 pointer fixup
            case 0x7:  // 32-bit offset fixup
            case 0x8:  // 32-bit self-relative offset fixup
                finalsize = 4;
                break;
            case 0x6:  // 16:32 pointer fixup
                finalsize = 6;
                break;
        } // switch

        if (srctype & 0x20) { // contains a source list
            srclist_count = *(fixup++);
        } else {
            srcoffset = *((uint16 *) fixup);
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
                // !!! FIXME: where do we use 16-bit selectors? What does this mean in a 32-bit app?
                if ((srctype & 0xF) != 0x2) { // not a 16-bit selector fixup?
                    if (fixupflags & 0x10) {  // 32-bit target offset
                        targetoffset = *((uint32 *) fixup); fixup += 4;
                    } else {  // 16-bit target offset
                        targetoffset = (uint32) (*((uint16 *) fixup)); fixup += 2;
                    } // else
                } // if

                // !!! FIXME: I have no idea if this is right at all.
                // !!! FIXME: Is this guaranteed to not references object pages I haven't loaded/fixed up yet?
                const LxObjectTableEntry *targetobj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset)) + (objectid - 1);
                const uint8 *ptr = (uint8 *) ((size_t) (targetobj->reloc_base_addr + targetoffset));
                switch (finalsize) {
                    case 1: finalval = (uint32) *((uint8 *) ptr); break;
                    case 2: finalval = (uint32) *((uint16 *) ptr); break;
                    case 4: finalval = (uint32) *((uint32 *) ptr); break;
                    case 6: finalval2 = (uint32) *((uint16 *) ptr); finalval = (uint32) *((uint32 *) (ptr + 2)); break;
                } // switch
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

                finalval = getModuleProcAddrByOrdinal(moduleid, importid);
                break;
            } // case

            case 0x2: { // Import by name fixup record
                fprintf(stderr, "FIXUP 0x2 WRITE ME\n");
                exit(1);
                break;
            } // case

            case 0x3: { // Internal entry table fixup record
                fprintf(stderr, "FIXUP 0x3 WRITE ME\n");
                exit(1);
                break;
            } // case
        } // switch

        uint32 additive = 0;
        if (fixupflags & 0x4) {  // Has additive.
            if (fixupflags & 0x20) { // 32-bit value
                additive = *((uint32 *) fixup); fixup += 4;
            } else {  // 16-bit value
                additive = (uint32) *((uint16 *) fixup);
                fixup += 2;
            } // else
        } // if
        finalval += additive;

        if (srctype & 0x20) {  // source list
            for (uint8 i = 0; i < srclist_count; i++) {
                const uint16 offset = *((uint16 *) fixup); fixup += 2;
                if ((srctype & 0xF) == 0x08) {  // self-relative fixup?
                    assert(finalval2 == 0);
                    doFixup(page, offset, finalval - (((uint32)page)+offset+4), 0, finalsize);
                } else {
                    doFixup(page, offset, finalval, finalval2, finalsize);
                }
            } // for
        } else {
            if ((srctype & 0xF) == 0x08) {  // self-relative fixup?
                assert(finalval2 == 0);
                doFixup(page, srcoffset, finalval - (((uint32)page)+srcoffset+4), 0, finalsize);
            } else {
                doFixup(page, srcoffset, finalval, finalval2, finalsize);
            }
        } // else
    } // while
} // fixupPage

static __attribute__((noreturn)) void loadExe(const char *exefname, uint8 *exe, uint32 exelen)
{
    const uint8 *origexe = exe;
    //const uint32 origexelen = exelen;

    if (!sanityCheckExe(&exe, &exelen))
        exit(1);

    const LxHeader *lx = (const LxHeader *) exe;

    const LxObjectTableEntry *obj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++) {
        uint32 vsize = obj->virtual_size;
        if ((vsize % lx->page_size) != 0)
             vsize += lx->page_size - (vsize % lx->page_size);

        const int prot = ((obj->object_flags & 0x1) ? PROT_READ : 0) |
                         ((obj->object_flags & 0x2) ? PROT_WRITE : 0) |
                         ((obj->object_flags & 0x4) ? PROT_EXEC : 0);

        const int mmapflags = MAP_ANON | MAP_PRIVATE | MAP_FIXED;
        void *base = (void *) ((size_t) obj->reloc_base_addr);
        void *mmapaddr = mmap(base, vsize, PROT_READ|PROT_WRITE, mmapflags, -1, 0);
        // we'll mprotect() these pages to the proper permissions later.

        if (mmapaddr == ((void *) MAP_FAILED)) {
            fprintf(stderr, "mmap(%p, %u, RW-, ANON|PRIVATE|FIXED, -1, 0) failed (%d): %s\n",
                    base, (unsigned int) vsize, errno, strerror(errno));
            exit(1);
        } // if

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

                //case 0x01:  // FIXME: write me ... iterated, this is exepack1, I think.
                //    src = origexe + lx->object_iter_pages_offset + (objpage->page_data_offset << lx->page_offset_shift);
                //    memcpy(dst, src, objpage->data_size);
                //    break;

                case 0x02: // INVALID, just zero it out.
                case 0x03: // ZEROFILL, just zero it out.
                    memset(dst, '\0', lx->page_size);
                    break;

                case 0x05:  // exepack2
                    src = origexe + lx->data_pages_offset + (objpage->page_data_offset << lx->page_offset_shift);
                    if (!decompressExePack2(dst, lx->page_size, src, objpage->data_size)) {
                        fprintf(stderr, "Failed to decompress object page (corrupt file or bug).\n");
                        exit(1);
                    } // if
                    break;

                case 0x08: // !!! FIXME: this is listed in lxlite, presumably this is exepack3 or something. Maybe this was new to Warp4, like exepack2 was new to Warp3. Pull this algorithm in from lxlite later.
                default:
                    fprintf(stderr, "Don't know how to load an object page of type %u!\n", (unsigned int) objpage->flags);
                    exit(1);
            } // switch

            // Now run any fixups for the page...
            fixupPage(lx, obj, dst);

            dst += lx->page_size;
        } // for

        // any bytes at the end of this object that we didn't initialize? Zero them out.
        const uint32 remain = vsize - ((uint32) (dst - ((uint8 *) mmapaddr)));
        if (remain) {
            memset(dst, '\0', remain);
        } // if

        // Now set all the pages of this object to the proper final permissions...
        if (mprotect(mmapaddr, vsize, prot) == -1) {
            fprintf(stderr, "mprotect(%p, %u, %s%s%s, ANON|PRIVATE|FIXED, -1, 0) failed (%d): %s\n",
                    base, (unsigned int) vsize,
                    (prot&PROT_READ) ? "R" : "-",
                    (prot&PROT_WRITE) ? "W" : "-",
                    (prot&PROT_EXEC) ? "X" : "-",
                    errno, strerror(errno));
            exit(1);
        } // if

    } // for

    // All the pages we need from the EXE are loaded into the appropriate spots in memory.
    //printf("mmap()'d everything we need!\n");

    uint32 eip = lx->eip;
    if (lx->eip_object != 0) {
        const LxObjectTableEntry *targetobj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset)) + (lx->eip_object - 1);
        eip += targetobj->reloc_base_addr;
    } // if

    // !!! FIXME: esp==0 means something special for programs (and is ignored for library init).
    uint32 esp = lx->esp;
    assert(esp != 0);
    if (lx->esp_object != 0) {  // !!! FIXME: ignore for libraries
        const LxObjectTableEntry *targetobj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset)) + (lx->esp_object - 1);
        esp += targetobj->reloc_base_addr;
    } // if

    // !!! FIXME: right now, we don't list any environment variables, because they probably don't make sense to drop in (even PATH uses a different separator on Unix).
    // !!! FIXME:  eventually, the environment table looks like this (double-null to terminate list):  var1=a\0var2=b\0var3=c\0\0
    // The command line, if I'm reading the Open Watcom __OS2Main() implementation correctly, looks like this...
    //   \0programname\0argv0\0argv1\0argvN\0
    static char *env = "TEST=abc\0\0hello.exe\0hello.exe\0\0";
    char *cmd = env + 10; // !!! FIXME

    // ...and you pass it the pointer to argv0. This is (at least as far as the docs suggest) appended to the environment table.
    //printf("jumping into LX land...! eip=0x%X esp=0x%X\n", (unsigned int) eip, (unsigned int) esp); fflush(stdout);

    // !!! FIXME: need to set up OS/2 TIB and put it in FS register.
    __asm__ __volatile__ (
        "movl %%esi,%%esp  \n\t"  // use the OS/2 process's stack.
        "pushl %%eax       \n\t"  // cmd
        "pushl %%ecx       \n\t"  // env
        "pushl $0          \n\t"  // reserved
        "pushl $0          \n\t"  // module handle  !!! FIXME
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
        // !!! FIXME: init other registers!
        "ret               \n\t"  // go to OS/2 land!
        "1:                \n\t"  //  ...and return here.
        "pushl %%eax       \n\t"  // call _exit() with whatever is in %eax.
        "call _exit        \n\t"
            : // no outputs
            : "a" (cmd), "c" (env), "S" (esp), "D" (eip)
            : "memory"
    );

    __builtin_unreachable();
} // loadExe


int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <program.exe>\n", argv[0]);
        return 1;
    }

    const char *exefname = argv[1];
    FILE *io = fopen(exefname, "rb");
    if (!io) {
        fprintf(stderr, "can't open '%s: %s'\n", exefname, strerror(errno));
        return 2;
    }

    if (fseek(io, 0, SEEK_END) < 0) {
        fprintf(stderr, "can't seek in '%s': %s\n", exefname, strerror(errno));
        return 3;
    }

    const uint32 exelen = ftell(io);
    uint8 *exe = (uint8 *) malloc(exelen);
    if (!exe) {
        fprintf(stderr, "Out of memory\n");
        return 4;
    }

    rewind(io);
    if (fread(exe, exelen, 1, io) != 1) {
        fprintf(stderr, "read failure on '%s': %s\n", exefname, strerror(errno));
        return 5;
    }

    fclose(io);

    loadExe(exefname, exe, exelen);

    free(exe);

    return 0;
} // main

// end of lx_loader.c ...

