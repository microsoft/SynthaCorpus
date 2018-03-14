#! /usr/bin/perl -w

use File::Copy;

#foreach $coll ("AcademicID", "classificationPaper", "clueWeb12BodiesLarge",
#	       "Indri-WT10g", "Top100M", "TREC-AP", "Wikipedia") {
#    $src = "../../Qbasher/QBASHER/indexes/$coll/QBASH.forward";
#    $dest = "../Experiments/$coll.TSV";
#    warn "Copy failed for $coll\n"
#	unless copy($src, $dest);
#}

emu("classificationPaper Piecewise markov-5e dlhisto ind");
emu("classificationPaper Piecewise markov-5e dlhisto ngrams3");
emu("Indri-WT10g Piecewise markov-5e dlhisto ind");
emu("Indri-WT10g Piecewise markov-5e dlhisto ngrams3");
emu("TREC-AP Piecewise markov-5e dlhisto ind");
emu("TREC-AP Piecewise markov-5e dlhisto ngrams2");
emu("TREC-AP Piecewise markov-5e dlhisto ngrams3");
emu("TREC-AP Piecewise markov-5e dlhisto ngrams5");
emu("Wikipedia Piecewise markov-5e dlhisto ind");
emu("Wikipedia Piecewise markov-5e dlhisto ngrams3");
emu("Top100M Piecewise markov-5e dlhisto ind");
emu("Top100M Piecewise markov-5e dlhisto ngrams3");
emu("AcademicID Piecewise markov-5e dlhisto ind");
emu("AcademicID Piecewise markov-5e dlhisto ngrams2");
emu("AcademicID Piecewise markov-5e dlhisto ngrams3");
emu("AcademicID Piecewise markov-5e dlhisto ngrams5");


exit(0);

# --------------------------------------------------------------------

sub emu  {
    my $args = shift;
    my $cmd = "./emulateARealCorpus.pl $args";
    my $code = system($cmd);
    die "Command $cmd failed with code $code\n"
	if $code;

}
