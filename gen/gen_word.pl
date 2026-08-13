#!/usr/bin/perl
# Build the word-character set from DerivedCoreProperties.txt and
# UnicodeData.txt.
#
#   Alphabetic  -- the derived property, which already folds in
#                  Lu/Ll/Lt/Lm/Lo, Nl and Other_Alphabetic
#   Nd          -- decimal digits
#   Mn, Mc      -- non-spacing and spacing combining marks, so that a base
#                  plus its accents is a single word
#
# Deliberately NOT included: Pc (connector punctuation, where '_' lives).
# A caller applying an intraword-underscore rule is deciding ABOUT the
# underscore and needs it to not already count as part of the word.  The
# ten Pc code points are exposed separately by utf_is_word_connector().
#
# Output is UnicodeData.txt-shaped so gen/classify can read it, one code
# point per line, sorted.

use strict;
use warnings;

my %word;

# Alphabetic, from the derived property file.
open my $fh, '<', 'DerivedCoreProperties.txt'
    or die "Cannot open DerivedCoreProperties.txt: $!";
while (<$fh>) {
    s/#.*//;
    next unless /\S/;
    my @f = map { s/^\s+|\s+$//gr } split /;/;
    next unless defined $f[1] && $f[1] eq 'Alphabetic';

    if ($f[0] =~ /^([0-9A-F]+)\.\.([0-9A-F]+)$/i) {
        $word{$_} = 1 for (hex($1) .. hex($2));
    } elsif ($f[0] =~ /^([0-9A-F]+)$/i) {
        $word{hex($1)} = 1;
    }
}
close $fh;
my $nAlpha = scalar keys %word;

# Nd, Mn, Mc from the general category.  UnicodeData.txt marks a range with
# a "<..., First>" / "<..., Last>" pair rather than listing every code point.
open my $fh2, '<', 'UnicodeData.txt' or die "Cannot open UnicodeData.txt: $!";
my $pending_first;
while (<$fh2>) {
    chomp;
    my @f = split /;/, $_, -1;
    next unless @f > 2;
    my ($cp, $name, $cat) = (hex($f[0]), $f[1], $f[2]);

    my $wanted = ($cat eq 'Nd' || $cat eq 'Mn' || $cat eq 'Mc');

    if ($name =~ /, First>$/) {
        $pending_first = $wanted ? $cp : undef;
        next;
    }
    if ($name =~ /, Last>$/) {
        if (defined $pending_first) {
            $word{$_} = 1 for ($pending_first .. $cp);
        }
        $pending_first = undef;
        next;
    }
    $word{$cp} = 1 if $wanted;
}
close $fh2;

my @sorted = sort { $a <=> $b } keys %word;
printf "%04X;WORD;Lo;0;L;;;;;N;;;;;\n", $_ for @sorted;

printf STDERR "%d word characters (%d Alphabetic, +%d from Nd/Mn/Mc).\n",
    scalar @sorted, $nAlpha, scalar(@sorted) - $nAlpha;
