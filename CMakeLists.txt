cmake_minimum_required(VERSION 3.4)
project(2ine)

# Note that this project doesn't work on Mac OS X because I can't mmap()
#  address 0x10000, which is the usual base address for OS/2 binaries.
# It appears the address is already accessible when main() begins, but no
#  objects seem to be loaded there. I don't know what it is...maybe some
#  thing allocated by the C runtime at startup? Have to solve that somehow or
#  give up on Mac support.
#
# Eventually, even on Linux, it might make sense to override malloc and friends
#  at the dynamic loader level, so we control all allocations even if they
#  happen in glibc. Once we have that, make sure they all sit in the upper
#  2 gigabytes (reserved for system use on OS/2, so this works out), probably
#  with them mmap()'d there via a local copy of dlmalloc or something.
#
# Note that 32-bit OS/2 programs only got 512 megabytes of the address space
#  (enormous in the days of machines with a couple megabytes of physical RAM!)
#  to accomodate thunking to and from 16 bit code. We don't bother with this
#  at the moment, though, but maybe we must at some point. I hope not.
#
# Also, it might be nice if we can run as a 64-bit process and thunk around in
#  32-bits as necessary, but I don't know the technical barriers to this yet.
#  If we can pull that off, it would allow an OS/2 app to have the full 4
#  gigabytes of memory, and we'll just put 2ine's data outside of that range,
#  and run the native bits with more registers, etc. Maybe even keep the
#  native functions out of low memory and only eat a few bytes of it with
#  trampolines. I'm probably getting ahead of myself, though, as it's not like
#  any OS/2 apps were _that_ memory hungry, and honestly? Port your software.

if(NOT CMAKE_BUILD_TYPE)
    set(DEBUG_BUILD ON)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_BUILD ON)
endif()

if(DEBUG_BUILD)
    add_definitions(-DDEBUG -O0)
else()
    add_definitions(-D_RELEASE)
    add_definitions(-DNDEBUG)
    add_definitions(-DRELEASE)
endif()

option(LX_LEGACY "Enable OS/2 binary support (vs just native apps)" TRUE)
if(NOT LX_LEGACY)
    add_definitions(-DLX_LEGACY=0)
endif()

add_definitions(-std=c99 -Wall -ggdb3)
add_definitions(-D_FILE_OFFSET_BITS=64)

set(CURSES_NEED_WIDE TRUE)
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses)
if(NOT CURSES_FOUND)
    message(FATAL_ERROR "ncurses not found")
endif()

include_directories(${CURSES_INCLUDE_DIR})
if(CURSES_HAVE_NCURSESW_NCURSES_H)
    add_definitions(-DHAVE_NCURSESW_NCURSES_H)
elseif(CURSES_HAVE_NCURSESW_CURSES_H)
    add_definitions(-DHAVE_NCURSESW_CURSES_H)
elseif(CURSES_HAVE_NCURSESW_H)
    add_definitions(-DHAVE_NCURSESW_H)
elseif(CURSES_HAVE_CURSES_H)
    add_definitions(-DHAVE_CURSES_H)
endif()

# !!! FIXME
include_directories("/usr/local/include/SDL2")

add_library(2ine SHARED "lib2ine.c")
if(LX_LEGACY)
    set_target_properties(2ine PROPERTIES COMPILE_FLAGS "-m32")
    set_target_properties(2ine PROPERTIES LINK_FLAGS "-m32 -ggdb3")
endif()
target_link_libraries(2ine "pthread")

foreach(_I doscalls;msg;nls;quecalls;viocalls;kbdcalls;sesmgr;som;pmwin;pmshapi;pmgpi;tcpip32)
    add_library(${_I} SHARED "native/${_I}.c")
    if(LX_LEGACY)
        set_target_properties(${_I} PROPERTIES COMPILE_FLAGS "-m32")
        set_target_properties(${_I} PROPERTIES LINK_FLAGS "-m32 -ggdb3")
    endif()
    target_link_libraries(${_I} 2ine)
endforeach()

target_link_libraries(doscalls "pthread")
# FIXME target_link_libraries(viocalls ${CURSES_LIBRARIES})
target_link_libraries(viocalls ncursesw)

# !!! FIXME: clean this up/
if(LX_LEGACY)
    target_link_libraries(pmwin "${CMAKE_CURRENT_SOURCE_DIR}/libSDL2-2.0.so.0")
else()
    target_link_libraries(pmwin "SDL2")
endif()

add_executable(lx_dump lx_dump.c)

if(LX_LEGACY)
    add_executable(lx_loader lx_loader.c)
    target_link_libraries(lx_loader 2ine)
    target_link_libraries(lx_loader "dl")
    set_target_properties(lx_loader PROPERTIES COMPILE_FLAGS "-m32")
    set_target_properties(lx_loader PROPERTIES LINK_FLAGS "-m32 -ggdb3")
endif()

# end of CMakeLists.txt ...

