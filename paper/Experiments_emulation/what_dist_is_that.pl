#! /usr/bin/perl -w 

# Does a frequency histogram better match a uniform, normal, or log-normal dist?
#
# Read a histogram datafile in 2-column TSV format in which col 1 is
# a numerical value (e.g. document length) and col 2 is the frequency
# with which that value is observed.  t is assumed that the rows are 
# sorted in ascending order of column 1.
#
# The primary target of this script is the QBASH.doclenhist file now produced by 
# QBASHI.
#
# Calculate mean, standard deviation, and mean and standard deviation of logs
# Convert the raw and transformed scores into standard Z scores and 
# produce Q-Q plots for observed and transformed distributions against theoretical
# normal distribution.  The Q-Q plots are put in the LengthPlots directory

die "Usage: $0 <indexname or index path>\n"
    unless $#ARGV == 0;

$|++;

print "\n*** $0 @ARGV\n\n";

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

print "QBASHER path: $qbp\nIndex_dir=$idxdir\nIndex name=$idxname\n";



if (! -d "LengthPlots") {
    die "Can't make a LengthPlots directory\n"
	unless mkdir "LengthPlots";
}

$dlhfile = "$idxdir/QBASH.doclenhist";
die "Can't open $dlhfile" unless open DLH, $dlhfile;

$c = 0;
while (<DLH>) {
    # Ignore blank lines and comments
    next if /^\s*#/;
    next if /^\s*$/;
    chomp;
    die "Line '$_' is not in correct format\n"
	unless /([+\-]?[0-9.]+)\s*\t\s*([0-9]+)\s*$/;
    $x = $1;
    $freq = $2;
    next if $x == 0;  # Can't take logs
    next if $freq == 0;  # find_quantiles() doesn't like empty entries
    # record the data in two arrays
    $x[$c] = $x;
    $f[$c] = $freq;
    $c++;

    $n += $freq;
    $lx = log($x);
    $fwx = $x * $freq;
    $fwlx = $lx * $freq;
    $sumx += $fwx;
    $sumlx += $fwlx;
    $sumx2 += $x * $x * $freq;
    $sumlx2 += $lx * $lx * $freq;
}
close(DLH);

$meanx = $sumx / $n;
$meanlx = $sumlx / $n;
$varx = ($sumx2 / $n) - ($meanx * $meanx);
$varlx = ($sumlx2 / $n) - ($meanlx * $meanlx);
$stdevx = sqrt($varx);
$stdevlx = sqrt($varlx);

$pmeanx = sprintf("%.4f", $meanx);
$pmeanlx = sprintf("%.4f", $meanlx);
$pstdevx = sprintf("%.4f", $stdevx);
$pstdevlx = sprintf("%.4f", $stdevlx);

print "Total cases: $n
Mean(x), Stdev(x): $pmeanx, $pstdevx
Mean(log(x)), Stdev(log(x)): $pmeanlx, $pstdevlx
";

if (0) {
    for $q (4, 5, 10, 11, 20) {
	find_quantiles($q, $n);
	print "$q-Quantiles:\n";
	for ($i = 1; $i < $q; $i++) {
	    print sprintf("%3d  %.4f\n", $i, $quantiles[$i - 1]);
	}
	print "\n";
    }
}

# Convert raw and transformed data into Z scores
for ($i = 0; $i < $c; $i++) {
    $z[$i] = ($x[$i] - $meanx) / $stdevx;
    $zl[$i] = (log($x[$i]) - $meanlx) / $stdevlx;
}

# Set up the decile points for a standard normal distribution
# Values obtained from the Brigham Young University calculator at
# http://sampson.byu.edu/courses/z2p2z-calculator.html

@z_deciles = (-1.281551, 
	     -0.841621, 
	     -0.524399,
	     -0.253345,
	     0,
	     0.253345,
	     0.524399,
	     0.841621,
	     1.281551,);
	     
# Find deciles in the standardised raw data and the standardised logged data

@x = @z;
find_quantiles(10, $n);
@raw_deciles = @quantiles;
@x = @zl;
find_quantiles(10, $n);
@logged_deciles = @quantiles;

die "Can't write to LengthPlots/Q-Q_$idxname.dat" unless open QQD, "> LengthPlots/Q-Q_$idxname.dat";
for ($i = 0; $i < 9; $i++) {

    print QQD "$z_deciles[$i]\t$raw_deciles[$i]\t$logged_deciles[$i]\n";
}

close QQD;

# Finally generate the Q-Q plots

$pcfile = " LengthPlots/Q-Q_${idxname}_plot.cmds";

die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

print P "
set terminal pdf
set xlabel \"Z scores (theoretical)\"
set style line 1 linewidth 4
set style line 2 linewidth 4
set style line 3 linewidth 4
set style line 4 linewidth 4
set style line 5 linewidth 4
set style line 6 linewidth 4
set style line 7 linewidth 4
set style line 8 linewidth 4
set style line 9 linewidth 4
set pointsize 0.5
";
print P "
set ylabel \"Z scores (observed)\"
set output \"LengthPlots/Q-Q_${idxname}_normal.pdf\"
plot \"LengthPlots/Q-Q_${idxname}.dat\" using 1:2 title \"\" pt 7 ps 0.25, x title \"\"

set ylabel \"Z scores (log of observed)\"
set output \"LengthPlots/Q-Q_${idxname}_lognormal.pdf\"
plot \"LengthPlots/Q-Q_${idxname}.dat\" using 1:3 title \"\" pt 7 ps 0.25, x  title \"\"


";


close P;

`gnuplot $pcfile > /dev/null 2>&1`;
die "Gnuplot failed with code $? for $pcfile!\n" if $?;

print "Run e.g. acroread to show the following plots

  -  LengthPlots/Q-Q_${idxname}_normal.pdf
  -  LengthPlots/Q-Q_${idxname}_lognormal.pdf

";

exit(0);

# ----------------------------------------------------------------
sub find_quantiles {
    my $q = shift;   # The number of quantiles required.  4 = quartiles, 10 = deciles etc.
    my $N = shift;   # The total number of cases
    # We're dealing with a histogram in which the i-th value is x[i] and the
    # frequency of that value is f[i].  That is in effect a compressed 
    # representation of a sorted list.  To find the q-quantiles we determine
    # cutpoints by dividing the sorted list into groups with equal numbers 
    # of items.  If the cutpoint lands exactly on an item, the value of the
    # item is the q-quantile we are looking for.  If it lands at a fractional point,
    # then the q-quantile is the weighted average of the two surrounding values.

    my $items_in_group = $N / $q;
    my $cutpoint_index = $items_in_group;
    my $cvi = 0;
    my $cumfreq = 0;

    undef @quantiles;  # Avoid hangovers

    print "find_quantiles($q, $N): $items_in_group\n";

    for (my $cp = 1; $cp < $q; $cp++) {
	# Loop over $q - 1 cutpoints. Each iteration stores one quantile
	# in the non-local @quantiles array.
	# $cumfreq is effectively an index into the sorted list
	while ($cumfreq < $cutpoint_index) {
	    $cumfreq += $f[$cvi++];
	}
	my $cpi_diff = $cumfreq - $cutpoint_index;

	if ($cpi_diff >= 1.0 || $cpi_diff < 0.001) {
	    # cutpoint falls fully within this set of equal values.
	    push @quantiles, $x[$cvi - 1];
	} else {
	    # cutpoint falls between two different values
	    my $frac = $cutpoint_index - int($cutpoint_index);
	    my $valdiff = $x[$cvi] - $x[$cvi - 1];
	    my $quan = $x[$cvi - 1] + $frac * $valdiff;
	    push @quantiles, sprintf("%.2f", $quan);
	}
	$cutpoint_index += $items_in_group;
    }    
}
