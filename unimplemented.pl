#!/usr/bin/perl -w

use warnings;
use strict;
use File::Basename;

my %implemented = ();

chdir(dirname(__FILE__)) or die("failed to chdir to script location: $!\n");
my $dirname = 'native';
opendir(DIRH, $dirname) or die("Failed to opendir '$dirname': $!\n");

while (readdir(DIRH)) {
    next if not /\-lx\.h\Z/;
    my $module = $_;
    my $header = "$dirname/$module";
    open(IN, '<', $header) or die("Failed to open '$header' for reading: $!\n");
    $module =~ s/\-lx\.h\Z//;
    $module =~ tr/a-z/A-Z/;

    while (<IN>) {
        chomp;
        if (/\A\s*LX_NATIVE_EXPORT\d*\(.*?, (\d+)\)/) {
            $implemented{"$module\@$1"} = 1;
        }
    }

    close(IN);
}

while (<STDIN>) {
    chomp;
    if (/\A.*? \((.*?\@\d+)\)/) {
        #my $isdef = defined $implemented{$1} ? "yes" : "no"; print("is '$1' defined? $isdef\n");
        next if defined $implemented{$1};
    }
    s/\A(...)32/$1/;  # Change "Dos32" or whatnot to just "Dos"
    print("$_\n");
}

# end of unimplemented.pl ...

