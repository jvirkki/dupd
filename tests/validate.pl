#!/usr/bin/perl

#
#  Copyright 2012-2014 Jyri J. Virkki <jyri@virkki.com>
#
#  This file is part of dupd.
#
#  dupd is free software: you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  dupd is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with dupd.  If not, see <http://www.gnu.org/licenses/>.
#

#
# This script will run cmp on each file pair reported as duplicates by
# 'dupd report'. Useful to validate dupd during testing.
#

$code = 0;

open(FOUT, "dupd report |");
while (<FOUT>)
{
    chomp;
    if (/used by duplicates/) {
        $first = <FOUT>;
        chomp $first;
        $first =~ s/^\s+|\s+$//g;

        $len = 0;
        do {
            $duplicate = <FOUT>;
            chomp $duplicate;
            $duplicate =~ s/^\s+|\s+$//g;
            $len = length($duplicate);
            if ($len > 1) {
                $cmd = "cmp \"$first\" \"$duplicate\"";
                $rv = system($cmd);
                if ($rv != 0) {
                    print "ERROR: $first $duplicate\n";
                    $code = 1;
                }
            }
        } while ($len > 1);
    }
}

exit($code);
