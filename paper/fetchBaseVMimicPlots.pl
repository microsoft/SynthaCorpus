#! /usr/bin/perl -w

# Fetch some base_v_mimic plotfiles created by paperRun.pl from a SynthaCorpus directory to an ImageFiles directory

$srcDir = "//stcmheavymetal.redmond.corp.microsoft.com/d\\\$/dahawkin/GIT/SynthaCorpus/Experiments/Emulation";

$dstRoot = "Imagefiles";

die "It is assumed that $0 is run in the GIT/allThingsSynthetic/journal directory
    There should be a $dstRoot subdirectory.\n"
    unless -d $dstRoot;

die "Can't open paperRun.pl\n"
    unless open PR, "paperRun.pl";

while (<PR>) {
    next unless /emu\(\"(.*?)\"\)/;
    # eg. classificationPaper Piecewise markov-5e dlhisto ind
    ($corpus, $emType, $termRep, $dl, $grams) = split /\s+/, $1;

    # Create the $emType sub-directory unless it exists.
    mkdir "$dstRoot/$emType"
	unless -d "$dstRoot/$emType";
    $subSub = "${termRep}_${dl}_${grams}";
    mkdir "$dstRoot/$emType/$subSub"
	unless -d "$dstRoot/$emType/$subSub";
    
    $src = "$srcDir/$emType/$subSub/${corpus}_base_v_mimic_*.pdf";
    $dstDir = "$dstRoot/$emType/$subSub";
    print "Copying $src to $dstDir\n";
    $code = system("cp $src $dstDir"); 
    die "Copy failed with code $code\n" if $code;
}

close(PR);

exit(0);
