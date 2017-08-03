#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


#Take an input file consisting of at least a pair of numbers on each line
#where the first is an x value and the second a y.  Try to fit with 
#a variety of functions.  Produce plot commands for the least squares
#best version of each type of function and then the overall best for all 
#functions tried.

#If <path_stem> includes a directory name, we write plot cmd files and pdfs in that
#directory, otherwise we write them in LsqfitPlots/, creating that if it doesn't
#already exist

# This is a script specifically oriented toward the Synthetic corpus 
# work.  It expects to find the data in <pathStem>.plot

$|++;

$plotter = `which gnuplot 2>&1`;
chomp($plotter);
$plotter .= '.exe' if ($^O =~ /cygwin/i || $^O =~ /MSWin/i);
undef $plotter if $plotter =~/^which: no/;


my(@xvals);
my(@yvals);
my(@coeffs);
my(@gx);
my(@ans);
my $bestsumsq = 1000000000;
my $ndp;    # Number of data points
$successes = 0;

die "Usage: $0 <path_stem>
   Expects to find data points in <path_stem>.plot and piecewise segment data in <path_stem>.segdat
 
   Plotfiles will be written in the directory path within <path_stem>.  If there is no such path they 
   will be written in the 'LsqfitPlots' directory, which will be created if necessary.
" unless $#ARGV == 0;


$pathStem = $ARGV[0];
$plotcmd = "plot \"$pathStem.plot\" title \"Observed\" pt 7 ps 0.4";

$inputdatafile = "$pathStem.plot";

$lsqfitdir = "";
if (!($pathStem =~ m@^.*/[^/]+$@)) {
    # No directory is present in the pathStem.  Create our own if nec.
    $lsqfitdir = "LsqfitPlots";
    if (! -d $lsqfitdir) {
	die "Can't create $lsqfitdir directory\n" unless mkdir "$lsqfitdir";
    }
    $lsqfitdir .= "/";
}

$ndp = read_data();

#try_constant();
$successes++ if (try_linear() == 0);
$successes++ if (try_quadratic() == 0);
#try_cubic();
#try_quartic();
#try_quintic();
#try_square_root();
#try_recip();
#try_log(40, 0.1);
#try_exponential(40, 0.1);

if ($successes == 0) {
    print "\n   *** No joy in least squares fitting ***\n\n";
    exit(0);
}


print "\n\nBest function was $bestfun\n\n";

$pcfile = "$lsqfitdir${pathStem}_lsqfit_plot.cmds";

if (-e "${pathStem}.segdat") {
    $plotcmd .= ", \"${pathStem}.segdat\" title \"Piecewise fit (h=10, s=10)\" w l ls 7";
} 

#else {
#    warn "lsqfit_general.pl:  Warning: can't open ${pathStem}.segdat
#     Continuing without it.
#";
#}


die "Can't open $pcfile" unless open PCMD, ">$pcfile";

print PCMD "
set terminal pdf
set size ratio 0.7071
set output \"$lsqfitdir${pathStem}_lsqfit.pdf\"
set xlabel \"Log(rank)\"
set ylabel \"Log(probability)\"
set style line 1 linewidth 3
set style line 2 linewidth 3
set style line 3 linewidth 3
set style line 4 linewidth 3
set style line 5 linewidth 3
set style line 6 linewidth 3
set style line 7 linewidth 3
set style line 8 linewidth 3
set style line 9 linewidth 3
set pointsize 3
";
print PCMD "
$plotcmd 

";


close PCMD;

if (defined($plotter)) {
    `$plotter $pcfile`;
    die "$plotter failed with code $? for $pcfile!\n" if $?;

    print "View synthetic data, real data and lines of best fit
    by using a PDF viewer, e.g. 

    acroread $lsqfitdir${pathStem}_lsqfit.pdf

    The corresponding plot.cmds files are in the same directory.\n";
} else {
    warn "\n\n$0: Warning: gnuplot not found.  PDFs of graphs will not be generated.\n\n";
}

print "\nAll done.  \n";
exit 0;

# -----------------------------------------------------------------------------------

sub read_data {
# read in the x,y pairs
    my $n = 0;
    print "Data will be rotated by $theta radians\n" if (defined($theta));

    die "Can't open $inputdatafile\n" 
	unless open I, "$inputdatafile";
    while (<I>) {
	if (!/^\s*\#/ && ! /^\s*$/) {
	    # Comment lines and blank lines are skipped
	    # Non-comment, non-blank lines should contain two numbers separated
	    # by white space
	    if (/([0-9.\-+]+)\s+([0-9.\-+]+)/) {
		$x = $1;
		$y = $2;
		#print "$x -------- $y\n";
		if (defined($theta)) {
		    $xvals[$n] = $x * cos($theta) - $y * sin($theta);
		    $yvals[$n] = $x * sin($theta) + $y * cos($theta);
		} else {
		    $xvals[$n] = $x;
		    $yvals[$n] = $y;
		}
		$n++;
	    } else {
		print "$0: Error in input: $_\n";
	    }
	}
    }
    close(I);
    print "Data points read: $n\n";

    # If we're rotating, we have to rotate the sample .dat file for plotting.
    if (defined($theta)) {
	die "Can't open ${pathStem}.dat\n" 
	    unless open I, "${pathStem}.dat";
	die "Can't open ${pathStem}_rotated.dat\n" 
	    unless open O, ">${pathStem}_rotated.dat";
	while (<I>) {
	    if (!/^\s*\#/ && ! /^\s*$/) {
		# Comment lines and blank lines are skipped
		($x, $y) = split ' ', $_  ;
		$xr = $x * cos($theta) - $y * sin($theta);
		$yr = $x * sin($theta) + $y * cos($theta);
		print O sprintf("%.4f %.4f\n", $xr, $yr);
	    }
	}
	close(I);
	close(O);
    }
    return $n;
}


sub lineq_solve {
    #Solve a system of linear equations for the required number of
    #unknown coefficients.

    # return value is non-zero in case of error, such as division by zero
    
    $m = shift(@_);   # the number of coefficients to solve for
    $m--;
    # p. 325 of Stark

    for ($k = 0; $k <= $m; $k++) {
	for ($j = 0; $j <= $m; $j++) {
	    $coeffs[$k][$j] = 0.0;
	    for ($i = 0; $i < $ndp; $i++) {
		$coeffs[$k][$j] += $gx[$k][$i] * $gx[$j][$i];
	    }
	}
    }

    #p. 326 of Stark
    for ($j = 0; $j <= $m; $j++) {
	$coeffs[$j][$m+1] = 0;
	for ($i = 0; $i < $ndp; $i++) {
	    $coeffs[$j][$m+1] += $yvals[$i] * $gx[$j][$i];
	}
    } 

    #p. 180 of Stark

    $mm = $m +1;   
    $nn = $mm + 1;

    # There are m  equations, loop over m-1 pivots
    for ($i = 0; $i <$mm-1 ; $i++) {
	$big = 0.0;
	for ($k = $i; $k < $mm ; $k++) {
	    $term = abs($coeffs[$k][$i]);
	    if ($term > $big) {
		$big = $term;
		$l = $k;
	    }
	}
	if ($big == 0.0) {
	    print STDERR "No non-zero pivot found in ${i}th column\n";
	    return 3;
	}
    
	# if $l != $i switch rows
	if ($l != $i) {
	    for ($j = 0; $j < $nn; $j++) {
		$temp = $coeffs[$i][$j];
		$coeffs[$i][$j] = $coeffs[$l][$j];
		$coeffs[$l][$j] = $temp;
	    }
	}
    
	# now start pivotal reduction
	$pivot = $coeffs[$i][$i];
	return 2 if abs($pivot) < 0.000000001;
	$nextr = $i + 1;
	for ($j = $nextr; $j < $mm; $j++) {
	    $const = $coeffs[$j][$i] / $pivot;
	    # Reducing each term in the jth row
	    for ($k = 0; $k < $nn; $k++) {
		$coeffs[$j][$k] -= $const * $coeffs[$i][$k];
	    }
	}
    }

    # Perform back substitution.
    for ($i = 0; $i < $mm; $i++) {
	$irev = $mm -$i -1;
	$y = $coeffs[$irev][$nn-1];
	if ($irev != $mm - 1) {
	    for ($j = 1; $j <= $i; $j++) {
		$k = $nn -$j - 1;
		$y -= $coeffs[$irev][$k] * $ans[$k];
	    }
	}
	return 1 if abs($coeffs[$irev][$irev]) < 0.000000001;
	$ans[$irev] = $y / $coeffs[$irev][$irev];
    }
    return 0;  # All good!
}

sub p4 {
    # Round the signed argument to 4 dec. places
    my $x = shift(@_);
    my $s5;
    if ($x < 0) {$s5 = -0.5} else {$s5 = 0.5} 
    $x = int(10000*$x+$s5)/10000;
    return $x;
}


sub check_fit {
# Now check the values
    my $m = shift(@_);   # the number of coefficients
    my $fun = shift(@_); 
    $sumsq=0;
    for ($i = 0; $i < $ndp; $i++) {
	$approx = 0 ;
	for ($q = 0; $q < $m; $q++) {
	    $approx += $ans[$q] * $gx[$q][$i];
	}
	#print "$xvals[$i]  $yvals[$i]  $approx\n";
	$diff = $yvals[$i] - $approx;
	$sumsq += $diff * $diff;
    }

    # Round all the answers to 4 decimal places
    for ($i = 0; $i < $m; $i++) {
	$ans[$i] = p4($ans[$i]);
    }
    if ($sumsq < $bestsumsq) {
	$bestsumsq = $sumsq;
	$bestfun = $fun;
    }
    return p4($sumsq);
}


sub try_constant {
    my $rslt;
    for ($i = 0; $i < $ndp; $i++) {
	$gx[0][$i] = 1;
    }
    return $err if ($err = lineq_solve(1)) ;
    $rslt = check_fit(1, "constant");
    print "Constant:  $rslt\n";
    $phrase = ", $ans[0] title \"Constant\" ls 1";
    #$plotcmd .= $phrase;
    print "$phrase\n";
    return 0;
}



sub try_linear {
    my $rslt;
    for ($i = 0; $i < $ndp; $i++) {
	$gx[0][$i] = 1;
	$gx[1][$i] = $xvals[$i];
    }
    return $err if ($err = lineq_solve(2)) ;
    $rslt = check_fit(2, "linear");
    print "Linear fit:  $rslt\n";
    $phrase =  ", $ans[0] + $ans[1] *x  title \"Linear fit\" ls 2";
    #$plotcmd .= $phrase;
    print "$phrase\n";
    return 0;
}


sub try_quadratic {
    my $rslt;
    for ($i = 0; $i < $ndp; $i++) {
	$gx[0][$i] = 1;
	$gx[1][$i] = $xvals[$i];
	$gx[2][$i] = $xvals[$i] * $xvals[$i];
    }
    return $err if ($err = lineq_solve(3)) ;
    $rslt = check_fit(3, "quadratic");
    print "\nQuadratic fit:  $rslt\n";
    $phrase =  ", $ans[0] + $ans[1] *x + $ans[2] *x**2  title \"Quadratic fit\" ls 3";
    #$plotcmd .= $phrase;
    print "$phrase\n";
    return 0;
}

sub try_cubic {
    my $rslt;
    for ($i = 0; $i < $ndp; $i++) {
	$gx[0][$i] = 1;
	$gx[1][$i] = $xvals[$i];
	$gx[2][$i] = $xvals[$i] * $xvals[$i];
	$gx[3][$i] = $xvals[$i] * $xvals[$i] * $xvals[$i];
    }
    return $err if ($err = lineq_solve(4)) ;
    $rslt = check_fit(4,"cubic");
    print "\nCubic:  $rslt\n";
    $phrase = ", $ans[0] + $ans[1] *x + $ans[2] *x**2 + $ans[3] *x**3  title \"Cubic\" ls 4";
    $plotcmd .= $phrase;
    print "$phrase\n";
    return 0;
}

sub try_quartic {
    my $rslt;
    for ($i = 0; $i < $ndp; $i++) {
	$gx[0][$i] = 1;
	$gx[1][$i] = $xvals[$i];
	$gx[2][$i] = $xvals[$i] * $xvals[$i];
	$gx[3][$i] = $xvals[$i] * $xvals[$i] * $xvals[$i];
	$gx[4][$i] = $xvals[$i] * $xvals[$i] * $xvals[$i] * $xvals[$i];
    }
    return $err if ($err = lineq_solve(5)) ;
    $rslt = check_fit(5,"quartic");
    print "\nQuartic:  $rslt\n";
    $phrase =  ", $ans[0] + $ans[1] *x + $ans[2] *x**2 + $ans[3] *x**3 + $ans[4] *x**4  title \"Quartic\" ls 5";
     $plotcmd .= $phrase;
    print "$phrase\n";
    return 0;
}


sub try_quintic {
    my $rslt;
    for ($i = 0; $i < $ndp; $i++) {
	$gx[0][$i] = 1;
	$gx[1][$i] = $xvals[$i];
	$gx[2][$i] = $xvals[$i] * $xvals[$i];
	$gx[3][$i] = $xvals[$i] * $xvals[$i] * $xvals[$i];
	$gx[4][$i] = $xvals[$i] * $xvals[$i] * $xvals[$i] * $xvals[$i];
	$gx[5][$i] = $xvals[$i] * $xvals[$i] * $xvals[$i] * $xvals[$i] * $xvals[$i];
    }
    return $err if ($err = lineq_solve(6)) ;
    $rslt = check_fit(6,"quintic");
    print "\nQuintic:  $rslt\n";
    $phrase =  ", $ans[0] + $ans[1] *x + $ans[2] *x**2 + $ans[3] *x**3 + $ans[4] *x**4  + $ans[5] *x**5 title \"Quintic\" ls 6";
     $plotcmd .= $phrase;
    print "$phrase\n";
    return 0;
}



sub try_square_root {
    my $rslt;
    for ($i = 0; $i < $ndp; $i++) {
	$gx[0][$i] = 1;
	$gx[1][$i] = $xvals[$i];
	$gx[2][$i] = sqrt($xvals[$i]);
    }
    return $err if ($err = lineq_solve(3)) ;
    $rslt = check_fit(3, "square root");
    print "\nSquare root:  $rslt\n";
    $phrase =  ", $ans[0] + $ans[1] *x + $ans[2] *sqrt(x)  title \"Square root\" ls 7";
    $plotcmd .= $phrase;
    print "$phrase\n";
    return 0;
}





sub try_recip {
    my $rslt;
    for ($i = 0; $i < $ndp; $i++) {
	$gx[0][$i] = 1;
        if ($xvals[$i] == 0) { print "Zero value.  Can't take recip.\n";   return -1}
	$gx[1][$i] = 1/$xvals[$i];
    }
    return $err if ($err = lineq_solve(2)) ;
    $rslt = check_fit(2, "reciprocal");
    print "\nReciprocal:  $rslt\n";
    $phrase = ", $ans[0] + $ans[1]/x  title \"Reciprocal\" ls 7";
    $plotcmd .= $phrase;
    print "$phrase\n";
    return 0;  
}

sub try_log {
    my $limit = shift(@_);
    my $step = shift(@_);
    my $rslt;
    my $bestpow = 0;
    my $bestrslt = 1000000000;
    my $i;

    for  ($pow = -$limit; $pow <= $limit; $pow+=$step)  {
	if ($pow == 0.0) {next;}
	for ($i = 0; $i < $ndp; $i++) {
	    $gx[0][$i] = 1;
	    $gx[1][$i] = $xvals[$i];
	    if ($xvals[$i] == 0) { return -1}
	    $gx[2][$i] = log($xvals[$i]);
	}
	return $err if ($err = lineq_solve(3)) ;
	$rslt = check_fit(3, "log");
	if ($rslt < $bestrslt) {
	    $bestpow = $pow;
	    $bestrslt = $rslt;
	    for ($i = 0; $i < 3; $i++) {
		$svans[$i] = $ans[$i];
	    }
	}
    }
    $bestpow = p4($bestpow);
    print "\nLog:  $rslt\n";
    $phrase = ", $svans[0] + $svans[1] *x + $svans[2] *log($bestpow * x)  title \"Log\" ls 8";
    $plotcmd .= $phrase;
    print "$phrase\n";
    return 0;
}

sub try_exponential {
    my $limit = shift(@_);
    my $step = shift(@_);
    my $rslt;
    my $bestpow = 0;
    my $bestrslt = 1000000000;
    my $i;

    for  ($pow = -$limit; $pow <= $limit; $pow+=$step)  {
	if ($pow == 0.0) {next;}
	# set up the gx s
	for ($i = 0; $i < $ndp; $i++) {
	    $gx[0][$i] = 1;
	    $gx[1][$i] = $xvals[$i];
	    $gx[2][$i] = exp($pow * $xvals[$i]);
	}
	return $err if ($err = lineq_solve(3)) ;
	$rslt = check_fit(3, "exponential");
	if ($rslt < $bestrslt) {
	    $bestpow = $pow;
	    $bestrslt = $rslt;
	    for ($i = 0; $i < 3; $i++) {
		$svans[$i] = $ans[$i];
	    }
	}
    }
    $bestpow = p4($bestpow);
    print "\nExponential:  $bestrslt\n";
    $phrase = ", $svans[0] + $svans[1] *x + $svans[2] *exp($bestpow * x)  title \"Exponential\" ls 9";
    $plotcmd .= $phrase;
    print "$phrase\n";   
    return 0;
}



