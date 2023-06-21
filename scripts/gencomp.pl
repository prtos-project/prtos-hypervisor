#!/usr/bin/perl

$USE="gencmp.pl <file>\n";

if ($#ARGV!=0) {
    die $USE;
}

my $file=$ARGV[0];

my $compilation=0;

if (open FILE, "<", $file) {
    while(my $line = <FILE>) {    
        if ($line =~ s/^#define BUILD_TIME ([0-9]+)\n/$1/) {
            $compilation=$line;
            $compilation++;
        }
    }    
    close FILE;
}

open FILE, ">", $file;
print FILE "/* Automatically generated: don't edit */\n";
print FILE "#ifndef __COMP_H_\n";
print FILE "#define __COMP_H_\n";
print FILE "#define BUILD_TIME ".$compilation."\n";
printf(FILE "#define BUILD_IDR 0x%x\n", rand((2**32)-1));
print FILE "#endif\n";
close FILE;


