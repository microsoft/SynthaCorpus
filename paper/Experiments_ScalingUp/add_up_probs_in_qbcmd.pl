#! /usr/bin/perl -w

# Read in a QBASHI command and add up the probabilities in head, middle, and tail sections

$cmd = <>;

while ($cmd =~ m@(\S+)=(\S+)@g) {
    $options{$1} = $2;
}

$head_prop = 0;
while ($options{-x_head_term_percentages} =~ m@([0-9.]+)@g) {
    $head_prop += $1;
}

$head_prop /= 100;  #Convert to proportion
print "Head proportion: $head_prop\n";

$tail_prop = ($options{-x_synth_vocab_size} * $options{-x_zipf_tail_perc} / 100.0) / $options{-x_synth_postings};
print "Tail proportion: $tail_prop\n";

$mid_prop = 0;
while ($options{-x_zipf_middle_pieces} =~ m@,.+?,.+?,([0-9.]+),.+?%@g) {
    $mid_prop += $1;
    print "          $1\n";
}

$should_be = 1 - ($head_prop + $tail_prop);
print "Mid proportion: $mid_prop
  (should be: $should_be)\n";

exit(0);
