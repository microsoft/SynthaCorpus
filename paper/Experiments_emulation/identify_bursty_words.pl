#! /usr/bin/perl -w

# Assumes that a repetitions.tsv file has been built by first running
# TFdistribution_from_TSV.exe with the -singletons_too option, and then
# running QBASH_vocab_lister.exe with -sort=alpha
# 
# If built correctly repetitions.tsv should show terms with frequency of
# occurrence after an @ symbol.  E.g. david@3 means the term david occurring
# exactly 3 times in a document.  The file should be sorted alphabetically
# so that all the records for a term are contiguous.

#  david@1 13970
#  david@10        2
#  david@12        1
#  david@2 1222
#  david@3 201
#  david@4 69
#  david@5 27
#  david@6 7
#  david@7 7
#  david@8 3
#  david@9 3

use POSIX;

$|++;

$show_non_bursties_too = 0;

$QB_subpath = "GIT/Qbasher";
$ix_subpath = "QBASHER/indexes";
@QB_paths = (
    "C:/Users/dahawkin/$QB_subpath",  # Dave Laptop
    "D:/dahawkin/$QB_subpath",  # Redsar
    "S:/dahawkin/$QB_subpath",    # HeavyMetal
    "F:/dahawkin/$QB_subpath",    # RelSci00
    );

    

die "Usage: $0 <indexname or index path>  [run_label]\n"
    unless $#ARGV >= 0 && $#ARGV <= 1;

$show_non_bursties_too = 1 
    if (defined($ARGV[1]) && $ARGV[1] =~ /-(all|non)/);

print "\n*** $0 @ARGV\n\n";

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


if ($#ARGV == 1) {
    $ilog = "$idxdir/$ARGV[1]_index.log";
} else {
    $ilog = "$idxdir/index.log";
}


$idxname = $idxdir;
$idxname =~ s@.*/@@;  # Strip all but the last element of the path.

get_index_sizes();  # Sets $N (num docs) and $postings from index.log
#$w = POSIX::floor($postings / $N);  # average document length
$w = $postings / $N;
print STDERR "N = $N
Postings = $postings
Ave doc len = $w
";
$bursty_count = 0;

die "Can't open $idxdir/repetitions.tsv\n"
    unless open REPS, "$idxdir/repetitions.tsv";

print STDERR "Processing $idxdir/repetitions.tsv

Word            \tTOF\tDF\tExpected_DF\t(burstiness)\n";


$curword = "*";

while (<REPS>) {
    die "Invalid record $_" unless /^(\S+)@(\d+)\t(\d+)/;
    $word = $1; 
    $ox = $2;
    $freq = $3;
    if ($word ne $curword) {
	if ($curword ne "*") {
	    # Symbols defined in Burstiness section of paper
	    $q = $tof / $postings;
	    $P = 1 - ((1 - $q) ** $w);
	    $expected_df = $N * $P;
	    $stdev = sqrt($expected_df * (1 - $P));
	    $CF = $expected_df - (1.65 * $stdev);
	    if ($stdev > 0.000001) {
		$Zscore = sprintf("%.3f", ($df - $expected_df) / $stdev);
	    } else {
		print "Would-be DIV by ZERO:  $_\n";
		$Zscore = "0.000";
	    }
	    $expected_df = sprintf("%.0f", $expected_df);
	    $pword = sprintf("%-16s", $word);
	    if ($df < $CF) {
		print "$pword\t$tof\t$df\t$expected_df\tBURSTY\t$Zscore\n";
		$bursty_count++;
	    } elsif ($show_non_bursties_too) {
		print "$pword\t$tof\t$df\t$expected_df\t-\t-\n";
	    }
	}
	$df = 0;
	$tof = 0;
	$curword = $word;
    }
    # Sum the dfs and term occurrence freqs for each of the entries for a word, 
    # e.g. bull@5, bull@3, bull@6  .. It's assumed they're contiguous
    $df += $freq;
    $tof += ($ox * $freq);
}

if ($curword ne "*") {
    # Symbols defined in Burstiness section of paper
    $q = $tof / $postings;
    $P = 1 - ((1 - $q) ** $w);
    $expected_df = $N * $P;
    $stdev = sqrt($expected_df * (1 - $P));
    $CF = $expected_df - (1.65 * $stdev);
    $Zscore = sprintf("%.3f", ($df - $expected_df) / $stdev);
    
    $expected_df = sprintf("%.0f", $expected_df);
    $pword = sprintf("%-16s", $word);
    if ($df < $CF) {
	print "$word\t$tof\t$df\t$expected_df\tBURSTY\t$Zscore\n";
	$bursty_count++;
    } elsif ($show_non_bursties_too) {
	print "$word\t$tof\t$df\t$expected_df\t-\t-\n";
    }
}

if ($#ARGV == 1) {
    print STDERR "\nBursty words found in $ARGV[1]: $bursty_count\n\n";
} else {
    print STDERR "\nBursty words found in $idxname: $bursty_count\n\n";
}


exit(0);


#-----------------------------------------------------------------------

sub get_index_sizes {
    # Extract num_docs and num_postings from index.log
    die "Can't read $ilog\n"
	unless -r "$ilog";

    my $itail = `tail -3 $ilog`;
    die "Tail failed\n"
	if $?;

    if ($itail =~ /Total postings: ([0-9]+);/s) {
	$postings = $1;
    } else {
	die "Can't extract postings from $ilog\n$itail\n";
    }

    if ($itail =~ /to index ([0-9]+) docs/s) {
	$N = $1;
    } else {
	die "Can't extract N from $ilog\n$itail\n";
    }

 }   

    
