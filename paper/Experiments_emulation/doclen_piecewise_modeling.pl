#! /usr/bin/perl -w

# Read a .doclenhist file and determine how to model the document length
# distribution:

$|++;

$thresh_perc_of_max = 1;
$bucketing_thresh = 256;  # Bucket if the range of lengths is larger than this.
$numbucks = 75;   # The number of buckets to use if we are bucketing.
$q = 0;

die "$0 <index>\n" unless $#ARGV == 0;

$QB_subpath = "GIT/Qbasher";
$ix_subpath = "QBASHER/indexes";
@QB_paths = (
    "C:/Users/dahawkin/$QB_subpath",  # Dave Laptop
    "D:/dahawkin/$QB_subpath",  # Redsar
    "S:/dahawkin/$QB_subpath",    # HeavyMetal
    "F:/dahawkin/$QB_subpath",    # RelSci00
    );

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
$dlhfile = "$idxdir/QBASH.doclenhist";

die "Can't open $dlhfile" unless open DLH, $dlhfile;

$cumfreq[0] = 0;
$lines = 0;
$minlen = 9999999999;
$maxlen = 0;
$maxfreq = 0;

while (<DLH>) {
    next if /^\s*#/;
    if (/^\s*([1-9][0-9]*)\s+([0-9]+)/) {   # Ignore zero length
	$l = $1;
	next if $2 < 1;
	$rfreq[$l] = $2;
	$cumfreq += $2;
	$maxfreq = $2 if $maxfreq < $2;
	$lines++;
	$minlen = $l if $minlen > $l;
	$maxlen = $l if $maxlen < $l;
    }
}

close(DLH);


# If there is a wide range of lengths, results may be improved (smoothed) by bucketing.

if ($maxlen - $minlen > $bucketing_thresh) {
    # Divide into buckets
    $buckstep = ($maxlen - $minlen) / $numbucks;
} else {
    # buckets just correspond to raw observations.
    $buckstep = 1;
    $numbucks = $maxlen - $minlen + 1;
}

# The freq array goes from 0 .. $numbucks - 1  and has the bucket count
# The meanlen array holds the average length for each bucket
$lastb = 0;
$bucksum = 0;
$buckcount = 0;
$sum = 0;
for ($i = $minlen; $i <= $maxlen; $i++) {
    $b = int(($i - $minlen) / $buckstep);
    if ($b > $lastb) {
	$meanlen[$lastb] = $bucksum / $buckcount;
	$bucksum = 0;
	$buckcount = 0;
	$lastb = $b;
    }	
    $bucksum += $i;
    $buckcount++;
    if (defined($rfreq[$i])) {
	$freq[$b] += $rfreq[$i];
    }
}

$meanlen[$lastb] = $bucksum / $buckcount;


# We need the first and last buckets to line up with the first 
# and last observed points
if ($buckstep != 1) {
    $meanlen[0] = $minlen;
    $freq[0] = $rfreq[$minlen];
    $meanlen[$lastb] = $maxlen;
    $freq[$lastb] = $rfreq[$maxlen];
}

# Now make an array of cumulative probabilities
$maxfreq = 0;
$sum = 0;
for ($b = 0; $b < $numbucks; $b++) {
    $freq[$b] = 0 unless defined($freq[$b]);
    print "Bucket $b:  $meanlen[$b]  $freq[$b]\n";
    $sum += $freq[$b]; 
    $cumprob[$b] = $sum / $cumfreq;
    $maxfreq = $freq[$b] if $maxfreq < $freq[$b];
}


$split_thresh = $thresh_perc_of_max * $maxfreq / 100; 


print "Total frequency: $cumfreq
Length range: $minlen -- $maxlen
No. distinct non-zero lengths: $lines
No. buckets: $numbucks
";

mkdir "LengthPlots" unless -e "LengthPlots";

$dl_segments = "Lengthplots/${idxname}_segments.dat";
die "Can't write to $dl_segments" unless open DLQ, ">$dl_segments";

my @segboundaries;
$q = 0;
#$cf = sprintf("%.10f", $cumprob[$minlen]);
#$segboundaries[$q++] = "$minlen,$cf";  # Will be appended to by segsplit.

segsplit(0, $numbucks - 1);

$segboundaries[$q++] .= "$maxlen,1.00000";

close(DLQ);

$qboption = "-x_synth_dl_segments=\"$q:$segboundaries[0]";
for ($i = 1; $i < $q; $i++) {
    $qboption .= ";$segboundaries[$i]";
}
$qboption .= "\"";

# Now do the plotting

$pcfile = "LengthPlots/${idxname}_dlsegs_fitting_plot.cmds";
die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

print P "
set terminal pdf
set size ratio 0.7071
set ylabel \"Frequency\"
set xlabel \"Document length\"
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
set output \"LengthPlots/${idxname}_dlsegs_fitting.pdf\"
plot \"$dlhfile\" pt 7 ps 0.75  title \"$idxname\", \"$dl_segments\" with lines ls 2 title \"Piecewise($thresh_perc_of_max% of max freq): $q segments\"
";

close P;

`gnuplot $pcfile > /dev/null 2>&1`;
die "Gnuplot failed with code $? for $pcfile!\n" if $?;

print "To see quality of fitting:

   acroread Lengthplots/${idxname}_dlsegs_fitting.pdf\n\n";

print "The QBASHI option is: $qboption\n";


exit(0);


# ---------------------------------------------------------------------------------

#split is a recursive function which takes a segment represented by l
# and r, where l and r are bucket indexes, and splits it at the point of 
# maximum deviation, provided that that deviation exceeds a threshold.

sub segsplit {
    my $l = shift;
    my $r = shift;

    return if ($l > ($r - 1));

    my $slope = ($freq[$r] - $freq[$l]) / ($meanlen[$r] - $meanlen[$l]);
    my $up;
    my $down;
    my $diff;

    print "Attempting to split segment from ($meanlen[$l], $freq[$l]) to ($meanlen[$r], $freq[$r]). Slope: $slope\n";

    my $ixofmaxdiff = 0;
    my $maxdiff = 0;
    for (my $i = $l + 1;  $i < $r; $i++) {
	next if (! defined($freq[$i]));
	my $expected = $freq[$l] + $slope * ($meanlen[$i] - $meanlen[$l]);
	my $actual = $freq[$i];
	if ($expected > $actual) {
	    $up = $expected;
	    $down = $actual;
	} else {
	    $up = $actual;
	    $down = $expected;
	}
	if ($down < 1) {
	    $diff = 0.0;
	} else {
	    $diff = $up - $down;
	}

	#print "Point $i: act: $actual; exp: $expected; diff: $diff\n";
	if ($diff > $maxdiff) {
	    $maxdiff = $diff;
	    $ixofmaxdiff = $i;
	}
    }

    if ($maxdiff > $split_thresh) {
	#print "Max diff is $maxdiff, for length $ixofmaxdiff\n";
	print "   splitting ...\n";
	segsplit($l, $ixofmaxdiff);
	segsplit($ixofmaxdiff, $r);
    } else {
	$scaled_l = $freq[$l] / $buckstep;
	$scaled_r = $freq[$r] / $buckstep;

	my $cf = sprintf("%.10f", $cumprob[$l]);
	$segboundaries[$q++] = "$meanlen[$l],$cf";
	print DLQ "$meanlen[$l] $scaled_l
$meanlen[$r] $scaled_r

";
    }
}
