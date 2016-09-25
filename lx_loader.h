#ifndef _INCL_LX_LOADER_H_
#define _INCL_LX_LOADER_H_ 1

#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

typedef int8_t sint8;
typedef int16_t sint16;
typedef int32_t sint32;

#pragma pack(push, 1)
typedef struct LxHeader
{
    uint8 magic_l;
    uint8 magic_x;
    uint8 byte_order;
    uint8 word_order;
    uint32 lx_version;
    uint16 cpu_type;
    uint16 os_type;
    uint32 module_version;
    uint32 module_flags;
    uint32 module_num_pages;
    uint32 eip_object;
    uint32 eip;
    uint32 esp_object;
    uint32 esp;
    uint32 page_size;
    uint32 page_offset_shift;
    uint32 fixup_section_size;
    uint32 fixup_section_checksum;
    uint32 loader_section_size;
    uint32 loader_section_checksum;
    uint32 object_table_offset;
    uint32 module_num_objects;
    uint32 object_page_table_offset;
    uint32 object_iter_pages_offset;
    uint32 resource_table_offset;
    uint32 num_resource_table_entries;
    uint32 resident_name_table_offset;
    uint32 entry_table_offset;
    uint32 module_directives_offset;
    uint32 num_module_directives;
    uint32 fixup_page_table_offset;
    uint32 fixup_record_table_offset;
    uint32 import_module_table_offset;
    uint32 num_import_mod_entries;
    uint32 import_proc_table_offset;
    uint32 per_page_checksum_offset;
    uint32 data_pages_offset;
    uint32 num_preload_pages;
    uint32 non_resident_name_table_offset;
    uint32 non_resident_name_table_len;
    uint32 non_resident_name_table_checksum;
    uint32 auto_ds_object_num;
    uint32 debug_info_offset;
    uint32 debug_info_len;
    uint32 num_instance_preload;
    uint32 num_instance_demand;
    uint32 heapsize;
} LxHeader;

typedef struct LxObjectTableEntry
{
    uint32 virtual_size;
    uint32 reloc_base_addr;
    uint32 object_flags;
    uint32 page_table_index;
    uint32 num_page_table_entries;
    uint32 reserved;
} LxObjectTableEntry;

typedef struct LxObjectPageTableEntry
{
    uint32 page_data_offset;
    uint16 data_size;
    uint16 flags;
} LxObjectPageTableEntry;

typedef struct LxResourceTableEntry
{
    uint16 type_id;
    uint16 name_id;
    uint32 resource_size;
    uint16 object;
    uint32 offset;
} LxResourceTableEntry;

#pragma pack(pop)


typedef struct LxMmaps
{
    void *addr;
    size_t size;
} LxMmaps;

typedef struct LxExportedOrdinal
{
    uint32 ordinal;
    uint32 addr;
} LxExportedOrdinal;

typedef struct LxExportedName
{
    char name[128];  // !!! FIXME: allocate this.
    uint32 addr;
} LxExportedName;

struct LxModule;
typedef struct LxModule LxModule;
struct LxModule
{
    LxHeader lx;
    uint32 refcount;
    char name[256];  // !!! FIXME: allocate this.
    LxModule **dependencies;
    LxMmaps *mmaps;
    uint32 num_ordinals;
    LxExportedOrdinal *exported_ordinals;
    uint32 num_names;
    LxExportedName *exported_names;
    void *nativelib;
    uint32 eip;
    uint32 esp;
    LxModule *prev;  // all loaded modules are in a doubly-linked list.
    LxModule *next;  // all loaded modules are in a doubly-linked list.
};

typedef LxModule *(*LxNativeReplacementEntryPoint)(void);

// !!! FIXME: this is nasty for several reasons.
#define NATIVE_REPLACEMENT_TABLE(modname) \
    LxModule *loadNativeLxModule(void) { \
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

// end of lx_loader.h ...

