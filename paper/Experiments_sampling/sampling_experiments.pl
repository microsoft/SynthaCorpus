#! /usr/bin/perl -w

# Given the name of a corpus, this script creates random samples of different 
# sizes and calculates corpus characteristics for each sample size:
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

# ----------------------------------------------------------------------
# This version of the script extracts a lot more data from the sample:
# mean document length and standard deviation, tail_percentage, and the
# frequency percentages of the ten head terms, plus parameters for piecewise
# linear fitting.  These are all recorded
# in the .dat and .tad files.  (This is to enable scaling-up experiments.)
#
# This version is also capable of running the temporal growth scenarios.
#
# Finally, this version avoids the cost of running a 100% sample -- it
# assumes that the full collection has already been indexed, vocab_listed,
# and bigrammed.

# Three types of data files are written:
#   .raw - the raw values for the sample
#   .dat - in which the sample values are fractions of the value for the biggest sample
#   .tad - in which the sample values are multiples of the value for the smallest sample


# Scripts are now called via $^X in case the perl we want is not in /usr/bin/

$perl = $^X;
$perl =~ s@\\@/@g;

$|++;

use Time::HiRes;
use File::Copy "cp";

$QB_subpath = "RelevanceSciences/Qbasher";
$ix_subpath = "QBASHER/indexes";
@QB_paths = (
    "C:/Users/dahawkin/BIGIR/$QB_subpath",  # Dave Laptop
    "D:/dahawkin/TFS_mlp/OSA/$QB_subpath",  # Redsar
    "S:/dahawkin/mlp/OSA/$QB_subpath",);    # HeavyMetal


die "Usage: $0 <index_name_or_directory> [-long_docs][-suppress_bigrams][-temporal_growth]

    - if -long_docs is given, QBASHI will index more than 255 words from a record.

    - if -suppress_bigrams is given, generation of bigrams will not occur.

    - if -temporal_growth is given, temporal subsets are used instead of
      samples, only one iteration is done for each subset, and different
      directories are used: TemporalSubsetPlots instead of SamplePlots 
      and TemporalSubset instead of Sample.

    For compatibility with other scripts, other options are permitted but ignored.\n"    
    unless $#ARGV >= 0;

print "\n*** $0 @ARGV\n\n";


$ix = $ARGV[0];
if (! -d ($fullidxdir = $ix)) {
    # Try prefixing paths for Dave's Laptop, Redsar and HeavyMetal
    for $p (@QB_paths) {
	$fullidxdir = "$p/$ix_subpath/$fullidxdir";
	print "Trying: $fullidxdir\n";
	if (-d $fullidxdir) {
	    $qbp = $p;
	    last;
	}
    }
    die "Couldn't locate index directory $ARGV[0] either at $ARGV[0] or in any of the known places.  [$qbp]  [$fullidxdir]\n"
	unless -d $qbp && -d $fullidxdir;
}


mkdir "Sample" unless -d "Sample";  # A directory in which to do all our work

mkdir "SamplePlots" unless -d "SamplePlots"; # A directory for plotfiles,
                                             # dat files, and PDFs

mkdir "TemporalSubset" unless -d "TemporalSubset";  

mkdir "TemporalSubsetPlots" unless -d "TemporalSubsetPlots"; 

$opts = "";
$suppress_bigrams = 0;
$temporal_growth = 0;
$tmpidxdir = "Sample";
$plotdir = "SamplePlots";
$prefix1 = "sample_";
$prefix2 = "sampling_";

for ($a = 1; $a <=$#ARGV; $a++) {
    if ($ARGV[$a] eq "-long_docs") { $opts = "-x_bigger_trigger=TRUE";}
    elsif ($ARGV[$a] eq "-suppress_bigrams") {$suppress_bigrams = 1;}
    elsif ($ARGV[$a] eq "-temporal_growth") {
	$tmpidxdir = "TemporalSubset";
	$plotdir = "TemporalSubsetPlots";
	$prefix1 = "subset_";
	$prefix2 = "growth_";
	$temporal_growth = 1;
	print "  -- Working with temporal subsets --\n";
    }
}



$idxname = $fullidxdir;
$idxname =~ s@.*/@@;  # Strip all but the last element of the path.

# The executables and scripts we need
$qbashi = check_exe("$qbp/src/qbashi/x64/Debug/QBASHI.exe");
$vocablister = check_exe("$qbp/src/vocab_lister/x64/Debug/QBASH_vocab_lister.exe");
$bigramslister = check_exe("$qbp/src/Bigrams_from_TSV/x64/Debug/Bigrams_from_TSV.exe");
$line_selector = check_exe("$qbp/src/select_random_lines_from_file/x64/Debug/select_random_lines_from_file.exe");
$lsqcmd = check_exe("../Experiments_emulation/lsqfit_general.pl");

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
# 8: indexing_rate
# 9: no_distinct_bigrams
# 10: highest_bigram_freq
# 11: mean doc length
# 12: doc length st dev
# 13: tail percentage - percentage of all term instances due to singletons
# 14-23: head term percentages
# 24-73: ten 5-element piecewise segment descriptors  (alpha, f, l, probrange, cumprob)
# 74: most_common_words
# 75: most_common_bigrams
# each of the 73 columns starting at 2 (num_docs) is a cumulative total 
# over all the samples run so far. most_common_words is a comma-separated 
# list, as is most_common_bigrams.  We replace the space in a bigram with 
# an underscore
";

$columns = 76;

# Set up the samples array 
$samples[0] = "Ignore element zero";
$i = 1;
foreach $perc (100, 50, 20, 10, 5, 2, 1) {
    $samples[$i] = "$perc";
    for ($j = 0; $j < ($columns - 3); $j++) {
	$samples[$i] .= " 0";
    }
    $samples[$i] .= " none none";
    print $samples[$i], "\n";
    $i++;
}

# Calculate how many documents there are in the complete collection
# to enable us to set the fractional sizes.

$cmd = "tail -1 $fullidxdir/index.log";
$rez = `$cmd`;
die "Can't execute $cmd.  Error $?\n" if $?; 
if ($rez =~ m@index ([0-9]+) docs\s@) {
    $totdox = $1;
    print "Total number of documents in $idxname: $totdox\n";
} else {
    die "Can't extract document count from final line of index.log\n";
}




$num_iters = $#samples;  # Cos we start with element one.
 
for ($i = 1; $i <= $num_iters; $i++) { 
    # We take an early exit from this loop if we're doing Temporal
    $iter = $num_iters - $i + 1;
    for ($j = $num_iters; $j >= $i; $j--) {
	@fields = split /\s+/, $samples[$j];
	die "Wrong number of columns in samples[$j]\n"
	    unless ($#fields == ($columns - 1));
	print "P: Iteration $iter: $fields[0]% sample.\n";

	$sample_name = "${prefix1}${idxname}_$fields[0]%";
	if ($fields[0] == 100) {
	    # 1. Don't create a sample or index it if this a 100% "sample"
	    $idxd2use = $fullidxdir;
	    $fields[2] = $totdox;
	    print "\n -- Using full index -- \n\n";
	    # Create the vocab.* files if they're not already there
	    if (! -r "${idxd2use}/vocab.segdat") {
		$cmd = "$vocablister ${idxd2use}/QBASH.vocab\n";
		$vlout = `$cmd`;
		die "$vocablister failed\n$cmd\n$vlout\n" if $?;
	    }
	} else {
	    # -----------------  1. Generate a sample or temporal subset of this size  ----------
	    $idxd2use = $tmpidxdir;	    

	    if ($temporal_growth) {
		$dox = int(($totdox * $fields[0]) / 100.0);
		$fields[2] = $dox;
		
		$cmd = "head -$dox $fullidxdir/QBASH.forward > ${idxd2use}/QBASH.forward\n";
		$headout = `$cmd`;
		die "head failed\n$cmd\n$headout\n" if $?; 


	    } else {
		$fields[2] += make_sample($fields[0] / 100);
	    }
	
	    # ----------------- 2. Index the sample ---------------------------------------------
	    $cmd = "$qbashi index_dir=${idxd2use} -sort_records_by_weight=FALSE -x_doc_length_histo=TRUE -$opts > ${idxd2use}/index.log\n";
	    $ixout = `$cmd`;
	    die "$qbashi failed\n$cmd\n$ixout\n" if $?;
	    # 3. -------------- Always create the vocab.tsv -------------------------------------
	    $cmd = "$vocablister ${idxd2use}/QBASH.vocab\n";
	    $vlout = `$cmd`;
	    die "$vocablister failed\n$cmd\n$vlout\n" if $?;
	}

	print " - $sample_name $idxd2use indexed and vocab_listed\n";

	# ------------------ 3. Get needed characteristics from the tail of the indexing log. -------------------
	$cmd = "tail -20 ${idxd2use}/index.log\n";
	$logtail = `$cmd`;

	die "tail of real index.log failed with code $?.\n" if $?;
	if ($logtail =~ /Record lengths: Mean: ([0-9.]+); St. Dev: ([0-9.]+) words.*Distinct words:\s+([0-9.]+);.*Total postings:\s+([0-9.]+);.*Longest postings list:\s+([0-9.]+).*Indexing rate:\s+([0-9.]+)/s) {
	    $fields[11] += $1;
	    $fields[12] += $2;
	    $fields[3] += $3;
	    $fields[4] += $4;
	    $total_postings = $4;
	    $fields[6] += $5;
	    $fields[8] += $6;
	} else {
	    die "Error: Unable to extract stuff from ${idxd2use}/index.log\n$logtail\n";
	}
	print " - $sample_name index.log extracted\n";

	# ------------------ 4. Get neededcharacteristics from ${idxd2use}/vocab.*. ------

	# First copy ${idxd2use}/vocab.plot to somewhere/$sample_name.plot
	cp("${idxd2use}/vocab.tfd", "$sample_name.tfd") or die "Cp tfd failed: $!\n";
	cp("${idxd2use}/vocab.plot", "$sample_name.plot") or die "Cp plot failed: $!\n";
	cp("${idxd2use}/vocab.segdat", "$sample_name.segdat") or die "Cp segdat failed: $!\n";
	
	# Next get the most frequent word from the head of vocab.tsv
	$cmd = "head -1 ${idxd2use}/vocab.tsv";
	print "$cmd\n";
	$rez = `$cmd`;
	die "Error: $cmd failed\n" if $?;

	if ($rez =~ /^(.*?)\t/) {
	    $word = $1;
	    $fields[74] = "" if $fields[74] eq "none";
	    $fields[74] .= "$word,";  # Highest frequency word
	}

	# Now get stuff out of vocab.tfd
	die "Can't open ${idxd2use}/vocab.tfd\n"
	    unless open TFD, "${idxd2use}/vocab.tfd";
	while (<TFD>) {
	    chomp;
	    if (m@(.*)=(.*)@) {
		$options{$1} = $2;
	    }
	}
	close(TFD);
	print " - $sample_name extracted stuff from vocab.tfd\n";
	
	$fields[5]  += $options{-x_zipf_tail_perc};
	$singletons = $options{-x_zipf_tail_perc} / 100.0 * $fields[3];
	$fields[13] += 100.0 * $singletons / $total_postings;

	# ++++++++++++++ head terms
	@headpercs = split /,/, $options{-x_head_term_percentages};
	for ($h = 0; $h < 10; $h++) {	    
	    $fields[14 + $h] += $headpercs[$h];
	}

	# ++++++++++++++ piecewise segments
	$options{-x_zipf_middle_pieces} =~ s/%$//;  # Strip off trailing '%' 
	@pieces = split /%/, $options{-x_zipf_middle_pieces};  # Each element is a comma separated list
	$k = 24;
	for (my $i = 0; $i < 10; $i++) {  # Each pieces element in turn.
	    foreach my $j (split /,/,$pieces[$i]) {
		#print "     adding $j to fields[$k]\n";
		$fields[$k++] += $j;
	    }
	}


	#4B. Run lsqfit command on .plot file to get alpha
	$cmd = "$lsqcmd $sample_name\n";
	$lsqout = `$cmd`;
	die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

	if ($lsqout =~ /,.*?\s([\-0-9.]+)\s*\*x\s*title "Linear fit".*?,.*?([\-0-9.]+)\s*\+\s*([\-0-9.]+)\s*\*x\s*\+\s*([\-0-9.]+)\s*\*x\*\*2\s+title "Quadratic fit"/s) {
	    $fields[7] += $1;
	    # Ignore quadratic fit
	} else {
	    die "Error: Unable to extract stuff from $lsqcmd output, which was:
------------\n$lsqout\n-------------\n\n";
	}


	# 5. Create the bigrams.tsv unless the task is likely to be
	#    too big.  Attempting to do the bigrams calculation for
	#    more than about 600 million tweets was taking days on
	#    redsar because 64 GB isn't enough for tables of two
	#    billion.  The full tweet index has 710.62M records
	#    and a vocab size of 340.75M. The bigrams table doubled 
	#    to 30 bits after 321M records -- maybe around 200M distinct
	#    words?  Vocab sizes for other collections:
	#
	#    Collection            Records       Vocab Size
	#    ---------------       -------       ----------
	#    clueWeb12Titles       728.88M       21.78M
	#    clueWeb12BodiesLarge   20.71M       24.30M
	#    AcademicID             89.53M       11.98M
	#    Top100M               100.00M        6.42M
	#    TREC-AP                 0.24M        0.29M
	#    Wikipedia              11.06M        2.32M
	#    classificationPaper    27.92M        0.91M
 
	if (!$suppress_bigrams) {
	    if (!($idxd2use eq $fullidxdir && (-r "$idxd2use/bigrams.segdat"))) {
		# Don't do it for full collection if it's already been done.
		# Always do it for samples because an existing bigrams.* set is not from this sample
		$cmd = "$bigramslister ${idxd2use}/QBASH.forward\n";
		$blout = `$cmd`;
		die "$bigramslister failed\n$cmd\n$blout\n" if $?;
		# Note: $bigramslister puts output in QBASH.bigrams in current directory
		$cmd ="/bin/mv QBASH.bigrams ${idxd2use}/";
		$mvout = `$cmd`;
		die "/bin/mv failed\n$cmd\n$mvout\n" if $?;
		$cmd = "$vocablister ${idxd2use}/QBASH.bigrams\n";
		$vlout = `$cmd`;
		die "$vocablister failed\n$cmd\n$vlout\n" if $?;
	    }

	    #5A. Get stuff from ${idxd2use}/bigrams.*
	    $cmd = "head -1 ${idxd2use}/bigrams.tsv\n";
	    $bigrams_head = `$cmd`;
	    die "head of ${idxd2use}/bigrams.tsv failed with code $?.\n" if $?;
	    $fields[75] = "" if $fields[75] eq "none";
	    $bigrams_head =~ /(\w+ \w+)\s+([0-9]+)/;
	    $big = $1;
	    $fields[10] += $2;
	    $big =~ s/ /_/;
	    $fields[75] .= "$big,";

	    $cmd = "grep x_synth_vocab_size ${idxd2use}/bigrams.tfd\n";
	    $bigrams_grep = `$cmd`;
	    die "$cmd failed with code $?.\n" if $?;
	    $bigrams_grep =~ /([0-9]+)/;
	    $fields[9] += $1;

	    print "

    BIGRAMS for $sample_name: $1

";
	} else {
	    $fields[9] = 0;
	    $fields[10] = 0;
	    $fields[75] .= "*UNKNOWN*,";
	}


        # 6. Update fields and write back into samples[$i]
	$fields[1]++;  # How many observations for this sample size

	# 7. Write the incremented values.
	$samples[$j] = "@fields";
    }
    # 8. Calculate and write averages.
    show_data();
    last if $temporal_growth;  # No need for averaging in this case
}  # End of outer loop

print $field_explanations;

generate_plots();

exit(0);

# ---------------------------------------------------------------------------------------

sub check_exe {
    my $exe = shift;
    if ($exe =~ /\.pl$/) {
	die "$exe doesn't exist.\n"
	    unless -r $exe;
	return "$perl $exe";
    } else {
	die "$exe doesn't exist or isn't executable.\n"
	    unless -x $exe;
	return $exe;
    }
}

sub make_sample {
    my $prob_of_selection = shift;
    my $in = "$fullidxdir/QBASH.forward";
    my $out = "${tmpidxdir}/QBASH.forward";

    # new version uses C exe for speed on HeavyMetal
    my $cmd = "$line_selector $in $out $prob_of_selection";
    my $rout = `$cmd`;
    die "Command $cmd failed with out:\n$rout\n"
	if $?;
    print $rout;
    die "Can't match output from $line_selector\n"
	unless $rout =~ /scanned.\s+([0-9]+) \//;
    return $1;
}

sub show_data {
    # Print all the averages in TSV format, up to this point in the processing
    print "\n";
    for (my $i = $num_iters; $i > 0; $i--) {  # Counting down.  I.e. increasing the size of the sample
	my @r = split /\s+/, $samples[$i];
	my @f = @r;  # Can't muck with the real array.
	$nobs = $f[1];
	$nobs = 1 if $nobs < 1;
	print "S:\t$f[0]\t$nobs";
	for (my $j = 2; $j < ($columns - 2); $j++) { 
	    $f[$j] = sprintf("%.5f", $f[$j]/$nobs);
	    print "\t$f[$j]";
	}
	print "\t$f[74]\t$f[75]\n";
    }
    print "\n";
}


sub generate_plots {

    # We're going to write the unnormalised data to the .raw file
    $pr = "${plotdir}/${prefix2}" . $idxname . ".raw";
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
	    $f[$j] = sprintf("%.5f", $f[$j]/$nobs);
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
	$index_rate[$k] = $f[8];
	$no_distinct_bigrams[$k] = $f[9];
	$highest_bigram_freq[$k] = $f[10];
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

	$s1_alpha[$k] = $f[24];
	$s1_f[$k] = $f[25];
	$s1_l[$k] = $f[26];
 	$s1_probrange[$k] = $f[27];
	$s1_cumprob[$k] = $f[28];

	$s2_alpha[$k] = $f[29];
	$s2_f[$k] = $f[30];
	$s2_l[$k] = $f[31];
 	$s2_probrange[$k] = $f[32];
	$s2_cumprob[$k] = $f[33];
 
	$s3_alpha[$k] = $f[34];
	$s3_f[$k] = $f[35];
	$s3_l[$k] = $f[36];
 	$s3_probrange[$k] = $f[37];
	$s3_cumprob[$k] = $f[38];
 
	$s4_alpha[$k] = $f[39];
	$s4_f[$k] = $f[40];
	$s4_l[$k] = $f[41];
 	$s4_probrange[$k] = $f[42];
	$s4_cumprob[$k] = $f[43];

	$s5_alpha[$k] = $f[44];
	$s5_f[$k] = $f[45];
	$s5_l[$k] = $f[46];
 	$s5_probrange[$k] = $f[47];
	$s5_cumprob[$k] = $f[48];
 
	$s6_alpha[$k] = $f[49];
	$s6_f[$k] = $f[50];
	$s6_l[$k] = $f[51];
 	$s6_probrange[$k] = $f[52];
	$s6_cumprob[$k] = $f[53];

	$s7_alpha[$k] = $f[54];
	$s7_f[$k] = $f[55];
	$s7_l[$k] = $f[56];
 	$s7_probrange[$k] = $f[57];
	$s7_cumprob[$k] = $f[58];

	$s8_alpha[$k] = $f[59];
	$s8_f[$k] = $f[60];
	$s8_l[$k] = $f[61];
 	$s8_probrange[$k] = $f[62];
	$s8_cumprob[$k] = $f[63];

	$s9_alpha[$k] = $f[64];
	$s9_f[$k] = $f[65];
	$s9_l[$k] = $f[66];
 	$s9_probrange[$k] = $f[67];
	$s9_cumprob[$k] = $f[68];

	$s10_alpha[$k] = $f[69];
	$s10_f[$k] = $f[70];
	$s10_l[$k] = $f[71];
 	$s10_probrange[$k] = $f[72];
	$s10_cumprob[$k] = $f[73];
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
	$index_rate[$k] = sprintf("%.4f", $index_rate[$k] / $index_rate[$f] * 100);

	# It's possible that the bigrams experiments aren't run.  
	# That can lead to a division by zero.  We need to avoid that.
	if ($suppress_bigrams || ($no_distinct_bigrams[$f] == 0) || ($highest_bigram_freq[$f] == 0)) {
	    $no_distinct_bigrams[$k] = 0;
	    $highest_bigram_freq[$k] = 0;
	} else {
	    $no_distinct_bigrams[$k] = sprintf("%.4f", $no_distinct_bigrams[$k] / $no_distinct_bigrams[$f] * 100);
	    $highest_bigram_freq[$k] = sprintf("%.4f", $highest_bigram_freq[$k] / $highest_bigram_freq[$f] * 100);
	}

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

	$s1_alpha[$k] = sprintf("%.4f",  $s1_alpha[$k] * 100.0 / $s1_alpha[$f]);
	$s1_f[$k] = sprintf("%.4f",  $s1_f[$k] * 100.0 / $s1_f[$f]);
	$s1_l[$k] = sprintf("%.4f",  $s1_l[$k] * 100.0 / $s1_l[$f]);
	$s1_probrange[$k] = sprintf("%.4f",  $s1_probrange[$k] * 100.0 / $s1_probrange[$f]);
	$s1_cumprob[$k] = sprintf("%.4f",  $s1_cumprob[$k] * 100.0 / $s1_cumprob[$f]);
	$s2_alpha[$k] = sprintf("%.4f",  $s2_alpha[$k] * 100.0 / $s2_alpha[$f]);
	$s2_f[$k] = sprintf("%.4f",  $s2_f[$k] * 100.0 / $s2_f[$f]);
	$s2_l[$k] = sprintf("%.4f",  $s2_l[$k] * 100.0 / $s2_l[$f]);
	$s2_probrange[$k] = sprintf("%.4f",  $s2_probrange[$k] * 100.0 / $s2_probrange[$f]);
	$s2_cumprob[$k] = sprintf("%.4f",  $s2_cumprob[$k] * 100.0 / $s2_cumprob[$f]);
	$s3_alpha[$k] = sprintf("%.4f",  $s3_alpha[$k] * 100.0 / $s3_alpha[$f]);
	$s3_f[$k] = sprintf("%.4f",  $s3_f[$k] * 100.0 / $s3_f[$f]);
	$s3_l[$k] = sprintf("%.4f",  $s3_l[$k] * 100.0 / $s3_l[$f]);
	$s3_probrange[$k] = sprintf("%.4f",  $s3_probrange[$k] * 100.0 / $s3_probrange[$f]);
	$s3_cumprob[$k] = sprintf("%.4f",  $s3_cumprob[$k] * 100.0 / $s3_cumprob[$f]);
	$s4_alpha[$k] = sprintf("%.4f",  $s4_alpha[$k] * 100.0 / $s4_alpha[$f]);
	$s4_f[$k] = sprintf("%.4f",  $s4_f[$k] * 100.0 / $s4_f[$f]);
	$s4_l[$k] = sprintf("%.4f",  $s4_l[$k] * 100.0 / $s4_l[$f]);
	$s4_probrange[$k] = sprintf("%.4f",  $s4_probrange[$k] * 100.0 / $s4_probrange[$f]);
	$s4_cumprob[$k] = sprintf("%.4f",  $s4_cumprob[$k] * 100.0 / $s4_cumprob[$f]);
	$s5_alpha[$k] = sprintf("%.4f",  $s5_alpha[$k] * 100.0 / $s5_alpha[$f]);
	$s5_f[$k] = sprintf("%.4f",  $s5_f[$k] * 100.0 / $s5_f[$f]);
	$s5_l[$k] = sprintf("%.4f",  $s5_l[$k] * 100.0 / $s5_l[$f]);
	$s5_probrange[$k] = sprintf("%.4f",  $s5_probrange[$k] * 100.0 / $s5_probrange[$f]);
	$s5_cumprob[$k] = sprintf("%.4f",  $s5_cumprob[$k] * 100.0 / $s5_cumprob[$f]);
	$s6_alpha[$k] = sprintf("%.4f",  $s6_alpha[$k] * 100.0 / $s6_alpha[$f]);
	$s6_f[$k] = sprintf("%.4f",  $s6_f[$k] * 100.0 / $s6_f[$f]);
	$s6_l[$k] = sprintf("%.4f",  $s6_l[$k] * 100.0 / $s6_l[$f]);
	$s6_probrange[$k] = sprintf("%.4f",  $s6_probrange[$k] * 100.0 / $s6_probrange[$f]);
	$s6_cumprob[$k] = sprintf("%.4f",  $s6_cumprob[$k] * 100.0 / $s6_cumprob[$f]);
	$s7_alpha[$k] = sprintf("%.4f",  $s7_alpha[$k] * 100.0 / $s7_alpha[$f]);
	$s7_f[$k] = sprintf("%.4f",  $s7_f[$k] * 100.0 / $s7_f[$f]);
	$s7_l[$k] = sprintf("%.4f",  $s7_l[$k] * 100.0 / $s7_l[$f]);
	$s7_probrange[$k] = sprintf("%.4f",  $s7_probrange[$k] * 100.0 / $s7_probrange[$f]);
	$s7_cumprob[$k] = sprintf("%.4f",  $s7_cumprob[$k] * 100.0 / $s7_cumprob[$f]);
	$s8_alpha[$k] = sprintf("%.4f",  $s8_alpha[$k] * 100.0 / $s8_alpha[$f]);
	$s8_f[$k] = sprintf("%.4f",  $s8_f[$k] * 100.0 / $s8_f[$f]);
	$s8_l[$k] = sprintf("%.4f",  $s8_l[$k] * 100.0 / $s8_l[$f]);
	$s8_probrange[$k] = sprintf("%.4f",  $s8_probrange[$k] * 100.0 / $s8_probrange[$f]);
	$s8_cumprob[$k] = sprintf("%.4f",  $s8_cumprob[$k] * 100.0 / $s8_cumprob[$f]);
	$s9_alpha[$k] = sprintf("%.4f",  $s9_alpha[$k] * 100.0 / $s9_alpha[$f]);
	$s9_f[$k] = sprintf("%.4f",  $s9_f[$k] * 100.0 / $s9_f[$f]);
	$s9_l[$k] = sprintf("%.4f",  $s9_l[$k] * 100.0 / $s9_l[$f]);
	$s9_probrange[$k] = sprintf("%.4f",  $s9_probrange[$k] * 100.0 / $s9_probrange[$f]);
	$s9_cumprob[$k] = sprintf("%.4f",  $s9_cumprob[$k] * 100.0 / $s9_cumprob[$f]);
	$s10_alpha[$k] = sprintf("%.4f",  $s10_alpha[$k] * 100.0 / $s10_alpha[$f]);
	$s10_f[$k] = sprintf("%.4f",  $s10_f[$k] * 100.0 / $s10_f[$f]);
	$s10_l[$k] = sprintf("%.4f",  $s10_l[$k] * 100.0 / $s10_l[$f]);
	$s10_probrange[$k] = sprintf("%.4f",  $s10_probrange[$k] * 100.0 / $s10_probrange[$f]);
	$s10_cumprob[$k] = sprintf("%.4f",  $s10_cumprob[$k] * 100.0 / $s10_cumprob[$f]);


	# -------------  .tad ----------------------------
	$g_num_docs[$k] = sprintf("%.4f", $num_docs[$k] / $num_docs[0]);
	$g_vocab_size[$k] = sprintf("%.4f", $vocab_size[$k] / $vocab_size[0]);
	$g_total_postings[$k] = sprintf("%.4f", $total_postings[$k] / $total_postings[0]);
	$g_perc_singletons[$k] = sprintf("%.4f", $perc_singletons[$k] / $perc_singletons[0]);
	$g_highest_word_freq[$k] =  sprintf("%.4f", $highest_word_freq[$k] / $highest_word_freq[0]);
	$g_alpha[$k] = sprintf("%.4f", $alpha[$k] / $alpha[0]);
	$g_index_rate[$k] = sprintf("%.4f", $index_rate[$k] / $index_rate[0]);

	# It's possible that the bigrams experiments aren't run.  
	# That can lead to a division by zero.  We need to avoid that.
	if ($suppress_bigrams || ($no_distinct_bigrams[0] == 0) || ($highest_bigram_freq[0] == 0)) {
	    $g_no_distinct_bigrams[$k] = 0;
	    $g_highest_bigram_freq[$k] = 0;
	} else {
	    $g_no_distinct_bigrams[$k] = sprintf("%.4f", $no_distinct_bigrams[$k] / $no_distinct_bigrams[0]);
	    $g_highest_bigram_freq[$k] = sprintf("%.4f", $highest_bigram_freq[$k] / $highest_bigram_freq[0]);
	}

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



	$g_s1_alpha[$k] = sprintf("%.4f",  $s1_alpha[$k] / $s1_alpha[0]);
	$g_s1_f[$k] = sprintf("%.4f",  $s1_f[$k] / $s1_f[0]);
	$g_s1_l[$k] = sprintf("%.4f",  $s1_l[$k] / $s1_l[0]);
	$g_s1_probrange[$k] = sprintf("%.4f",  $s1_probrange[$k] / $s1_probrange[0]);
	$g_s1_cumprob[$k] = sprintf("%.4f",  $s1_cumprob[$k] / $s1_cumprob[0]);
	$g_s2_alpha[$k] = sprintf("%.4f",  $s2_alpha[$k] / $s2_alpha[0]);
	$g_s2_f[$k] = sprintf("%.4f",  $s2_f[$k] / $s2_f[0]);
	$g_s2_l[$k] = sprintf("%.4f",  $s2_l[$k] / $s2_l[0]);
	$g_s2_probrange[$k] = sprintf("%.4f",  $s2_probrange[$k] / $s2_probrange[0]);
	$g_s2_cumprob[$k] = sprintf("%.4f",  $s2_cumprob[$k] / $s2_cumprob[0]);
	$g_s3_alpha[$k] = sprintf("%.4f",  $s3_alpha[$k] / $s3_alpha[0]);
	$g_s3_f[$k] = sprintf("%.4f",  $s3_f[$k] / $s3_f[0]);
	$g_s3_l[$k] = sprintf("%.4f",  $s3_l[$k] / $s3_l[0]);
	$g_s3_probrange[$k] = sprintf("%.4f",  $s3_probrange[$k] / $s3_probrange[0]);
	$g_s3_cumprob[$k] = sprintf("%.4f",  $s3_cumprob[$k] / $s3_cumprob[0]);
	$g_s4_alpha[$k] = sprintf("%.4f",  $s4_alpha[$k] / $s4_alpha[0]);
	$g_s4_f[$k] = sprintf("%.4f",  $s4_f[$k] / $s4_f[0]);
	$g_s4_l[$k] = sprintf("%.4f",  $s4_l[$k] / $s4_l[0]);
	$g_s4_probrange[$k] = sprintf("%.4f",  $s4_probrange[$k] / $s4_probrange[0]);
	$g_s4_cumprob[$k] = sprintf("%.4f",  $s4_cumprob[$k] / $s4_cumprob[0]);
	$g_s5_alpha[$k] = sprintf("%.4f",  $s5_alpha[$k] / $s5_alpha[0]);
	$g_s5_f[$k] = sprintf("%.4f",  $s5_f[$k] / $s5_f[0]);
	$g_s5_l[$k] = sprintf("%.4f",  $s5_l[$k] / $s5_l[0]);
	$g_s5_probrange[$k] = sprintf("%.4f",  $s5_probrange[$k] / $s5_probrange[0]);
	$g_s5_cumprob[$k] = sprintf("%.4f",  $s5_cumprob[$k] / $s5_cumprob[0]);
	$g_s6_alpha[$k] = sprintf("%.4f",  $s6_alpha[$k] / $s6_alpha[0]);
	$g_s6_f[$k] = sprintf("%.4f",  $s6_f[$k] / $s6_f[0]);
	$g_s6_l[$k] = sprintf("%.4f",  $s6_l[$k] / $s6_l[0]);
	$g_s6_probrange[$k] = sprintf("%.4f",  $s6_probrange[$k] / $s6_probrange[0]);
	$g_s6_cumprob[$k] = sprintf("%.4f",  $s6_cumprob[$k] / $s6_cumprob[0]);
	$g_s7_alpha[$k] = sprintf("%.4f",  $s7_alpha[$k] / $s7_alpha[0]);
	$g_s7_f[$k] = sprintf("%.4f",  $s7_f[$k] / $s7_f[0]);
	$g_s7_l[$k] = sprintf("%.4f",  $s7_l[$k] / $s7_l[0]);
	$g_s7_probrange[$k] = sprintf("%.4f",  $s7_probrange[$k] / $s7_probrange[0]);
	$g_s7_cumprob[$k] = sprintf("%.4f",  $s7_cumprob[$k] / $s7_cumprob[0]);
	$g_s8_alpha[$k] = sprintf("%.4f",  $s8_alpha[$k] / $s8_alpha[0]);
	$g_s8_f[$k] = sprintf("%.4f",  $s8_f[$k] / $s8_f[0]);
	$g_s8_l[$k] = sprintf("%.4f",  $s8_l[$k] / $s8_l[0]);
	$g_s8_probrange[$k] = sprintf("%.4f",  $s8_probrange[$k] / $s8_probrange[0]);
	$g_s8_cumprob[$k] = sprintf("%.4f",  $s8_cumprob[$k] / $s8_cumprob[0]);
	$g_s9_alpha[$k] = sprintf("%.4f",  $s9_alpha[$k] / $s9_alpha[0]);
	$g_s9_f[$k] = sprintf("%.4f",  $s9_f[$k] / $s9_f[0]);
	$g_s9_l[$k] = sprintf("%.4f",  $s9_l[$k] / $s9_l[0]);
	$g_s9_probrange[$k] = sprintf("%.4f",  $s9_probrange[$k] / $s9_probrange[0]);
	$g_s9_cumprob[$k] = sprintf("%.4f",  $s9_cumprob[$k] / $s9_cumprob[0]);
	$g_s10_alpha[$k] = sprintf("%.4f",  $s10_alpha[$k] / $s10_alpha[0]);
	$g_s10_f[$k] = sprintf("%.4f",  $s10_f[$k] / $s10_f[0]);
	$g_s10_l[$k] = sprintf("%.4f",  $s10_l[$k] / $s10_l[0]);
	$g_s10_probrange[$k] = sprintf("%.4f",  $s10_probrange[$k] / $s10_probrange[0]);
	$g_s10_cumprob[$k] = sprintf("%.4f",  $s10_cumprob[$k] / $s10_cumprob[0]);
   }

    $pc = "${plotdir}/${prefix2}" . $idxname . "_plot.cmds";
    $pd = "${plotdir}/${prefix2}" . $idxname . ".dat";
    $pt = "${plotdir}/${prefix2}" . $idxname . ".tad";

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
	# field explanations.
	print PD "$sample_size[$k] $num_docs[$k] $vocab_size[$k] $total_postings[$k] $perc_singletons[$k] $highest_word_freq[$k] $alpha[$k] $index_rate[$k] $no_distinct_bigrams[$k] $highest_bigram_freq[$k] $doclen_mean[$k] $doclen_stdev[$k] $tail_perc[$k] $h1_perc[$k] $h2_perc[$k] $h3_perc[$k] $h4_perc[$k] $h5_perc[$k]  $h6_perc[$k] $h7_perc[$k] $h8_perc[$k] $h9_perc[$k] $h10_perc[$k] $s1_alpha[$k] $s1_f[$k] $s1_l[$k] $s1_probrange[$k] $s1_cumprob[$k] $s2_alpha[$k] $s2_f[$k] $s2_l[$k] $s2_probrange[$k] $s2_cumprob[$k] $s3_alpha[$k] $s3_f[$k] $s3_l[$k] $s3_probrange[$k] $s3_cumprob[$k] $s4_alpha[$k] $s4_f[$k] $s4_l[$k] $s4_probrange[$k] $s4_cumprob[$k] $s5_alpha[$k] $s5_f[$k] $s5_l[$k] $s5_probrange[$k] $s5_cumprob[$k] $s6_alpha[$k] $s6_f[$k] $s6_l[$k] $s6_probrange[$k] $s6_cumprob[$k] $s7_alpha[$k] $s7_f[$k] $s7_l[$k] $s7_probrange[$k] $s7_cumprob[$k] $s8_alpha[$k] $s8_f[$k] $s8_l[$k] $s8_probrange[$k] $s8_cumprob[$k] $s9_alpha[$k] $s9_f[$k] $s9_l[$k] $s9_probrange[$k] $s9_cumprob[$k] $s10_alpha[$k] $s10_f[$k] $s10_l[$k] $s10_probrange[$k] $s10_cumprob[$k]\n";

 	print PT "$sample_size[$k] $g_num_docs[$k] $g_vocab_size[$k] $g_total_postings[$k] $g_perc_singletons[$k] $g_highest_word_freq[$k] $g_alpha[$k] $g_index_rate[$k] $g_no_distinct_bigrams[$k] $g_highest_bigram_freq[$k] $g_doclen_mean[$k] $g_doclen_stdev[$k] $g_tail_perc[$k] $g_h1_perc[$k] $g_h2_perc[$k] $g_h3_perc[$k] $g_h4_perc[$k] $g_h5_perc[$k]  $g_h6_perc[$k] $g_h7_perc[$k] $g_h8_perc[$k] $g_h9_perc[$k] $g_h10_perc[$k] $g_s1_alpha[$k] $g_s1_f[$k] $g_s1_l[$k] $g_s1_probrange[$k] $g_s1_cumprob[$k] $g_s2_alpha[$k] $g_s2_f[$k] $g_s2_l[$k] $g_s2_probrange[$k] $g_s2_cumprob[$k] $g_s3_alpha[$k] $g_s3_f[$k] $g_s3_l[$k] $g_s3_probrange[$k] $g_s3_cumprob[$k] $g_s4_alpha[$k] $g_s4_f[$k] $g_s4_l[$k] $g_s4_probrange[$k] $g_s4_cumprob[$k] $g_s5_alpha[$k] $g_s5_f[$k] $g_s5_l[$k] $g_s5_probrange[$k] $g_s5_cumprob[$k] $g_s6_alpha[$k] $g_s6_f[$k] $g_s6_l[$k] $g_s6_probrange[$k] $g_s6_cumprob[$k] $g_s7_alpha[$k] $g_s7_f[$k] $g_s7_l[$k] $g_s7_probrange[$k] $g_s7_cumprob[$k] $g_s8_alpha[$k] $g_s8_f[$k] $g_s8_l[$k] $g_s8_probrange[$k] $g_s8_cumprob[$k] $g_s9_alpha[$k] $g_s9_f[$k] $g_s9_l[$k] $g_s9_probrange[$k] $g_s9_cumprob[$k] $g_s10_alpha[$k] $g_s10_f[$k] $g_s10_l[$k] $g_s10_probrange[$k] $g_s10_cumprob[$k]\n";
   }
    close(PD);
    close(PT);

    my $label = $idxname;

    # Now write the plot commands
    print PC "
set terminal pdf
set size ratio 0.7071
set xlabel \"Percentage Sample\"
set ylabel \"Percentage of value for full collection\"
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set style line 4 linewidth 4
set style line 5 linewidth 4
set style line 6 linewidth 4
set style line 7 linewidth 4
set style line 8 linewidth 4
set style line 9 linewidth 4
set style line 10 linewidth 4
set style line 11 linewidth 4
set pointsize 0.5

";
    print PC "
set output \"${plotdir}/${prefix1}${label}_vocab.pdf\"
plot [0:100][0:120] \"$pd\" using 2:3 title \"vocab_size\" w lp ls 1, \"$pd\" using 2:9 title \"distinct_bigrams\" w lp ls 2, \"$pd\" using 2:4 title \"total_postings\" w lp ls 3

";

    print PC "
set output \"${plotdir}/${prefix1}${label}_hifreq.pdf\"
plot [0:100][0:120] \"$pd\" using 2:6 title \"highest_word_freq\" w lp ls 1, \"$pd\" using 2:10 title \"highest_bigram_freq\" w lp ls 2

";



    print PC "
set output \"${plotdir}/${prefix1}${label}_zipf.pdf\"
plot [0:100][0:120] \"$pd\" using 2:7 title \"Zipf alpha\" w lp ls 1

";

    print PC "
set output \"${plotdir}/${prefix1}${label}_singletons.pdf\"
plot [0:100][0:120] \"$pd\" using 2:5 title \"Singletons\" w lp ls 1

";

print PC "
set output \"${plotdir}/${prefix1}${label}_indexing_rate.pdf\"
plot [0:100][0:120] \"$pd\" using 2:8 title \"Indexing_rate\" w lp ls 1

";

    generate_tad_plots();  ### ------------------------------------>

    close PC;

    $cmd = "gnuplot $pc\n";
    $plout = `$cmd`;
die "Gnuplot failed for $pc with code $?!\nOutput:\n$plout" if $?;

    print "
Dat plots in the following files:
  ${plotdir}/${prefix1}${label}_vocab.pdf
  ${plotdir}/${prefix1}${label}_hifreq.pdf
  ${plotdir}/${prefix1}${label}_zipf.pdf
  ${plotdir}/${prefix1}${label}_singletons.pdf
  ${plotdir}/${prefix1}${label}_indexing_rate.pdf

";
}


sub generate_tad_plots {
    # Output plot commands to show how key property values change
    # as sample size increases from 1% to 100%
    my $label = $idxname;

print PC "set xlabel \"Multiple of smallest sample\"
set ylabel \"Multiple of value for smallest sample\"
";


# no. postings
    print PC "
set output \"${plotdir}/scaling_${label}_total_postings.pdf\"
plot \"$pt\" using 2:4 title \"\" w lp ls 1, x title \"y=x\" ls 2";


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
plot \"$pt\" using 4:3 title \"\" w lp ls 1, x**$slope_vs ls 2
unset logscale xy
";


# distinct bigrams - model as y = x^a, plot in log log space.
    die "Can't write to tad.tmp1B.plot\n"
	unless open TT, ">tad.tmp1B.plot";

    for (my $k = 0; $k <= $#g_vocab_size; $k++) {
	my $x = log($g_total_postings[$k]);
	my $y = log($g_no_distinct_bigrams[$k]);
	print TT "$x\t$y\n";
    }
    close(TT);
    my $cmd = "$lsqcmd tad.tmp1B\n";
    my $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    if ($lsqout =~ /,\s*[\-0-9.]+\s*\+\s*([\-0-9.]+)\s*\*x\s*title "Linear fit"/s) {
	$slope_db = $1;
    } else {
	die "Error: Unable to extract stuff from $lsqcmd output\n";
    }

    print PC "
set logscale xy
set output \"${plotdir}/scaling_${label}_distinct_bigrams.pdf\"
plot \"$pt\" using 4:9 title \"\" w lp ls 1, x**$slope_db ls 2
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
plot \"$pt\" using 4:7 title \"\" w lp ls 1, x**$slope_za ls 2
unset logscale xy
";

    
# doc. length
    print PC "
set output \"${plotdir}/scaling_${label}_doc_length.pdf\"
plot \"$pt\" using 4:11 title \"Mean\" w lp ls 1, \"$pt\" using 4:12 title \"St. dev\" w lp ls 2
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
plot \"$pt\" using 4:5 title \"\" w lp ls 1, x**$slope_tp ls 2
unset logscale xy
";


# head terms
    print PC "
set output \"${plotdir}/scaling_${label}_head_terms.pdf\"
plot \"$pt\" using 4:14 title \"Term 1\" w lp ls 1, \"$pt\" using 4:15 title \"Term 2\" w lp ls 2, \"$pt\" using 4:16 title \"Term 3\" w lp ls 3, \"$pt\" using 4:17 title \"Term 4\" w lp ls 4, \"$pt\" using 4:18 title \"Term 5\" w lp ls 5, \"$pt\" using 4:19 title \"Term 6\" w lp ls 6, \"$pt\" using 4:20 title \"Term 7\" w lp ls 7, \"$pt\" using 4:21 title \"Term 8\" w lp ls 8, \"$pt\" using 4:22 title \"Term 9\" w lp ls 9, \"$pt\" using 4:23 title \"Term 10\" w lp ls 10, 1 title \"y=1\" ls 11";


    # alpha values for the 10 segments
    $col = 24;
    print PC "
set output \"${plotdir}/scaling_${label}_piecewise_alpha.pdf\"
plot ";
    for ($seg = 1; $seg <= 10; $seg++) {
        print PC "\"$pt\" using 4:${col} title \"seg $seg\" w lp ls $seg,";
	$col +=5;
    }
    print "\n\n";

    # piecewise segments - Value of l
    $col = 26;
    print PC "
set output \"${plotdir}/scaling_${label}_piecewise_l.pdf\"
plot ";
    for ($seg = 1; $seg <= 10; $seg++) {
        print PC "\"$pt\" using 4:${col} title \"seg $seg\" w lp ls $seg,";
	$col +=5;
    }
    print "\n\n";

    # piecewise segments - Value of probrange
    $col = 27;
    print PC "
set output \"${plotdir}/scaling_${label}_piecewise_probrange.pdf\"
plot ";
    for ($seg = 1; $seg <= 10; $seg++) {
        print PC "\"$pt\" using 4:${col} title \"seg $seg\" w lp ls $seg,";
	$col +=5;
    }
    print "\n\n";


    # Now write the scaling model, based on the smallest sample
    $smf = "$plotdir/scaling_model_for_$label.txt";
    die "Can't write to $smf\n"
	unless open SM, ">$smf";
    print SM "#scaling model for $label collection based on a 1% sample
#  Replace SF wherever it occurs with the actual scale-up factor.


";
    print SM "-x_synth_postings=$smallest_sample[4] * SF  # linear
-x_synth_vocab_size=$smallest_sample[3]*SF**$slope_vs     # power law
-x_synth_distinct_bigrams=$smallest_sample[3]*SF**$slope_db # power law --- not a QBASHI option.
-x_synth_doc_length=$smallest_sample[11]                  # constant
-x_synth_doc_length_stdev=$smallest_sample[12]            # constant
-x_zipf_alpha=$smallest_sample[7]*SF**$slope_za           # power law
-x_zipf_tail_perc=$smallest_sample[5]*SF**$slope_tp      # power law
-x_head_term_percentages=$smallest_sample[14],$smallest_sample[15],$smallest_sample[16],$smallest_sample[17],$smallest_sample[18],$smallest_sample[19],$smallest_sample[20],$smallest_sample[21],$smallest_sample[22],$smallest_sample[23]                          # constant
";

    close(SM);

    print "Tad plots in:
    ${plotdir}/scaling_${label}_total_postings.pdf
    ${plotdir}/scaling_${label}_vocab_size.pdf
    ${plotdir}/scaling_${label}_distinct_bigrams.pdf
    ${plotdir}/scaling_${label}_alpha.pdf
    ${plotdir}/scaling_${label}_doc_length.pdf
    ${plotdir}/scaling_${label}_tail_perc.pdf  
    ${plotdir}/scaling_${label}_head_terms.pdf
    ${plotdir}/scaling_${label}_piecewise_alpha.pdf 
    ${plotdir}/scaling_${label}_piecewise_l.pdf
    ${plotdir}/scaling_${label}_piecewise_probrange.pdf

Scaling model in:
    $smf

";


}
