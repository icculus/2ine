#!/usr/bin/perl -w
#
# 2ine; an OS/2 emulator for Linux.
#
# Please see the file LICENSE.txt in the source's root directory.
#
#  This file written by Ryan C. Gordon.


# run this on the output from ndisasm.

use warnings;
use strict;

while (<>) {
    chomp;
    my ($bytes, $asm) = /\A.*?\s+(.*?)\s+(.*)\Z/;
    my $first = 1;
    while ($bytes ne '') {
        $bytes =~ s/\A(..)//;
        my $hex = $1;
        if ($first and ($bytes eq '')) {
            print("    *(ptr++) = 0x$hex;  /* $asm */\n");
        } elsif ($first) {
            print("    *(ptr++) = 0x$hex;  /* $asm... */\n");
        } else {
            print("    *(ptr++) = 0x$hex;  /*  ...$asm */\n");
        }
        $first = 0;
    }
}

