#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Try to synthesize a text corpus with properties as close as possible
# to a real one, based on a sample of the latter.   We assume that 
# samplingExperiments.pl from this directory has been run for 
# the corpus whose name C is given to this script and it has
# produced a datafile SamplePlots/scaling_model_for_C.txt, recording 
# parameter values extracted from the smallest (1%) sample and 
# models of how those parameters grow when the collection is scaled
# up by a scaling factor SF.

# Iff the scaling factor is 100, we try to compare the scaled-up sample
# with the original base corpus.

# Scripts are now called via $^X in case the perl we want is not in /usr/bin/
 
$perl = $^X;
$perl =~ s@\\@/@g;

use Cwd;
$cwd = getcwd;
if ($cwd =~ m@/cygdrive@) {
    $cwd =~ s@/cygdrive/([a-zA-Z])/@$1:/@;
}

$|++;


die "Usage: $0 <corpusName> <scaleFactor> <tf_model> <doc_length_model> <term_repn_model> <dependence_model> 
    <corpus_name> is a name (e.g. TREC-AP) not a path.  We expect to find a single file called
          <corpus_name>.tsv or <corpus_name>.starc in the directory ../Experiments/Emulation
    <tf_model> ::= Piecewise|Linear.   *** Unfortunately Piecewise is not implemented yet ***
          If Piecewise we'll use a 3-segment term-frequency model with 10 headpoints, 10 linear
          segments in the middle and an explicit count of singletons.  If Linear we'll approximate
          the whole thing as pure Zipf.
    <doc_length_model> ::= dlnormal|dlgamma  *** Unfortunately dlgamma is not implemented yet ***
    <term_repn_model> ::= tnum|base26|bubble_babble|bodo_dave|from_tsv|markov-9e?
          The Markov order is specified by the single digit, represented by '9'.
          If present, the 'e' specifies use of the end-of-word symbol.  Otherwise
          a random length will be generated for each word and it will be cut off there.
          The Markov model will be trained on the base corpus.
          If from_tsv is given, the vocab will be that of the base corpus.
          Recommended: from_tsv if appropriate or markov-5e (6e or 7e on large RAM machines)
    <dependence_model> ::= ind|ngrams[2-5]|bursts|coocs|fulldep
          Currently, only ind(ependent) and ngramsX are implemented.  Ind means that words are 
          generated completely independently of each other.  Fulldep means ngrams + bursts 
          + coocs.  Dependence models are only applied if the relevant files, i.e ngrams.termids,
          bursts.termids, coocs.termids, are available for the base corpus. 
\n"

    unless $#ARGV >= 5 ;

print "\n*** $0 @ARGV\n\n";

$plotter = `which gnuplot 2>&1`;
chomp($plotter);
$plotter .= '.exe' if ($^O =~ /cygwin/i || $^O =~ /MSWin/i);
undef $plotter if $plotter =~/^which: no/;

$markov_defaults = "-markov_lambda=0 -markov_model_word_lens=TRUE -markov_use_vocab_probs=FALSE -markov_full_backoff=FALSE -markov_assign_reps_by_rank=TRUE -markov_favour_pronouncable=TRUE";

$corpusName = $ARGV[0];
$sf = $ARGV[1];

for ($a = 2; $a <=$#ARGV; $a++) {
    if ($ARGV[$a] eq "dlnormal") { $dlModel = $ARGV[$a];}
    
    elsif ($ARGV[$a] eq "tnum") { $trModel = $ARGV[$a];}
    elsif ($ARGV[$a] eq "base26") { $trModel = $ARGV[$a];}
    elsif ($ARGV[$a] eq "bubblea_babble") { $trModel = $ARGV[$a];}
    elsif ($ARGV[$a] eq "bodo_dave") { $trModel = $ARGV[$a];}
    elsif ($ARGV[$a] eq "from_tsv") { $trModel = $ARGV[$a];}
    elsif ($ARGV[$a] =~ /^markov-[0-9]/) { $trModel = $ARGV[$a];}

    elsif ($ARGV[$a] eq "Piecewise") {
	print "\nUnfortunately Piecewise scale-up is not yet implemented.  Falling back to Linear\n\n";
	$syntyp = "Linear";
    } 
    elsif ($ARGV[$a] eq "Linear") {$syntyp = $ARGV[a];}
    
    elsif ($ARGV[$a] eq "ind") {$depModel = "ind";}
    elsif ($ARGV[$a] =~ /^ngrams([2-5])$/) {$ngram_degree = $1; $depModel = "ngrams$ngram_degree";}
    elsif ($ARGV[$a] eq "bursts") {
	print "\nUnfortunately \"bursts\" is not yet implemented.  Falling back to ind\n\n";
	$depModel = "ind";
    }
    elsif ($ARGV[$a] eq "coocs") {
	print "\nUnfortunately \"coocs\" is not yet implemented.  Falling back to ind\n\n";
	$depModel = "ind";
    }
    elsif ($ARGV[$a] eq "fulldep") {
	print "\nUnfortunately \"fulldep\" is not yet implemented.  Falling back to ind\n\n";
	$depModel = "ind";
     }

    else {die "Unrecognized argument $ARGV[$a]\n";}
}

die "No document length model specified\n"
    unless defined($dlModel);

die "No term representation model specified\n"
    unless defined($trModel);

die "No term frequency distribution model specified\n"
    unless defined($syntyp);

die "No term dependence model specified\n"
    unless defined($depModel);

$emuMethod = "${trModel}_${dlModel}_${depModel}";



$top_terms = 25;


$experimentRoot = $cwd;
$experimentRoot =~ s@/[^/]*$@@;  # strip off the last component of the path
$experimentRoot .= "/Experiments";
die "$experimentRoot must exist\n"
    unless -d $experimentRoot;

$baseDir = "$experimentRoot/Base";

$scalingModelFile = "$experimentRoot/Sampling/SamplePlots/scaling_model_for_${corpusName}.txt";
die "Can't read $scalingModelFile\n" 
    unless open SMF, $scalingModelFile;

# The executables and scripts we need
$generator = check_exe("corpusGenerator.exe");
$lsqcmd = check_exe("lsqfit_general.pl");
$extractor = check_exe("corpusPropertyExtractor.exe");


$experimentDir = "$experimentRoot/Scalingup";
mkdir $experimentDir unless -d $experimentDir;
$workDir = "${experimentDir}/Working";
mkdir $workDir unless -d $workDir;  # A directory in which to do all our work
$plotDir = "${experimentDir}/Plots";
mkdir $plotDir unless -d $plotDir; # A directory for plotfiles, result files, and PDFs

print "
------------------------- Settings Summary ----------------------------
Output directories: $workDir, $plotDir
Term frequency model: $syntyp
Document length model: $dlModel
Term representation model: $trModel
Term depedence model: $depModel
Corpus name: $corpusName
propertyExtractor options: $propertyExtractorOptions
-----------------------------------------------------------------------

";
sleep(3);  # Give people an instant to read the settings.



$genOpts = "-file_synth_docs=$workDir/${corpusName}.tsv";

$gencmd = "$generator $genOpts";

while (<SMF>) {
    next if (/^\s*#/);   # Skip comment lines
    next if (/^\s*$/);   # Skip blank lines
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
	$gencmd .=" $attr$val";
    } else {
	print "Erroneous line in $scalingModelFile:

$_
";
	exit(1);
    }
   
}
close(SMF);

print "\nGenerator command is: $gencmd\n";



####################################################################################################
#    Now generate and index the scaled up corpus in working subdirectory and generate vocab.tsv    #
####################################################################################################


$cmd = "$gencmd > $workDir/generator.log\n";
print $cmd, "\n";
$code = system($cmd);
die "$generator failed in directory $workDir with code $code.\n" 
    if $code;


$cmd = "$extractor -inputFileName=$workDir/${corpusName}.tsv  -outputStem=$workDir/${corpusName}";
print $cmd, "\n";
$code = system($cmd);
die "$extractor failed in directory $workDir with code $code.\n" 
    if $code;



print "\n ----- Scaled up corpus has been generated and its properties extracted. -----\n\n";


if ($sf != 100) {
    printf "Scaling factor $sf is not 100, therefore it makes no sense to
run comparisons.  Farewell cruel world!\n";
    exit(0);
}

####################################################################################################
#                              Preparing to compare Base v. scaled-up Mimic                        #
####################################################################################################

$fit_thresh = 0.02;

print "Since scale_factor == 100 we're going to try to do a comparison.  ";

# Assume that the properties of the base index have been extracted into
# $baseDir/${corpusName}_* 

# Extract some stuff from the Base _summary.txt  (for comparison with the generated one)

$sumry = "$baseDir/${corpusName}_summary.txt";
die "Can't open $sumry\n" unless open S, $sumry;

while (<S>) {
    next if /^\s*#/;  # skip comments
    if (/(\S+)=(\S+)/) {
	$base{$1} = $2;
    }
}
close(S);
    


####################################################################################################
#                         Now extract the properties of the Scaled_Up index                        #
####################################################################################################


# Extract some stuff from the scaled_up summary.txt

$sumry = "$workDir/${corpusName}_summary.txt";
die "Can't open $sumry\n" unless open S, $sumry;

while (<S>) {
    next if /^\s*#/;  # skip comments
    if (/(\S+)=(\S+)/) {
	$scaledUp{$1} = $2;
    }
}
close(S);
    

# Use lsqcmd to compute alpha for Scaled Up corpus
$cmd = "$lsqcmd $workDir/${corpusName}_vocab\n";
$lsqout = `$cmd`;
die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

print $lsqout;

if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
    $plot_elt_scaledUp = $1;
    $plot_elt_scaledUp =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
    $scaledUp{alpha} = $1;
    $plot_elt_scaledUp =~ s/Linear fit/Fitted scaledUp/;
} else {
    die "Error: Unable to extract stuff from $lsqcmd output\n$lsqout\n";
}

$scaledUp{doclength} = sprintf("%.2f", $scaledUp{total_postings} / $scaledUp{docs});


die "Can't write to $plotDir/${corpusName}_base_v_scaledUp_summary.txt\n"
    unless open RVM, ">$plotDir/${corpusName}_base_v_scaledUp_summary.txt";

print RVM "\n                              Base v. ScaledUp";
print RVM "\n==================================================\n";

foreach $k (sort keys %base) {
    $perc = "NA";
    if (defined($scaledUp{$k})) {
	$mim = $scaledUp{$k};
	$perc = sprintf("%+.1f%%", 100.0 * ($scaledUp{$k} - $base{$k}) / $base{$k})
	    unless !defined($base{$k}) || $base{$k} == 0;
    }
    else {$mim = "UNDEF";}
    print RVM sprintf("%27s", $k), "  $base{$k} v. $mim   ($perc)\n";
}

close(RVM);

####################################################################################################
#         Now plot the term frequency distributions for base and scaledUp     #
####################################################################################################

$pcfile = "$plotDir/${corpusName}_base_v_scaledUp_plot.cmds";

die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

print P "
set terminal pdf
set size ratio 0.7071
set output \"$plotDir/${corpusName}_base_v_scaledUp.pdf\"
set xlabel \"Ln(rank)\"
set ylabel \"Ln(probability)\"
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set pointsize 3
";
#print P "
#plot \"$corpusName.plot\" title \"Base\" pt 7 ps 0.25, $plot_elt_base ls 1, \"$plotDir/${corpusName}_vocab.plot\" title \"ScaledUp\" pt 7 ps 0.25 , $plot_elt_scaledUp ls 3
#
#";

print P "
plot \"$baseDir/${corpusName}_vocab.plot\" title \"Base\" pt 7 ps 0.25, \"$workDir/${corpusName}_vocab.plot\" title \"Scaled Up\" pt 7 ps 0.25

";


close P;


if (defined($plotter)) {
    `$plotter $pcfile > /dev/null 2>&1`;
    die "$plotter failed with code $? for $pcfile!\n" if $?;
} else {
    warn "\n\n$0: Warning: gnuplot not found.  PDFs of graphs will not be generated.\n\n";
}


print "
Scaling up all done.  


To see a summary of comparisons between Base and Scaled Up collection

    cat $plotDir/${corpusName}_base_v_scaledUp_summary.txt

";
print "
    
To view word frequency distributions for Base and Scaled Up

    acroread $plotDir/${corpusName}_base_v_scaledUp.pdf 

" if ($runGnuplot);

exit 0;

#----------------------------------------------------------------------

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


