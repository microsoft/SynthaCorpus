#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# 1. Run corpusPropertyExtractor.exe
#     - this creates a whole bunch of files which can be used for plotting
#       and analysis as well as for generating the parameters needed in
#       step 2
# 2. generateACorpus.exe is documented in ../doc/how_to_synthesize_a_corpus.pdf


# 
# Notes on modeling the term frequency distribution:
# --------------------------------------------------
# 1. Singleton terms are treated separately.  We count how many there are.
# 2. Terms at the head are also treated separately.   For several 
#    corpora, the first few terms show substantial deviations from either
#    a linear or a quadratic fit (in log-log space).  Therefore, we calculate
#    the term occurrence percentages of the 10 most frequent terms.  
# 3. The non-singleton, non-head part of the term frequency distribution
#    is best modeled in piecewise linear fashion.  We assume 10 pieces.
# 4. Naive least squares best fitting results in domination by the tail terms
#    and poor fit toward the head.  Therefore we do the fitting based on 
#    a subset of the data which ensures that datapoints are only picked if
#    they are more than a threshold X distance from the previous picked point.
#    Adamic uses bucketing -- our method is similar and may have some advantage.

# Scripts are now called via quotemeta($^X) in case the perl we want is not in /usr/bin/
#  (see check_exe())
#


# Versions after 05 Jan 2016 do all the work in corpus specific sub-directories, (A) to
# allow this script to be run simultaneously on multiple corpora, and (B) to
# declutter.

$perl = $^X;
$perl =~ s@\\@/@g;

use File::Copy "cp";
use Cwd;
$cwd = getcwd();
if ($cwd =~ m@/cygdrive@) {
    $cwd =~ s@/cygdrive/([a-zA-Z])/@$1:/@;
}

$plotter = `which gnuplot 2>&1`;
chomp($plotter);
$plotter .= '.exe' if ($^O =~ /cygwin/i || $^O =~ /MSWin/i);
undef $plotter if $plotter =~/^which: no/;

$markov_defaults = "-markov_lambda=0 -markov_model_word_lens=TRUE -markov_use_vocab_probs=FALSE -markov_full_backoff=FALSE -markov_assign_reps_by_rank=TRUE -markov_favour_pronouncable=TRUE";
$top_terms = 25;

$|++;

die "Usage: $0 <corpus_name> <tf_model> <doc_length_model> <term_repn_model> <dependence_model> [-dependencies=neither|both|base|mimic]
    <corpus_name> is a name (e.g. TREC-AP) not a path.  We expect to find a single file called
          <corpus_name>.tsv or <corpus_name>.starc in the directory ../Experiments
    <tf_model> ::= Piecewise|Linear|Copy
          If Piecewise we'll use a 3-segment term-frequency model with 10 headpoints, 10 linear
          segments in the middle and an explicit count of singletons.  If Linear we'll approximate
          the whole thing as pure Zipf.  If Copy we'll copy the exact term frequency distribution
          from the base corpus.
    <doc_length_model> ::= dlnormal|dlsegs|dlhisto
          If dlhisto or dlsegs is given, the necessary data will be taken from the base corpus.
          Recommended: dlhisto (Unfortunately dlgamma not available in this version)
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

    The default value for dependencies is both.  This means that corpusPropertyExtractor will
    extract term dependency data for both the base and the mimic corpus.   That may be memory 
    intensive and very time consuming. Other values can be used to suppress that computation 
    for either or both of the base and mimic corpus. 
\n"

    unless $#ARGV >= 4 ;

$corpusName = $ARGV[0];

die "\nUnfortunately, SynthaCorpus runs into trouble when there are spaces or various sorts of
punctuation in directory paths.  Your current working directory, or the corpusName you have
specified contain such a character.  Please reinstall synthaCorpus in a new place which doesn't
have that problem or rename the corpus file to overcome this difficulty.  The only characters 
permitted are letters, numbers, period, hyphen, underscore and (in the current working directory
only) colons and slashes of either persuasion.\n\n
CWD: $cwd
corpusName: $corpusName

"
    if ($cwd =~ /[^:a-z0-9\-_\/\.\\]/i) || ($corpusName =~  /[^a-z0-9\-_\.]/i);


print "\n*** $0 @ARGV\n\n";

$propertyExtractorOptions = "-obs_thresh=10";  # (must be the same for base and mimic corpora)

$dependencies = "both";

for ($a = 1; $a <=$#ARGV; $a++) {
    if ($ARGV[$a] eq "dlnormal") { $dl_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "dlsegs") { $dl_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "dlhisto") { $dl_model = $ARGV[$a];}
    
    elsif ($ARGV[$a] eq "tnum") { $tr_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "base26") { $tr_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "bubblea_babble") { $tr_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "bodo_dave") { $tr_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "from_tsv") { $tr_model = $ARGV[$a];}
    elsif ($ARGV[$a] =~ /^markov-[0-9]/) { $tr_model = $ARGV[$a];}

    elsif ($ARGV[$a] eq "Piecewise") {$syntyp = "Piecewise";}
    elsif ($ARGV[$a] eq "Linear") {$syntyp = "Linear";}
    elsif ($ARGV[$a] eq "Copy") {$syntyp = "Copy";}
    
    elsif ($ARGV[$a] eq "ind") {$dep_model = "ind";}
    elsif ($ARGV[$a] =~ /^ngrams([2-5])$/) {$ngram_degree = $1; $dep_model = "ngrams$ngram_degree";}
    elsif ($ARGV[$a] eq "bursts") {$dep_model = "bursts";}
    elsif ($ARGV[$a] eq "coocs") {$dep_model = "coocs";}
    elsif ($ARGV[$a] eq "fulldep") {$dep_model = "fulldep";}

    elsif ($ARGV[$a] =~ /^-?dependencies=(.*)/) { 
	$dependencies = $1;    
    } else {die "Unrecognized argument $ARGV[$a]\n";}
}

die "Invalid argument $dependencies for -dependencies\n"
    unless $dependencies =~ /^(both|neither|base|mimic)$/;

die "No document length model specified\n"
    unless defined($dl_model);

die "No term representation model specified\n"
    unless defined($tr_model);

die "No term frequency distribution model specified\n"
    unless defined($syntyp);

die "No term dependence model specified\n"
    unless defined($dep_model);

$experimentRoot = $cwd;
$experimentRoot =~ s@/[^/]*$@@;  # strip off the last component of the path
$experimentRoot .= "/Experiments";
die "$experimentRoot must exist and must contain the corpus file to be emulated\n"
    unless -d $experimentRoot;

foreach $suffix (".starc", ".STARC", ".tsv", ".TSV") {
    $cf = "$experimentRoot/$corpusName$suffix";
    if (-r $cf) {
	$corpusFile = $cf;
	$fileType = $suffix;
	last;   # Give preference to STARC over TSV.
    }
}
    
die "Corpus file not at either $experimentRoot/$corpusName.tsv/TSV or $experimentRoot/$corpusName.starc/STARC\n"
	unless defined($corpusFile);


$emuMethod = "${tr_model}_${dl_model}_${dep_model}";

$experimentDir = "${experimentRoot}/Emulation";
if (! -e $experimentDir) {
    die "Can't mkdir $experimentDir\n"
	unless mkdir $experimentDir;
}
mkdir "$experimentDir/$syntyp" unless -d "$experimentDir/$syntyp";  
$emulationDir = "$experimentDir/$syntyp/$emuMethod";
mkdir $emulationDir unless -d $emulationDir;  # A directory in which to do all our work  



print "
------------------------- Settings Summary ----------------------------
Output directory: $emulationDir
Term frequency model: $syntyp
Document length model: $dl_model
Term representation model: $tr_model
Term dependence model: $dep_model
Corpus name: $corpusName
Output format: $fileType
propertyExtractor options: $propertyExtractorOptions
dependencies: $dependencies
-----------------------------------------------------------------------

    ";

sleep(3);  # Give people an instant to read the settings.


# The executables and scripts we need
$extractor = check_exe("corpusPropertyExtractor.exe");
$generator = check_exe("corpusGenerator.exe");
$lsqcmd = check_exe("lsqfit_general.pl");
$doclenCmd = check_exe("doclen_piecewise_modeling.pl");
$plotDoclengths = check_exe("plot_doclengths.pl");
$plotWdlengths = check_exe("plot_wdlengths.pl");
$compare_top_terms = check_exe("compare_base_v_mimic_top_terms.pl");


$fit_thresh = 0.02;

if ($syntyp eq "Piecewise") {
    $head_terms = 10; 
    $middle_segs = 10;
} else {
    $head_terms = 0; 
    $middle_segs = 1;
}


$baseDir = "$experimentRoot/Base";
if (! -e $baseDir) {
    die "Can't make $baseDir\n"
	unless mkdir $baseDir;
}

$base{gzip_ratio} = get_compression_ratio($corpusFile, $baseDir, $corpusName);

#Run the corpusPropertyExtractor
$cmd = "$extractor inputFileName=$corpusFile outputStem=$baseDir/$corpusName";
$cmd .= " ignoreDependencies=TRUE"
    if ($dependencies =~/^(neither|mimic)$/);
print $cmd, "\n";
$code = system($cmd);
die "Corpus property extraction failed with code $code. Cmd was:\n   $cmd\n"
    if ($code);


# Extract some stuff from the the summary file  - Format: '<attribute>=<value>'
$sumryfile = "$baseDir/${corpusName}_summary.txt";
die "Can't open $sumryfile\n"
    unless open S, $sumryfile;
while (<S>) {
    next if /^\s*#/;  # skip comments
    if (/(\S+)=(\S+)/) {
	$base{$1} = $2;
    }
}
close(S);


# Extract values for piecewise model of document lengths
$cmd = "$doclenCmd $baseDir/${corpusName}_docLenHist.tsv $corpusName $syntyp $tr_model $dl_model $dep_model\n";
print $cmd;
$rez = `$cmd`;
die "$doclenCmd failed with code $?.\n$rez" if $?;

die "Can't match synth_dl_segments= option output\n$rez\n" 
    unless $rez =~ /(synth_dl_segments=\"[0-9.:,;]+\")/s;

$base_dl_segs_option = $1;
print "DL Piecewise Segs: $base_dl_segs_option\n";

#  --------------------------------------------------------------------------------------

# Use lsqcmd to compute alpha for unigrams
$cmd = "$lsqcmd $baseDir/${corpusName}_vocab\n";
$lsqout = `$cmd`;
die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

print $lsqout;


if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
    $plot_elt_real = $1;
    $plot_elt_real =~ /([\-0-9.]+)\s*\*x\s*title "Linear fit"/;
    $base{alpha} = $1;
    $plot_elt_real =~ s/Linear fit/Fitted base/;
} else {
    die "Error: Unable to extract stuff from base corpus $lsqcmd output\n";
}

if ($dependencies eq "both" || $dependencies eq "base") {
    # Use lsqcmd to compute alpha for bigrams
    $cmd = "$lsqcmd $baseDir/${corpusName}_bigrams\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    print $lsqout;


    if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	$plot_elt_real_bg = $1;
	$plot_elt_real_bg =~ /([\-0-9.]+)\s*\*x\s*title "Linear fit"/;
	$base{alpha_bigrams} = $1;
	$plot_elt_real_bg =~ s/Linear fit/Fitted real/;
    } else {
	die "Error: Unable to extract stuff from base corpus bigrams $lsqcmd output\n";
    }


    # Use lsqcmd to compute alpha for repetitions
    $cmd = "$lsqcmd $baseDir/${corpusName}_repetitions\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    print $lsqout;


    if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	$plot_elt_real_re = $1;
	$plot_elt_real_re =~ /([\-0-9.]+)\s*\*x\s*title "Linear fit"/;
	$base{alpha_repetitions} = $1;
	$plot_elt_real_re =~ s/Linear fit/Fitted real/;
    } else {
	die "Error: Unable to extract stuff from base corpus repetitions $lsqcmd output\n";
    }


    if (-e "$baseDir/${corpusName}_coocs.plot") {
	# Use lsqcmd to compute alpha for cooccurs (Only if the cooccurs files are available)
	$cmd = "$lsqcmd $baseDir/${corpusName}_cooccurs\n";
	$lsqout = `$cmd`;
	die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

	print $lsqout;


	if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	    $plot_elt_real_co = $1;
	    $plot_elt_real_co =~ /([\-0-9.]+)\s*\*x\s*title "Linear fit"/;
	    $base{alpha_cooccurrences} = $1;
	    $plot_elt_real_co =~ s/Linear fit/Fitted real/;
	} else {
	    die "Error: Unable to extract stuff from base corpus cooccurs $lsqcmd output\n";
	}
    }
} else {
    print "Skipping word dependency analyses for the base corpus.\n";
}

print "



-----------------------------------------------------------------------
        We've done everything needed for the base corpus.
-----------------------------------------------------------------------


";

sleep(3);  # Let that sink in for a moment

####################################################################################################
#                                         Do the emulation                                         #
####################################################################################################

$dlMean = $base{doclen_mean};
$dlStdev = $base{doclen_stdev};

# Read in synthesis options from the Base .tfd file
die "Can't open ${baseDir}/${corpusName}_vocab.tfd\n"
    unless open TFD, "${baseDir}/${corpusName}_vocab.tfd";
while (<TFD>) {
    chomp;
    if (m@-*(.*)=([^#]*)@) {  # Ignore hyphens.  Ignore trailing comments
	$attr = $1;
	$val = $2;
	$options{$attr} = $val
	    unless (($attr eq "zipf_middle_pieces") && ($syntyp eq "Linear")) 
	    ||(($attr eq "zipf_middle_pieces") && ($syntyp eq "Copy"))
	    ||(($attr eq "zipf_alpha") && ($syntyp eq "Copy"))
	    ||(($attr eq "zipf_alpha") && ($syntyp eq "Piecewise"));
    }
}
close(TFD);

foreach $k (keys %options) {
    print "\$options{$k} = $options{$k}\n";
}


if ($dl_model eq "dlnormal") {
    $doclenopts = "synth_doc_length=$dlMean synth_doc_length_stdev=$dlStdev";
#} elsif ($dl_model eq "dlgamma") {  
#    $doclenopts = "synth_dl_gamma_shape=$base{gamma_shape} synth_dl_gamma_scale=$base{gamma_scale}";
} elsif ($dl_model eq "dlsegs") {
    $doclenopts = $base_dl_segs_option;
} elsif ($dl_model eq "dlhisto") {
    $doclenopts = "synth_dl_read_histo=${baseDir}/${corpusName}_docLenHist.tsv";
}

$termrepopts = "-synth_term_repn_method=$tr_model";
if ($tr_model eq "from_tsv") {
    $termrepopts .= " -synth_input_vocab=${baseDir}/${corpusName}_vocab_by_freq.tsv";
} elsif ($tr_model =~ /^markov-/) {
    $termrepopts .= " -synth_input_vocab=${baseDir}/${corpusName}_vocab_by_freq.tsv ";
    $termrepopts .= $markov_defaults;
}


$generator_cmd = "$generator $doclenopts $termrepopts synth_postings=$options{synth_postings} synth_vocab_size=$options{synth_vocab_size} file_synth_docs=$emulationDir/$corpusName$fileType";

# ...................... set up options for term dependence .....................
if (($dep_model eq "fulldep" || $dep_model =~ /ngrams/) && (-r "${baseDir}/${corpusName}_ngrams.termids")) {
    print "\nGoing to attempt ${ngram_degree}-gram emulation using ${baseDir}/${corpusName}_ngrams.termids\n\n";
    $generator_cmd .= " -synth_input_ngrams=${baseDir}/${corpusName}_ngrams.termids";
}

# The following two are in advance of implementation ....
if (($dep_model eq "fulldep" || $dep_model eq "bursts") && (-r "${baseDir}/${corpusName}_bursts.termids")) {
    print "\nGoing to attempt n-gram emulation using ${baseDir}/${corpusName}_bursts.termids\n\n";
    $generator_cmd .= " -synth_input_ngrams=${baseDir}/${corpusName}_bursts.termids";
}

if (($dep_model eq "fulldep" || $dep_model eq "coocs") && (-r "${baseDir}/${corpusName}_coocs.termids")) {
    print "\nGoing to attempt n-gram emulation using ${baseDir}/${corpusName}_coocs.termids\n\n";
    $generator_cmd .= " -synth_input_ngrams=${baseDir}/${corpusName}_coocs.termids";
}
# ...............................................................................

undef %mimic;


#..................................................................................................#
#        generate with those characteristics in subdirectory $syntyp/$emuMethod                  #
#..................................................................................................#

# ------------------- Generating the corpus --------------------------------
$cmd = $generator_cmd;
if ($syntyp eq "Piecewise") {
    $cmd .= " zipf_tail_perc=$options{zipf_tail_perc} head_term_percentages=$options{head_term_percentages} zipf_middle_pieces=$options{zipf_middle_pieces}";
} elsif ($syntyp eq "Copy") {
    $cmd .= " -tfd_use_base_vocab=TRUE";
} else {
    $cmd .= " zipf_tail_perc=$options{zipf_tail_perc} head_term_percentages=$options{head_term_percentages} zipf_alpha=$options{zipf_alpha}";
}

die "Can't open $emulationDir/gencorp.cmd\n" 
    unless open QC, ">$emulationDir/gencorp.cmd";
print QC $cmd, "\n";
close QC;

$cmd .=  " > $emulationDir/gencorp.log\n";
print $cmd;
$code = system($cmd);
die "Corpus generation failed with code $code.\nCmd was $cmd\n" if $code;

# ------------------ Measure compressibility --------------------
$mimic{gzip_ratio} = get_compression_ratio("$emulationDir/$corpusName$fileType", $emulationDir, $corpusName);


#..................................................................................................#
#                         Now extract the properties of the $syntyp index                          #
#..................................................................................................#

#Run the corpusPropertyExtractor after determining whether the extension is TSV or STARC
$cmd = "$extractor inputFileName=$emulationDir/${corpusName}$fileType outputStem=$emulationDir/$corpusName";
$cmd .= " ignoreDependencies=TRUE"
	if $dependencies =~ /^(neither|base)$/;	
print $cmd, "\n";

$code = system($cmd);
die "Corpus property extraction failed with code $code. Cmd was:\n   $cmd\n"
    if ($code);


# Extract some stuff from the the summary file  - Format: '<attribute>=<value>'
$sumryfile = "$emulationDir/${corpusName}_summary.txt";
die "Can't open $sumryfile\n"
    unless open S, $sumryfile;
while (<S>) {
    next if /^\s*#/;  # skip comments
    if (/(\S+)=(\S+)/) {
	$mimic{$1} = $2;
    }
}
close(S);


# Extract values for piecewise model of mimic document lengths

$cmd = "$doclenCmd $emulationDir/${corpusName}_docLenHist.tsv $corpusName $syntyp $tr_model $dl_model $dep_model\n";
print $cmd;
$rez = `$cmd`;
die "$doclenCmd failed with code $?.\n" if $?;
die "Can't match synth_dl_segments= option output\n$rez\n" 
    unless $rez =~ /(synth_dl_segments=\"[0-9.:,;]+\")/s;
$mimic_dl_segs_option = $1;
print "DL Piecewise Segs: $mimic_dl_segs_option\n";

#  --------------------------------------------------------------------------------------


# Use lsqcmd to compute unigram alpha for $syntyp
$lsq_stem = "$emulationDir/${corpusName}_vocab";
$cmd = "$lsqcmd $lsq_stem\n";
$lsqout = `$cmd`;
die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

print $lsqout;

if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
    $plot_elt_mimic{$syntyp} = $1;
    $plot_elt_mimic{$syntyp} =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
    $mimic{alpha} = $1;
    $plot_elt_mimic{$syntyp} =~ s/Linear fit/Fitted $syntyp mimic/;
} else {
    die "Error: Unable to extract stuff from $lsqcmd $lsq_stem output\n";
}

if ($dependencies eq "both" || $dependencies eq "mimic") {
    # Use lsqcmd to compute alpha for $syntyp _bigrams
    $lsq_stem = "$emulationDir/${corpusName}_bigrams";
    $cmd = "$lsqcmd $lsq_stem\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    print $lsqout;

    if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	$plot_elt_mimic_bg{$syntyp} = $1;
	$plot_elt_mimic_bg{$syntyp} =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
	$mimic{alpha_bigrams} = $1;
	$plot_elt_mimic_bg{$syntyp} =~ s/Linear fit/Fitted $syntyp mimic/;
    } else {
	warn "Warning: Unable to extract stuff from $lsqcmd $lsq_stem output\n";
    }

    # Use lsqcmd to compute alpha for $syntyp _repetitions
    $lsq_stem = "$emulationDir/${corpusName}_repetitions";
    $cmd = "$lsqcmd $lsq_stem\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    print $lsqout;

    if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	$plot_elt_mimic_re{$syntyp} = $1;
	$plot_elt_mimic_re{$syntyp} =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
	$mimic{alpha_repetitions} = $1;
	$plot_elt_mimic_re{$syntyp} =~ s/Linear fit/Fitted $syntyp mimic/;
    } else {
	warn "Warning: Unable to extract stuff from $lsqcmd $lsq_stem output\n";
    }


    $lsq_stem = "$emulationDir/${corpusName}_coocs";
    if (-e "$lsq_stem.plot") {
	# Use lsqcmd to compute alpha for $syntyp _cooccurs (Only if files exist)
	
	$cmd = "$lsqcmd $lsq_stem\n";
	$lsqout = `$cmd`;
	die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

	print $lsqout;

	if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	    $plot_elt_mimic_co{$syntyp} = $1;
	    $plot_elt_mimic_co{$syntyp} =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
	    $mimic{alpha_cooccurs} = $1;
	    $plot_elt_mimic_co{$syntyp} =~ s/Linear fit/Fitted $syntyp mimic/;
	} else {
	    warn "Warning: Unable to extract stuff from $lsqcmd $lsq_stem output\n";
	    $cooccurs = 0;
	}
    }
} else {
    print "Skipping word dependency analyses for mimic corpus\n";
}

die "Can't write to $emulationDir/${corpusName}_base_v_mimic_summary.txt\n"
    unless open RVM, ">$emulationDir/${corpusName}_base_v_mimic_summary.txt";

print RVM "\n                          Base v. Mimic";
print RVM "\n===============================================================\n";

foreach $k (sort keys %base) {
    $perc = "NA";
    if (defined($mimic{$k})) {
	$mim = $mimic{$k};
	$perc = sprintf("%+.1f%%", 100.0 * ($mimic{$k} - $base{$k}) / $base{$k})
	    unless !defined($base{$k}) || $base{$k} == 0;
    }
    else {$mim = "UNDEF";}
    print RVM sprintf("%27s", $k), "  $base{$k} v. $mim   ($perc)\n";
}

close(RVM);

#...............................................................................#
#         Now plot the term frequency distributions etc. for base and mimic     #
#...............................................................................#

$pcfile = "$emulationDir/${corpusName}_base_v_mimic_plot.cmds";
die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

print P "
set terminal pdf
set size ratio 0.7071
set xlabel \"Log10(rank)\"
set ylabel \"Log10(frequency)\"
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set pointsize 3
";
print P "
set output \"$emulationDir/${corpusName}_base_v_mimic_unigrams.pdf\"
plot \"${baseDir}/${corpusName}_vocab.plot\" title \"Base\" pt 7 ps 0.4, \"$emulationDir/${corpusName}_vocab.plot\" title \" $syntyp mimic\" pt 7 ps 0.17
";


    print P "
set output \"$emulationDir/${corpusName}_base_v_mimic_bigrams.pdf\"
plot \"${baseDir}/${corpusName}_bigrams.plot\" title \"Base\" pt 7 ps 0.4, \"$emulationDir/${corpusName}_bigrams.plot\" title \"$syntyp mimic\" pt 7 ps 0.17

set output \"$emulationDir/${corpusName}_base_v_mimic_repetitions.pdf\"
plot \"${baseDir}/${corpusName}_repetitions.plot\" title \"Base\" pt 7 ps 0.4, \"$emulationDir/${corpusName}_repetitions.plot\" title \"$syntyp mimic\" pt 7 ps 0.17

";

    print P "
set ylabel \"Distinct words per doc.\"
set xlabel \"Word occurrences per doc.\"
set output \"$emulationDir/${corpusName}_base_v_mimic_distinct_terms.pdf\"
plot \"${baseDir}/${corpusName}_termRatios.tsv\" title \"Base\" pt 7 ps 0.4, \"$emulationDir/${corpusName}_termRatios.tsv\" title \"$syntyp mimic\" pt 7 ps 0.17

";


if (-e "$emulationDir/coocs.plot") {
    print P "
set xlabel \"Log10(rank)\"
set ylabel \"Log10(frequency)\"
set output \"${experimentDir}/$syntyp/${lbl}/${corpusName}_base_v_mimic_cooccurs.pdf\"
plot \"${baseDir}/${corpusName}_cooccurs.plot\" title \"Base\" pt 7 ps 0.4, \"$emulationDir/${corpusName}_cooccurs.plot\" title \"$syntyp mimic\" pt 7 ps 0.17

";
}

close P;

if (defined($plotter)) {
    $rslt = `$plotter $pcfile > /dev/null 2>&1`;
    die "$plotter failed with code $? for $pcfile!\n$rslt\n" if $?;
} else {
    warn "\n\n$0: Warning: gnuplot not found.  PDFs of graphs will not be generated.\n\n";
}

    

#...........................................................................#
#         Now plot the document length distributions for base and mimic     #
#...........................................................................#

$cmd = "$plotDoclengths $corpusName $syntyp $tr_model $dl_model $dep_model\n";
$out = `$cmd`;
die "$cmd failed with code $?.\nCommand was $cmd.\n" if $?;


#...........................................................................#
#         Now plot the word length distributions for base and mimic     #
#...........................................................................#

$cmd = "$plotWdlengths $corpusName $syntyp $tr_model $dl_model $dep_model\n";
$out = `$cmd`;
die "$cmd failed with code $?.\nCommand was $cmd.\n" if $?;



print "

--------------------------- $syntyp --------------------------

Emulation done for $syntyp/$emuMethod.  

";


print "-----------------------------------------------------------------


";


mkdir "${experimentDir}/ComparisonPlots" unless -e "${experimentDir}/ComparisonPlots";
mkdir "${experimentDir}/ComparisonPlots/$emuMethod" unless -e "${experimentDir}/ComparisonPlots/$emuMethod";

# Finally, create a plotfile to compare the unigram and bigram frequency distributions
# in various ways.
    $pcfile = "${experimentDir}/ComparisonPlots/${emuMethod}/compare_tfds_plot.cmds";
    die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

    print P "
set terminal pdf
set size ratio 0.7071
set xlabel \"Log10(rank)\"
set ylabel \"Log10(frequency)\"
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set pointsize 3
";

if (-e "${baseDir}/${corpusName}_vocab.plot") {
    print P "
set output \"${experimentDir}/ComparisonPlots/${emuMethod}/${corpusName}_compare_tfds.pdf\"
plot \"${baseDir}/${corpusName}_vocab.plot\" title \"Unigrams (Base)\"  pt 7 ps 0.25, \"${baseDir}/${corpusName}_bigrams.plot\" title \"Bigrams (Base)\"  pt 7 ps 0.25, \"${baseDir}/${corpusName}_cooccurs.plot\" title \"Cooccurrences (Base)\"  pt 7 ps 0.25, \"${baseDir}/${corpusName}_repetitions.plot\" title \"Repeated terms (Base)\"  pt 7 ps 0.25 
";
} else {
     print P "
set output \"ComparisonPlots/${emuMethod}/${corpusName}_compare_tfds.pdf\"
plot \"${baseDir}/${corpusName}_vocab.plot\" title \"Unigrams (Base)\"  pt 7 ps 0.25, \"${baseDir}/${corpusName}_bigrams.plot\" title \"Bigrams (Base)\"  pt 7 ps 0.25, \"${baseDir}/${corpusName}_repetitions.plot\" title \"Repeated terms (Base)\"  pt 7 ps 0.25 
";
}  

close P;

if (defined($plotter)) {
    `$plotter $pcfile > /dev/null 2>&1`;
    die "$plotter failed with code $? for $pcfile!\n" if $?;
}

$cmd = "$compare_top_terms $corpusName $syntyp $tr_model $dl_model $dep_model $top_terms";
$out = `$cmd`;
print $out;   # Just a message saying how to print the comparison file
die "$cmd failed with code $?.\nCommand was $cmd.\n" if $?;

print "To see base v. mimic statistics: 

      cat ${emulationDir}/${corpusName}_base_v_mimic_summary.txt

    ";

if (defined($plotter)) {
    print  "To see comparison term-frequency-distributions for unigrams, bigrams, cooccurrences and repetitions:

    acroread $experimentDir/ComparisonPlots/${emuMethod}/${corpusName}_compare_tfds.pdf

To see fitting plots for the base collection:

";
    foreach $t ("vocab", "bigrams", "repetitions") {
	print "      acroread ${baseDir}/${corpusName}_${t}_lsqfit.pdf\n";
    }
    print "      acroread ${baseDir}/${corpusName}_cooccurs_lsqfit.pdf\n" 
	if -e "${baseDir}/${corpusName}_cooccurs_lsqfit.pdf";
    print "\n";

    print "To see base v. mimic comparisons for word length and doc length distributions:

    acroread ${emulationDir}/${corpusName}_base_v_mimic_wdlens.pdf
      acroread ${emulationDir}/${corpusName}_base_v_mimic_wdfreqs.pdf
      acroread ${emulationDir}/${corpusName}_base_v_mimic_doclens.pdf

";



    print "To see base v. mimic comparisons for unigrams, bigrams, cooccurrences and repetitions (if created):
 
";


    foreach $t ("unigrams", "bigrams", "repetitions", "distinct_terms") {
	print "      acroread ${emulationDir}/${corpusName}_base_v_mimic_$t.pdf\n";
    }
    print "      acroread ${emulationDir}/${corpusName}_base_v_mimic_cooccurs.pdf\n" 
	if -e "${emulationDir}/${corpusName}_base_v_mimic_cooccurs.pdf";

    print "\n";

} else {
    print "\nNote: production of PDF plots suppressed because $plotter not found.\n\n";
}


print "\nSuccessful exit from $0.\n";
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


sub get_compression_ratio {
    my $file = shift;
    my $dir = shift;   
    my $corpusName = shift;
    my $tmpFile = "$dir/$corpusName.gz";  # Choose a path which reduces risk of clash during
                                          # multiple simultaneous runs.
    my $cmd = "gzip -c $file > $tmpFile";
    my $code = `$cmd`;
    if ($code) {
	warn "Command $cmd failed with code $code\n";
	return "N/A";
    }

#    my $gzo = `gzip -l $tmpFile`;  # Produced wrong answers for a multi-gigabyte file
#    if ($?) {
#	warn "gzip -l failed with code $?\n";
#	unlink "$tmpFile";
#	return "N/A";
#    }
# 
    #    $gzo =~ /([0-9]+)\s+([0-9]+)\s+([0-9.]+%)/s;

    my $uncoSize = -s $file;
    my $coSize = -s $tmpFile;
    
    $ratio = sprintf("%.3f", $uncoSize / $coSize);
    
    print "


$tmpFile: compression ratio (uncompressed size : compressed size) is $ratio



";
    unlink $tmpFile;
    return $ratio;
}
