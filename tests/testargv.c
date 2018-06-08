#include <stdio.h>

extern char **environ;

int main(int argc, char **argv) {
    int i;
    char **envp = environ;

    printf("Command line: (argc == %d)\n", argc);
    for (i = 0; i <= argc; i++) {
        printf("argv[%d] = '%s'\n", i, argv[i] ? argv[i] : "(null)");
    } // for

    printf("\nEnvironment:\n");
    for (i = 0; envp[i]; i++) {
        printf("envp[%d] = '%s'\n", i, envp[i] ? envp[i] : "(null)");
    } // for

    return 0;
} // main

// end of testargv.c ...

