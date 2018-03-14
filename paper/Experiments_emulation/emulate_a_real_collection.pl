#! /usr/bin/perl

# Post SIGIR-2016 version for Journal - makes use of new vocab.tfd file created by QBASH_vocab_lister

# Try to extract the characteristics from a real QBASHER collection, then 
# index a synthetic collection generated using those characteristics, and 
# check how accurate the simulation is.
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
# This version extracts parameters for both normal and gamma models of
# document length distribution but uses gamma.

# Now supports an optional correction for singleton over-generation


$perl = $^X;
$perl =~ s@\\@/@g;

use File::Copy "cp";
use Cwd;

$cwd = getcwd();
$wincwd = $cwd;
$wincwd =~ s@/cygdrive/([a-zA-Z])/@$1:/@;

$|++;

$QB_subpath = "GIT/Qbasher";
$ix_subpath = "QBASHER/indexes";
@QB_paths = (
    "C:/Users/dahawkin/GIT/Qbasher",  # Dave Laptop
    "D:/dahawkin/GIT/Qbasher",  # Redsar
    "S:/dahawkin/GIT/Qbasher",  # StCMHeavyMetal
    "F:/dahawkin/GIT/Qbasher",  # CCPRelSci00
    );

die "Usage: $0 <indexname or index path> dlnormal|dlsegs|dlgamma|dlhisto [-long_docs][-rebuild][-no_term_reps][-dl_adjust] [-suppress_extras] [-cooccurs] [-keep_indexes] [-overgen_factor=<float>]
    if -long_docs is given, QBASHI will index more than 255 words from long records.
    if -rebuild is given, indexes and other structures will be rebuilt even if there already.
    if -no_term_reps, words will be essentially numerical, with h, m, and s to distinguish head,
    middle and singletons.
    if -suppress_extras, the programs creating bigrams, and term repetitions will not be run.
    Cooccurrences are only run (very expensive) if the -cooccurs flag is given.
    The dl options allow specification of whether document length modeling should use
        a normal model, adaptive piecewise modeling, or a gamma distribution.
    if -dl_adjust, doc length parameters will be adjusted (only for dlnormal).
    if -overgen_factor, an attempt will be made to correct for the over-generation of singletons. Typical value 1.1.
\n"

    unless $#ARGV >= 1 ;

print "\n*** $0 @ARGV\n\n";

$opts = "-x_doc_length_histo=TRUE";
$rebuild = 0;
$cooccurs = 0;
$suppress = 0;
$keep_indexes = 0;
$dl_model = "dlnormal";
$dlm_adjustment = 1.0;
$dls_adjustment = 1.0;
$overgen_factor = 1;  


for ($a = 1; $a <=$#ARGV; $a++) {
    if ($ARGV[$a] eq "-long_docs") { $opts .= " -x_bigger_trigger=TRUE";}
    elsif ($ARGV[$a] eq "-no_term_reps") { 
	$opts .= " -x_zipf_generate_terms=FALSE";
	print "Artificial term representations will not be generated\n";
    } elsif ($ARGV[$a] eq "-cooccurs") { 
	$cooccurs = 1;
	print "Cooccurrence data will be generated\n";
    } elsif ($ARGV[$a] eq "-suppress_extras") { 
	$suppress = 1;
	print "Generation of extras will be suppressed\n";
    } elsif ($ARGV[$a] eq "-keep_indexes") { 
	$keep_indexes = 1;
	print "Indexes will be kept\n";
    } elsif ($ARGV[$a] eq "-dl_adjust") { $dlm_adjustment = 0.875; $dls_adjustment = 1.175;} 
    elsif ($ARGV[$a] eq "-rebuild") { 
	print "Indexes and vocab files will be rebuilt\n";
	$rebuild = 1;
    } elsif ($ARGV[$a] eq "dlnormal") { $dl_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "dlsegs") { $dl_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "dlgamma") { $dl_model = $ARGV[$a];}
    elsif ($ARGV[$a] eq "dlhisto") { $dl_model = $ARGV[$a];}
    elsif ($ARGV[$a] =~ /-overgen_factor=([0-9.]+)/) { 
	$dl_model = $2;
    } else {die "Unrecognized argument $ARGV[$a]\n";}
}

print "
Overgen_factor = $overgen_factor
DL_model = $dl_model;
DL_adjustment = $dl_adjust

";
  
$ix = $ARGV[0];
if (! -d ($idxdir = $ix)) {
    # Try prefixing paths for Dave's Laptop, Redsar and HeavyMetal
    for $p (@QB_paths) {
	$idxdir = "$p/$ix_subpath/$ix";
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


# The executables and scripts we need
$qbashi = check_exe("$qbp/src/qbashi/x64/Release/QBASHI.exe");
$vocablister = check_exe("$qbp/src/vocab_lister/x64/Release/QBASH_vocab_lister.exe");
$bigrammer = check_exe("$qbp/src/Bigrams_from_TSV/x64/Release/Bigrams_from_TSV.exe");
$coocc = check_exe("$qbp/src/Term_cooccurrences_from_TSV/x64/Release/Term_cooccurrences_from_TSV.exe");
$term_reps = check_exe("$qbp/src/TFdistribution_from_TSV/x64/Release/TFdistribution_from_TSV.exe");
$lsqcmd = check_exe("$cwd/lsqfit_general.pl");
$doclen_cmd = check_exe("$cwd/doclen_piecewise_modeling.pl");
$plot_doclengths = check_exe("$cwd/plot_doclengths.pl");
$gamma_lens = check_exe("$qbp/src/ModelDocumentLengthsWithGamma/x64/Release/ModelDocumentLengthsWithGamma.exe");
$bursty_terms = check_exe("$cwd/identify_bursty_words.pl");

$unique_terms = check_exe("$wincwd/01_getplot.py");

$fit_thresh = 0.02;

$head_terms = 10; 
$middle_segs = 10;

# If necessary, generate an index
if ($rebuild || (! (-f "$idxdir/QBASH.vocab")  || !(-f "$idxdir/index.log"))) {
    $cmd = "$qbashi -index_dir=$idxdir -sort_records_by_weight=FALSE -x_doc_length_histo=TRUE $opts > $idxdir/index.log\n";
    print $cmd;
    $code = system($cmd);
    die "Indexing failed with code $code.\n" if $code;
    unlink "$idxdir/vocab.tsv" 
	if -e "$idxdir/vocab.tsv";   # Don't risk using an old one
} else {
    print "Work saved: Real index already exists.\n";
}

# Extract some stuff from the tail of the real index.log
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
} else {
    die "Error: Unable to extract stuff from real index.log\n$logtail\n

Suggestion: Rerun this command with -rebuild.   (Maybe it has been rebuilt with the wrong options.)\n\n";
}


#  -------------------  Extracting data for document length models ---------------------
$real{doclength} = sprintf("%.2f", $real{postings} / $real{docs});  # Should = $real{doclen_mean}

# Extract values for approximating gamma function for document lengths
$cmd = "$gamma_lens $idxdir/QBASH.doclenhist\n";
print $cmd;
$rez = `$cmd`;
die "Gamma distribution estimation failed with code $code.\n" if $code;
die "Can't match gamma params from $gamma_lens output\n$rez\n" 
    unless $rez =~ /Gamma shape: ([0-9.]+);  scale: ([0-9.]+)/s;
$real{gamma_shape} = sprintf("%.4f", $1);
$real{gamma_scale} = sprintf("%.4f", $2);

print "Gamma: shape = $real{gamma_shape}; scale = $real{gamma_scale}\n";


# Extract values for piecewise model of document lengths

$cmd = "$doclen_cmd $idxname\n";
print $cmd;
$rez = `$cmd`;
die "$doclen_cmd failed with code $?.\n$rez" if $?;

die "Can't match -x_synth_dl_segments= option output\n$rez\n" 
    unless $rez =~ /(-x_synth_dl_segments=\"[0-9.:,;]+\")/s;

$real_dl_segs_option = $1;
print "DL Piecewise Segs: $real_dl_segs_option\n";

#  --------------------------------------------------------------------------------------


# Build QBASH.bigrams (if desired and necessary)
if (! $suppress) {
    if ($rebuild || (! (-f "$idxdir/QBASH.bigrams"))) {
	$cmd = "$bigrammer $idxdir/QBASH.forward\n";
	print $cmd;
	$code = system($cmd);
	die "Bigram generation failed with code $code.\n" if $code;
    } else {
	print "Work saved: Real QBASH.bigrams file already exists.\n";
    }


    # ----  Always have to do repetitions.  (Or we could recode to avoid the ---- 
    # ----  run $term_reps twice.                                            ----
    # We run $term_reps twice (with different options), first to identify 
    # bursty terms, and then to generate Nick graphs and Zipfian plots

    $cmd = "$term_reps $idxdir/QBASH.forward -singletons_too\n";
    print $cmd;
    $code = system($cmd);
    die "Term repetition generation -singletons_too failed with code $code.\n" if $code;

    $cmd = "$vocablister $idxdir/QBASH.repetitions sort=alpha\n";
    print $cmd;
    $code = system($cmd);
    die "Repetitions generation failed with code $code.\n" if $code;

    $cmd = "$bursty_terms $idxdir > $idxdir/bursties.tsv\n";
    print $cmd;
    $code = system($cmd);
    die "$bursty_terms failed with code $code.\n" if $code;


    # Second go for $term_reps.  Note that this overwrites QBASH.repetitions
    $cmd = "$term_reps $idxdir/QBASH.forward\n";
    print $cmd;
    $code = system($cmd);
    die "Term repetition generation failed with code $code.\n" if $code;

    # Collect data for Nick's graphs
    $cmd = "$unique_terms $idxdir/term_ratios.tsv\n";
    print $cmd;
    $code = system($cmd);
    die "$unique_terms generation failed with code $code.\n" if $code;
    print "Created: $idxdir/term_ratios.tsv\n";

}


if ($cooccurs) {
    if ($rebuild || (! (-f "$idxdir/QBASH.cooccurs"))) {
	$cmd = "$coocc $idxdir/QBASH.forward\n";
	print $cmd;
	$code = system($cmd);
	die "Cooccurrence generation failed with code $code.\n" if $code;
    } else {
	print "Work saved: Real QBASH.cooccurs file already exists.\n";
    }
}


# If necessary, generate vocab.tsv.  Note that this now builds vocab.tfd (term freq 
# dist), vocab.plot (a subset of data in log log space, for plotting.) and
# vocab.segdat
if ($rebuild || (! (-f "$idxdir/vocab.tsv")) || (! (-f "$idxdir/vocab.tfd"))
    || (! (-f "$idxdir/vocab.plot")) || (! (-f "$idxdir/vocab.segdat"))) {
    $cmd = "$vocablister $idxdir/QBASH.vocab\n";
    print $cmd;
    $code = system($cmd);
    die "Vocab generation failed with code $code.\n" if $code;
} else {
    print "Work saved: Real vocab.* files already exist.\n";
}

if (! $suppress) {
    # If desired and necessary, convert binary extras files to TSV format
    if ($rebuild || (! (-f "$idxdir/bigrams.tsv")) ||  (! (-f "$idxdir/bigrams.tfd"))
	|| (! (-f "$idxdir/bigrams.plot")) || (! (-f "$idxdir/bigrams.segdat"))) {
	$cmd = "$vocablister $idxdir/QBASH.bigrams\n";
	print $cmd;
	$code = system($cmd);
	die "Vocab generation failed with code $code.\n" if $code;
    } else {
	print "Work saved: Real bigrams.* files already exist.\n";
    }
}

if ($cooccurs) {
    if ($rebuild || (! (-f "$idxdir/cooccurs.tsv")) ||  (! (-f "$idxdir/cooccurs.tfd"))
	|| (! (-f "$idxdir/cooccurs.plot")) || (! (-f "$idxdir/cooccurs.segdat"))) {
	$cmd = "$vocablister $idxdir/QBASH.cooccurs\n";
	print $cmd;
	$code = system($cmd);
	die "Vocab generation failed with code $code.\n" if $code;
    } else {
	print "Work saved: Real cooccurs.* files already exist.\n";
    }
}


mkdir "Real" unless -d "Real";  # A directory to save stuff from the real collection.  

# Copy files vocab.* files to current directory.
cp("$idxdir/vocab.tfd", "Real/$idxname.tfd") or die "Copy failed1: $!\n";
cp("$idxdir/vocab.plot", "Real/$idxname.plot") or die "Copy failed2: $!\n";
cp("$idxdir/vocab.segdat", "Real/$idxname.segdat") or die "Copy failed3: $!\n";

if (! $suppress) {
    cp("$idxdir/bigrams.tfd", "Real/${idxname}_bigrams.tfd") or die "Copy failedB1: $!\n";
    cp("$idxdir/bigrams.plot", "Real/${idxname}_bigrams.plot") or die "Copy failedB2: $!\n";
    cp("$idxdir/bigrams.segdat", "Real/${idxname}_bigrams.segdat") or die "Copy failedB3: $!\n";


    cp("$idxdir/repetitions.tfd", "Real/${idxname}_repetitions.tfd") or die "Copy failedR1: $!\n";
    cp("$idxdir/repetitions.plot", "Real/${idxname}_repetitions.plot") or die "Copy failedR2: $!\n";
    cp("$idxdir/repetitions.segdat", "Real/${idxname}_repetitions.segdat") or die "Copy failedR3: $!\n";
    cp("$idxdir/term_ratios.tsv", "Real/${idxname}_term_ratios.tsv") or die "Copy failedR4: $!\n";
    cp("$idxdir/term_ratios.out", "Real/${idxname}_term_ratios.out") or die "Copy failedR5: $!\n";
}

if ($cooccurs) {
    cp("$idxdir/cooccurs.tfd", "Real/${idxname}_cooccurs.tfd") or die "Copy failedC1: $!\n";
    cp("$idxdir/cooccurs.plot", "Real/${idxname}_cooccurs.plot") or die "Copy failedC2: $!\n";
    cp("$idxdir/cooccurs.segdat", "Real/${idxname}_cooccurs.segdat") or die "Copy failedC3: $!\n";
}

print "

Index name: $idxname

";


# Use lsqcmd to compute alpha for unigrams
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
    die "Error: Unable to extract stuff from base corpus $lsqcmd output\n";
}

if (! $suppress) {
# Use lsqcmd to compute alpha for bigrams
    $cmd = "$lsqcmd Real/${idxname}_bigrams\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    print $lsqout;


    if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	$plot_elt_real_bg = $1;
	$plot_elt_real_bg =~ /([\-0-9.]+)\s*\*x\s*title "Linear fit"/;
	$real{alpha_bg} = $1;
	$plot_elt_real_bg =~ s/Linear fit/Fitted real/;
    } else {
	die "Error: Unable to extract stuff from base corpus bigrams $lsqcmd output\n";
    }

# Use lsqcmd to compute alpha for repetitions
    $cmd = "$lsqcmd Real/${idxname}_repetitions\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    print $lsqout;


    if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	$plot_elt_real_re = $1;
	$plot_elt_real_re =~ /([\-0-9.]+)\s*\*x\s*title "Linear fit"/;
	$real{alpha_re} = $1;
	$plot_elt_real_re =~ s/Linear fit/Fitted real/;
    } else {
	die "Error: Unable to extract stuff from base corpus repetitions $lsqcmd output\n";
    }
}


if ($cooccurs) {
# Use lsqcmd to compute alpha for cooccurs
    $cmd = "$lsqcmd Real/${idxname}_cooccurs\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    print $lsqout;


    if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	$plot_elt_real_co = $1;
	$plot_elt_real_co =~ /([\-0-9.]+)\s*\*x\s*title "Linear fit"/;
	$real{alpha_co} = $1;
	$plot_elt_real_co =~ s/Linear fit/Fitted real/;
    } else {
	die "Error: Unable to extract stuff from base corpus cooccurs $lsqcmd output\n";
    }
}

####################################################################################################
#                                  Do emulation, possibly for multiple models                      #
####################################################################################################

$dl_mean = $real{doclen_mean} * $dlm_adjustment;
$dl_stdev = $real{doclen_stdev} * $dls_adjustment;

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

if ($dl_model eq "dlnormal") {
    $doclenopts = "-x_synth_doc_length=$dl_mean -x_synth_doc_length_stdev=$dl_stdev";
} elsif ($dl_model eq "dlgamma") {
    $doclenopts = "-x_synth_dl_gamma_shape=$real{gamma_shape} -x_synth_dl_gamma_scale=$real{gamma_scale}";
} elsif ($dl_model eq "dlsegs") {
    $doclenopts = $real_dl_segs_option;
} elsif ($dl_model eq "dlhisto") {
    $doclenopts = "-x_synth_dl_read_histo=$idxdir/QBASH.doclenhist";
}



$base_cmd = "$qbashi index_dir=ZIPF-GENERATOR $doclenopts -x_synth_postings=$options{-x_synth_postings} -x_synth_vocab_size=$options{-x_synth_vocab_size} -x_zipf_tail_perc=$options{-x_zipf_tail_perc} -x_head_term_percentages=$options{-x_head_term_percentages} -x_file_synth_docs=QBASH.forward $opts";

$base_cmd .= " -x_synth_overgen_adjust=$overgen_factor"  
    if ($overgen_factor > 1.0);

$lbl = "${idxname}_${dl_model}";

foreach $syntyp (
    #"Linear",    # Don't bother with this unless we specifically need it
    "Piecewise") {
    undef %mimic;


    #..................................................................................................#
    #        index with those characteristics in subdirectory $syntyp and generate vocab.tsv           #
    #..................................................................................................#

    mkdir $syntyp unless -d $syntyp;  # A directory in which to do all our work  


    chdir $syntyp;    # CHDIR ===================>
    $cmd = $base_cmd;
    if ($syntyp eq "Piecewise") {
	$cmd .= " -x_zipf_middle_pieces=$options{-x_zipf_middle_pieces}";
    } else {
	$cmd .= " -x_zipf_alpha=$real{alpha}";
    }

    die "Can't open $syntyp/${lbl}_index.cmd\n" 
	unless open QC, ">${lbl}_index.cmd";
    print QC $cmd, "\n";
    close QC;

    $cmd .=  " > ${lbl}_index.log\n";
    print $cmd;
    $code = system($cmd);
    die "Indexing failed with code $code.\nCmd was $cmd\n" if $code;


    # Extract values for approximating gamma function for mimic document lengths
    $cmd = "$gamma_lens QBASH.doclenhist\n";
    print $cmd;
    $rez = `$cmd`;
    die "Gamma distribution estimation failed with code $code.\n" if $code;
    die "Can't match gamma params from $gamma_lens output\n$rez\n" 
	unless $rez =~ /Gamma shape: ([0-9.]+);  scale: ([0-9.]+)/;
    $mimic{gamma_shape} = sprintf("%.4f", $1);
    $mimic{gamma_scale} = sprintf("%.4f", $2);


    # Extract values for piecewise model of mimic document lengths

    $cmd = "$doclen_cmd .\n";
    print $cmd;
    $rez = `$cmd`;
    die "$doclen_cmd failed with code $?.\n" if $?;
    die "Can't match -x_synth_dl_segments= option output\n$rez\n" 
	unless $rez =~ /(-x_synth_dl_segments=\"[0-9.:,;]+\")/s;
    $mimic_dl_segs_option = $1;
    print "DL Piecewise Segs: $mimic_dl_segs_option\n";

#  --------------------------------------------------------------------------------------


    if (! $suppress) {
	# Build QBASH.bigrams
	$cmd = "$bigrammer QBASH.forward\n";
	print $cmd;
	$code = system($cmd);
	die "Bigram generation failed with code $code.\n" if $code;

	# We run $term_reps twice (with different options), first to identify 
	# bursty terms, and then to generate Nick graphs and Zipfian plots

	$cmd = "$term_reps QBASH.forward -singletons_too\n";
	print $cmd;
	$code = system($cmd);
	die "Term repetition generation -singletons_too failed with code $code.\n" if $code;

	$cmd = "$vocablister QBASH.repetitions\n";
	print $cmd;
	$code = system($cmd);
	die "Repetitions generation failed with code $code.\n" if $code;

	$cmd = "$bursty_terms . $lbl > bursties.tsv\n";
	print $cmd;
	$code = system($cmd);
	die "$bursty_terms failed with code $code.\n" if $code;


	# Second go for $term_reps to build QBASH.repetitions
	$cmd = "$term_reps QBASH.forward\n";
	print $cmd;
	$code = system($cmd);
	die "Repetitions generation failed with code $code.
  Command was $cmd\n" if $code;
	$cmd = "$unique_terms term_ratios.tsv\n";
	print $cmd;
	$code = system($cmd);
	die "$unique_terms generation failed with code $code.
  Command was $cmd\n" if $code;
	print "Created: term_ratios.out\n";

    }

    if ($cooccurs) {
	# Build QBASH.cooccurs
	$cmd = "$coocc QBASH.forward\n";
	print $cmd;
	$code = system($cmd);
	die "Cooccurs generation failed with code $code.
  Command was $cmd\n" if $code;
    }

    chdir "..";  # <========================== CHDIR ==>

    $cmd = "$vocablister $syntyp/QBASH.vocab\n";
    print $cmd;
    $code = system($cmd);
    die "Vocab generation failed for $syntyp with code $code.\n" if $code;


    die "Rename to $syntyp/${idxname}_vocab.tfd failed\n"
	unless rename "$syntyp/vocab.tfd", "$syntyp/${idxname}.tfd";
    die "Rename to $syntyp/${idxname}_vocab.plot failed\n"
	unless rename "$syntyp/vocab.plot", "$syntyp/${idxname}.plot";
    die "Rename to $syntyp/${idxname}_vocab.segdat failed\n"
	unless rename "$syntyp/vocab.segdat", "$syntyp/${idxname}.segdat";
    die "Rename to $syntyp/${lbl}_QBASH.doclenhist failed\n"
	unless rename "$syntyp/QBASH.doclenhist", "$syntyp/${lbl}_QBASH.doclenhist";
    die "Rename to $syntyp/${lbl}_bursties.tsv failed\n"
	unless rename "$syntyp/bursties.tsv", "$syntyp/${lbl}_bursties.tsv";

    print "  vocab.* files renamed to $syntyp/${idxname}_vocab.*
  $syntyp/QBASH.doclenhist renamed to $syntyp/${idxname}_QBASH.doclenhist
\n";

    if (! $suppress) {
	$cmd = "$vocablister $syntyp/QBASH.bigrams\n";
	print $cmd;
	$code = system($cmd);
	die "Bigram.tsv generation failed for $syntyp with code $code.\n" if $code;

	die "Rename to $syntyp/${idxname}_bigrams.tfd failed\n"
	    unless rename "$syntyp/bigrams.tfd", "$syntyp/${idxname}_bigrams.tfd";
	die "Rename to $syntyp/${idxname}_bigrams.plot failed\n"
	    unless rename "$syntyp/bigrams.plot", "$syntyp/${idxname}_bigrams.plot";
	die "Rename to $syntyp/${idxname}_bigrams.segdat failed\n"
	    unless rename "$syntyp/bigrams.segdat", "$syntyp/${idxname}_bigrams.segdat";

	$cmd = "$vocablister $syntyp/QBASH.repetitions\n";
	print $cmd;
	$code = system($cmd);
	die "Repetitions.tsv generation failed for $syntyp with code $code.\n" if $code;

	die "Rename to $syntyp/${idxname}_repetitions.tfd failed\n"
	    unless rename "$syntyp/repetitions.tfd", "$syntyp/${idxname}_repetitions.tfd";
	die "Rename to $syntyp/${idxname}_repetitions.plot failed\n"
	    unless rename "$syntyp/repetitions.plot", "$syntyp/${idxname}_repetitions.plot";
	die "Rename to $syntyp/${idxname}_repetitions.segdat failed\n"
	    unless rename "$syntyp/repetitions.segdat", "$syntyp/${idxname}_repetitions.segdat";
	die "Rename to $syntyp/${idxname}_term_ratios.tsv failed\n"
	    unless rename "$syntyp/term_ratios.tsv", "$syntyp/${idxname}_term_ratios.tsv";
	die "Rename to $syntyp/${idxname}_term_ratios.out failed\n"
	    unless rename "$syntyp/term_ratios.out", "$syntyp/${idxname}_term_ratios.out";

    }

    if ($cooccurs) {
	$cmd = "$vocablister $syntyp/QBASH.cooccurs\n";
	print $cmd;
	$code = system($cmd);
	die "Cooccurs.tsv generation failed for $syntyp with code $code.\n" if $code;

	die "Rename to $syntyp/${idxname}_cooccurs.tfd failed\n"
	    unless rename "$syntyp/cooccurs.tfd", "$syntyp/${idxname}_cooccurs.tfd";
	die "Rename to $syntyp/${idxname}_cooccurs.plot failed\n"
	    unless rename "$syntyp/cooccurs.plot", "$syntyp/${idxname}_cooccurs.plot";
	die "Rename to $syntyp/${idxname}_cooccurs.segdat failed\n"
	    unless rename "$syntyp/cooccurs.segdat", "$syntyp/${idxname}_cooccurs.segdat";
    }

    #..................................................................................................#
    #                         Now extract the properties of the $syntyp index                          #
    #..................................................................................................#


    # Extract some stuff from the tail of the mimic ${lbl}_index.log
    $cmd = "tail -20 $syntyp/${lbl}_index.log\n";
    print $cmd;
    $logtail = `$cmd`;
    $code = $?;
    die "tail of mimic ${lbl}_index.log failed with code $code.\n" if $code;
    if ($logtail =~ /Records \(excluding ignoreds\):\s+([0-9]+).*?Record lengths: Mean:\s+([0-9.]+); St. Dev:\s+([0-9.]+) words.*?building:\s+([0-9.]+) sec.*?traversal:\s+([0-9.]+) sec.*?Collisions per posting:\s+([0-9.]+);.*?chunks allocated:\s+([0-9.]+).*?Distinct words:\s+([0-9.]+);.*Total postings:\s+([0-9.]+);.*Longest postings list:\s+([0-9.]+).*Indexing rate:\s+([0-9.]+)/s) {
	$mimic{docs} = $1;
	$mimic{doclen_mean} = $2;
	$mimic{doclen_stdev} = $3;
	$mimic{listbuild_time} = $4;
	$mimic{listtraversal_time} = $5;
	$mimic{collisions_per_insertion} = $6;
	$mimic{chunks} = $7;
	$mimic{vocab_size} = $8;
	$mimic{postings} = $9;
	$mimic{longest_list} = $10;
	$mimic{indexing_rate} = $11;
    } else {
	die "Unable to extract stuff from $syntyp ${lbl}_index.log. Tail was:\n$logtail\n";
    }

   $mimic{doclength} = sprintf("%.2f", $mimic{postings} / $mimic{docs});


    # Use lsqcmd to compute alpha for $syntyp
    $cmd = "$lsqcmd $syntyp/$idxname\n";
    $lsqout = `$cmd`;
    die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

    print $lsqout;

    if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	$plot_elt_mimic{$syntyp} = $1;
	$plot_elt_mimic{$syntyp} =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
	$mimic{alpha} = $1;
	$plot_elt_mimic{$syntyp} =~ s/Linear fit/Fitted $syntyp mimic/;
    } else {
	die "Error: Unable to extract stuff from $syntyp $lsqcmd output\n";
    }

    if (! $suppress) {
	# Use lsqcmd to compute alpha for $syntyp _bigrams
	$cmd = "$lsqcmd $syntyp/${idxname}_bigrams\n";
	$lsqout = `$cmd`;
	die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

	print $lsqout;

	if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	    $plot_elt_mimic_bg{$syntyp} = $1;
	    $plot_elt_mimic_bg{$syntyp} =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
	    $mimic{alpha_bigrams} = $1;
	    $plot_elt_mimic_bg{$syntyp} =~ s/Linear fit/Fitted $syntyp mimic/;
	} else {
	    die "Error: Unable to extract stuff from $syntyp _bigrams $lsqcmd output\n";
	}

	# Use lsqcmd to compute alpha for $syntyp _repetitions
	$cmd = "$lsqcmd $syntyp/${idxname}_repetitions\n";
	$lsqout = `$cmd`;
	die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

	print $lsqout;

	if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	    $plot_elt_mimic_re{$syntyp} = $1;
	    $plot_elt_mimic_re{$syntyp} =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
	    $mimic{alpha_repetitions} = $1;
	    $plot_elt_mimic_re{$syntyp} =~ s/Linear fit/Fitted $syntyp mimic/;
	} else {
	    die "Error: Unable to extract stuff from $syntyp _repetitions $lsqcmd output\n";
	}

    }

    if ($cooccurs) {
	# Use lsqcmd to compute alpha for $syntyp _cooccurs
	$cmd = "$lsqcmd $syntyp/${idxname}_cooccurs\n";
	$lsqout = `$cmd`;
	die "$lsqcmd failed with code $?.\nCommand was $cmd.\n" if $?;

	print $lsqout;

	if ($lsqout =~ /,(.*?\s[\-0-9.]+\s*\*x\s*title "Linear fit")/s){
	    $plot_elt_mimic_co{$syntyp} = $1;
	    $plot_elt_mimic_co{$syntyp} =~ /(-[0-9.]+)\s*\*x\s*title "Linear fit"/;
	    $mimic{alpha_cooccurs} = $1;
	    $plot_elt_mimic_co{$syntyp} =~ s/Linear fit/Fitted $syntyp mimic/;
	} else {
	    die "Error: Unable to extract stuff from $syntyp _cooccurs $lsqcmd output\n";
	}
    }

    die "Can't write to $syntyp${idxname}_real_v_mimic_summary.txt\n"
	unless open RVM, ">$syntyp/${idxname}_real_v_mimic_summary.txt";

    print RVM "\n                              Real v. Mimic";
    print RVM "\n==================================================\n";

    foreach $k (sort keys %real) {
	$perc = "NA";
	if (defined($mimic{$k})) {
	    $mim = $mimic{$k};
	    $perc = sprintf("%+.1f%%", 100.0 * ($mimic{$k} - $real{$k}) / $real{$k})
		unless !defined($real{$k}) || $real{$k} == 0;
	}
	else {$mim = "UNDEF";}
	print RVM sprintf("%27s", $k), "  $real{$k} v. $mim   ($perc)\n";
    }

    close(RVM);

#...............................................................................#
#         Now plot the term frequency distributions etc. for real and mimic     #
#...............................................................................#

    $pcfile = "$syntyp/${idxname}_real_v_mimic_plot.cmds";
    die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

    print P "
set terminal pdf
set size ratio 0.7071
set xlabel \"Log(rank)\"
set ylabel \"Log(probability)\"
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
set output \"$syntyp/${idxname}_real_v_mimic_unigrams.pdf\"
plot \"Real/${idxname}.plot\" title \"Real\" pt 7 ps 0.25, $plot_elt_real ls 1, \"$syntyp/$syntyp_${idxname}.plot\" title \" $syntyp mimic\" pt 7 ps 0.25, $plot_elt_mimic{$syntyp} ls 3
";


    if (! $suppress) {

	print P "
set output \"$syntyp/${idxname}_real_v_mimic_bigrams.pdf\"
plot \"Real/${idxname}_bigrams.plot\" title \"Real\" pt 7 ps 0.25, $plot_elt_real_bg ls 1, \"$syntyp/$syntyp_${idxname}_bigrams.plot\" title \"$syntyp mimic\" pt 7 ps 0.25, $plot_elt_mimic_bg{$syntyp} ls 3

set output \"$syntyp/${idxname}_real_v_mimic_repetitions.pdf\"
plot \"Real/${idxname}_repetitions.plot\" title \"Real\" pt 7 ps 0.25, $plot_elt_real_re ls 1, \"$syntyp/$syntyp_${idxname}_repetitions.plot\" title \"$syntyp mimic\" pt 7 ps 0.25, $plot_elt_mimic_re{$syntyp} ls 3

";

	print P "
set ylabel \"Distinct words per doc.\"
set xlabel \"Word occurrences per doc.\"
set output \"$syntyp/${idxname}_real_v_mimic_distinct_terms.pdf\"
plot \"Real/${idxname}_term_ratios.out\" title \"Real\" pt 7 ps 0.25, x title \"\", \"$syntyp/${idxname}_term_ratios.out\" title \"$syntyp mimic\" pt 7 ps 0.25

";
    }

    if ($cooccurs) {
	print P "
set xlabel \"Log(rank)\"
set ylabel \"Log(probability)\"
set output \"$syntyp/${idxname}_real_v_mimic_cooccurs.pdf\"
plot \"Real/${idxname}_cooccurs.plot\" title \"Real\" pt 7 ps 0.25, $plot_elt_real_co ls 1, \"$syntyp/$syntyp_${idxname}_cooccurs.plot\" title \"$syntyp mimic\" pt 7 ps 0.25, $plot_elt_mimic_co{$syntyp} ls 3

";
    }


    close P;

    `gnuplot $pcfile > /dev/null 2>&1`;
    die "Gnuplot failed with code $? for $pcfile!\n" if $?;

#...........................................................................#
#         Now plot the document length distributions for real and mimic     #
#...........................................................................#

    $cmd = "$plot_doclengths $idxname $dl_model\n";
    $out = `$cmd`;
    die "$cmd failed with code $?.\nCommand was $cmd.\n" if $?;
    
    print "To compare real v. mimic doc length distributions:

        acroread Piecewise/${idxname}_${dl_model}_real_v_mimic_doclens.pdf

";

    print "
  --------------------------- $syntyp --------------------------

Emulation done for $syntyp.  A summary of comparisons between real and 
emulated collection is in $syntyp/${idxname}_real_v_mimic_summary.txt.

";


    # Clean up the index files (unless instructed not to)
    if (! $keep_indexes) {
	for $f ("$syntyp/vocab.tsv", glob "$syntyp/QBASH.*") {
	    print "  removing $f\n";
	    unlink $f;
	}  
    } else {
	print "QBASH.* and vocab.tsv kept in $syntyp\n";
    }

    print "  -----------------------------------------------------------------

";

print "Bursty terms for the synthetic collection are listed in 
$syntyp/${lbl}_bursties.tsv. For the real one look in 
$idxdir/bursties.tsv\n\n";



    
}  # --- end of loop over Syn Types

if (! $suppress) {

mkdir "ComparisonPlots" unless -e "ComparisonPlots";

# Finally, create a plotfile to compare the unigram and bigram frequency distributions
# in various ways.
    $pcfile = "ComparisonPlots/${idxname}_compare_tfds_plot.cmds";
    die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

    print P "
set terminal pdf
set size ratio 0.7071
set xlabel \"Log(rank)\"
set ylabel \"Log(probability)\"
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
set output \"ComparisonPlots/${idxname}_compare_tfds.pdf\"
plot \"Real/${idxname}.plot\" title \"Unigrams (real)\"  pt 7 ps 0.25, \"Real/${idxname}_bigrams.plot\" title \"Bigrams (real)\"  pt 7 ps 0.25, \"Real/${idxname}_cooccurs.plot\" title \"Cooccurrences (real)\"  pt 7 ps 0.25, \"Real/${idxname}_repetitions.plot\" title \"Repeated terms (real)\"  pt 7 ps 0.25 
";

close P;

`gnuplot $pcfile > /dev/null 2>&1`;
die "Gnuplot failed with code $? for $pcfile!\n" if $?;

print  "
To see comparison term-frequency-distributions for unigrams, bigrams, cooccurrences and repetitions:

      acroread ComparisonPlots/${idxname}_compare_tfds.pdf

The linear and quadratic fitting plots for the real collection can be seen via:

";
    foreach $t ("", "_bigrams", "_cooccurs", "_repetitions") {
	print "      acroread Real/${idxname}${t}_lsqfit.pdf\n";
    }

print "

To see real v. mimic comparisons for unigrams, bigrams, cooccurrences and repetitions (if created):
 
";
}

foreach $syntyp (
    #"Linear", 
    "Piecewise") {
    foreach $t ("unigrams", "bigrams", "cooccurs", "repetitions", 
		"distinct_terms") {
	print "      acroread $syntyp/${idxname}_real_v_mimic_$t.pdf\n";
    }
}

print "\n";




print "\nSuccessful exit from $0.\n";
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


