#! /usr/bin/perl -w

# emulate_a_real_collection.pl asks QBASHI to generate a corpus with
# the same percentage of singleton terms as were in the seed corpus.
# It always generates too many because the generation of the middle
# section is random and usually results in extra singletons.
#
# Rather than mathematically calculate the expected number of
# singletons generated randomly, we look at the .tfd files for
# real and emulated collections, and calculate correction factors
#
# This script should be run in the Experiments_emulation directory.
# It expects to find Real and Piecewise directories

die "Real: No such directory\n"
    unless -d "Real";

die "Piecewise: No such directory\n"
    unless -d "Piecewise";

for $realtfd (glob "Real/*.tfd") {
    next if $realtfd =~ /_bigrams|_cooccurs|_repetitions/;
    $real = `grep "x_zipf_tail_perc=" $realtfd`;
    die "grep1 error" if $?;
    $real =~ /_perc=([0-9.]+)/;
    $realtailperc = $1;

    $realtfd =~ m@Real/(.*)\.tfd@;
    $corpus = $1;
    
    $mimictfd = "Piecewise/$corpus.tfd";
    if (! -e $mimictfd) {
	#print "   $mimictfd not found\n";
	next;
    }
    $mimic = `grep "x_zipf_tail_perc=" $mimictfd`;
    die "grep1 error" if $?;
    $mimic =~ /_perc=([0-9.]+)/;
    $mimictailperc = $1;

    $adjustment_factor = $realtailperc / $mimictailperc;
    print sprintf("%-30s", $corpus), ":  $realtailperc\t$mimictailperc\t$adjustment_factor\n";
}

exit(0);
