#! /usr/bin/perl

# Try to synthesize a collection with properties as close as possible
# to a real one (Real), based on a (Seed) sample of the latter.   We assume that 
# ../Experiments_sampling/sampling_experiments.pl has been run for 
# the collection whose name C is given to this script and it has
# produced a datafile SamplePlots/scaling_model_for_C.txt, recording 
# parameter values extracted from the smallest (1%) sample (Seed) and 
# models of how those parameters grow when the collection is scaled
# up by a scaling factor SF.  


# Scripts are now called via $^X in case the perl we want is not in /usr/bin/
 
$perl = $^X;
$perl =~ s@\\@/@g;

use File::Copy "cp";

$QB_subpath = "RelevanceSciences/Qbasher";
$ix_subpath = "QBASHER/indexes";
@QB_paths = (
    "C:/Users/dahawkin/BIGIR/$QB_subpath",  # Dave Laptop
    "D:/dahawkin/TFS_mlp/OSA/$QB_subpath",  # Redsar
    "S:/dahawkin/mlp/OSA/$QB_subpath",);    # HeavyMetal
    

$|++;

die "Usage: $0 <indexname> <scale_factor> [-long_docs] [-noindexing]
  e.g. $0 TREC-AP 100 -long_docs.
       The -noindexing allows analysis of a previously built scaled-up 
       index.\n"
    unless $#ARGV >= 1 ;  # Tolerate superfluous arguments

print "\n*** $0 @ARGV\n\n";

$ixopts = "";
$indexing = 1;


for ($a = 2; $a <= $#ARGV; $a++) {
    if ($ARGV[$a] eq "-long_docs") { $ixopts .= " -x_bigger_trigger=TRUE";}
    elsif ($ARGV[$a] eq "-noindexing") { $indexing = 0;}
}


$ix = $ARGV[0];
if (! -d ($idxdir = $ix)) {
    # Try prefixing paths for Dave's Laptop, Redsar and HeavyMetal
    for $p (@QB_paths) {
	$idxdir = "$p/$ix_subpath/$idxdir";
	print "Trying: $idxdir\n";
	if (-d $idxdir) {
	    $qbp = $p;
	    last;
	}
    }
    die "Couldn't locate index directory$ARGV[0] either at $ARGV[0] or in any of the known places.  [$qbp]  [$idxdir]\n"
	unless -d $qbp && -d $idxdir;
}

$idxname = $idxdir;
$idxname =~ s@.*/@@;  # Strip all but the last element of the path.

print "QBASHER path: $qbp\nIndex_dir=$idxdir\nIndex name=$idxname\n";


$sf = $ARGV[1];

# The executables and scripts we need
$qbashi = check_exe("$qbp/src/qbashi/x64/Debug/QBASHI.exe");
$vocablister = check_exe("$qbp/src/vocab_lister/x64/Debug/QBASH_vocab_lister.exe");
$lsqcmd = check_exe("../Experiments_emulation/lsqfit_general.pl");

$qbcmd = "$qbashi index_dir=ZIPF-GENERATOR -x_doc_length_histo=TRUE $ixopts";



#------------------------------------------------------------------------------------------
#
# Getting stuff out of the Seed collection (1% sample)
#
#------------------------------------------------------------------------------------------

$scaling_model_file = "../Experiments_sampling/SamplePlots/scaling_model_for_${idxname}.txt";
die "Can't read $scaling_model_file\n" 
    unless open SMF, $scaling_model_file;

while (<SMF>) {
    next if (/^\s*#/);   # Skip comment lines
    next if (/^\s*$/);   # Skip blank lines
    next if /not a QBASHI option/;
    chomp;
    s/#.*//;  # Strip trailing comments
    if (m@(.*?=)(.*)@) {
	$attr = $1; 
	$rawval = $2;
	# There are three types of model: power law, linear and constant
	# Pattern match to distinguish them and set $val accordingly.
	if ($rawval =~ m@([\-0-9.]+)\s*\*\s*SF\s*\*\*([\-0-9.]+)@) {
	    # Power law
	    $const = $1;
	    $val = sprintf("%.4f", $1 * ($sf**$2));
	} elsif ($rawval =~ m@([\-0-9.]+)\s*\*\s*SF@) {
	    # Linear
	    $val = sprintf("%.4f", $1 * $sf);
	} elsif ($rawval =~ m@([\-0-9.,]+)@) { 
	    # Constant, possibly a comma-separated list.
	    $val = $1;
	} else {
	    print "Unrecognized value expression: $rawval\n";
	    exit(1);
	}
	$qbcmd .=" $attr$val";
    } else {
	print "Erroneous line in $scaling_model_file:

$_
";
	exit(1);
    }
    

}
close(SMF);

# At least temporarily, derive the piecewise segment definitions from the .tfd file for Seed
# Read in QBASHI options from the ScaledUp .tfd file
die "Can't open ../Experiments_sampling/sample_${idxname}_1%.tfd\n"
    unless open TFD, "../Experiments_sampling/sample_${idxname}_1%.tfd";
while (<TFD>) {
    chomp;
    if (m@(.*)=(.*)@) {
	$Seed_options{$1} = $2;
    }
}
close(TFD);

# Unfortunately, the segment definitions must change in various ways as we scale up,
# even if alpha remains constant:
#   1. The number of ranks in the middle section increases, so f and l values 
#      must be re-computed.
#   2. The probability that a term occurrence is a singleton drops as the 
#      size increases.  This means that the total term occurrence probability
#      for the middle section increases, because the head proportion seems to 
#      stay the same. We tried linearly growing the probranges (and recalculating
#      the cum probs), but the linear growth over-emphasised the tail

#print "QBCMD: $qbcmd\n";

# Get the useful stuff from $qbcmd
$qbcmd =~ m@x_synth_postings=([0-9]+).*?vocab_size=([0-9]+).*?tail_perc=([0-9.]+).*?x_head_term_percentages=(\S+)@
    or die "Can't match $qbcmd\n";
$su_np = $1;
$su_vs = $2;
$su_tp = $3;
$su_head = $4;

print "QBCMD: $qbcmd
  su_np: $su_np
  su_vs: $su_vs
  su_tp: $su_tp
  su_head: $su_head
";


$L = $su_vs - ($su_tp * $su_vs / 100.0);
$F = 11;

# Calculate probabilities for tail, head and middle sections
$su_tailprob = ($su_tp * $su_vs / 100.0) / $su_np;  

$su_headprob = 0;
while ($su_head =~ m@([0-9.]+)@g) {$su_headprob += $1;}
$su_headprob /= 100.0;

$su_midprob = 1.0 - $su_headprob - $su_tailprob;
print "\nHeadprob: $su_headprob\nMidprob: $su_midprob\nTailprob: $su_tailprob\n";

#Calculate the factors by which we'll blow up the segment probranges

$tmp = $Seed_options{-x_zipf_middle_pieces};
$tmp =~ s/%$//;  # Strip trailing %
@segs = split /%/, $tmp;
$us_midprob = 0;
for ($s = 0; $s < 10; $s++) {
    $segs[$s] =~ m@,.*?,.*?,([0-9.]+)@;
    $us_midprob += $1;
}

$blowup = $su_midprob / $us_midprob;


print "Unscaled midprob: $us_midprob
Scaled up midprob: $su_midprob
Linear blow-up factor: $blowup
";
$bias = 1.3;
$sum = 0;
for ($s = 0; $s < 10; $s++) {
    $blowup[$s] = $blowup * $bias;
    $sum += $bias;
    if ($s == 0) {$bias += 0.1;}
    elsif($s > 1) {$bias -= 0.1;}
}

$ave = $sum/ 10;

for ($s = 0; $s < 10; $s++) {
    $blowup[$s] /= $ave;
    $sum += $blowup[$s];
    print "     $blowup[$s] -- $sum\n";
}

$sum = 0;
for ($s = 0; $s < 10; $s++) {
    $segs[$s] =~ m@,.*?,.*?,([0-9.]+)@;
    $sum += $1 * $blowup[$s];
}

print "Is $sum still the same as $su_midprob?\n";

$blowup2 = $su_midprob / $sum;
for ($s = 0; $s < 10; $s++) {
    $blowup[$s] *= $blowup2;
}


$logdomstep = (log($L) - log($F)) / 10.0;

# Now insert new f, l, probrange and cumprob values into the elements of @segs 
# and append revise seg descriptors to $msstring.

$msstring = "";
$f = $F;
$ll = log($f);
$su_cumprob = $su_headprob;
for ($s = 0; $s < 10; $s++) {
    $segs[$s] =~ m@([0-9.]+),.*?,.*?,([0-9.]+),@;
    $alpha = $1 * $blowup;
    $pr = sprintf("%.6f", $2 * $blowup[$s]);
    $su_cumprob += $pr;
    $cp = sprintf("%.6f", $su_cumprob);
    $ll += $logdomstep;
    $l = int (exp($ll) + 0.5);
    $segs[$s] =~ s/([0-9.]+),([0-9]+),([0-9]+),([0-9.]+),([0-9.]+)/$alpha,$f,$l,$pr,$su_cumprob/;
    $f = $l + 1;
    $msstring .= "$segs[$s]%";
}

$qbcmd .= " -x_zipf_middle_pieces=$msstring";


#------------------------------------------------------------------------------------------
#
# End of getting stuff out of the Seed collection and generating the synthesis command
#
#------------------------------------------------------------------------------------------





if ($indexing) {print "Command to generate scaled-up synthetic index will be: $qbcmd\n\n";}
else {print "Command to generate scaled-up synthetic index would have been: $qbcmd\n\n";}


$head_terms = 10; 

# If necessary, generate a real index
if ($indexing && (! (-f "$idxdir/QBASH.vocab")  || !(-f "$idxdir/index.log"))) {
    $cmd = "$qbashi -index_dir=$idxdir -sort_records_by_weight=FALSE -x_doc_length_histo=TRUE $opts > $idxdir/index.log\n";
    print $cmd;
    $code = system($cmd);
    die "Indexing failed with code $code.\n" if $code;
    unlink "$idxdir/vocab.tsv" 
	if -e "$idxdir/vocab.tsv";   # Don't risk using an old one
} else {
    print "Work saved: Real index already exists.\n";
}

# Extract some stuff from the tail of the real index.log (for comparison with the generated one)
$cmd = "tail -20 $idxdir/index.log\n";
print $cmd;

$logtail = `$cmd`;
$code = $?;
die "tail of real index.log failed with code $code.\n" if $code;


if ($logtail =~ /Records \(excluding ignoreds\):\s+([0-9]+).*?Record lengths: Mean:\s+([0-9.]+); St. Dev:\s+([0-9.]+) words.*?building:\s+([0-9.]+) sec.*?traversal:\s+([0-9.]+) sec.*?Collisions per posting:\s+([0-9.]+);.*?chunks allocated:\s+([0-9.]+).*?Distinct words:\s+([0-9.]+);.*Total postings:\s+([0-9.]+);.*Longest postings list:\s+([0-9.]+).*Indexing rate:\s+([0-9.]+)/s) 
{
    $real{docs} = $1;
    $real{doclen_mean} = $2;
    $real{doclen_stdev} = $3;
    $real{listbuild_time} = $4;
    $real{listtraversal_time} = $5;
    $real{collisions_per_insertion} = $6;
    $real{chunks} = $7;
    $real{vocab_size} = $8;
    $real{postings} = $9;
    $real{longest_list} = $10;
    $real{indexing_rate} = $11;
} elsif ($logtail =~ /Records \(excluding ignoreds\):\s+([0-9]+).*?building:\s+([0-9.]+) sec.*?traversal:\s+([0-9.]+) sec.*?Collisions per posting:\s+([0-9.]+);.*?chunks allocated:\s+([0-9.]+).*?Distinct words:\s+([0-9.]+);.*Total postings:\s+([0-9.]+);.*Longest postings list:\s+([0-9.]+).*Indexing rate:\s+([0-9.]+)/s) {
    #indexed without -x_doc_length_histo
    $real{docs} = $1;
    $real{doclen_mean} = "UNK";
    $real{doclen_stdev} = "UNK";
    $real{listbuild_time} = $2;
    $real{listtraversal_time} = $3;
    $real{collisions_per_insertion} = $4;
    $real{chunks} = $5;
    $real{vocab_size} = $6;
    $real{postings} = $7;
    $real{longest_list} = $8;
    $real{indexing_rate} = $9;

} else {
    die "Error: Unable to extract stuff from real index.log\n$logtail\n";
}

# If necessary, generate vocab.*
if ($indexing && (! (-f "$idxdir/vocab.segdat"))) {
    $cmd = "$vocablister $idxdir/QBASH.vocab\n";
    print $cmd;
    $code = system($cmd);
    die "Vocab generation failed with code $code.\n" if $code;
} else {
    print "Work saved: Real vocab.* already exists.\n";
}

mkdir "Real" unless -d "Real";  # A directory to save stuff from the real collection.  

# Copy files vocab.* files to current directory.
cp("$idxdir/vocab.tfd", "Real/$idxname.tfd") or die "Copy failed1: $!\n";
cp("$idxdir/vocab.plot", "Real/$idxname.plot") or die "Copy failed2: $!\n";
cp("$idxdir/vocab.segdat", "Real/$idxname.segdat") or die "Copy failed3: $!\n";

# Read in QBASHI options from the Real .tfd file
die "Can't open Real/$idxname.tfd\n"
    unless open TFD, "Real/$idxname.tfd";
while (<TFD>) {
    chomp;
    if (m@(.*)=(.*)@) {
	$options{$1} = $2;
    }
}
close(TFD);

$singletons = $options{-x_synth_vocab_size} * $options{-x_zipf_tail_perc} / 100.0;

print "

Index name: $idxname

";


$real{tail_perc} = $options{-x_zipf_tail_perc};

# Use lsqcmd to compute alpha for the real collection
$cmd = "$lsqcmd Real/$idxname\n";
$lsqout = `$cmd`;
die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

print $lsqout;


if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
    $plot_elt_real = $1;
    $plot_elt_real =~ /([\-0-9.]+)\s*\*x\s*title "Linear fit"/;
    $real{alpha} = $1;
    $plot_elt_real =~ s/Linear fit/Fitted real/;
} else {
    die "Error: Unable to extract stuff from $lsqcmd output\n$lsqout\n";
}

$real{doclength} = sprintf("%.2f", $real{postings} / $real{docs});

####################################################################################################
#         Now generate the scaled up index in subdirectory ScaledUp and generate vocab.*           #
####################################################################################################

if ($indexing) {
    mkdir "ScaledUp" unless -d "ScaledUp";  # A directory in which to do all our work  
    mkdir "ScaledUpPlots" unless -d "ScaledUpplots";  # A directory for plotfiles,
    # dat files, and PNGs


    chdir "ScaledUp";    # CHDIR --------->

    # Write the indexer command into a file in the ScaledUp directory.
    $icf = "${idxname}_index.cmd";
    die "Can't write to $icf\n" unless open ICF, ">$icf";
    print ICF $qbcmd;
    close(ICF);

    $cmd = "$qbcmd > index.log\n";
    print $cmd;
    $code = system($cmd);
    die "Indexing failed in directory ScaledUp with code $code.\nCmd was $cmd\n" 
	if $code;

    $cmd = "$vocablister QBASH.vocab\n";
    print $cmd;
    $code = system($cmd);
    die "Vocab generation failed for ScaledUp with code $code.\n" if $code;

    rename "vocab.tfd", "$idxname.tfd";
    rename "vocab.plot", "$idxname.plot";
    cp "$idxname.plot", "../ScaledUpPlots/$idxname.plot";
    rename "vocab.segdat", "$idxname.segdat";


    chdir "..";  #  <----------- CHDIR

} else {
    print "Assuming ScaledUp index already exists.\n";
}
####################################################################################################
#                         Now extract the properties of the Scaled_Up index                        #
####################################################################################################


# Extract some stuff from the tail of the scaled_up index.log
$cmd = "tail -20 ScaledUp/index.log\n";
print $cmd;
$logtail = `$cmd`;
$code = $?;
die "tail of scaled_up index.log failed with code $code.\n" if $code;
if ($logtail =~ /Records \(excluding ignoreds\):\s+([0-9]+).*?Record lengths: Mean:\s+([0-9.]+); St. Dev:\s+([0-9.]+) words.*?building:\s+([0-9.]+) sec.*?traversal:\s+([0-9.]+) sec.*?Collisions per posting:\s+([0-9.]+);.*?chunks allocated:\s+([0-9.]+).*?Distinct words:\s+([0-9.]+);.*Total postings:\s+([0-9.]+);.*Longest postings list:\s+([0-9.]+).*Indexing rate:\s+([0-9.]+)/s) {
    $scaled_up{docs} = $1;
    $scaled_up{doclen_mean} = $2;
    $scaled_up{doclen_stdev} = $3;
    $scaled_up{listbuild_time} = $4;
    $scaled_up{listtraversal_time} = $5;
    $scaled_up{collisions_per_insertion} = $6;
    $scaled_up{chunks} = $7;
    $scaled_up{vocab_size} = $8;
    $scaled_up{postings} = $9;
    $scaled_up{longest_list} = $10;
    $scaled_up{indexing_rate} = $11;
} elsif ($logtail =~ /Records \(excluding ignoreds\):\s+([0-9]+).*?building:\s+([0-9.]+) sec.*?traversal:\s+([0-9.]+) sec.*?Collisions per posting:\s+([0-9.]+);.*?chunks allocated:\s+([0-9.]+).*?Distinct words:\s+([0-9.]+);.*Total postings:\s+([0-9.]+);.*Longest postings list:\s+([0-9.]+).*Indexing rate:\s+([0-9.]+)/s) {
    #indexed without -x_doc_length_histo
    $scaled_up{docs} = $1;
    $scaled_up{doclen_mean} = "UNK";
    $scaled_up{doclen_stdev} = "UNK";
    $scaled_up{listbuild_time} = $2;
    $scaled_up{listtraversal_time} = $3;
    $scaled_up{collisions_per_insertion} = $4;
    $scaled_up{chunks} = $5;
    $scaled_up{vocab_size} = $6;
    $scaled_up{postings} = $7;
    $scaled_up{longest_list} = $8;
    $scaled_up{indexing_rate} = $9;

} else {
    die "Unable to extract stuff from scaled_up index.log\n";
}




# Read in QBASHI options from the ScaledUp .tfd file
die "Can't open ScaledUp/$idxname.tfd\n"
    unless open TFD, "ScaledUp/$idxname.tfd";
while (<TFD>) {
    chomp;
    if (m@(.*)=(.*)@) {
	$su_options{$1} = $2;
    }
}
close(TFD);


$scaled_up{tail_perc} = $su_options{-x_zipf_tail_perc};

# Use lsqcmd to compute alpha for ScaledUp
$cmd = "$lsqcmd ScaledUpPlots/$idxname\n";
$lsqout = `$cmd`;
die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

print $lsqout;

if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
    $plot_elt_scaled_up = $1;
    $plot_elt_scaled_up =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
    $scaled_up{alpha} = $1;
    $plot_elt_scaled_up =~ s/Linear fit/Fitted scaled_up/;
} else {
    die "Error: Unable to extract stuff from $lsqcmd output\n$lsqout\n";
}

$scaled_up{doclength} = sprintf("%.2f", $scaled_up{postings} / $scaled_up{docs});


die "Can't write to ScaledUpPlots/${idxname}_real_v_scaled_up_summary.txt\n"
    unless open RVM, ">ScaledUpPlots/${idxname}_real_v_scaled_up_summary.txt";

print RVM "\n                              Real v. Scaled_Up";
print RVM "\n==================================================\n";

foreach $k (sort keys %real) {
    $perc = "NA";
    if (defined($scaled_up{$k})) {
	$mim = $scaled_up{$k};
	$perc = sprintf("%+.1f%%", 100.0 * ($scaled_up{$k} - $real{$k}) / $real{$k})
	    unless !defined($real{$k}) || $real{$k} == 0;
    }
    else {$mim = "UNDEF";}
    print RVM sprintf("%27s", $k), "  $real{$k} v. $mim   ($perc)\n";
}

close(RVM);

####################################################################################################
#         Now plot the term frequency distributions for real and scaled_up     #
####################################################################################################

$pcfile = "ScaledUpPlots/${idxname}_real_v_scaled_up_plot.cmds";

die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

print P "
set terminal pdf
set size ratio 0.7071
set output \"ScaledUpPlots/${idxname}_real_v_scaled_up.pdf\"
set xlabel \"Ln(rank)\"
set ylabel \"Ln(probability)\"
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set style line 4 linewidth 4
set style line 5 linewidth 4
set style line 6 linewidth 4
set style line 7 linewidth 4
set style line 8 linewidth 4
set style line 9 linewidth 4
set pointsize 3
";
print P "
plot \"Real/$idxname.plot\" title \"Real\" pt 7 ps 0.25, $plot_elt_real ls 1, \"ScaledUpPlots/$idxname.plot\" title \"Scaled_Up\" pt 7 ps 0.25 , $plot_elt_scaled_up ls 3

";


close P;

`gnuplot $pcfile > /dev/null 2>&1`;
die "Gnuplot failed with code $? for $pcfile!\n" if $?;

print "

Scaling up all done.  A summary of comparisons between real and emulated collection
is in ScaledUpPlots/${idxname}_real_v_scaled_up_summary.txt.

View synthetic data, real data and lines of best fit using a PDF viewer, e.g.

     acroread ScaledUpPlots/${idxname}_real_v_scaled_up.pdf 

";

exit 0;

#----------------------------------------------------------------------

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
