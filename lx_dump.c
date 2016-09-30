#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "lx_loader.h"

// !!! FIXME: some cut-and-paste with lx_loader.c ...

static int sanityCheckExe(uint8 **_exe, uint32 *_exelen)
{
    if (*_exelen < 196) {
        fprintf(stderr, "not an OS/2 LX EXE\n");
        return 0;
    }
    const uint32 header_offset = *((uint32 *) (*_exe + 0x3C));
    //printf("header offset is %u\n", (uint) header_offset);
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
        fprintf(stderr, "Program is unknown LX EXE version (%u)\n", (uint) lx->lx_version);
        return 0;
    }

    if (lx->cpu_type > 3) { // 1==286, 2==386, 3==486
        fprintf(stderr, "Program needs unknown CPU type (%u)\n", (uint) lx->cpu_type);
        return 0;
    }

    if (lx->os_type != 1) { // 1==OS/2, others: dos4, windows, win386, unknown.
        fprintf(stderr, "Program needs unknown OS type (%u)\n", (uint) lx->os_type);
        return 0;
    }

    if (lx->page_size != 4096) {
        fprintf(stderr, "Program page size isn't 4096 (%u)\n", (uint) lx->page_size);
        return 0;
    }

    return 1;
} // sanityCheckExe


static void parseExe(const char *exefname, uint8 *exe, uint32 exelen)
{
    printf("%s\n", exefname);

    const uint8 *origexe = exe;
    if (!sanityCheckExe(&exe, &exelen))
        return;

    const LxHeader *lx = (const LxHeader *) exe;
    printf("module version: %u\n", (uint) lx->module_version);

    printf("module flags:");
    if (lx->module_flags & 0x4) printf(" LIBINIT");
    if (lx->module_flags & 0x10) printf(" INTERNALFIXUPS");
    if (lx->module_flags & 0x20) printf(" EXTERNALFIXUPS");
    if (lx->module_flags & 0x100) printf(" PMINCOMPAT");
    if (lx->module_flags & 0x200) printf(" PMCOMPAT");
    if (lx->module_flags & 0x300) printf(" USESPM");
    if (lx->module_flags & 0x2000) printf(" NOTLOADABLE");
    if (lx->module_flags & 0x8000) printf(" LIBRARYMODULE");
    if (lx->module_flags & 0x18000) printf(" PROTMEMLIBRARYMODULE");
    if (lx->module_flags & 0x20000) printf(" PHYSDRIVERMODULE");
    if (lx->module_flags & 0x28000) printf(" VIRTDRIVERMODULE");
    if (lx->module_flags & 0x40000000) printf(" LIBTERM");
    printf("\n");

    printf("Number of pages in module: %u\n", (uint) lx->module_num_pages);
    printf("EIP Object number: %u\n", (uint) lx->eip_object);
    printf("EIP: 0x%X\n", (uint) lx->eip);
    printf("ESP Object number: %u\n", (uint) lx->esp_object);
    printf("ESP: 0x%X\n", (uint) lx->esp);
    printf("Page size: %u\n", (uint) lx->page_size);
    printf("Page offset shift: %u\n", (uint) lx->page_offset_shift);
    printf("Fixup section size: %u\n", (uint) lx->fixup_section_size);
    printf("Fixup section checksum: 0x%X\n", (uint) lx->fixup_section_checksum);
    printf("Loader section size: %u\n", (uint) lx->loader_section_size);
    printf("Loader section checksum: 0x%X\n", (uint) lx->loader_section_checksum);
    printf("Object table offset: %u\n", (uint) lx->object_table_offset);
    printf("Number of objects in module: %u\n", (uint) lx->module_num_objects);
    printf("Object page table offset: %u\n", (uint) lx->object_page_table_offset);
    printf("Object iterated pages offset: %u\n", (uint) lx->object_iter_pages_offset);
    printf("Resource table offset: %u\n", (uint) lx->resource_table_offset);
    printf("Number of resource table entries: %u\n", (uint) lx->num_resource_table_entries);
    printf("Resident name table offset: %u\n", (uint) lx->resident_name_table_offset);
    printf("Entry table offset: %u\n", (uint) lx->entry_table_offset);
    printf("Module directives offset: %u\n", (uint) lx->module_directives_offset);
    printf("Number of module directives: %u\n", (uint) lx->num_module_directives);
    printf("Fixup page table offset: %u\n", (uint) lx->fixup_page_table_offset);
    printf("Fixup record table offset: %u\n", (uint) lx->fixup_record_table_offset);
    printf("Import module table offset: %u\n", (uint) lx->import_module_table_offset);
    printf("Number of inport module entries: %u\n", (uint) lx->num_import_mod_entries);
    printf("Import procedure name table offset: %u\n", (uint) lx->import_proc_table_offset);
    printf("Per-page checksum offset: %u\n", (uint) lx->per_page_checksum_offset);
    printf("Data pages offset: %u\n", (uint) lx->data_pages_offset);
    printf("Number of preload pages: %u\n", (uint) lx->num_preload_pages);
    printf("Non-resident name table offset: %u\n", (uint) lx->non_resident_name_table_offset);
    printf("Non-resident name table length: %u\n", (uint) lx->non_resident_name_table_len);
    printf("Non-resident name table checksum: 0x%X\n", (uint) lx->non_resident_name_table_checksum);
    printf("Auto data segment object number: %u\n", (uint) lx->auto_ds_object_num);
    printf("Debug info offset: %u\n", (uint) lx->debug_info_offset);
    printf("Debug info length: %u\n", (uint) lx->debug_info_len);
    printf("Number of instance pages in preload section: %u\n", (uint) lx->num_instance_preload);
    printf("Number of instance pages in demand section: %u\n", (uint) lx->num_instance_demand);
    printf("Heap size: %u\n", (uint) lx->heapsize);

    /* This is apparently a requirement as of OS/2 2.0, according to lxexe.txt. */
    if ((lx->object_iter_pages_offset != 0) && (lx->object_iter_pages_offset != lx->data_pages_offset)) {
        fprintf(stderr, "Object iterator pages offset must be 0 or equal to Data pages offset\n");
    }

    // when an LX file says "object" it's probably more like "section" or "segment" ...?
    printf("\n");
    printf("Object table (%u entries):\n", (uint) lx->module_num_objects);
    for (uint32 i = 0; i < lx->module_num_objects; i++) {
        const LxObjectTableEntry *obj = ((const LxObjectTableEntry *) (exe + lx->object_table_offset)) + i;
        printf("Object #%u:\n", (uint) i+1);
        printf("Virtual size: %u\n", (uint) obj->virtual_size);
        printf("Relocation base address: 0x%X\n", (uint) obj->reloc_base_addr);
        printf("Object flags:");
        if (obj->object_flags & 0x1) printf(" READ");
        if (obj->object_flags & 0x2) printf(" WRITE");
        if (obj->object_flags & 0x4) printf(" EXEC");
        if (obj->object_flags & 0x8) printf(" RESOURCE");
        if (obj->object_flags & 0x10) printf(" DISCARD");
        if (obj->object_flags & 0x20) printf(" SHARED");
        if (obj->object_flags & 0x40) printf(" PRELOAD");
        if (obj->object_flags & 0x80) printf(" INVALID");
        if (obj->object_flags & 0x100) printf(" ZEROFILL");
        if (obj->object_flags & 0x200) printf(" RESIDENT");
        if (obj->object_flags & 0x300) printf(" RESIDENT+CONTIG");
        if (obj->object_flags & 0x400) printf(" RESIDENT+LONGLOCK");
        if (obj->object_flags & 0x800) printf(" SYSRESERVED");
        if (obj->object_flags & 0x1000) printf(" 16:16");
        if (obj->object_flags & 0x2000) printf(" BIG");
        if (obj->object_flags & 0x4000) printf(" CONFORM");
        if (obj->object_flags & 0x8000) printf(" IOPL");
        printf("\n");
        printf("Page table index: %u\n", (uint) obj->page_table_index);
        printf("Number of page table entries: %u\n", (uint) obj->num_page_table_entries);
        printf("System-reserved field: %u\n", (uint) obj->reserved);

        printf("Object pages:\n");
        const LxObjectPageTableEntry *objpage = ((const LxObjectPageTableEntry *) (exe + lx->object_page_table_offset)) + (obj->page_table_index - 1);
        const uint32 *fixuppage = (((const uint32 *) (exe + lx->fixup_page_table_offset)) + (obj->page_table_index - 1));
        for (uint32 i = 0; i < obj->num_page_table_entries; i++, objpage++, fixuppage++) {
            printf("Object Page #%u:\n", (uint) (i + obj->page_table_index));
            printf("Page data offset: 0x%X\n", (uint) objpage->page_data_offset);
            printf("Page data size: %u\n", (uint) objpage->data_size);
            printf("Page flags: (%u)", (uint) objpage->flags);
            if (objpage->flags == 0x0) printf(" PHYSICAL");
            else if (objpage->flags == 0x1) printf(" ITERATED");
            else if (objpage->flags == 0x2) printf(" INVALID");
            else if (objpage->flags == 0x3) printf(" ZEROFILL");
            else if (objpage->flags == 0x4) printf(" RANGE");
            else if (objpage->flags == 0x5) printf(" COMPRESSED");
            else printf(" UNKNOWN");
            printf("\n");
            const uint32 fixupoffset = *fixuppage;
            const uint32 fixuplen = fixuppage[1] - fixuppage[0];
            printf("Page's fixup record offset: %u\n", (uint) fixupoffset);
            printf("Page's fixup record size: %u\n", (uint) fixuplen);
            printf("Fixup records:\n");
            const uint8 *fixup = (exe + lx->fixup_record_table_offset) + fixupoffset;
            const uint8 *fixupend = fixup + fixuplen;
            for (uint32 i = 0; fixup < fixupend; i++) {
                printf("Fixup Record #%u:\n", (uint) (i + 1));
                printf("Source type: ");
                const uint8 srctype = *(fixup++);
                if (srctype & 0x10) printf("[FIXUPTOALIAS] ");
                if (srctype & 0x20) printf("[SOURCELIST] ");
                switch (srctype & 0xF) {
                    case 0x00: printf("Byte fixup"); break;
                    case 0x02: printf("16-bit selector fixup"); break;
                    case 0x03: printf("16:16 pointer fixup"); break;
                    case 0x05: printf("16-bit offset fixup"); break;
                    case 0x06: printf("16:32 pointer fixup"); break;
                    case 0x07: printf("32-bit offset fixup"); break;
                    case 0x08: printf("32-bit self-relative offset fixup"); break;
                    default: printf("(undefined fixup)"); break;
                } // switch
                printf("\n");

                const uint8 fixupflags = *(fixup++);
                printf("Target flags:");
                switch (fixupflags & 0x3) {
                    case 0x0: printf(" INTERNAL"); break;
                    case 0x1: printf(" IMPORTBYORDINAL"); break;
                    case 0x2: printf(" IMPORTBYNAME"); break;
                    case 0x3: printf(" INTERNALVIAENTRY"); break;
                } // switch

                if (fixupflags & 0x4) printf(" ADDITIVE");
                if (fixupflags & 0x8) printf(" INTERNALCHAINING");
                if (fixupflags & 0x10) printf(" 32BITTARGETOFFSET");
                if (fixupflags & 0x20) printf(" 32BITADDITIVE");
                if (fixupflags & 0x40) printf(" 16BITORDINAL");
                if (fixupflags & 0x80) printf(" 8BITORDINAL");
                printf("\n");

                uint8 srclist_count = 0;
                if (srctype & 0x20) { // source list
                    srclist_count = *(fixup++);
                    printf("Source offset list count: %u\n", (uint) srclist_count);
                } else {
                    const sint16 srcoffset = *((sint16 *) fixup); fixup += 2;
                    printf("Source offset: %d\n", (int) srcoffset);
                } // else
                printf("\n");

                switch (fixupflags & 0x3) {
                    case 0x0:
                        printf("Internal fixup record:\n");
                        if (fixupflags & 0x40) { // 16 bit value
                            const uint16 val = *((uint16 *) fixup); fixup += 2;
                            printf("Object: %u\n", (uint) val);
                        } else {
                            const uint8 val = *(fixup++);
                            printf("Object: %u\n", (uint) val);
                        } // else

                        printf("Target offset: ");
                        if ((srctype & 0xF) == 0x2) { // 16-bit selector fixup
                            printf("[not used for 16-bit selector fixups]\n");
                        } else if (fixupflags & 0x10) {  // 32-bit target offset
                            const uint32 val = *((uint32 *) fixup); fixup += 4;
                            printf("%u\n", (uint) val);
                        } else {  // 16-bit target offset
                            const uint16 val = *((uint16 *) fixup); fixup += 2;
                            printf("%u\n", (uint) val);
                        } // else
                        break;

                    case 0x1:
                        printf("Import by ordinal fixup record:\n");
                        if (fixupflags & 0x40) { // 16 bit value
                            const uint16 val = *((uint16 *) fixup); fixup += 2;
                            printf("Module ordinal: %u\n", (uint) val);
                        } else {
                            const uint8 val = *(fixup++);
                            printf("Module ordinal: %u\n", (uint) val);
                        } // else

                        if (fixupflags & 0x80) { // 8 bit value
                            const uint8 val = *(fixup++);
                            printf("Import ordinal: %u\n", (uint) val);
                        } else if (fixupflags & 0x10) {  // 32-bit value
                            const uint32 val = *((uint32 *) fixup); fixup += 4;
                            printf("Import ordinal: %u\n", (uint) val);
                        } else {  // 16-bit value
                            const uint16 val = *((uint16 *) fixup); fixup += 2;
                            printf("Import ordinal: %u\n", (uint) val);
                        } // else

                        uint32 additive = 0;
                        if (fixupflags & 0x4) {  // Has additive.
                            if (fixupflags & 0x20) { // 32-bit value
                                additive = *((uint32 *) fixup);
                                fixup += 4;
                            } else {  // 16-bit value
                                additive = *((uint16 *) fixup);
                                fixup += 2;
                            } // else
                        } // if
                        printf("Additive: %u\n", (uint) additive);
                        break;

                    case 0x2:
                        printf("Import by name fixup record:\n");
                        printf("WRITE ME\n"); exit(1);
                        break;

                    case 0x3:
                        printf("Internal entry table fixup record:\n");
                        printf("WRITE ME\n"); exit(1);
                        break;
                } // switch

                if (srctype & 0x20) { // source list
                    printf("Source offset list:");
                    for (uint8 i = 0; i < srclist_count; i++) {
                        const sint16 val = *((sint16 *) fixup); fixup += 2;
                        printf(" %d", (int) val);
                    } // for
                } // if

                printf("\n\n");
            } // while

            printf("\n");
        } // for
    } // for

    printf("\n");
    printf("Resource table (%u entries):\n", (uint) lx->num_resource_table_entries);
    for (uint32 i = 0; i < lx->num_resource_table_entries; i++) {
        const LxResourceTableEntry *rsrc = ((const LxResourceTableEntry *) (exe + lx->resource_table_offset)) + i;
        printf("%u:\n", (uint) i);
        printf("Type ID: %u\n", (uint) rsrc->type_id);
        printf("Name ID: %u\n", (uint) rsrc->name_id);
        printf("Resource size: %u\n", (uint) rsrc->resource_size);
        printf("Object: %u\n", (uint) rsrc->object);
        printf("Offset: 0x%X\n", (uint) rsrc->offset);
        printf("\n");
    } // for

    printf("\n");

    printf("Entry table:\n");
    int bundleid = 1;
    int ordinal = 1;
    const uint8 *entryptr = exe + lx->entry_table_offset;
    while (*entryptr) {  /* end field has a value of zero. */
        const uint8 numentries = *(entryptr++);  /* number of entries in this bundle */
        const uint8 bundletype = (*entryptr) & ~0x80;
        const uint8 paramtypes = (*(entryptr++) & 0x80) ? 1 : 0;

        printf("Bundle %d (%u entries): ", bundleid, (uint) numentries);
        bundleid++;

        if (paramtypes)
            printf("[PARAMTYPES] ");

        switch (bundletype) {
            case 0x00:
                printf("UNUSED\n");
                printf(" %u unused entries.\n\n", (uint) numentries);
                ordinal += numentries;
                break;

            case 0x01:
                printf("16BIT\n");
                printf(" Object number: %u\n", (uint) *((const uint16 *) entryptr)); entryptr += 2;
                for (uint8 i = 0; i < numentries; i++) {
                    printf(" %d:\n", ordinal++);
                    printf(" Flags:");
                    if (*entryptr & 0x1) printf(" EXPORTED");
                    printf("\n");
                    printf(" Parameter word count: %u\n",  (uint) ((*entryptr & 0xF8) >> 3)); entryptr++;
                    printf(" Offset: %u\n",  (uint) *((const uint16 *) entryptr)); entryptr += 2;
                    printf("\n");
                }
                break;

            case 0x02:
                printf("286CALLGATE\n");
                printf(" Object number: %u\n", (uint) *((const uint16 *) entryptr)); entryptr += 2;
                for (uint8 i = 0; i < numentries; i++) {
                    printf(" %d:\n", ordinal++);
                    printf(" Flags:");
                    if (*entryptr & 0x1) printf(" EXPORTED");
                    printf("\n");
                    printf(" Parameter word count: %u\n",  (uint) ((*entryptr & 0xF8) >> 3)); entryptr++;
                    printf(" Offset: %u\n",  (uint) *((const uint16 *) entryptr)); entryptr += 2;
                    printf(" Callgate selector: %u\n",  (uint) *((const uint16 *) entryptr)); entryptr += 2;
                    printf("\n");
                }
                break;

            case 0x03: printf("32BIT\n");
                printf(" Object number: %u\n", (uint) *((const uint16 *) entryptr)); entryptr += 2;
                for (uint8 i = 0; i < numentries; i++) {
                    printf(" %d:\n", ordinal++);
                    printf(" Flags:");
                    if (*entryptr & 0x1) printf(" EXPORTED");
                    printf("\n");
                    printf(" Parameter word count: %u\n",  (uint) ((*entryptr & 0xF8) >> 3)); entryptr++;
                    printf(" Offset: %u\n",  (uint) *((const uint32 *) entryptr)); entryptr += 4;
                    printf("\n");
                }
                break;

            case 0x04: printf("FORWARDER\n"); break;
                printf(" Reserved field: %u\n", (uint) *((const uint16 *) entryptr)); entryptr += 2;
                for (uint8 i = 0; i < numentries; i++) {
                    printf(" %d:\n", ordinal++);
                    printf(" Flags:");
                    const int isordinal = (*entryptr & 0x1);
                    if (isordinal) printf(" IMPORTBYORDINAL");
                    printf("\n");
                    printf(" Reserved for future use: %u\n",  (uint) ((*entryptr & 0xF8) >> 3)); entryptr++;
                    printf(" Module ordinal number: %u\n", (uint) *((const uint16 *) entryptr)); entryptr += 2;
                    if (isordinal) {
                        printf(" Import ordinal number: %u\n",  (uint) *((const uint32 *) entryptr)); entryptr += 4;
                    } else {
                        printf(" Import name offset: %u\n",  (uint) *((const uint32 *) entryptr)); entryptr += 4;
                    }
                    printf("\n");
                }
                break;

            default:
                printf("UNKNOWN (%u)\n\n", (uint) bundletype);
                break;  // !!! FIXME: what to do?
        } // switch
    } // while
    printf("\n");

    printf("Module directives (%u entries):\n", (uint) lx->num_module_directives);
    const uint8 *dirptr = exe + lx->module_directives_offset;
    for (uint32 i = 0; i < lx->num_module_directives; i++) {
        printf("%u:\n", (uint) i+1);
        printf("Directive ID: %u\n", (uint) *((const uint16 *) dirptr)); dirptr += 2;
        printf("Data size: %u\n", (uint) *((const uint16 *) dirptr)); dirptr += 2;
        printf("Data offset: %u\n", (uint) *((const uint32 *) dirptr)); dirptr += 4;
        printf("\n");
        // !!! FIXME: verify record directive table, etc, based on Directive ID
    }
    printf("\n");

    if (lx->per_page_checksum_offset == 0) {
        printf("No per-page checksums available.\n");
    } else {
        printf("!!! FIXME: look at per-page checksums!\n");
    }
    printf("\n");


    printf("Import modules (%u entries):\n", (uint) lx->num_import_mod_entries);
    const uint8 *import_modules_table = exe + lx->import_module_table_offset;
    for (uint32 i = 0; i < lx->num_import_mod_entries; i++) {
        char name[128];
        const uint8 namelen = *(import_modules_table++);
        // !!! FIXME: name can't be more than 127 chars, according to docs. Check this.
        memcpy(name, import_modules_table, namelen);
        import_modules_table += namelen;
        name[namelen] = '\0';
        printf("%u: %s\n", (uint) i+1, name);
    }

    const uint8 *name_table;

    printf("Resident name table:\n");
    name_table = exe + lx->resident_name_table_offset;
    for (uint32 i = 0; *name_table; i++) {
        const uint8 namelen = *(name_table++);
        char name[256];
        memcpy(name, name_table, namelen);
        name[namelen] = '\0';
        name_table += namelen;
        const uint16 ordinal = *((const uint16 *) name_table); name_table += 2;
        printf("%u: '%s' (ordinal %u)\n", (uint) i, name, (uint) ordinal);
    } // for

    printf("Non-resident name table:\n");
    name_table = origexe + lx->non_resident_name_table_offset;
    const uint8 *end_of_name_table = name_table + lx->non_resident_name_table_len;
    for (uint32 i = 0; (name_table < end_of_name_table) && *name_table; i++) {
        const uint8 namelen = *(name_table++);
        char name[256];
        memcpy(name, name_table, namelen);
        name[namelen] = '\0';
        name_table += namelen;
        const uint16 ordinal = *((const uint16 *) name_table); name_table += 2;
        printf("%u: '%s' (ordinal %u)\n", (uint) i, name, (uint) ordinal);
    } // for

#if 0
    const uint8 *fixup_record_table = exe + fixup_record_table_offset;
    const uint8 *import_proc_table = exe + lx->import_proc_table_offset;
    for (uint32 i = 0; i < 1000; i++) {
        char name[128];
        const uint8 namelen = *(import_proc_table++) & 0x7F;
        // !!! FIXME: name can't be more than 127 chars, according to docs. Check this.
        memcpy(name, import_proc_table, namelen);
        import_proc_table += namelen;
        name[namelen] = '\0';
        printf("PROC: %s\n", name);
    }
#endif
} // parseExe

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

    parseExe(exefname, exe, exelen);

    free(exe);

    return 0;
} // main

// end of lx_dump.c ...
