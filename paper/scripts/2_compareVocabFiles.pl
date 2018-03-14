#!c:/Perl64/bin/perl
##!c:/activeStatePerl64-5_20_1/bin/perl
use strict;
use warnings;
#use Digest::FNV::XS;
$| = 1;

my $showDebugData = 0;
#my $debug = 1000;
my $debug = 0;
my $useFrequencies = 0;
my $log2Value = log(2);

if (scalar @ARGV < 1) {
    die "Usage: 2_compareVocabFiles.pl <directory/collection name: try 'academicPapers'> <optional: number of lines per file to be read (default: all)>\n";
}
if (scalar @ARGV > 1) {
    $debug = $ARGV[1];
}
my $directory = $ARGV[0];
print "Using collection: $directory\n(Assuming for now that we will always use the same files in each collection - can be changed later.)\n";

if (! -d "../images/$directory") {
    die "Can't make directory ../images/$directory\n"
	unless mkdir "../images/$directory";
}

my $graphFileLengths = "../images/$directory/termLengthHistogram.pdf";
my $graphFileUnigrams = "../images/$directory/unigramOccurrenceHistogram.pdf";
my $graphFileBigrams = "../images/$directory/bigramOccurrenceHistogram.pdf";
my $graphFileCommands = "gnuPlotInputFile.cmd";
my $graphDataFileTermLengths = "dataTermLength.dat";
my $graphDataFileUnigramCounts = "dataUnigramCounts.dat";
my $graphDataFileBigramCounts = "dataBigramCounts.dat";


my @files = ();
my %filesToGraph = ();
if (scalar @files == 0) {
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/vocab.tsv", 1);
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/markov-0.tsv", 1);
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/markov-1.tsv", 0);
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/markov-2.tsv", 0);
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/markov-3.tsv", 0);
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/markov-4.tsv", 0);
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/markov-5.tsv", 1);
    #addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/bubble_babble.tsv");
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/bodo_dave.tsv", 0);
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/base26.tsv", 1);
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/base26-sparsity.tsv", 1);
    #addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/base26-enhanced.tsv");
    #addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/base26-asparsity.tsv");
    addExperimentalFile(\@files, \%filesToGraph, "../data/$directory/fnv_base26.tsv", 1);
}


my %fileNameToMethodNameMappings = ();
foreach my $fileName (@files) {
    my $methodName = $fileName;
    $methodName =~ s@^.*/@@;
    $methodName =~ s/\..*$//;
    $methodName =~ s/_/-/g;
    if ($methodName eq "vocab") {
        $methodName = "actual-text";
    }
    $fileNameToMethodNameMappings{$fileName} = $methodName;
}



#my @histograms = ();
my @statistics = ();

my %lengthHistogram = ();
my %characterUnigramHistogram = ();
my %characterBigramHistogram = ();

printf "Reading files:\n";
if ($showDebugData != 0) {
    printf "collection\tterms\ttotal count\tcharacters\thash collisions\n";
}
#foreach my $file (@files) {
foreach (my $x = 0; $x < scalar @files; $x++) {
    #my %hash = ();
    my %statsHash = ();
    #push(@histograms, %hash);
    push(@statistics, %statsHash);
    my $file = $files[$x];
    printf "\tReading file $fileNameToMethodNameMappings{$files[$x]}\n\t\t";
    my $distinctTermCount = 0;

    my $tempFileForHashes = $files[$x];
    $tempFileForHashes =~ s/\.tsv/-hashes.txt/;
    die "Hash table file name is the same as input file name - exiting!\n" if ($files[$x] eq $tempFileForHashes);
    #open HASHES, "> $tempFileForHashes";
    my $totalTermCount = 0;
    my $totalUnigramCount = 0;
    my $totalBigramCount = 0;
    my $hashCollisions = 0;

    die "Can't find file '$file'.  Exiting.\n" unless -r $file;

    my $fileLength = $debug;

    if ($debug > 0) {
        open IN, "head -$debug $file | ";
    } else {
        open IN, "$file";
        my $wcCommand = "wc -l $file";
        $fileLength = `$wcCommand`;
        $fileLength =~ s/ .*$//;
    }

    my $powerOfTwo = 0;
    while ((2 ** $powerOfTwo) < ($fileLength * 1.2)) {
        $powerOfTwo++;
    }
    my $vocabSize = 2 ** $powerOfTwo;
    $statistics[$x]{hashTableSize} = $vocabSize;
    $statistics[$x]{hashTablePowerOfTwo} = $powerOfTwo;

    my $line = "";
    while (defined($line = <IN>)) {
        if ($. % 100000 == 0) {
            printf ".";
        }
        my ($term, $frequency) = split(/[\r\t\n]/, $line);
        if ($useFrequencies == 0) {
            $frequency = 1;
        }

        $totalTermCount += $frequency;
        if ($frequency > 0) {
            #my $hash = Digest::FNV::XS::fnv1a_64 $term, 0;
            my $hash = 2;
            $hash = $hash % $vocabSize;
            #print HASHES "$hash\n";
        }

        my $length = length($term);
        $lengthHistogram{$x}{$length} += $frequency;
        $lengthHistogram{0}{$length} += 0;
        my $previousCharacter = '';
        for my $character (split(//, $term)) {
        #for (my $i = 0; $i < 10; $i++) {
            $characterUnigramHistogram{$x}{$character} += $frequency;
            $characterUnigramHistogram{0}{$character} += 0;
            $totalUnigramCount += $frequency;
            if ($previousCharacter ne '') {
                $characterBigramHistogram{$x}{$previousCharacter . $character} += $frequency;
                $characterBigramHistogram{0}{$previousCharacter . $character} += 0;
                $totalBigramCount += $frequency;
            }
            $previousCharacter = $character;
        }
        $distinctTermCount++;
        if ($distinctTermCount % 100000 == 0) {
            printf ".";
        }
    }
    close IN;
    printf "\n\t\tCalculating hash collisions now. - ";
    if ($showDebugData != 0) {
        printf "real\t%d\t%d\t%d\t%d\n", $distinctTermCount, $totalTermCount, $totalUnigramCount, $hashCollisions;
    }

=cut
    close HASHES;
    my $tempFile = "tempUniqueHashes.txt";
    my $command = "d:/bodob/bin/tmsnsort/tmsnsort -q -t 7 -u $tempFileForHashes $tempFile";
    system("$command");
    $command = "wc -l $tempFile";
    my $hashTableSize = `$command`;
    $hashTableSize =~ s/ .*$//;
    $statistics[$x]{hashTableUsed} = $hashTableSize;
    $statistics[$x]{hashCollisions} = $totalTermCount - $hashTableSize;
=cut
    $statistics[$x]{hashTableUsed} = 10;
    $statistics[$x]{hashCollisions} = $totalTermCount - 10;
    $statistics[$x]{totalTermCount} = $totalTermCount;
    $statistics[$x]{totalUnigramCount} = $totalUnigramCount;
    $statistics[$x]{totalBigramCount} = $totalBigramCount;
    $statistics[$x]{numberOfDistinctTerms} = $distinctTermCount;
    printf "Done.\n";
}
print "\nHash table collisions:\n";
printf "Collection\t#Distinct Terms\t2 ** x\tCollisions\tUsed slots (/TableSize)\tfraction of collisions\n";
foreach (my $x = 0; $x < scalar @files; $x++) {
    printf "%s\t%d\t%d\t%d\t%d (/%d)\t%f\n", $fileNameToMethodNameMappings{$files[$x]}, $statistics[$x]{numberOfDistinctTerms}, $statistics[$x]{hashTablePowerOfTwo}, $statistics[$x]{hashCollisions}, $statistics[$x]{hashTableUsed}, $statistics[$x]{hashTableSize}, $statistics[$x]{hashTableUsed} / $statistics[$x]{hashTableSize};
}

print "\nLength Divergences:\n";
foreach (my $x = 1; $x < scalar @files; $x++) {
    my $jsdPQ = calculateJSD(\%{$lengthHistogram{0}}, \%{$lengthHistogram{$x}}, $statistics[0]{totalTermCount}, $statistics[$x]{totalTermCount});
    printf "\t%s\t%f\n", $fileNameToMethodNameMappings{$files[$x]}, $jsdPQ;
}

open OUT_DATA_TERM_LENGTH, "> $graphDataFileTermLengths";
printf OUT_DATA_TERM_LENGTH "TermLength";
if ($showDebugData != 0) {
    print "\nLength Histogram:\n";
    printf "TermLength";
}
my $columnsToGraph = 0;
foreach (my $x = 0; $x < scalar @files; $x++) {
    if ($filesToGraph{$files[$x]} == 0) {
        next;
    }
    $columnsToGraph++;
    printf OUT_DATA_TERM_LENGTH "\t%s", $fileNameToMethodNameMappings{$files[$x]};
    if ($showDebugData != 0) {
        printf "\t%s", $fileNameToMethodNameMappings{$files[$x]};
    }
}
printf OUT_DATA_TERM_LENGTH "\n";
foreach my $length (sort{$a <=> $b} keys %{$lengthHistogram{0}}) {
    printf OUT_DATA_TERM_LENGTH "$length";
    if ($showDebugData != 0) {
        printf "$length";
    }
    foreach (my $x = 0; $x < scalar @files; $x++) {
        if ($filesToGraph{$files[$x]} == 0) {
            next;
        }
        my $lengthCount = 0;
        if (defined($lengthHistogram{$x}{$length})) {
            $lengthCount = $lengthHistogram{$x}{$length};
        }
        printf OUT_DATA_TERM_LENGTH "\t%f", $lengthCount / $statistics[$x]{totalTermCount};
        if ($showDebugData != 0) {
            printf "\t%f", $lengthCount / $statistics[$x]{totalTermCount};
        }
    }
    printf OUT_DATA_TERM_LENGTH "\n";
    if ($showDebugData != 0) {
        printf "\n";
    }
}
close OUT_DATA_TERM_LENGTH;

open OUT_GRAPH, "> $graphFileCommands";
print OUT_GRAPH "
set terminal pdf
set output \"$graphFileLengths\"
# Make the x axis labels easier to read.
#set xtics rotate out 5
# set xtics 5
# Select histogram data
set style data histogram
set style histogram clustered
set style fill solid border
set title \"Term Length Histogram\"
# set xrange [0:24]
set xlabel \"Term Length\"
set ylabel \"Probability\"
plot for [COL=2:" . ($columnsToGraph + 1) . "] '$graphDataFileTermLengths' using COL:xticlabels(1) title columnheader
";
print OUT_GRAPH "\n\n";









open OUT_DATA_UNIGRAM_COUNTS, "> $graphDataFileUnigramCounts";
printf OUT_DATA_UNIGRAM_COUNTS "Frequency";
if ($showDebugData != 0) {
    print "\nCharacter Histogram:\n";
    printf "Frequency";
}
foreach (my $x = 0; $x < scalar @files; $x++) {
    if ($filesToGraph{$files[$x]} == 0) {
        next;
    }
    printf OUT_DATA_UNIGRAM_COUNTS "\t%s", $fileNameToMethodNameMappings{$files[$x]};
    if ($showDebugData != 0) {
        printf "\t%s", $fileNameToMethodNameMappings{$files[$x]};
    }
}
printf OUT_DATA_UNIGRAM_COUNTS "\n";
if ($showDebugData != 0) {
    printf "\n";
}
foreach my $frequency (sort{$a cmp $b} keys %{$characterUnigramHistogram{0}}) {
    printf OUT_DATA_UNIGRAM_COUNTS "$frequency";
    if ($showDebugData != 0) {
        printf "$frequency";
    }
    foreach (my $x = 0; $x < scalar @files; $x++) {
        if ($filesToGraph{$files[$x]} == 0) {
            next;
        }
        my $frequencyCount = 0;
        if (defined($characterUnigramHistogram{$x}{$frequency})) {
            $frequencyCount = $characterUnigramHistogram{$x}{$frequency};
        }
        printf OUT_DATA_UNIGRAM_COUNTS "\t%f", $frequencyCount / $statistics[$x]{totalUnigramCount};
        if ($showDebugData != 0) {
            printf "\t%d", $frequencyCount;
        }
    }
    printf OUT_DATA_UNIGRAM_COUNTS "\n";
    if ($showDebugData != 0) {
        printf "\n";
    }
}
close OUT_DATA_UNIGRAM_COUNTS;

print OUT_GRAPH "
set terminal pdf
set output \"$graphFileUnigrams\"
# Make the x axis labels easier to read.
#set xtics rotate out 5
# set xtics 5
# Select histogram data
set style data histogram
set style histogram clustered
set style fill solid border
set title \"Unigram Histogram\"
# set xrange [0:24]
set xlabel \"Unigram Occurrences\"
set ylabel \"Probability\"
plot for [COL=2:" . ($columnsToGraph + 1) . "] '$graphDataFileUnigramCounts' using COL:xticlabels(1) title columnheader
";
print OUT_GRAPH "\n\n";

print "\nUnigram Frequency Divergences:\n";
foreach (my $x = 1; $x < scalar @files; $x++) {
    my $jsdPQ = calculateJSD(\%{$characterUnigramHistogram{0}}, \%{$characterUnigramHistogram{$x}}, $statistics[0]{totalUnigramCount}, $statistics[$x]{totalUnigramCount});
    printf "\t%s\t%f\n", $fileNameToMethodNameMappings{$files[$x]}, $jsdPQ;
}




if ($showDebugData != 0) {
    print "\nCharacter Bigram Histogram:\n";
}
open OUT_DATA_BIGRAM_COUNTS, "> $graphDataFileBigramCounts";
printf OUT_DATA_BIGRAM_COUNTS "Frequency";
foreach (my $x = 0; $x < scalar @files; $x++) {
    if ($filesToGraph{$files[$x]} == 0) {
        next;
    }
    printf OUT_DATA_BIGRAM_COUNTS "\t%s", $fileNameToMethodNameMappings{$files[$x]};
    if ($showDebugData != 0) {
        printf "\t%s", $fileNameToMethodNameMappings{$files[$x]};
    }
}
printf OUT_DATA_BIGRAM_COUNTS "\n";
if ($showDebugData != 0) {
    printf "\n";
}
foreach my $frequency (sort{$a cmp $b} keys %{$characterBigramHistogram{0}}) {
    printf OUT_DATA_BIGRAM_COUNTS "$frequency";
    if ($showDebugData != 0) {
        printf "$frequency";
    }
    foreach (my $x = 0; $x < scalar @files; $x++) {
        if ($filesToGraph{$files[$x]} == 0) {
            next;
        }
        my $frequencyCount = 0;
        if (defined($characterBigramHistogram{$x}{$frequency})) {
            $frequencyCount = $characterBigramHistogram{$x}{$frequency};
        }
        if ($statistics[$x]{totalBigramCount} == 0) {
            printf OUT_DATA_BIGRAM_COUNTS "\t%f", 0;
        } else {
            printf OUT_DATA_BIGRAM_COUNTS "\t%f", $frequencyCount / $statistics[$x]{totalBigramCount};
        }
        if ($showDebugData != 0) {
            printf "\t%d", $frequencyCount;
        }
    }
    printf OUT_DATA_BIGRAM_COUNTS "\n";
    if ($showDebugData != 0) {
        printf "\n";
    }
}
close OUT_DATA_BIGRAM_COUNTS;

print OUT_GRAPH "
set terminal pdf
set output \"$graphFileBigrams\"
# Make the x axis labels easier to read.
#set xtics rotate out 5
# set xtics 5
# Select histogram data
set style data histogram
set style histogram clustered
set style fill solid border
set title \"Bigram Histogram\"
# set xrange [0:24]
set xlabel \"Bigram Occurrences\"
set ylabel \"Probability\"
plot for [COL=2:" . ($columnsToGraph + 1) . "] '$graphDataFileBigramCounts' using COL:xticlabels(1) title columnheader
";
print OUT_GRAPH "\n\n";
close OUT_GRAPH;

print "\nBigram Frequency Divergences:\n";
foreach (my $x = 1; $x < scalar @files; $x++) {
    my $jsdPQ = calculateJSD(\%{$characterBigramHistogram{0}}, \%{$characterBigramHistogram{$x}}, $statistics[0]{totalBigramCount}, $statistics[$x]{totalBigramCount});
    printf "\t%s\t%f\n", $fileNameToMethodNameMappings{$files[$x]}, $jsdPQ;
    #my $jsdPQ = calculateJSD(\%{$characterBigramHistogram{$x}}, \%{$characterBigramHistogram{0}}, $statistics[$x]{totalBigramCount}, $statistics[0]{totalBigramCount});
    #printf "\t%s\t%f\n", $fileNameToMethodNameMappings{$files[$x]}, $jsdPQ;
}

system("gnuplot $graphFileCommands");
if ($debug == 0) {
    system("rm $graphDataFileTermLengths");
    system("rm $graphDataFileBigramCounts");
    system("rm $graphFileCommands");
}







sub addExperimentalFile {
    my ($array, $graphingHash, $file, $graphingIndicator) = @_;
    if (-r $file) {
        push(@{$array}, $file);
        $graphingHash->{$file} = $graphingIndicator;
        return 0;
    } else {
        printf "\tCouldn't find file $file - ignoring\n";
        return 1;
    }
}

sub calculateJSD {
    my ($distrib1, $distrib2, $totalCount1, $totalCount2) = @_;
    my $dpm = 0;
    my $dqm = 0;
    my $jsdPM = 0;
    my $jsdQM = 0;
    foreach my $attribute (keys %{$distrib1}) {
        my $qi = $distrib1->{$attribute} / $totalCount1;
        my $pi = 0;
        if (defined($distrib2->{$attribute})) {
            $pi = $distrib2->{$attribute} / $totalCount2;
        }
        my $mi = ($pi + $qi) / 2;
#printf "\t$attribute: mi = $mi\n";
        if ($mi != 0) {
            if ($pi != 0) {
                $jsdPM += ($pi * log2($pi/$mi));
#printf "jsdPM = $jsdPM\n";
            }
            if ($qi != 0) {
                $jsdQM += ($qi * log2($qi/$mi));
#printf "jsdPM = $jsdQM\n";
            }
        }
    }
    my $jsdPQ = $jsdPM / 2 + $jsdQM / 2;
    return $jsdPQ;
}

sub log2 {
    my $n = shift;
    return log($n)/$log2Value;
}
