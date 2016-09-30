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

#include "lx_loader.h"

static LxLoaderState _GLoaderState;
static LxLoaderState *GLoaderState = &_GLoaderState;


// !!! FIXME: move this into an lx_common.c file.
static int sanityCheckLxModule(uint8 **_exe, uint32 *_exelen)
{
    if (*_exelen < 196) {
        fprintf(stderr, "not an OS/2 LX module\n");
        return 0;
    }
    const uint32 header_offset = *((uint32 *) (*_exe + 0x3C));
    //printf("header offset is %u\n", (unsigned int) header_offset);
    if ((header_offset + sizeof (LxHeader)) >= *_exelen) {
        fprintf(stderr, "not an OS/2 LX module\n");
        return 0;
    }

    *_exe += header_offset;  // skip the DOS stub, etc.
    *_exelen -= header_offset;

    const LxHeader *lx = (const LxHeader *) *_exe;

    if ((lx->magic_l != 'L') || (lx->magic_x != 'X')) {
        fprintf(stderr, "not an OS/2 LX module\n");
        return 0;
    }

    if ((lx->byte_order != 0) || (lx->word_order != 0)) {
        fprintf(stderr, "Program is not little-endian!\n");
        return 0;
    }

    if (lx->lx_version != 0) {
        fprintf(stderr, "Program is unknown LX module version (%u)\n", (unsigned int) lx->lx_version);
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

    if (lx->module_flags & 0x2000) {
        fprintf(stderr, "Module is flagged as not-loadable\n");
        return 0;
    }

    // !!! FIXME: check if EIP and ESP are non-zero vs per-process library bits, etc.

    return 1;
} // sanityCheckLxModule

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

static void missingEntryPointCalled(const char *module, const char *entry)
{
    void ***ebp = NULL;
    __asm__ __volatile__ ( "movl %%ebp, %%eax  \n\t" : "=a" (ebp) );
    void *caller = ebp[0][1];

    fflush(stdout);
    fflush(stderr);
    fprintf(stderr, "\n\nMissing entry point '%s' in module '%s' called at %p!\n", entry, module, caller);
    fprintf(stderr, "Aborting.\n\n\n");
    //STUBBED("output backtrace");
    fflush(stderr);
    _exit(1);
} // missing_ordinal_called

static uint32 generateMissingTrampoline(const char *_module, const char *_entry)
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
        page = valloc(pagesize);
        mprotect(page, pagesize, PROT_READ | PROT_WRITE | PROT_EXEC);
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

    printf("Generated trampoline %p for module '%s' export '%s'\n", trampoline, module, entry);

    return (uint32) (size_t) trampoline;
} // generateMissingTrampoline

static __attribute__((noreturn)) void runLxModule(LxModule *lxmod, const int argc, char **argv)
{
    // !!! FIXME: right now, we don't list any environment variables, because they probably don't make sense to drop in (even PATH uses a different separator on Unix).
    // !!! FIXME:  eventually, the environment table looks like this (double-null to terminate list):  var1=a\0var2=b\0var3=c\0\0
    // The command line, if I'm reading the Open Watcom __OS2Main() implementation correctly, looks like this...
    //   \0programname\0argv0\0argv1\0argvN\0
    size_t len = strlen(argv[0]) + 1;
    for (int i = 0; i < argc; i++) {
        len += strlen(argv[i]) + 1;
    }

    const char *tmpenv = "IS_2INE=1";
    const size_t envlen = strlen(tmpenv);
    len += envlen;
    len += 4;  // null terminators.

    char *env = (char *) malloc(len);
    if (!env) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    } // if

    strcpy(env, tmpenv);
    char *ptr = env + envlen;
    *(ptr++) = '\0';
    *(ptr++) = '\0';
    char *cmd = ptr;
    strcpy(ptr, argv[0]);
    ptr += strlen(argv[0]);
    *(ptr++) = '\0';
    for (int i = 0; i < argc; i++) {
        strcpy(ptr, argv[i]);
        ptr += strlen(argv[i]);
        *(ptr++) = '\0';
    } // for
    *(ptr++) = '\0';

    lxmod->env = env;
    lxmod->cmd = cmd;

    // ...and you pass it the pointer to argv0. This is (at least as far as the docs suggest) appended to the environment table.
    printf("jumping into LX land...! eip=0x%X esp=0x%X\n", (unsigned int) lxmod->eip, (unsigned int) lxmod->esp); fflush(stdout);

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
        "andl $-16, %%esp  \n\t"  // align the stack for macOS.
        "subl $-4, %%esp  \n\t"   // align the stack for macOS.
        "pushl %%eax       \n\t"  // call _exit() with whatever is in %eax.
        "call _exit        \n\t"
            : // no outputs
            : "a" (cmd), "c" (env), "S" (lxmod->esp), "D" (lxmod->eip)
            : "memory"
    );

    __builtin_unreachable();
} // runLxModule

static void runLxLibraryInitOrTerm(LxModule *lxmod, const int isTermination)
{
    // ...and you pass it the pointer to argv0. This is (at least as far as the docs suggest) appended to the environment table.
    printf("jumping into LX land to %s library...! eip=0x%X esp=0x%X\n", isTermination ? "terminate" : "initialize", (unsigned int) lxmod->eip, (unsigned int) GLoaderState->main_module->esp); fflush(stdout);

    // !!! FIXME: need to set up OS/2 TIB and put it in FS register.
    __asm__ __volatile__ (
        "pushal            \n\t"  // save all the current registers.
        "pushfl            \n\t"  // save all the current flags.
        "movl %%esp,%%ecx  \n\t"  // save our stack to a temporary register.
        "movl %%esi,%%esp  \n\t"  // use the OS/2 process's stack.
        "pushl %%ecx       \n\t"  // save original stack pointer for real.
        "pushl %%eax       \n\t"  // isTermination
        "pushl $0          \n\t"  // library module handle  !!! FIXME
        "leal 1f,%%eax     \n\t"  // address that entry point should return to.
        "pushl %%eax       \n\t"
        "pushl %%edi       \n\t"  // the OS/2 library entry point (we'll "ret" to it instead of jmp, so stack and registers are all correct).
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
        "addl $8,%%esp      \n\t"  // drop arguments to entry point.
        "popl %%esp        \n\t"  // restore native process stack now.
        "popfl             \n\t"  // restore our original flags.
        "popal             \n\t"  // restore our original registers.
            : // no outputs
            : "a" (isTermination), "S" (GLoaderState->main_module->esp), "D" (lxmod->eip)
            : "memory"
    );

    // !!! FIXME: this entry point returns a result...do we abort if it reports error?
    printf("...survived time in LX land!\n"); fflush(stdout);
} // runLxLibraryInitOrTerm

static inline void runLxLibraryInit(LxModule *lxmod) { runLxLibraryInitOrTerm(lxmod, 0); }
static inline void runLxLibraryTerm(LxModule *lxmod) { runLxLibraryInitOrTerm(lxmod, 1); }

static void freeLxModule(LxModule *lxmod)
{
    if (!lxmod)
        return;

    // !!! FIXME: mutex from here
    lxmod->refcount--;
    printf("unref'd module '%s' to %u\n", lxmod->name, (unsigned int) lxmod->refcount);
    if (lxmod->refcount > 0)
        return;  // something is still using it.

    if (lxmod->initialized)
        runLxLibraryTerm(lxmod);

    if (lxmod->next)
        lxmod->next->prev = lxmod->prev;

    if (lxmod->prev)
        lxmod->prev->next = lxmod->next;

    if (lxmod == GLoaderState->loaded_modules)
        GLoaderState->loaded_modules = lxmod->next;

    if (GLoaderState->main_module == lxmod)
        GLoaderState->main_module = NULL;
    // !!! FIXME: mutex to here

    for (uint32 i = 0; i < lxmod->lx.num_import_mod_entries; i++)
        freeLxModule(lxmod->dependencies[i]);
    free(lxmod->dependencies);

    for (uint32 i = 0; i < lxmod->lx.module_num_objects; i++) {
        if (lxmod->mmaps[i].addr)
            munmap(lxmod->mmaps[i].addr, lxmod->mmaps[i].size);
    } // for
    free(lxmod->mmaps);

    free(lxmod->exported_names);
    free(lxmod->exported_ordinals);

    if (lxmod->nativelib)
        dlclose(lxmod->nativelib);

    free(lxmod);
} // freeLxModule

static LxModule *loadLxModuleByModuleNameInternal(const char *modname, const int dependency_tree_depth);

static uint32 getModuleProcAddrByOrdinal(const LxModule *module, const uint32 ordinal)
{
    fprintf(stderr, "lookup module == '%s', ordinal == %u\n", module->name, (unsigned int) ordinal);

    const LxExportedOrdinal *expord = module->exported_ordinals;
    for (uint32 i = 0; i < module->num_ordinals; i++, expord++) {
        if (expord->ordinal == ordinal)
            return expord->addr;
    } // for

    #if 1
    char entry[128];
    snprintf(entry, sizeof (entry), "ORDINAL_%u", (unsigned int) ordinal);
    return generateMissingTrampoline(module->name, entry);
    #else
    return 0;
    #endif
} // getModuleProcAddrByOrdinal

#if 0
static uint32 getModuleProcAddrByName(const LxModule *module, const char *name)
{
    fprintf(stderr, "lookup module == '%s', name == '%s'\n", module->name, name);

    const LxExportedName *expname = module->exported_names;
    for (uint32 i = 0; i < module->num_names; i++, expname++) {
        if (strcmp(expname->name, name) == 0)
            return expname->addr;
    } // for

    #if 1
    return generateMissingTrampoline(module->name, name);
    #else
    return 0;
    #endif
} // getModuleProcAddrByName
#endif

static void doFixup(uint8 *page, const sint16 offset, const uint32 finalval, const uint16 finalval2, const uint32 finalsize)
{
    #if 1
    if (finalsize == 6) {
        printf("fixing up %p to 0x%X:0x%X (6 bytes)...\n", page + offset, (unsigned int) finalval2, (unsigned int) finalval);
    } else {
        printf("fixing up %p to 0x%X (%u bytes)...\n", page + offset, (unsigned int) finalval, (unsigned int) finalsize);
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

static void fixupPage(const uint8 *exe, LxModule *lxmod, const LxObjectTableEntry *obj, const uint32 pagenum, uint8 *page)
{
    const LxHeader *lx = &lxmod->lx;
    const uint32 *fixuppage = (((const uint32 *) (exe + lx->fixup_page_table_offset)) + ((obj->page_table_index - 1) + pagenum));
    const uint32 fixupoffset = *fixuppage;
    const uint32 fixuplen = fixuppage[1] - fixuppage[0];
    const uint8 *fixup = (exe + lx->fixup_record_table_offset) + fixupoffset;
    const uint8 *fixupend = fixup + fixuplen;

    while (fixup < fixupend) {
        const uint8 srctype = *(fixup++);
        const uint8 fixupflags = *(fixup++);
        uint8 srclist_count = 0;
        sint16 srcoffset = 0;

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
                // !!! FIXME: where do we use 16-bit selectors? What does this mean in a 32-bit app?
                if ((srctype & 0xF) != 0x2) { // not a 16-bit selector fixup?
                    if (fixupflags & 0x10) {  // 32-bit target offset
                        targetoffset = *((uint32 *) fixup); fixup += 4;
                    } else {  // 16-bit target offset
                        targetoffset = (uint32) (*((uint16 *) fixup)); fixup += 2;
                    } // else
                } // if

                const uint32 base = (uint32) (size_t) lxmod->mmaps[objectid - 1].addr;
                finalval = base + targetoffset;
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

                if (moduleid == 0) {
                    fprintf(stderr, "uhoh, looking for module ordinal 0, which is illegal.\n");
                } else if (moduleid > lx->num_import_mod_entries) {
                    fprintf(stderr, "uhoh, looking for module ordinal %u, but only %u available.\n", (unsigned int) moduleid, (unsigned int) lx->num_import_mod_entries);
                } else {
                    finalval = getModuleProcAddrByOrdinal(lxmod->dependencies[moduleid-1], importid);
                } // else
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

// !!! FIXME: break up this function.
static LxModule *loadLxModule(const char *fname, uint8 *exe, uint32 exelen, int dependency_tree_depth)
{
    LxModule *retval = NULL;
    const uint8 *origexe = exe;
    //const uint32 origexelen = exelen;

    if (!sanityCheckLxModule(&exe, &exelen))
        goto loadlx_failed;

    const LxHeader *lx = (const LxHeader *) exe;
    const uint32 module_type = lx->module_flags & 0x00038000;

    if ((module_type != 0x0000) && (module_type != 0x8000)) {
        fprintf(stderr, "This is not a standard .exe or .dll (maybe device driver?)\n");
        goto loadlx_failed;
    } // if

    const int isDLL = (module_type == 0x8000);

    if (isDLL && !GLoaderState->main_module) {
        fprintf(stderr, "uhoh, need to load an .exe before a .dll!\n");
        goto loadlx_failed;
    } else if (!isDLL && GLoaderState->main_module) {
        fprintf(stderr, "uhoh, loading an .exe after already loading one!\n");
        goto loadlx_failed;
    } // if else if

    retval = (LxModule *) malloc(sizeof (LxModule));
    if (!retval) {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    } // if
    memset(retval, '\0', sizeof (*retval));
    retval->refcount = 1;
    retval->dependencies = (LxModule **) malloc(sizeof (LxModule *) * lx->num_import_mod_entries);
    retval->mmaps = (LxMmaps *) malloc(sizeof (LxMmaps) * lx->module_num_objects);
    if (!retval->dependencies || !retval->mmaps) {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    } // if
    memset(retval->dependencies, '\0', sizeof (LxModule *) * lx->num_import_mod_entries);
    memset(retval->mmaps, '\0', sizeof (LxMmaps) * lx->module_num_objects);
    memcpy(&retval->lx, lx, sizeof (*lx));

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

    if (!isDLL) { // !!! FIXME: mutex?
        GLoaderState->main_module = retval;
    } // else if

    const char *modname = retval->name;
    printf("ref'd new module '%s' to %u\n", modname, 1);

    // !!! FIXME: apparently OS/2 does 1024, but they're not loading the module into RAM each time.
    // !!! FIXME: the spec mentions the 1024 thing for "forwarder" records in the entry table,
    // !!! FIXME:  specifically. _Can_ two DLLs depend on each other? We'll have to load both and
    // !!! FIXME:  then fix them up without recursing.
    if (++dependency_tree_depth > 32) {
        fprintf(stderr, "Likely circular dependency in module '%s'\n", modname);
            goto loadlx_failed;
    } // if

    const LxObjectTableEntry *obj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset));
    for (uint32 i = 0; i < lx->module_num_objects; i++, obj++) {
        if (obj->object_flags & 0x8)  // !!! FIXME: resource object; ignore this until resource support is written, later.
            continue;

        uint32 vsize = obj->virtual_size;
        if ((vsize % lx->page_size) != 0)
             vsize += lx->page_size - (vsize % lx->page_size);

        const int mmapflags = MAP_ANON | MAP_PRIVATE | (isDLL ? 0 : MAP_FIXED);
        void *base = isDLL ? NULL : (void *) ((size_t) obj->reloc_base_addr);
        void *mmapaddr = mmap(base, vsize, PROT_READ|PROT_WRITE, mmapflags, -1, 0);
        // we'll mprotect() these pages to the proper permissions later.

        if (mmapaddr == ((void *) MAP_FAILED)) {
            fprintf(stderr, "mmap(%p, %u, RW-, ANON|PRIVATE%s, -1, 0) failed (%d): %s\n",
                    base, (unsigned int) vsize, isDLL ? "" : "|FIXED", errno, strerror(errno));
            goto loadlx_failed;
        } // if

        retval->mmaps[i].addr = mmapaddr;
        retval->mmaps[i].size = vsize;

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
                        goto loadlx_failed;
                    } // if
                    break;

                case 0x08: // !!! FIXME: this is listed in lxlite, presumably this is exepack3 or something. Maybe this was new to Warp4, like exepack2 was new to Warp3. Pull this algorithm in from lxlite later.
                default:
                    fprintf(stderr, "Don't know how to load an object page of type %u!\n", (unsigned int) objpage->flags);
                    goto loadlx_failed;
            } // switch

            dst += lx->page_size;
        } // for

        // any bytes at the end of this object that we didn't initialize? Zero them out.
        const uint32 remain = vsize - ((uint32) (dst - ((uint8 *) mmapaddr)));
        if (remain) {
            memset(dst, '\0', remain);
        } // if
    } // for

    // All the pages we need from the EXE are loaded into the appropriate spots in memory.
    printf("mmap()'d everything we need!\n");

    retval->eip = lx->eip;
    if (lx->eip_object != 0) {
        const uint32 base = (uint32) ((size_t)retval->mmaps[lx->eip_object - 1].addr);
        retval->eip += base;
    } // if

    // !!! FIXME: esp==0 means something special for programs (and is ignored for library init).
    // !!! FIXME: "A zero value in this field indicates that the stack pointer is to be initialized to the highest address/offset in the object"
    if (!isDLL) {
        retval->esp = lx->esp;
        if (lx->esp_object != 0) {
            const uint32 base = (uint32) ((size_t)retval->mmaps[lx->esp_object - 1].addr);
            retval->esp += base;
        } // if
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
        }
    } // while

    retval->exported_ordinals = (LxExportedOrdinal *) malloc(sizeof (LxExportedOrdinal) * total_ordinals);
    if (!retval->exported_ordinals) {
        fprintf(stderr, "Out of memory!\n");
        goto loadlx_failed;
    } // if
    memset(retval->exported_ordinals, '\0', sizeof (LxExportedOrdinal) * total_ordinals);

    LxExportedOrdinal *expord = retval->exported_ordinals;
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
                    expord->ordinal = ordinal++;
                    expord->addr = (uint32) (size_t) ((uint8 *) retval->mmaps[objidx].addr) + *((const uint16 *) entryptr);
                    expord++;
                    entryptr += 2;
                } // for
                break;

            case 0x03: // 32BIT
                objidx = *((const uint16 *) entryptr) - 1;
                entryptr += 2;
                for (uint8 i = 0; i < numentries; i++) {
                    entryptr++;
                    expord->ordinal = ordinal++;
                    expord->addr = (uint32) (size_t) ((uint8 *) retval->mmaps[objidx].addr) + *((const uint32 *) entryptr);
                    expord++;
                    entryptr += 4;
                }
                break;

            case 0x02: // 286CALLGATE
            case 0x04: // FORWARDER
                fprintf(stderr, "WRITE ME %s:%d\n", __FILE__, __LINE__);
                goto loadlx_failed;

            default:
                fprintf(stderr, "UNKNOWN ENTRY TYPE (%u)\n\n", (unsigned int) bundletype);
                goto loadlx_failed;
        } // switch
    } // while

    retval->num_ordinals = (uint32) (expord - retval->exported_ordinals);

    // Now load named entry points.
    printf("FIXME: load named entry points\n");

    // Load other dependencies of this module.
    const uint8 *import_modules_table = exe + lx->import_module_table_offset;
    for (uint32 i = 0; i < lx->num_import_mod_entries; i++) {
        const uint8 namelen = *(import_modules_table++);
        if (namelen > 127) {
            fprintf(stderr, "Import module %u name is > 127 chars (%u). Corrupt file or bug!\n", (unsigned int) (i + 1), (unsigned int) namelen);
            goto loadlx_failed;
        }
        char name[128];
        memcpy(name, import_modules_table, namelen);
        import_modules_table += namelen;
        name[namelen] = '\0';
        retval->dependencies[i] = loadLxModuleByModuleNameInternal(name, dependency_tree_depth);
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
        if ((i == 1) && (strcmp(modname, "EMX") == 0)) {
            uint8 *ptr = ((uint8 *) retval->mmaps[1].addr) + 28596;
            for (uint32 i = 0; i < 37; i++)
                *(ptr++) = 0x90; // nop
        }


        // Now set all the pages of this object to the proper final permissions...
        const int prot = ((obj->object_flags & 0x1) ? PROT_READ : 0) |
                         ((obj->object_flags & 0x2) ? PROT_WRITE : 0) |
                         ((obj->object_flags & 0x4) ? PROT_EXEC : 0);

        if (mprotect(retval->mmaps[i].addr, retval->mmaps[i].size, prot) == -1) {
            fprintf(stderr, "mprotect(%p, %u, %s%s%s, ANON|PRIVATE|FIXED, -1, 0) failed (%d): %s\n",
                    retval->mmaps[i].addr, (unsigned int) retval->mmaps[i].size,
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
        if (retval->eip) {
            assert(GLoaderState->main_module != NULL);
            assert(GLoaderState->main_module != retval);
            runLxLibraryInit(retval);
        } // if

        retval->initialized = 1;

        // module is ready to use, put it in the loaded list.
        // !!! FIXME: mutex this
        if (GLoaderState->loaded_modules) {
            retval->next = GLoaderState->loaded_modules;
            GLoaderState->loaded_modules->prev = retval;
        } // if
        GLoaderState->loaded_modules = retval;
    } // if

    return retval;

loadlx_failed:
    freeLxModule(retval);
    return NULL;
} // loadLxModule

static LxModule *loadLxModuleByPathInternal(const char *fname, const int dependency_tree_depth)
{
    const char *what = NULL;
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

    LxModule *retval = loadLxModule(fname, module, modulelen, dependency_tree_depth);
    free(module);
    return retval;

loadmod_failed:
    fprintf(stderr, "%s failure on '%s: %s'\n", what, fname, strerror(errno));
    if (io)
        fclose(io);
    free(module);
    return NULL;
} // loadLxModuleByPathInternal

static inline LxModule *loadLxModuleByPath(const char *fname)
{
    return loadLxModuleByPathInternal(fname, 0);
} // loadLxModuleByPath

static LxModule *loadNativeReplacement(const char *fname)
{
    LxModule *retval = NULL;
    void *lib = NULL;
    LxNativeReplacementEntryPoint fn = NULL;
    char *os2path = makeOS2Path(fname);

    if (!os2path)
        goto loadnative_failed;

    // !!! FIXME: mutex this.
    lib = dlopen(fname, RTLD_LOCAL | RTLD_NOW);
    if (lib == NULL)
        goto loadnative_failed;

    fn = (LxNativeReplacementEntryPoint) dlsym(lib, "loadNativeLxModule");
    if (!fn)
        goto loadnative_failed;

    retval = fn(GLoaderState);
    if (!retval)
        goto loadnative_failed;

    printf("Loaded native replacement library '%s' (%u exports).\n", fname, (unsigned int) retval->num_ordinals);
    retval->nativelib = lib;
    retval->os2path = os2path;

    return retval;

loadnative_failed:
    if (lib) dlclose(lib);
    free(os2path);
    return NULL;
} // loadNativeReplacement

static LxModule *loadLxModuleByModuleNameInternal(const char *modname, const int dependency_tree_depth)
{
    // !!! FIXME: mutex this
    for (LxModule *i = GLoaderState->loaded_modules; i != NULL; i = i->next) {
        if (strcasecmp(i->name, modname) == 0) {
            i->refcount++;
            printf("ref'd module '%s' to %u\n", i->name, (unsigned int) i->refcount);
            return i;
        } // if
    } // for

    // !!! FIXME: decide the right path to the file, or if it's a native replacement library.
    char fname[256];
    snprintf(fname, sizeof (fname), "%s.dll", modname);
    LxModule *retval = loadLxModuleByPathInternal(fname, dependency_tree_depth);
    if (!retval) {
        snprintf(fname, sizeof (fname), "native/%s.so", modname);
        for (char *ptr = fname; *ptr; ptr++) {
            *ptr = (((*ptr >= 'A') && (*ptr <= 'Z')) ? (*ptr - ('A' - 'a')) : *ptr);
        } // for
        retval = loadNativeReplacement(fname);
    } // if
    return retval;
} // loadLxModuleByModuleNameInternal

static inline LxModule *loadLxModuleByModuleName(const char *modname)
{
    return loadLxModuleByModuleNameInternal(modname, 0);
} // loadLxModuleByModuleName

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <program.exe>\n", argv[0]);
        return 1;
    }

    const LxModule *lxmod = loadLxModuleByPath(argv[1]);
    if (lxmod) {
        argc--;
        argv++;
        runLxModule(lxmod, argc, argv);
    } // if

    return 0;
} // main

// end of lx_loader.c ...

