#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# Run in Experiments_emulation directory
# Plot the vocab.wdlens and vocab.wdfreqs files for base and mimic corpora

die "Usage: $0 <idxname> <tf model> <tr model> <dl model> <dependence model>

   e.g. $0 AS_top500k Piecewise markov-5e dlhisto " unless $#ARGV == 4;

use File::Copy "cp";
use Cwd;
$cwd = getcwd();
if ($cwd =~ m@/cygdrive@) {
    $cwd =~ s@/cygdrive/([a-zA-Z])/@$1:/@;
}

die "Usage: $0 <corpusName> <tfModel> <tr_model> <dl_model> <dependence_model>

     e.g. $0 AS_top500k Linear markov-5e dlhisto ngrams

  " unless $#ARGV == 4;

$experimentRoot = "$cwd";  # Assume we are run from src directory
$experimentRoot =~ s@/[^/]*$@@;  # strip off the last component of the path
$experimentRoot .= "/Experiments";
$experimentDir = "$experimentRoot/Emulation";

$corpusName = $ARGV[0];
$tfModel = $ARGV[1];
$tr_model = $ARGV[2];
$dl_model = $ARGV[3];
$dep_model = $ARGV[4];
${emulation_method} = "${tr_model}_${dl_model}_${dep_model}";
die "Emulation type  must be either 'dlnormal', 'dlsegs' (adaptive piecewise), 'dlgamma', or
dlhisto (read from a .doclenhist file)\n"
    unless $dl_model eq "dlsegs" || $dl_model eq "dlgamma" || $dl_model eq "dlnormal"
    || $dl_model eq "dlhisto";

print "
Note: You'll have to look in the logfiles to find out what term representation
parameters were used in generating .wdlens and .wdfreqs -- unless you remember of course

";

$base_dir = "$experimentRoot/Base";
$emu_dir = "$experimentDir/$tfModel/$emulation_method";

die "$0: Base directory $base_dir doesn't exist\n"
    unless -d $base_dir;

die "$0: Emulation directory $emu_dir doesn't exist\n"
    unless -d $emu_dir;
  
$wdl[0] = "$base_dir/${corpusName}_vocab.wdlens";
$wdl[1] = "$emu_dir/${corpusName}_vocab.wdlens";
$wdf[0] = "$base_dir/${corpusName}_vocab.wdfreqs";
$wdf[1] = "$emu_dir/${corpusName}_vocab.wdfreqs";


print 
"$0: Wdlens: $wdl[0], $wdl[1]
$0: Wdfreqs: $wdf[0], $wdf[1]
$0: Corpus name=$corpusName\n";



$pcfile = "${emu_dir}/${corpusName}_base_v_mimic_doclens_plot.cmds";
die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

print P "
set terminal pdf
set size ratio 0.7071
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set pointsize 3
";
    print P "
set xlabel \"Word length (Unicode characters)\"
set ylabel \"Probability (for distinct words)\"
set output \"${emu_dir}/${corpusName}_base_v_mimic_wdlens.pdf\"
plot \"$wdl[0]\" title \"Base\" pt 7 ps 0.4, \"$wdl[1]\" title \"Mimic\" pt 7 ps 0.17

set xlabel \"Word length\"
set ylabel \"Mean frequency\"
set logscale y
set output \"${emu_dir}/${corpusName}_base_v_mimic_wdfreqs.pdf\"
plot \"$wdf[0]\" title \"Base\" pt 7 ps 0.4, \"$wdf[1]\" title \"Mimic\" pt 7 ps 0.17
";

close P;

undef $plotter;
if (! (-x "/usr/bin/gnuplot")) {
    warn "\n\nWarning: /usr/bin/gnuplot not found.  PDFs of graphs will not be generated.\n";
} else {
    $plotter = "/usr/bin/gnuplot";
}

if (defined($plotter)) {
    $cmd = "$plotter $pcfile\n";
    $code = system($cmd);
    die "$cmd failed" if $code;
}


print "Plot commands in $pcfile. To view the PDFs use:

    acroread ${emu_dir}/${corpusName}_base_v_mimic_wdlens.pdf
    acroread ${emu_dir}/${corpusName}_base_v_mimic_wdfreqs.pdf


";
exit(0);
