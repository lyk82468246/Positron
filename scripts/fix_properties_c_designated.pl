#!/usr/bin/perl
#
# scripts/fix_properties_c_designated.pl
#
# VS2008 (C89) cannot parse the C99 array-designated initializer in libcss
# src/parse/properties/properties.c:
#
#     static const ... prop_unit_mask[CSS_N_PROPERTIES] = {
#         [CSS_PROP_AZIMUTH] = UNIT_MASK_AZIMUTH,
#         ...
#     };
#
# The table is (almost) in enum order but a couple of entries
# (FILL_OPACITY / STROKE_OPACITY) are written out of order, so we cannot just
# strip the "[...] =" prefixes. Instead we rebuild the array body as a *plain
# positional* initializer in enum-index order, taking each property's value
# from the original designated entry (0 for any property not listed, matching
# C99 zero-init of unlisted indices).
#
# Idempotent-ish: run once against the committed (designated) form. The
# original is preserved in git (commit 9d55f7a).

use strict;
use warnings;

my $H = "netsurf-all-3.11/libcss/include/libcss/properties.h";
my $C = "netsurf-all-3.11/libcss/src/parse/properties/properties.c";

# 1) enum css_properties_e order: names with "= 0xNNN", in file (index) order
open(my $hf, '<', $H) or die "open $H: $!";
my @order;
while (<$hf>) {
	push @order, $1 if /^\s*(CSS_PROP_[A-Z0-9_]+)\s*=\s*0x[0-9a-fA-F]+/;
}
close($hf);
die "no enum entries found in $H\n" unless @order;

# 2) read properties.c; capture prop->value and the [first,last] designated row
open(my $cf, '<', $C) or die "open $C: $!";
my @lines = <$cf>;
close($cf);

my %val;
my ($first, $last) = (-1, -1);
for my $i (0 .. $#lines) {
	if ($lines[$i] =~ /^\s*\[\s*(CSS_PROP_[A-Z0-9_]+)\s*\]\s*=\s*(.+?)\s*,?\s*$/) {
		$val{$1} = $2;
		$first = $i if $first < 0;
		$last = $i;
	}
}
die "no [CSS_PROP_*] designated rows found in $C\n" if $first < 0;

# 3) rebuild: prefix (incl. the "= {" line) + positional body in enum order + suffix
my @pre = @lines[0 .. $first - 1];
my @suf = @lines[$last + 1 .. $#lines];
my @body = map {
	my $v = exists $val{$_} ? $val{$_} : "0";
	"\t$v, /* $_ */\n";
} @order;

open(my $of, '>', $C) or die "write $C: $!";
print $of @pre, @body, @suf;
close($of);

my $listed = scalar keys %val;
printf "properties.c: enum=%d entries, body=%d emitted, %d had explicit values, %d filled with 0\n",
	scalar(@order), scalar(@body), $listed, scalar(@order) - $listed;
