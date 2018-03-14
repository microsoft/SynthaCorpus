#! /usr/bin/perl -w
# Run in Experiments_emulation directory
# Plot the doclenhist files for real and mimic versions of the same index

die "Usage: $0 <idxname> dlnormal|dlsegs|dlgamma|dlhisto" unless $#ARGV == 1;

$QB_subpath = "GIT/Qbasher";
$ix_subpath = "QBASHER/indexes";
@QB_paths = (
    "C:/Users/dahawkin/$QB_subpath",  # Dave Laptop
    "D:/dahawkin/$QB_subpath",  # Redsar
    "S:/dahawkin/$QB_subpath",    # HeavyMetal
    "F:/dahawkin/$QB_subpath",    # RelSci00
    );



$ix = $ARGV[0];
$emultype = $ARGV[1];
die "Emulation type  must be either 'dlnormal', 'dlsegs' (adaptive piecewise), 'dlgamma', or
dlhisto (read from a .doclenhist file)\n"
    unless $emultype eq "dlsegs" || $emultype eq "dlgamma" || $emultype eq "dlnormal"
    || $emultype eq "dlhisto";

if (! -d ($ix)) {
    # Try prefixing paths for Dave's Laptop, Redsar and HeavyMetal
    for $p (@QB_paths) {
	$idxdir = "$p/$ix_subpath/$ix";
	print "Trying: $idxdir\n";
	if (-d $idxdir) {
	    $qbp = $p;
	    last;
	}
    }
    die "Couldn't locate index directory $ARGV[0] either at $ARGV[0] or in any of the known places.  [$qbp]  [$idxdir]\n"
	unless -d $qbp && -d $idxdir;
    $dlh[0] = "$idxdir/QBASH.doclenhist";
    $ix = $idxdir;
    $ix =~ s@.*/@@;  # Strip all but the last element of the path.
} else {
    $dlh[0] = "$ix/QBASH.doclenhist";
}
  
$dlh[1] = "Piecewise/${ix}_${emultype}_QBASH.doclenhist";

print "Doclenhists: $dlh[0], $dlh[1]\nIndex_name=$ix\n";



$pcfile = "Piecewise/${ix}_${emultype}_real_v_mimic_doclens_plot.cmds";
die "Can't open $pcfile for writing\n" unless open P, ">$pcfile";

print P "
set terminal pdf
set size ratio 0.7071
set xlabel \"Doc Length\"
set ylabel \"Frequency\"
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
set output \"Piecewise/${ix}_${emultype}_real_v_mimic_doclens.pdf\"
plot \"$dlh[0]\" title \"Real\" pt 7 ps 0.75, \"$dlh[1]\" title \"Mimic\" pt 7 ps 0.25
";

close P;

$cmd = "gnuplot $pcfile\n";
$code = system($cmd);
die "$cmd failed" if $code;


print "Plot commands in $pcfile. To view the PDF use:

    acroread Piecewise/${ix}_${emultype}_real_v_mimic_doclens.pdf
\n";
exit(0);
