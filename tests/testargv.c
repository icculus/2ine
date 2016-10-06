#include <stdio.h>

int main(int argc, char **argv, char **envp) {
    int i;

    printf("Command line: (argc == %d)\n", argc);
    for (i = 0; i <= argc; i++) {
        printf("argv[%d] = '%s'\n", i, argv[i]);
    } // for

    printf("\nEnvironment:\n");
    for (i = 0; envp[i]; i++) {
        printf("envp[%d] = '%s'\n", i, envp[i]);
    } // for

    return 0;
} // main

// end of testargv.c ...

