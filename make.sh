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

set -x
set -e

gcc -m32 -std=c99 -Wall -O0 -ggdb3 -fPIC -shared -Wl,-soname,msg.so -o native/msg.so native/msg.c
gcc -m32 -std=c99 -Wall -O0 -ggdb3 -fPIC -shared -Wl,-soname,doscalls.so.so -o native/doscalls.so native/doscalls.c
gcc -m32 -std=c99 -Wall -O0 -ggdb3 -fPIC -shared -Wl,-soname,nls.so -o native/nls.so native/nls.c
gcc -m32 -std=c99 -Wall -O0 -ggdb3 -fPIC -shared -Wl,-soname,quecalls.so -o native/quecalls.so native/quecalls.c
gcc -m32 -std=c99 -Wall -O0 -ggdb3 -fPIC -shared -Wl,-soname,viocalls.so -o native/viocalls.so native/viocalls.c
gcc -m32 -std=c99 -Wall -O0 -ggdb3 -fPIC -shared -Wl,-soname,kdbcalls.so -o native/kbdcalls.so native/kbdcalls.c
gcc -m32 -std=c99 -Wall -O0 -ggdb3 -fPIC -shared -Wl,-soname,sesmgr.so -o native/sesmgr.so native/sesmgr.c

gcc -std=c99 -Wall -O0 -ggdb3 -o lx_dump lx_dump.c
gcc -m32 -std=c99 -Wall -O0 -ggdb3 -o lx_loader lx_loader.c -ldl
#./lx_dump tests/hello.exe
./lx_loader tests/helloc.exe

