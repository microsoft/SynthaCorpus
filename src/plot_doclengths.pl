#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# Run in Experiments/Emulation directory
# Plot the doclenhist files for real and mimic versions of the same index

use File::Copy "cp";
use Cwd;
$cwd = getcwd();
if ($cwd =~ m@/cygdrive@) {
    $cwd =~ s@/cygdrive/([a-zA-Z])/@$1:/@;
}

die "Usage: $0 <corpus_name> <tfModel> <trModel> <dlModel> <dependence_model>

     e.g. $0 AS_top500k Linear markov-5e dlhisto ngrams

  " unless $#ARGV == 4;

$plotter = `which gnuplot 2>&1`;
chomp($plotter);
$plotter .= '.exe' if ($^O =~ /cygwin/i || $^O =~ /MSWin/i);
undef $plotter if $plotter =~/^which: no/;

$experimentRoot = $cwd;  # Assume we are run from the src directory
$experimentRoot =~ s@/[^/]*$@@;  # strip off the last component of the path
$experimentRoot .= "/Experiments";
$experimentDir = "$experimentRoot/Emulation";

$corpusName = $ARGV[0];
$tfModel = $ARGV[1];
$trModel = $ARGV[2];
$dlModel = $ARGV[3];
$depModel = $ARGV[4];
${emulationMethod} = "${trModel}_${dlModel}_${depModel}";
die "Doc length model must be either 'dlnormal', 'dlsegs' (adaptive piecewise), 'dlgamma', or
dlhisto (read from a .doclenhist file)\n"
    unless $dlModel eq "dlsegs" || $dlModel eq "dlgamma" || $dlModel eq "dlnormal"
    || $dlModel eq "dlhisto";

$baseDir = "$experimentRoot/Base";
$emuDir = "$experimentDir/$tfModel/$emulationMethod";

die "$0: Base directory $baseDir doesn't exist\n"
    unless -d $baseDir;

die "$0: Emulation directory $emuDir doesn't exist\n"
    unless -d $emuDir;
  
$dlh[0] = "$baseDir/${corpusName}_docLenHist.tsv";
$dlh[1] = "$emuDir/${corpusName}_docLenHist.tsv";

print "$0: Doclenhists: $dlh[0], $dlh[1]\n$0:Corpus Name = $corpusName\n";

$T = "$baseDir/$corpusName.tmp";
$U = "$emuDir/$corpusName.tmp";

avoid_zero_data_in_file($dlh[0], $T);
avoid_zero_data_in_file($dlh[1], $U);


$pcfile = "$emuDir/${corpusName}_base_v_mimic_doclens_plot.cmds";
die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

print P "
set terminal pdf
set size ratio 0.7071
set xlabel \"Doc Length\"
set ylabel \"Frequency\"
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set pointsize 3
";
    print P "
set output \"$emuDir/${corpusName}_base_v_mimic_doclens.pdf\"
plot \"$T\" title \"Base\" pt 7 ps 0.4, \"$U\" title \"Mimic\" pt 7 ps 0.17
";

close P;


if (defined($plotter)) {
    $cmd = "$plotter $pcfile\n";
    $code = system($cmd);
    die "$cmd failed" if $code;


    print "Plot commands in $pcfile (but tmp data deleted). To view the PDF use:

    acroread $emuDir/${corpusName}_base_v_mimic_doclens.pdf
\n";
} else {
    warn "\n\nWarning: gnuplot not found.  PDFs of graphs will not be generated.\n\n";
}


# Remove temporary files
unlink $T;
unlink $U;

exit(0);


# -----------------------------------------------------------------------------

sub avoid_zero_data_in_file {
    my $inn = shift;
    my $outt = shift;

    print "\nOpening $inn and >$outt\n\n";
    die "Can't open $inn" unless open I, $inn;
    die "Can't open $outt" unless open O, ">$outt";
    while (<I>) {
	next unless /^\s*([0-9]+)\t\s*([0-9]+)/;
	next if $1 == 0;  # Zero length
	next if $2 == 0;  # Zero freq
	print O $_;
    }
    close I;
    close O;
}
