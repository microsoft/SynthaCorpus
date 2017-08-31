#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Given the name of a corpus, this script creates random samples of different 
# sizes and extracts properties for each sample size:
#
#   1. Vocabulary size
#   2. Tail percentage (Singletons as %age of total vocab size)
#   3. Frequency %age (of total postings) of ten most common words
#   4. Closest approximation to Zipf Alpha
#   5. Mean and St.dev of document lengths
#
#
# This data is all recorded in the .dat and .tad files.  (This is to 
# enable scaling-up experiments.)
#
# This version is capable of running the temporal growth scenarios.
#
# The experiment is controlled by the @samples[] array, whose entries must be
# stored in order of increasing sample fraction.
#
# Because observed characteristics will vary from sample to sample even 
# when the samples are the same size, and because the variability will 
# increase as the sample size gets smaller, we do repeated runs and 
# average the characteristics, but we do more runs for the smaller 
# sample sizes.   This approach should give reliable averages while
# reducing the potentially crippling time cost of doing many runs over large 
# sample sizes.


# Three types of data files are written:
#   .raw - the raw values for the sample
#   .dat - in which the sample values are fractions of the value for the biggest sample
#   .tad - in which the sample values are multiples of the value for the smallest sample


# Scripts are now called via $^X in case the perl we want is not in /usr/bin/

$perl = $^X;
$perl =~ s@\\@/@g;

use File::Copy "cp";
use Cwd;
$cwd = getcwd();
if ($cwd =~ m@/cygdrive@) {
    $cwd =~ s@/cygdrive/([a-zA-Z])/@$1:/@;
}

$|++;

use Time::HiRes;


die "Usage: $0 <corpusName> [-temporalGrowth]

    Root directory is ../Experiments/Sampling

    - if -temporal_growth is given, temporal subsets are used instead of
      samples, only one iteration is done for each subset, and different
      directories are used: TemporalSubsetPlots instead of SamplePlots 
      and TemporalSubset instead of Sample.

    Other options are permitted but ignored.\n"    
    unless $#ARGV >= 0;

print "\n*** $0 @ARGV\n\n";


$corpusName = $ARGV[0];

$plotter = `which gnuplot 2>&1`;
chomp($plotter);
$plotter .= '.exe' if ($^O =~ /cygwin/i || $^O =~ /MSWin/i);
undef $plotter if $plotter =~/^which: no/;

$experimentRoot = "../Experiments";
mkdir $experimentRoot unless -d $experimentRoot;
$experimentDir = "$experimentRoot/Sampling";
mkdir $experimentDir unless -d $experimentDir;
$baseDir = "$experimentRoot/Base";

mkdir "$experimentDir/Sample" 
    unless -d "$experimentDir/Sample";  # A directory in which to do all our work

mkdir "$experimentDir/SamplePlots" 
    unless -d "$experimentDir/SamplePlots"; # A directory for plotfiles,
                                               # dat files, and PDFs

die "$experimentRoot must exist and must contain the corpus file to be emulated\n"
    unless -d $experimentRoot;

foreach $suff (".tsv", ".starc", ".TSV", ".STARC") {
    $cf = "$experimentRoot/$corpusName$suff";
    if (-r $cf) {
	$corpusFile = $cf;
	$fileType = $suff;
	last;
    }  
}
    
die "Corpus file not at either $experimentRoot/$corpusName.tsv or $experimentRoot/$corpusName.starc\n"
	unless defined($corpusFile);


$temporalGrowth = 0;
$usualWorkingDir = "$experimentDir/Sample";  # Overridden in case of 100% sample
$plotdir = "$experimentDir/SamplePlots";
$prefix1 = "sample_";
$prefix2 = "sampling_";

for ($a = 1; $a <=$#ARGV; $a++) {
    if ($ARGV[$a] eq "-temporalGrowth") {
	$temporalGrowth = 1;
	print "  -- Working with temporal subsets --\n";
    }
}

if ($temporalGrowth) {
    mkdir "$experimentDir/TemporalSubset" 
	unless -d "$experimentDir/TemporalSubset";  

    mkdir "$experimentDir/TemporalSubsetPlots" 
	unless -d "$experimentDir/TemporalSubsetPlots";
    $workingDir = "$experimentDir/TemporalSubset";
    $plotdir = "$experimentDir/TemporalSubsetPlots";
    $prefix1 = "subset_";
    $prefix2 = "growth_";

    # In temporal subsetting we need to know the total number of documents.
    $totDox = 0;
    $docCounter = check_exe("countDocsInCorpus.exe");
    $cmd = "$docCounter $corpusFile";
    $out = `$cmd`;
    die "Command $cmd failed with code $?\n"
	if $?;
    die "Output from $cmd unparseable: $out\n"
	unless ($out =~ /Documents:\s+([0-9]+)/s);
    $totDox = $1;
}


# The executables and scripts we need, using paths relative to $experimentDir  
$propertyExtractor = check_exe("corpusPropertyExtractor.exe");
$docSelector = check_exe("selectRecordsFromFile.exe");
$lsqcmd = check_exe("lsqfit_general.pl");

# samples lines comprise white space separated items, with the following fields:

$field_explanations = 
"# 0: percentage_sample 
# 1: iterations 
# 2: num_docs 
# 3: vocab_size
# 4: total_postings
# 5: perc_singleton_words 
# 6: highest_word_freq
# 7: alpha 
# 8: bigram_alpha
# 9: no distinct significant bigrams
# 10: highest_bigram_freq  ## Can we calculate this one
# 11: mean doc length
# 12: doc length st dev
# 13: tail percentage - percentage of all term instances due to singletons
# 14-23: head term percentages
# each of the 25 columns starting at 2 (num_docs) is a cumulative total 
# over all the samples run so far. 
";

$columns = 24;

@samples = (
    "Ignore element zero",
    "100 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "50 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "20 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "10 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "5 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "2 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
    );


$num_iters = $#samples;  # Cos we ignore element zero.
 
for ($i = 1; $i <= $num_iters; $i++) { # This loop enumerates iterations, but each
                                       # iteration, one less sample takes part
    # We take an early exit from this outer loop if we're doing Temporal
    $iter = $num_iters - $i + 1;
    for ($j = $num_iters; $j >= $i; $j--) {
	@fields = split /\s+/, $samples[$j];
	die "Wrong number of columns in samples[$j]\n"
	    unless ($#fields == ($columns - 1));
	print "$iter: Iteration $iter: $fields[0]% sample.\n\n";
	

	if ($fields[0] == 100 && -r "$baseDir/${corpusName}_summary.txt") {
	    # Save a lot of time and effort if the properties of the base have
	    # already been extracted.
	    $workingDir = $baseDir;
	    print "Avoiding the need to generate a sample and extracting its properties\n";
	} else {
	    $workingDir = $usualWorkingDir;
	    # -----------------  1. Generate a temporal subset or sample of this size  ----------
	    print "    Generating the sample ...\n\n";
	    if ($temporalGrowth) {
		$dox = int(($totDox * $fields[0]) / 100.0);
		$cmd = "$docSelector $corpusFile $workingDir/$corpusName$fileType head $dox";
		$rslts = `$cmd`;
		die "Command $cmd failed with code $?\n$rslts\n"
		    if $?;
		die "Can't match doc selector output\n$rslts"
		    unless $rslts =~ m@SelectHeadRecords: ([0-9]+) lines output.@s;
		# $fields[2] = $1; # Don't do this cos it should be added and it's done later anyway
	    } else {
		$proportion = $fields[0] / 100;
		$cmd = "$docSelector $corpusFile $workingDir/$corpusName$fileType random $proportion";
		$rslts = `$cmd`;
		die "Command $cmd failed with code $?\n$rslts\n"
		    if $?;
		die "Can't match doc selector output\n$rslts"
		    unless $rslts =~ m@SelectRandomRecords: ([0-9]+) /@s;
		# $fields[2] = $1; # Don't do this cos it should be added and it's done later anyway
	    }
	    
	    # ----------------- 2. Extract the properties of the sample ------------------------------------------------
	    print "    Extracting properties from the sample ...\n\n";
	    
	    # ---------------- 2A. Get stuff from ${workingDir}/${corpusName}_summary.txt  -----------------------------
	    $cmd = "$propertyExtractor inputFileName=$workingDir/$corpusName$fileType outputStem=$workingDir/$corpusName\n";
	    $rout = `$cmd`;
	    die "Command $cmd failed\n$rout\n" if $?;
	    undef %sample;

	}
	
	print "    Extracting info from the _summary.txt file\n\n";
	# Extract some stuff from the the summary file  - Format: '<attribute>=<value>'
	$sumryfile = "$workingDir/${corpusName}_summary.txt";
	die "Can't open $sumryfile\n"
	    unless open S, $sumryfile;
	while (<S>) {
	    next if /^\s*#/;  # skip comments
	    if (/(\S+)=(\S+)/) {
		$sample{$1} = $2;
	    }
	}
	close(S);

	$fields[2] += $sample{docs};
	$fields[3] += $sample{vocab_size};
	$vocabSize = $sample{vocab_size};
	$fields[4] += $sample{total_postings};
	$totalPostings = $sample{total_postings};
	$fields[6] += $sample{longest_list};
	$fields[11] += $sample{doclen_mean};
	$fields[12] += $sample{doclen_stdev};

	# ---------------- 2B. Get stuff from ${workingDir}/${corpusName}_vocab.tfd  -----------------------------
	print "    Extracting info from the _vocab.tfd file\n\n";
	die "Can't read ${workingDir}/${corpusName}_vocab.tfd"
	    unless open V, "${workingDir}/${corpusName}_vocab.tfd";

	while (<V>) {
	    next if /^\s*#/;  # skip comments
	    if (/-zipf_tail_perc=([0-9.]+)/) {   #  percentage of distinct words
		$fields[5]  += $1;
		$fields[13] += 100.0 * (($1 / 100.0 * $vocabSize) / $totalPostings);
	    } elsif (/-head_term_percentages=([0-9.,]+)/) {
		my $list = $1;
		my @hpercs = split /,/,$list;
		for (my $r = 0; $r < 10; $r++) {
		    $fields[14 + $r] = $hpercs[$r];  # 14 - 23 are the frequencies of the top-10 words
		}
	    } elsif (/-zipf_alpha=([-0-9.]+)/) {
		$fields[7] = $1;
	    }
	}
	close(V);

	# ---------------- 2C. Get no. distinct bigrams and highest bigram freq. from ${workingDir}/${corpusName}_bigrams.tfd  ----------
	print "    Extracting info from the _bigrams.tfd file\n\n";
	die "Can't read ${workingDir}/${corpusName}_bigrams.tfd"
	    unless open B, "${workingDir}/${corpusName}_bigrams.tfd";

	while (<B>) {
	    next if /^\s*#/;  # skip comments
	    if (/-synth_postings=([0-9]+)/) {  # This line comes [must come] ahead of head_term_percentages
		$totBigramPostings = $1;   
	    } elsif (/-zipf_alpha=([\-0-9.]+)/) {
		$fields[8] += $1;
	    }  elsif (/-head_term_percentages=([0-9.]+)/) {
		$highestBigramFreq = $1 * $totBigramPostings / 100.0;
		$fields[10] += $highestBigramFreq;
	    } 	
	}
	close(B);

	# ---------------- 2D. Get the number of distinct bigrams from ${workingDir}/${corpusName}_bigrams.tsv  ------------------------
	$cmd = "wc -l ${workingDir}/${corpusName}_bigrams.tsv";
	$rout = `$cmd`;
	die "Command $cmd failed with code $?\n"
	    if $?;
	$rslts =~ m@([0-9]+)@;
	$fields[9] = $1;

        # 6. Update fields and write back into samples[$i]
	$fields[1]++;  # How many observations for this sample size

	# 7. Write the incremented values.
	$samples[$j] = "@fields";
    }
    # 8. Calculate and write averages.
    show_data();
    last if $temporalGrowth;  # No need for averaging in this case
}  # End of outer loop

print $field_explanations;


generate_plots();  # Generate plot.cmds files even if there's gno gnuplot

exit(0);

# ---------------------------------------------------------------------------------------

sub check_exe {
    # The argument is expected to be the name of either a
    # perl script or an executable.  For a perl script we check that
    # a script of that name exists in the current directory.  If it
    # does we convert its name into an absolute path and return the
    # command to run it using the perl interpreter which invoked us.
    #
    # In the case of an EXE, we look in the current directory first
    # (priority to the GCC executables).  If not there, we look in
    # the place where VS2015 puts its executables.  If we succeed
    # we return an absolute path to the EXE.
    #
    # Error exit if we don't find what we want.
    my $exe = shift;
    if ($exe =~ /\.pl$/) {
	die "$exe doesn't exist.\n"
	    unless -r $exe;
	return $exe if ($exe =~ m@/@);  # It was a path, not a name.
	return "$perl $cwd/$exe";
    } else {
	# Try the GCC built EXE first
	if (-x $exe) {
	    return $exe if ($exe =~ m@/@);  # It was a path, not a name.
	    return "$cwd/$exe";
	} else {
	    my $vsexe = "BuildAllExes/x64/Release/$exe";	    
	    die "Can't find either GCC or VS2015 versions of $exe.\n"
		unless -x $vsexe;
	    return "$cwd/$vsexe";
	}
    }
}


sub show_data {
    # Print all the averages in TSV format, up to this point in the processing
    print "\n";
    for (my $i = $num_iters; $i > 0; $i--) {  # Counting down.  I.e. increasing the size of the sample
	my @r = split /\s+/, $samples[$i];
	my @f = @r;  # Can't muck with the real array.
	$nobs = $f[1];
	$nobs = 1 if $nobs < 1;
	for (my $j = 2; $j < ($columns - 2); $j++) { $f[$j] = sprintf("%.2f", $f[$j]/$nobs);}
	print "S:\t$f[0]\t$nobs";
	for (my $k = 2; $k < $columns; $k++) {  print "\t$f[$k]"; }
	print "\n";
    }
    print "\n";
}


sub generate_plots {

    # We're going to write the unnormalised data to the .raw file
    $pr = "${plotdir}/${prefix2}" . $corpusName . ".raw";
    die "Can't write to $pr\n" unless open PR, ">$pr";
    print PR "# This file shows the raw values for each variable for each sample or subset size.
    #  Where appropriate the values are averaged across multiple samples of the same size.\n";


    for (my $i = $num_iters; $i > 0; $i--)  {# Counting down.  I.e. increasing the size of the sample
	my @r = split /\s+/, $samples[$i];
	my @f = @r;  # Just in case: Don't muck with the real array.


        # Average across the multiple observations for this sample
	$nobs = $f[1];
	$nobs = 1 if $nobs < 1;
	for (my $j = 2; $j < ($columns - 2); $j++) {  # Last two columns are text
	    print "  Col $j:  $f[$j] / $nobs\n";
	    $f[$j] = sprintf("%.4f", $f[$j]/$nobs);
	}

	@smallest_sample = @f
	    if $i == $num_iters ; # Save the averaged raw values for the scaling model.
	
	# Write a row to the .raw file in TSV format
	print PR "$f[0]";
	for (my $j = 1; $j < $columns; $j++) {
	    print PR "\t$f[$j]";
	}
	print PR "\n";

	# Make an array of the observation-averaged data for each column
	my $k = $num_iters - $i;
	$sample_size[$k] = $f[0];
	$num_docs[$k] = $f[2];
	$vocab_size[$k] = $f[3];
	$total_postings[$k] = $f[4];
	$perc_singletons[$k] = $f[5];
	$highest_word_freq[$k] = $f[6];
	$alpha[$k] = $f[7];
	$no_distinct_significant_bigrams[$k] = $f[9];
	$highest_bigram_freq[$k] = $f[10];
	$bigram_alpha[$k] = $f[8];
	$doclen_mean[$k] = $f[11];
	$doclen_stdev[$k] = $f[12];
	$tail_perc[$k] = $f[13];
	$h1_perc[$k] = $f[14];
	$h2_perc[$k] = $f[15];
	$h3_perc[$k] = $f[16];
	$h4_perc[$k] = $f[17];
	$h5_perc[$k] = $f[18];
	$h6_perc[$k] = $f[19];
	$h7_perc[$k] = $f[20];
	$h8_perc[$k] = $f[21];
	$h9_perc[$k] = $f[22];
	$h10_perc[$k] = $f[23];
    }

    close(PR);


    # Now normalise the relevant columns relative to the value for the full collection
    # for writing in the .dat file
    # That value is in $array[$num_iters - 1];
    # Also normalise relative to the smallest sample and write into .tad -- variables
    # prefixed with "g_" for growth

    my $f = $num_iters - 1;


    for ($k = 0; $k <= $num_iters - 1; $k++) {


	# ------------ .dat ----------------------------
	$num_docs[$k] = sprintf("%.4f", $num_docs[$k] / $num_docs[$f] * 100);
	$vocab_size[$k] = sprintf("%.4f", $vocab_size[$k] / $vocab_size[$f] * 100);
	$total_postings[$k] = sprintf("%.4f", $total_postings[$k] / $total_postings[$f] * 100);
	$perc_singletons[$k] = sprintf("%.4f", $perc_singletons[$k] / $perc_singletons[$f] * 100);
	$highest_word_freq[$k] =  sprintf("%.4f", $highest_word_freq[$k] / $highest_word_freq[$f] * 100);
	$alpha[$k] = sprintf("%.4f", $alpha[$k] / $alpha[$f] * 100);

	$no_distinct_significant_bigrams[$k] = sprintf("%.4f", $no_distinct_significant_bigrams[$k] 
						       / $no_distinct_significant_bigrams[$f] * 100);
	$highest_bigram_freq[$k] = sprintf("%.4f", $highest_bigram_freq[$k] / $highest_bigram_freq[$f] * 100);
	$bigram_alpha[$k] = sprintf("%.4f", $bigram_alpha[$k] / $bigram_alpha[$f] * 100);

 	$doclen_mean[$k] = sprintf("%.4f", $doclen_mean[$k] * 100.0 / $doclen_mean[$f]);
	$doclen_stdev[$k] = sprintf("%.4f", $doclen_stdev[$k] * 100.0 / $doclen_stdev[$f]);
	$tail_perc[$k] = sprintf("%.4f", $tail_perc[$k] * 100.0 / $tail_perc[$f]);
	$h1_perc[$k] = sprintf("%.4f", $h1_perc[$k] * 100.0 / $h1_perc[$f]);
	$h2_perc[$k] = sprintf("%.4f", $h2_perc[$k] * 100.0 / $h2_perc[$f]);
	$h3_perc[$k] = sprintf("%.4f", $h3_perc[$k] * 100.0 / $h3_perc[$f]);
	$h4_perc[$k] = sprintf("%.4f", $h4_perc[$k] * 100.0 / $h4_perc[$f]);
	$h5_perc[$k] = sprintf("%.4f", $h5_perc[$k] * 100.0 / $h5_perc[$f]);
	$h6_perc[$k] = sprintf("%.4f", $h6_perc[$k] * 100.0 / $h6_perc[$f]);
	$h7_perc[$k] = sprintf("%.4f", $h7_perc[$k] * 100.0 / $h7_perc[$f]);
	$h8_perc[$k] = sprintf("%.4f", $h8_perc[$k] * 100.0 / $h8_perc[$f]);
	$h9_perc[$k] = sprintf("%.4f", $h9_perc[$k] * 100.0 / $h9_perc[$f]);
	$h10_perc[$k] = sprintf("%.4f", $h10_perc[$k] * 100.0 / $h10_perc[$f]);

	# -------------  .tad ----------------------------
	$g_sample_size[$k] = sprintf("%.4f", $sample_size[$k] / $sample_size[0]);
	$g_num_docs[$k] = sprintf("%.4f", $num_docs[$k] / $num_docs[0]);
	$g_vocab_size[$k] = sprintf("%.4f", $vocab_size[$k] / $vocab_size[0]);
	$g_total_postings[$k] = sprintf("%.4f", $total_postings[$k] / $total_postings[0]);
	$g_perc_singletons[$k] = sprintf("%.4f", $perc_singletons[$k] / $perc_singletons[0]);
	$g_highest_word_freq[$k] =  sprintf("%.4f", $highest_word_freq[$k] / $highest_word_freq[0]);
	$g_alpha[$k] = sprintf("%.4f", $alpha[$k] / $alpha[0]);

	$g_no_distinct_significant_bigrams[$k] = sprintf("%.4f", $no_distinct_significant_bigrams[$k] / $no_distinct_significant_bigrams[0])
	    unless $no_distinct_significant_bigrams[0] == 0;
	$g_highest_bigram_freq[$k] = sprintf("%.4f", $highest_bigram_freq[$k] / $highest_bigram_freq[0])
	    unless $highest_bigram_freq[0] == 0;
	$g_bigram_alpha[$k] = sprintf("%.4f", $bigram_alpha[$k] / $bigram_alpha[0])
	    unless $bigram_alpha[0] == 0;

 	$g_doclen_mean[$k] = sprintf("%.4f", $doclen_mean[$k] / $doclen_mean[0]);
	$g_doclen_stdev[$k] = sprintf("%.4f", $doclen_stdev[$k] / $doclen_stdev[0]);
	$g_tail_perc[$k] = sprintf("%.4f", $tail_perc[$k] / $tail_perc[0]);
	$g_h1_perc[$k] = sprintf("%.4f", $h1_perc[$k] / $h1_perc[0]);
	$g_h2_perc[$k] = sprintf("%.4f", $h2_perc[$k] / $h2_perc[0]);
	$g_h3_perc[$k] = sprintf("%.4f", $h3_perc[$k] / $h3_perc[0]);
	$g_h4_perc[$k] = sprintf("%.4f", $h4_perc[$k] / $h4_perc[0]);
	$g_h5_perc[$k] = sprintf("%.4f", $h5_perc[$k] / $h5_perc[0]);
	$g_h6_perc[$k] = sprintf("%.4f", $h6_perc[$k] / $h6_perc[0]);
	$g_h7_perc[$k] = sprintf("%.4f", $h7_perc[$k] / $h7_perc[0]);
	$g_h8_perc[$k] = sprintf("%.4f", $h8_perc[$k] / $h8_perc[0]);
	$g_h9_perc[$k] = sprintf("%.4f", $h9_perc[$k] / $h9_perc[0]);
	$g_h10_perc[$k] = sprintf("%.4f", $h10_perc[$k] / $h10_perc[0]);
   }

    $pc = "${plotdir}/${prefix2}" . $corpusName . "_plot.cmds";
    $pd = "${plotdir}/${prefix2}" . $corpusName . ".dat";
    $pt = "${plotdir}/${prefix2}" . $corpusName . ".tad";

    die "Can't write to $pd\n" unless open PD, ">$pd";
    die "Can't write to $pt\n" unless open PT, ">$pt";
    die "Can't write to $pc\n" unless open PC, ">$pc";

    print PD "#The values in each row are expressed as percentages of those for the full collection
";
    print PT "#The values in each row are expressed as multiples of those for the smallest (1%) sample
";
    # Now write the data file for plotting
    for ($k = 0; $k <= $num_iters - 1; $k++) {
	# Include the sample_size so that columns in .tad and .dat line up better with the 
	# field explanations.  The 0.0 corresponds to an unused field
	print PD "$sample_size[$k] $num_docs[$k] $vocab_size[$k] $total_postings[$k] $perc_singletons[$k] $highest_word_freq[$k] $alpha[$k] $bigram_alpha[$k] $no_distinct_significant_bigrams[$k] $highest_bigram_freq[$k] $doclen_mean[$k] $doclen_stdev[$k] $tail_perc[$k] $h1_perc[$k] $h2_perc[$k] $h3_perc[$k] $h4_perc[$k] $h5_perc[$k]  $h6_perc[$k] $h7_perc[$k] $h8_perc[$k] $h9_perc[$k] $h10_perc[$k]\n";
 	print PT "$sample_size[$k] $g_num_docs[$k] $g_vocab_size[$k] $g_total_postings[$k] $g_perc_singletons[$k] $g_highest_word_freq[$k] $g_alpha[$k] $g_bigram_alpha[$k] $g_no_distinct_significant_bigrams[$k] $g_highest_bigram_freq[$k] $g_doclen_mean[$k] $g_doclen_stdev[$k] $g_tail_perc[$k] $g_h1_perc[$k] $g_h2_perc[$k] $g_h3_perc[$k] $g_h4_perc[$k] $g_h5_perc[$k]  $g_h6_perc[$k] $g_h7_perc[$k] $g_h8_perc[$k] $g_h9_perc[$k] $g_h10_perc[$k]\n";
   }
    close(PD);
    close(PT);

    my $label = $corpusName;

    # Now write the plot commands
    print PC "
set terminal pdf
set size ratio 0.7071
set xlabel \"Percentage Sample\"
set ylabel \"Percentage of value for full collection\"
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set pointsize 0.5

";
    print PC "
set output \"${plotdir}/${prefix1}${label}_vocab.pdf\"
plot [0:100][0:120] \"$pd\" using 2:3 title \"vocab size\" w lp ls 1, \"$pd\" using 2:9 title \"distinct significant bigrams\" w lp ls 2, \"$pd\" using 2:4 title \"total postings\" w lp ls 3

";

    print PC "
set output \"${plotdir}/${prefix1}${label}_hifreq.pdf\"
plot [0:100][0:120] \"$pd\" using 2:6 title \"highest word freq\" w lp ls 1, \"$pd\" using 2:10 title \"highest bigram freq\" w lp ls 2

";



    print PC "
set output \"${plotdir}/${prefix1}${label}_zipf.pdf\"
plot [0:100][0:120] \"$pd\" using 2:7 title \"Zipf alpha\" w lp ls 1

";

    print PC "
set output \"${plotdir}/${prefix1}${label}_singletons.pdf\"
plot [0:100][0:120] \"$pd\" using 2:5 title \"Singletons\" w lp ls 1

";

    generate_tad_plots();  ### ------------------------------------>

    close PC;

    if (defined($plotter)) {
	
	$cmd = "$plotter $pc\n";
	$plout = `$cmd`;
	die "$plotter failed for $pc with code $?!\nOutput:\n$plout" if $?;

	print "
Dat plots (generally linear scales, percentages relative to full collection):
  acroread ${plotdir}/${prefix1}${label}_vocab.pdf
  acroread ${plotdir}/${prefix1}${label}_hifreq.pdf
  acroread ${plotdir}/${prefix1}${label}_zipf.pdf
  acroread ${plotdir}/${prefix1}${label}_singletons.pdf

";

	print "Tad plots (often log log, ratios relative to smallest sample):
    acroread ${plotdir}/scaling_${label}_total_postings.pdf
    acroread ${plotdir}/scaling_${label}_vocab_size.pdf
    acroread ${plotdir}/scaling_${label}_alpha.pdf
    acroread ${plotdir}/scaling_${label}_doc_length.pdf
    acroread ${plotdir}/scaling_${label}_tail_perc.pdf  
    acroread ${plotdir}/scaling_${label}_head_terms.pdf
    acroread ${plotdir}/scaling_${label}_bigram_alpha.pdf
    acroread ${plotdir}/scaling_${label}_significant_bigrams.pdf
    acroread ${plotdir}/scaling_${label}_highest_bigram_freq.pdf

";

    }  else {
	warn "\n\n$0: Warning: gnuplot not found.  PDFs of graphs will not be generated.\n\n";
    }


    print "
Scaling model:
    cat $smf

";
}


sub generate_tad_plots {
    # TAD measurements are relative to the smallest (1%) sample
    # Output plot commands to show how key property values change
    # as scaling factor increases from 1 to 100
    my $label = $corpusName;

    $maxind = $num_iters - 1;
    
print PC "set xlabel \"Multiple of smallest sample\"
set ylabel \"Multiple of value for smallest sample\"
";


# no. postings
    print PC "
set output \"${plotdir}/scaling_${label}_total_postings.pdf\"
plot \"$pt\" using 2:4 title \"Averaged ob.s\" w lp ls 1, x title \"y=x\" ls 2";


# vocab size - model as y = x^a, plot in log log space.
    die "Can't write to tad.tmp1.plot\n"
	unless open TT, ">tad.tmp1.plot";

    for (my $k = 0; $k <= $#g_vocab_size; $k++) {
	my $x = log($g_total_postings[$k]);
	my $y = log($g_vocab_size[$k]);
	print TT "$x\t$y\n";
    }
    close(TT);
    my $cmd = "$lsqcmd tad.tmp1\n";
    my $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    if ($lsqout =~ /,\s*[\-0-9.]+\s*\+\s*([\-0-9.]+)\s*\*x\s*title "Linear fit"/s) {
	$slope_vs = $1;
    } else {
	die "Error: Unable to extract stuff from $lsqcmd output\n";
    }

    print PC "
set logscale xy
set output \"${plotdir}/scaling_${label}_vocab_size.pdf\"
plot \"$pt\" using 2:3 title \"Averaged ob.s\" w lp ls 1, x**$slope_vs ls 2
unset logscale xy
";

    
# Zipf alpha - model as y = x^a, plot in log log space.
    die "Can't write to tad.tmp2.plot\n"
	unless open TT, ">tad.tmp2.plot";

    for (my $k = 0; $k <= $#g_alpha; $k++) {
	my $x = log($g_total_postings[$k]);
	my $y = log($g_alpha[$k]);
	print TT "$x\t$y\n";
    }
    close(TT);
    $cmd = "$lsqcmd tad.tmp2\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    if ($lsqout =~ /,\s*[\-0-9.]+\s*\+\s*([\-0-9.]+)\s*\*x\s*title "Linear fit"/s) {
	$slope_za = $1;
    } else {
	die "Error: Unable to extract stuff from $lsqcmd output\n";
    }

    print PC "
set logscale xy
set output \"${plotdir}/scaling_${label}_alpha.pdf\"
plot \"$pt\" using 2:7 title \"Averaged ob.s\" w lp ls 1, x**$slope_za ls 2
unset logscale xy
";

    
# doc. length
    print PC "
set output \"${plotdir}/scaling_${label}_doc_length.pdf\"
plot \"$pt\" using 2:11 title \"Mean\" w lp ls 1, \"$pt\" using 2:12 title \"St. dev\" w lp ls 2
";

# singleton_terms  - model as y = x^a, plot in log log space.
    die "Can't write to tad.tmp3.plot\n"
	unless open TT, ">tad.tmp3.plot";

    for (my $k = 0; $k <= $#g_perc_singletons; $k++) {
	my $x = log($g_total_postings[$k]);
	my $y = log($g_perc_singletons[$k]);
	print TT "$x\t$y\n";
    }
    close(TT);
    $cmd = "$lsqcmd tad.tmp3\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    if ($lsqout =~ /,\s*[\-0-9.]+\s*\+\s*([\-0-9.]+)\s*\*x\s*title "Linear fit"/s) {
	$slope_tp = $1;
    } else {
	die "Error: Unable to extract stuff from $lsqcmd output\n";
    }

    print PC "
set logscale xy
set output \"${plotdir}/scaling_${label}_tail_perc.pdf\"
plot \"$pt\" using 2:5 title \"Averaged ob.s\" w lp ls 1, x**$slope_tp ls 2
unset logscale xy
";


    # head terms
    print PC "
set output \"${plotdir}/scaling_${label}_head_terms.pdf\"
plot \"$pt\" using 2:14 title \"Term 1\" w lp ls 1, \"$pt\" using 2:15 title \"Term 2\" w lp ls 2, \"$pt\" using 2:16 title \"Term 3\" w lp ls 3, \"$pt\" using 2:17 title \"Term 4\" w lp ls 4, \"$pt\" using 2:18 title \"Term 5\" w lp ls 5, \"$pt\" using 2:19 title \"Term 6\" w lp ls 6, \"$pt\" using 2:20 title \"Term 7\" w lp ls 7, \"$pt\" using 2:21 title \"Term 8\" w lp ls 8, \"$pt\" using 2:22 title \"Term 9\" w lp ls 9, \"$pt\" using 2:23 title \"Term 10\" w lp ls 10, 1 title \"y=1\" ls 11
";

    if ($g_bigram_alpha[$maxind] > 1.0) {
	# bigram alpha
	# Try a cheating way of estimating alpha.  Slope = log(value of biggest sample) / log(size of biggest sample)
	$maxind = $#samples - 1;
	$alf_bigal = sprintf("%.4f", log($g_bigram_alpha[$maxind]) / log($g_sample_size[$maxind]));
	
	print PC "
set logscale xy
set output \"${plotdir}/scaling_${label}_bigram_alpha.pdf\"
plot \"$pt\" using 2:8 title \"Averaged ob.s\" w lp ls 1, x**$alf_bigal title \"y=x**$alf_bigal\" ls 2
unset logscale xy
";
    } else {
	print "Disaster averted: g_bigram_alpha[$maxind] = $g_bigram_alpha[$maxind]\n";
    }
	
    if ($g_no_distinct_significant_bigrams[$maxind] > 1.0) {
	# number of significant bigrams
	# Try a cheating way of estimating alpha.  Slope = log(value of biggest sample) / log(size of biggest sample)
	$alf_sigbigs = sprintf("%.4f", log($g_no_distinct_significant_bigrams[$maxind]) / log($g_sample_size[$maxind]));

	print PC "
set logscale xy
set output \"${plotdir}/scaling_${label}_significant_bigrams.pdf\"
plot \"$pt\" using 2:9 title \"Averaged ob.s\" w lp ls 1, x**$alf_sigbigs title \"y=x**$alf_sigbigs\" ls 2
unset logscale xy
";
      } else {
	print "Disaster averted: g_no_distinct_significant_bigrams[$maxind] = $g_no_distinct_significant_bigrams[$maxind]\n";
    }
 
    # highest bigram frequency
     print PC "
set output \"${plotdir}/scaling_${label}_highest_bigram_freq.pdf\"
plot \"$pt\" using 2:10 title \"Averaged ob.s\" w lp ls 1, x title \"y=x\" ls 2
";

    # Now write the scaling model, based on the smallest sample
    $smf = "$plotdir/scaling_model_for_$label.txt";
    die "Can't write to $smf\n"
	unless open SM, ">$smf";
    print SM "#scaling model for $label collection based on a 1% sample
#  Replace SF wherever it occurs with the actual scale-up factor.
#  The argument names are those for generate_a_corpus_plus.exe

";

    
    print SM "-synth_postings=$smallest_sample[4]*SF    # linear
-synth_vocab_size=$smallest_sample[3]*SF**$slope_vs     # power law
-synth_doc_length=$smallest_sample[11]                  # constant
-synth_doc_length_stdev=$smallest_sample[12]            # constant
-zipf_alpha=$smallest_sample[7]*SF**$slope_za              # power law
-zipf_tail_perc=$smallest_sample[5]*SF**$slope_tp         # power law
-head_term_percentages=$smallest_sample[14],$smallest_sample[15],$smallest_sample[16],$smallest_sample[17],$smallest_sample[18],$smallest_sample[19],$smallest_sample[20],$smallest_sample[21],$smallest_sample[22],$smallest_sample[23]                          # constant

#bigram_alpha=$smallest_sample[8]*SF**$alf_bigal             # power law
#bigrams_highest_freq=$smallest_sample[10]*SF            # linear
#bigrams_tot_signif=$smallest_sample[9]*SF**$alf_sigbigs          # power law
";

    close(SM);


}
