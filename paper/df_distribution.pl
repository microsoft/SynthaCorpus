#! /usr/bin/perl -w

# Given a collection of N documents, each of length w words and a term T 
# whose total occurrence frequency is f, what is the expected DF of term T,
# assuming term independence and zero auto-correlation.

$N = 1000;
$w = 100;
$trials = 100;

for ($i = 0; $i < $trials; $i++) {
    foreach $f (10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000) {
	undef %tfs;
	# distribute the occurrences
	for ($j = 0; $j < $f; $j++) {
	    $doc = int(rand($N));
	    $tfs{$doc}++;
	}
	$dfs{$f} += keys %tfs;
    }
}

print "Documents: $N
Length of each document: $w

Term_freq  Observed_df   Expected_df
";

for $f (sort {$dfs{$a} <=> $dfs{$b}} keys %dfs) {
    $tprob = $f / ($N * $w);  # Probability that a word occurrence is this term
    $p = (1 - ((1 - $tprob) ** $w));  # probability that a document has one or
                                      # more occurrences of this term
    $expected_df = $N * $p;
    
    print sprintf("%9d %12.1f  %12.1f\n", $f, $dfs{$f} / $trials , $expected_df);
}
