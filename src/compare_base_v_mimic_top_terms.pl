#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

use File::Copy "cp";
use Cwd;
$cwd = getcwd();
if ($cwd =~ m@/cygdrive@) {
    $cwd =~ s@/cygdrive/([a-zA-Z])/@$1:/@;
}

print "Being run in directory: $cwd\n";


$k = 15;   # We'll compare the top 15 most frequent words by default

${corpus_name} = $ARGV[0];
$tfmodel = $ARGV[1];
$trmodel = $ARGV[2];
$dlmodel = $ARGV[3];
$depmodel = $ARGV[4];
${emulation_method} = "${trmodel}_${dlmodel}_${depmodel}";
$vocabBase = "../Experiments/Base/${corpus_name}_vocab_by_freq.tsv";
$vocabMimic = "../Experiments/Emulation/$tfmodel/${emulation_method}/${corpus_name}_vocab_by_freq.tsv";


die "Usage: $0 <corpus_name> <tf_model> <tr_model> <dl_model> [<k>]\n
  - must run in same directory as this script.
\n"
    unless defined($corpus_name) && defined($tfmodel) && defined($trmodel) && defined($dlmodel);

die "Failed to read $vocabBase\n"
    unless -r $vocabBase;
die "Failed to read $vocabMimic\n"
    unless -r $vocabMimic;


$k = $ARGV[5] if ($#ARGV >= 5);
$comparison_file = "../Experiments/Emulation/$tfmodel/$emulation_method/${corpus_name}_base_v_mimic_top_${k}_terms.txt";

die "VBASE" unless open VBASE, $vocabBase;
die "VMIMIC" unless open VMIMIC, $vocabMimic;
die "Can't open output file $comparison_file\n" unless open CF, ">$comparison_file";

print CF "# Comparison of top $k most frequent terms, base v. mimic for $corpus_name
#                Base                 Mimic
# -----------------------------------------
";

for ($l = 0; $l < $k; $l++) {
    $vb = <VBASE>;
    $vm = <VMIMIC>;
    $vb =~ s/\t.*\n//;
    $vm =~ s/\t.*\n//;
    print CF sprintf("%20s  %20s\n", $vb, $vm);
}

close VBASE;
close VMIMIC;
close CF;

print "To compare popular words in the base and synthetic corpora: 

      cat $comparison_file

";

exit(0);


# -------------------------------------------------------


