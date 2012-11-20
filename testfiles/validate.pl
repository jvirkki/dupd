#!/usr/bin/perl

#
# This script will run cmp on each file pair reported as duplicates by
# 'dupd report'. Useful to validate dupd during testing.
#

$code = 0;

open(FOUT, "./dupd report |");
while (<FOUT>)
{
    chomp;
    if (/used by duplicates/) {
        $first = <FOUT>;
        chomp $first;

        $len = 0;
        do {
            $duplicate = <FOUT>;
            chomp $duplicate;
            $len = length($duplicate);
            if ($len > 1) {
                $rv = system("cmp $first $duplicate");
                if ($rv != 0) {
                    print "ERROR: $first $duplicate\n";
                    $code = 1;
                }
            }
        } while ($len > 1);
    }
}

exit($code);
