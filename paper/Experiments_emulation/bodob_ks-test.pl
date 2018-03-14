#! /usr/bin/perl -w
use strict;
use warnings;

#my $test = 1000;
#my $test = 10000;
my $test = 0;

my %cOfAlpha = (
    1.22 => 0.1,
    1.36 => 0.05,
    1.48 => 0.025,
    1.63 => 0.01,
    1.73 => 0.005,
    1.95 => 0.001,
);
my $printedDotCountIncrement = 1.2;


if (scalar @ARGV < 4) {
    die "Usage: bodob_ks-test.pl <vocabReal.tsv (for instance '../../../../Qbasher/QBASHER/indexes/AcademicID/vocab.tsv')> <vocabSynth.tsv (such as 'Synth/vocab.tsv') <label> <directory>

  label (e.g. collection name) is included in filenames
  directory - directory in which plot output is placed.

\n";
}
my $forwardFile1 = $ARGV[0];
my $forwardFile2 = $ARGV[1];
my $label = $ARGV[2];
my $plotdir = $ARGV[3];

my $line ="";
print "Comparing $forwardFile1 with $forwardFile2\n";
my @collections = ();

my @realHistogram = ();
my @synthHistogram = ();
my $totalReal = 0;
my $totalSynth = 0;

$totalReal = getTermHistogram($ARGV[0], \@realHistogram);
$totalSynth = getTermHistogram($ARGV[1], \@synthHistogram);

sub getTermHistogram {
    my ($filename, $histogram) = @_;
    my $total = 0;
    if ($test > 0) {
	die "Can't take head of $filename\n"
	    unless open IN, "head -$test $filename |";
    } else {
	die "Can't read $filename\n"
	    unless open IN, "$filename";
    }
    while (defined($line = <IN>)) {
        my ($terms, $count) = split(/[\r\t\n]/, $line);
        push(@{$histogram}, $count);
        $total += $count;
    }
    close IN;
    return $total;
}



my $graphDataFileTerms = "$plotdir/KS_${label}_cumulativeProbability.dat";
my $graphFileCommands = "$plotdir/KS_${label}_gnuPlotInputFile.cmd";
my $graphFile = "$plotdir/KS_${label}_zipfDistributions.pdf";
my $numberOfPointsToGraph = 1000;

my $lineReal = "";
my $lineSynth = "";
my $D = 0;
my $nReal = scalar @realHistogram;
my $nSynth = scalar @synthHistogram;
my $DhitAt = 0;

open OUT_REAL, "> $graphDataFileTerms";
printf OUT_REAL "rank\treal\tsynthetic\n";
my $currentTotalReal = 0;
my $currentTotalSynth = 0;
my $lineCount = 0;
my $yMax = 0;
my $yMin = 1;
my $nextDotToPrint = 1;
while (@realHistogram or @synthHistogram) {
    my $realDistributionTermCount = 0;
    my $randomDistributionTermCount = 0;
    if (@realHistogram) {
        $realDistributionTermCount = shift(@realHistogram);
	$currentTotalReal += $realDistributionTermCount;
    }
    if (@synthHistogram) {
        $randomDistributionTermCount = shift(@synthHistogram);
	$currentTotalSynth += $randomDistributionTermCount;
    }
    if ($lineCount >= $nextDotToPrint) {
        printf OUT_REAL "$lineCount\t%.5f\t%.5f\n", $currentTotalReal / $totalReal, $currentTotalSynth / $totalSynth;
        $nextDotToPrint = $lineCount * $printedDotCountIncrement;
    }

    my $currentDistance = abs(($currentTotalReal / $totalReal) - ($currentTotalSynth / $totalSynth));
    if ($D < $currentDistance) {
        $D = $currentDistance;
        $DhitAt = $lineCount;
        if ((($currentTotalReal / $totalReal) - ($currentTotalSynth / $totalSynth)) > 0) {
            $yMax = $currentTotalReal / $totalReal;
            $yMin = $currentTotalSynth / $totalSynth;
        } else {
            $yMax = $currentTotalSynth / $totalSynth;
            $yMin = $currentTotalReal / $totalReal;
        }
    }
    $lineCount++;
}
close OUT_REAL;

print "KS distance is $D at point $DhitAt\n";
my $actualCofAlphaValue = $D / (sqrt(($nReal + $nSynth) / ($nReal * $nSynth)));
my $statsSigAt = 0;
foreach my $tableCofAlpha (sort{$b > $a} keys %cOfAlpha) {
    if ($actualCofAlphaValue < $tableCofAlpha) {
        $statsSigAt = $cOfAlpha{$tableCofAlpha};
	last;
    }
}
my $significance = "";
if ($statsSigAt == 0) {
    print "Not statistically significant -> Null Hypothesis supported (distributions are not similar (enough))\n";
    $significance = "(Kolmogorov-Smirnov test: actualCofAlphaValue = $actualCofAlphaValue - distributions not similar.)";
} else {
    print "Statistically significant \@ alpha=$statsSigAt -> Null Hypothesis rejected (distributions are similar (enough))\n";
    $significance = "(Kolmogorov-Smirnov test: Stats sig (alpha value of $statsSigAt) - distributions are similar.)";
}
print "\tThere were $nReal datapoints and $totalReal total number of terms in the real distribution\n";
print "\tThere were $nSynth datapoints and $totalSynth total number of terms in the random distribution\n";

my $yLabel = $yMin + ($yMax - $yMin) * (3/4);

open OUT_GRAPH, "> $graphFileCommands";
print OUT_GRAPH "
set terminal pdf
set size ratio 0.7071
set output \"$graphFile\"
set autoscale
set xlabel \"Rank\"
set ylabel \"Cumulative Probability\"
set pointsize 0.5
set label \"Maximum distance\" at $DhitAt, $yLabel
set arrow from $DhitAt,$yMin to $DhitAt, $yMax
set arrow from $DhitAt,$yMax to $DhitAt, $yMin
unset log
unset logscale
set logscale x
plot '$graphDataFileTerms' using 1:2 title columnheader with linespoints, '$graphDataFileTerms' using 1:3 title columnheader with linespoints
";
print OUT_GRAPH "\n\n";

system("gnuplot $graphFileCommands");
print "Output plot is in $graphFile\n";

exit;
system("rm $graphDataFileTerms");
system("rm $graphFileCommands");


