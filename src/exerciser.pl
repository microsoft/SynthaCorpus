#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.

# Scripts are now called via quotemeta($^X) in case the perl we want is not in /usr/bin/
$perl = $^X;

use Cwd;
$cwd = getcwd;
if ($cwd =~ m@/cygdrive@) {
    $cwd =~ s@/cygdrive/([a-zA-Z])/@$1:/@;
}


$|++;

if ($#ARGV >= 0) {
    for ($a = 0; $a <= $#ARGV; $a++) {  
	if ($ARGV[$a] =~/thorough/i) {
	    $thorough = "yes";
	} elsif ($ARGV[$a] =~/VS/i) {
	    $useVS = 1;
	} else {
	    die "Error: $0: Unrecognized argument $ARVG[$a]\n";
	}
    }
}

mkdir "../Experiments"
	unless -e "../Experiments";


run("make cleanest");
run("make") if (!$useVS);

$converter = check_exe("convertPGtoSTARC.exe");
$queryGen = check_exe("queryGenerator.exe");


run("$converter ../ProjectGutenberg/*.txt > ../Experiments/PG.STARC");
if (defined($thorough)) {
    print "Running a thorough series of emulations ...\n";
    run("$perl ./emulateARealCorpus.pl PG Linear base26 dlhisto ind");
    run("$perl ./emulateARealCorpus.pl PG Linear base26 dlhisto ind -dependencies=neither");
    run("$perl ./emulateARealCorpus.pl PG Linear base26 dlhisto ind -dependencies=base");
    run("$perl ./emulateARealCorpus.pl PG Linear base26 dlhisto ind -dependencies=mimic");
    run("$perl ./emulateARealCorpus.pl PG Linear base26 dlhisto ind -dependencies=both");
    run("$perl ./emulateARealCorpus.pl PG Copy from_tsv dlhisto ngrams2");
    run("$perl ./emulateARealCorpus.pl PG Piecewise base26 dlhisto ind");
    run("$perl ./emulateARealCorpus.pl PG Piecewise base26 dlnormal ind");
    #run("$perl ./emulateARealCorpus.pl PG Piecewise markov-0 dlhisto ind");
    run("$perl ./emulateARealCorpus.pl PG Piecewise markov-0e dlhisto ind");
    #run("$perl ./emulateARealCorpus.pl PG Piecewise markov-5 dlhisto ind");
    run("$perl ./emulateARealCorpus.pl PG Piecewise markov-5e dlhisto ngrams3");
} else {
    print "Skipping the thorough tests ...\n";
    sleep 3;
}

run("$perl ./emulateARealCorpus.pl PG Piecewise markov-5e dlhisto ngrams3");
run("$queryGen corpusFileName=../Experiments/PG.STARC propertiesStem=../Experiments/Base/PG  -numQueries=1000");
run("$queryGen corpusFileName=../Experiments/Emulation/Piecewise/markov-5e_dlhisto_ngrams3/PG.starc propertiesStem=../Experiments/Emulation/Piecewise/markov-5e_dlhisto_ngrams3/PG  -numQueries=1000");
run("$perl ./samplingExperiments.pl PG");
run("$perl ./scaleUpASample.pl PG 100 Linear markov-5e dlnormal ind");
run("$queryGen corpusFileName=../Experiments/Scalingup/Working/PG.tsv propertiesStem=../Experiments/Scalingup/Working/PG  -numQueries=1000");

print "

    Brilliant!  The exerciser script has finished normally :-)

";  

    exit(0);

# ---------------------------------------------



sub check_exe {
    # The argument is expected to be the name of either a
    # perl script or an executable.  For a perl script we check that
    # a script of that name exists in the current directory.  If it
    # does we convert its name into an absolute path and return the
    # command to run it using the perl interpreter which invoked us.
    #
    # In the case of an EXE, we look in the current directory first
    # (priority to the GCC executables).  If not there, we look in
    # the place where VS2015 puts its executables.  If we succeed
    # we return an absolute path to the EXE.
    #
    # Error exit if we don't find what we want.
    my $exe = shift;
    if ($exe =~ /\.pl$/) {
	die "$exe doesn't exist.\n"
	    unless -r $exe;
	return $exe if ($exe =~ m@/@);  # It was a path, not a name.
	return "$perl $cwd/$exe";
    } else {
	# Try the GCC built EXE first
	if (-x $exe) {
	    print "Is executable: $exe\n";
	    return $exe if ($exe =~ m@/@);  # It was a path, not a name.
	    return "$cwd/$exe";
	} else {
	    my $vsexe = "BuildAllExes/x64/Release/$exe";	    
	    die "Can't find either GCC or VS2015 versions of $exe.\n"
		unless -x $vsexe;
	    return "$cwd/$vsexe";
	}
    }
}




sub run {
    my $cmd = shift;
    print $cmd, "\n";
    my $code = system($cmd);
    die "Command $cmd failed with code $code\n"
	if ($code);
}
