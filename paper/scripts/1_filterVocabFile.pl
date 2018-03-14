#!/usr/bin/perl
use warnings;

# Read the vocab.tsv file for a specified QBASHER collection in the usual 
# place on Redsar, Heavymetal, or Dave's laptop and filter out lines whose
# first column contains anything other than an ASCII lower case letter.
# Write the result into a vocab.tsv in a sub-directory of ../data with the 
# same name as the collection.  (After creating the sub-directory if it 
# didn't already exist.)


die "Usage: $0 <collection_name>\n"
    unless $#ARGV == 0;

die "Must run this script in the syntheticTermGeneration/scripts directory\n"
    unless -d "../../syntheticTermGeneration" && -d "../data";

$collection = $ARGV[0];

# The path of the parent directory of the collection varies depending
# upon the machine.   Try all of them.

$common = "RelevanceSciences/Qbasher/QBASHER/indexes/";

@roots = (
    "/cygdrive/d/dahawkin/TFS_mlp/OSA",
    "/cygdrive/S/dahawkin/mlp/OSA",
    "/cygdrive/C/Users/dahawkin/BIGIR",
    );

foreach $root (@roots) {
    $v = "$root/$common/$collection/vocab.tsv";
    print "  ... trying $v ...\n";
    if (-r $v) {
	$vocabFile = $v;
	print "\nFound $vocabFile\n";
	last;
    }
}

die "Couldn't find vocab.tsv in any of the expected places\n"
    unless defined($vocabFile);

if (! -d "../data/$collection") {
    die "Can't make directory '../data/$collection'\n"
	unless mkdir "../data/$collection";
    print "\nMade directory '../data/$collection'\n";
}

my $filteredFile = "../data/$collection/vocab.tsv";

my $line = "";
die "Can't open $vocabFile\n"
    unless open IN, "$vocabFile";
die "Can't open $filteredFile\n"
    unless open OUT, "> $filteredFile";

my $filtered = 0;
my $total = 0;
while (defined($line = <IN>)) {
    if ($line =~ m/^[a-z]*\t.*$/) {
        print OUT "$line";
        $filtered++;
    }
    $total++;
}
close IN;
close OUT;
printf "\nFinished filtering. %d lines of %d retained after filtering for [a-z]\n", $filtered, $total;
