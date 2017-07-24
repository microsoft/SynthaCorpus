#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.



if ($#ARGV >= 0) {
    if ($ARGV[0] =~/thorough/i) {
	$thorough = "yes";
    } else {
	die "Error: $0: Unrecognized argument\n";
    }
}

mkdir "../Experiments"
    unless -e "../Experiments";

run("make cleanest");
run("make");

run("./convertPGtoSTARC.exe ../ProjectGutenberg/*.txt > ../Experiments/PG.STARC");
if (defined($thorough)) {
    print "Running a thorough series of emulations ...\n";
    run("./emulateARealCorpus.pl PG Linear base26 dlhisto ind");
    run("./emulateARealCorpus.pl PG Copy from_tsv dlhisto ngrams2");
    run("./emulateARealCorpus.pl PG Piecewise base26 dlhisto ind");
    run("./emulateARealCorpus.pl PG Piecewise base26 dlnormal ind");
    #run("./emulateARealCorpus.pl PG Piecewise markov-0 dlhisto ind");
    run("./emulateARealCorpus.pl PG Piecewise markov-0e dlhisto ind");
    #run("./emulateARealCorpus.pl PG Piecewise markov-5 dlhisto ind");
    run("./emulateARealCorpus.pl PG Piecewise markov-5e dlhisto ngrams3");
} else {
    print "Skipping the thorough tests ...\n";
    sleep 3;
}

run("./emulateARealCorpus.pl PG Piecewise markov-5e dlhisto ngrams3");
run("./queryGenerator.exe corpusFileName=../Experiments/PG.STARC propertiesStem=../Experiments/Base/PG  -numQueries=1000");
run("./queryGenerator.exe corpusFileName=../Experiments/Emulation/Piecewise/markov-5e_dlhisto_ngrams3/PG.starc propertiesStem=../Experiments/Emulation/Piecewise/markov-5e_dlhisto_ngrams3/PG  -numQueries=1000");
run("./samplingExperiments.pl PG");
run("./scaleUpASample.pl PG 100 Linear markov-5e dlnormal ind");
run("./queryGenerator.exe corpusFileName=../Experiments/Scalingup/Working/PG.tsv propertiesStem=../Experiments/Scalingup/Working/PG  -numQueries=1000");

print "

    Brilliant!  The exerciser script has finished normally :-)

";  

    exit(0);

# ---------------------------------------------

sub run {
    my $cmd = shift;
    print $cmd, "\n";
    my $code = system($cmd);
    die "Command $cmd failed with code $code\n"
	if ($code);
}
