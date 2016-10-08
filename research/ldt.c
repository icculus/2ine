// "man modify_ldt"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <asm/ldt.h>

int main(void)
{
    struct user_desc ldt[8 * 1024];
    long rc;

    //rc = syscall(123, (int) 0, (void *) ldt, (unsigned long) sizeof (ldt));
    //printf("read syscall == %ld\n", rc);

    char *str = malloc(64);
    strcpy(str, "hello world!");
    printf("&str == %p\n", str);

    unsigned int fs = 0xFFFFFFFF;
    __asm__ __volatile__ (
        "xorl %%eax, %%eax \n\t"
        "movw %%fs, %%ax \n\t"
            : "=a" (fs)
    );

    printf("Current %%fs: 0x%X\n", fs);

    struct user_desc *entry = ldt;
    entry->entry_number = -1;
    entry->base_addr = (unsigned long) str;
    entry->limit = 64;
    entry->seg_32bit = 1;
    entry->contents = MODIFY_LDT_CONTENTS_DATA;
    entry->read_exec_only = 0;
    entry->limit_in_pages = 0;
    entry->seg_not_present = 0;
    entry->useable = 1;

    printf("entry=%u, base=%p, limit=%u\n", entry->entry_number, (void *) entry->base_addr, entry->limit);

// modify_ldt
//    rc = syscall(SYS_modify_ldt, (int) 1, (void *) entry, (unsigned long) sizeof (*entry));
//    printf("write syscall == %ld\n", rc);

// set_thread_area
    rc = syscall(SYS_set_thread_area, entry);
    printf("write syscall == %ld\n", rc);

    printf("entry==%d\n", (int) (entry->entry_number));

// this doesn't give you a user_desc struct back, I think it gives you the real LDT entry...?
//    rc = syscall(SYS_modify_ldt, (int) 0, (void *) entry, (unsigned long) sizeof (*entry) * 128);
//    printf("read syscall == %ld\n", rc);

//    long i;
//    for (i = 0; i < 128; i++, entry++)
//        printf("entry=%u, base=%p, limit=%u\n", entry->entry_number, (void *) entry->base_addr, entry->limit);

    // I have no idea why this needs the "<< 3 | 3", but it's probably
    //  specified in the Intel manuals. I got this from looking at how
    //  Wine does it.
    __asm__ __volatile__ (
        //"pushw %%ax\n\t"
        //"popw %%fs \n\t"
        "movw %%ax,%%fs \n\t"
            :
            : "a" ((entry->entry_number << 3) | 3)
    );

    fs = 0xFFFFFFFF;
    __asm__ __volatile__ (
        "xorl %%eax, %%eax \n\t"
        "movw %%fs, %%ax \n\t"
            : "=a" (fs)
    );

    printf("New %%fs: 0x%X\n", fs);

    char newstr[16];
    memset(newstr, '\0', sizeof (newstr));
    __asm__ __volatile__ (
        "movl %%fs:0,%%eax\n\t"
        "movl %%eax,(%%edi)\n\t"
        "movl %%fs:4,%%eax\n\t"
        "movl %%eax,4(%%edi)\n\t"
        "movl %%fs:8,%%eax\n\t"
        "movl %%eax,8(%%edi)\n\t"
        :
        : "D" (newstr)
        : "eax"
    );

    printf("copied string == '%s'\n", newstr);

    return 0;
}

// end of ldt.c ...

