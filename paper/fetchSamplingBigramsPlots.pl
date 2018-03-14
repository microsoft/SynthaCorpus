#! /usr/bin/perl -w

# Copies some plotfiles from a SynthaCorpus directory to an ImageFiles directory

$srcDir = "//ccpsofsrelsci/data/dahawkin/GIT/SynthaCorpus/Experiments/Sampling/SamplePlots";

$dstDir = "Imagefiles";

die "It is assumed that $0 is run in the GIT/allThingsSynthetic/journal directory
    There should be a $dstDir subdirectory.\n"
    unless -d $dstDir;


foreach $corpus (  # Outer loop over corpora
		   "TREC-AP",
		   "Indri-WT10g"
    ) {
    foreach $type (# Inner loop over types of files to copy
		   "highest_bigram_freq",
		   "significant_bigrams",
		   "bigram_alpha"
	) {

	$srcFile = "$srcDir/scaling_${corpus}_${type}.pdf";
	print "Copying $srcFile to $dstDir\n";
	$code = system("cp $srcFile $dstDir");
	die "Copy failed with code $code\n" if $code;
    }
}

exit(0);
