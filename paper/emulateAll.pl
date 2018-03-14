#! /usr/bin/perl -w

use File::Copy;

@base = (
    "AcademicID",
    "classificationPaper",
    "clueWeb12BodiesLarge",
    "clueWeb12Titles",
    "Indri-WT10g",
    "Top100M",
    "TREC-AP",
    "Tweets",
    "Wikipedia",);


$ExptDir = "../../SynthaCorpus/Experiments";
$QBDir = "../../Qbasher/QBASHER/indexes";
$SrcDir = "../../Synthacorpus/src";

die "Couldn't find $ExptDir.  Are you running this from GIT/allThingsSynthetic/journal ?\n"
    unless -d $ExptDir;

# Now check for the existence of the TSV file for each of the base corpora.  
# If not present, attempt to copy over from QBDir.  If not possible, Die!

foreach $b1 (@base) {
    print "Checking for presence of $b1\n";
    if (! (-e "$ExptDir/$b1.tsv")) {
	print "Copying $QBDir/$b1/QBASH.forward to $ExptDir/$b1.tsv ... \n";
	copy("$QBDir/$b1/QBASH.forward", "$ExptDir/$b1.tsv")
	    or die "Copy failed\n";
    }
}

# Now sort the base corpora by increasing size so that any problems manifest themselves
# sooner rather than later.

@sbase = sort {(-s "$ExptDir/$a.tsv") <=> (-s "$ExptDir/$b.tsv")} @base;

chdir $SrcDir;

foreach $b1 (@sbase) {
    $cmd = "./emulateARealCorpus.pl $b1 @ARGV > ../Experiments/emulateAll_$b1.log\n";
    print "*** $cmd\n";
    $code = system($cmd);
    warn "Failure: Emulation of $b1 failed with code $code\n"
	if $code;
}

exit(0);
