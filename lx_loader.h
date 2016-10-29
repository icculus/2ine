#ifndef _INCL_LX_LOADER_H_
#define _INCL_LX_LOADER_H_ 1

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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

typedef unsigned int uint;

#ifndef _STUBBED
    #define _STUBBED(x, what) do { \
        static int seen_this = 0; \
        if (!seen_this) { \
            seen_this = 1; \
            fprintf(stderr, "2INE " what ": %s at %s (%s:%d)\n", x, __FUNCTION__, __FILE__, __LINE__); \
        } \
    } while (0)
#endif

#if 0
#ifndef STUBBED
#define STUBBED(x) _STUBBED(x, "STUBBED")
#endif
#ifndef FIXME
#define FIXME(x) _STUBBED(x, "FIXME")
#endif
#endif

#ifndef STUBBED
    #define STUBBED(x) do {} while (0)
#endif
#ifndef FIXME
    #define FIXME(x) do {} while (0)
#endif

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
    void *mapped;
    void *addr;
    size_t size;
    uint16 alias;  // 16:16 alias, if one.
} LxMmaps;

typedef struct LxExport
{
    uint32 ordinal;
    const char *name;  // can be NULL
    void *addr;
    const LxMmaps *object;
} LxExport;

struct LxModule;
typedef struct LxModule LxModule;
struct LxModule
{
    LxHeader lx;
    uint32 refcount;
    char name[256];  // !!! FIXME: allocate this.
    LxModule **dependencies;
    LxMmaps *mmaps;

    const LxExport *exports;
    uint32 num_exports;
    void *nativelib;
    uint32 eip;
    uint32 esp;
    int initialized;
    char *os2path;  // absolute path to module, in OS/2 format
    // !!! FIXME: put this elsewhere?
    uint32 signal_exception_focus_count;
    LxModule *prev;  // all loaded modules are in a doubly-linked list.
    LxModule *next;  // all loaded modules are in a doubly-linked list.
};

#pragma pack(push, 1)
typedef struct LxTIB2
{
    uint32 tib2_ultid;
    uint32 tib2_ulpri;
    uint32 tib2_version;
    uint16 tib2_usMCCount;
    uint16 tib2_fMCForceFlag;
} LxTIB2;

typedef struct LxTIB
{
    void *tib_pexchain;
    void *tib_pstack;
    void *tib_pstacklimit;
    LxTIB2 *tib_ptib2;
    uint32 tib_version;
    uint32 tib_ordinal;
} LxTIB;

typedef struct LxPIB
{
    uint32 pib_ulpid;
    uint32 pib_ulppid;
    void *pib_hmte;
    char *pib_pchcmd;
    char *pib_pchenv;
    uint32 pib_flstatus;
    uint32 pib_ultype;
} LxPIB;

#pragma pack(pop)

// We put the 128 bytes of TLS slots after the TIB structs.
#define LXTIBSIZE (sizeof (LxTIB) + sizeof (LxTIB2) + 128)

typedef struct LxLoaderState
{
    LxModule *loaded_modules;
    LxModule *main_module;
    uint8 main_tibspace[LXTIBSIZE];
    LxPIB pib;
    int subprocess;
    int running;
    int trace_native;
    uint16 original_cs;
    uint16 original_ss;
    uint16 original_ds;
    uint32 ldt[8192];
    char *libpath;
    uint32 libpathlen;
    uint32 *tlspage;
    uint32 tlsmask;  // one bit for each TLS slot in use.
    uint8 tlsallocs[32];  // number of TLS slots allocated in one block, indexed by starting block (zero if not the starting block).
    void (*dosExit)(uint32 action, uint32 result);
    uint16 (*initOs2Tib)(uint8 *tibspace, void *_topOfStack, const size_t stacklen, const uint32 tid);
    void (*deinitOs2Tib)(const uint16 selector);
    int (*findSelector)(const uint32 addr, uint16 *outselector, uint16 *outoffset);
    void (*freeSelector)(const uint16 selector);
    void *(*convert1616to32)(const uint32 addr1616);
    uint32 (*convert32to1616)(void *addr32);
    LxModule *(*loadModule)(const char *modname);
    int (*locatePathCaseInsensitive)(char *buf);
    char *(*makeUnixPath)(const char *os2path, uint32 *err);
    char *(*makeOS2Path)(const char *fname);
    void __attribute__((noreturn)) (*terminate)(const uint32 exitcode);
} LxLoaderState;

typedef const LxExport *(*LxNativeModuleInitEntryPoint)(LxLoaderState *lx_state, uint32 *lx_num_exports);
typedef void (*LxNativeModuleDeinitEntryPoint)(void);

#endif

// end of lx_loader.h ...

