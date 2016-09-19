#!/bin/sh

# Note that this doesn't work on Mac OS X because I can't mmap()
#  address 0x10000, which is the usual base address for OS/2 binaries.
# It appears the address is already accessible when main() begins, but no
#  objects seem to be loaded there. I don't know what it is...maybe some
#  thing allocated by the C runtime at startup? Have to solve that somehow or
#  give up on Mac support.
#
# 32-bit Linux totally lets me have that space, though.  :)
#clang -pagezero_size 0x1000 -image_base 0x100000000 -Wall -O0 -ggdb3 -o lx_loader lx_loader.c && ./lx_loader ./cmd.exe

gcc -std=c99 -Wall -O0 -ggdb3 -o lx_dump lx_dump.c &&
gcc -m32 -std=c99 -Wall -O0 -ggdb3 -o lx_loader lx_loader.c &&
./lx_dump tests/hello.exe &&
./lx_loader tests/hello.exe
