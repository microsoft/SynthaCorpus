#! /usr/bin/perl -w

# Runs the emulation scripts for each of the collections
# to be used in the planned collection simulation paper.  
# Scripts are now called via $^X in case the perl we want is not in /usr/bin/

$|++;

$usage = "Usage: $0 dlnormal|dlsegs|dlgamma|dlhisto -test|-quick|-all [-rebuild][-suppress_extras][-parallel=<int>]

   If -rebuild is given, it will be passed to the collection emulation script
   causing indexes and other structures to be rebuilt.
\n";
die $usage unless $#ARGV >= 1;

$dlmodel = $ARGV[0];
die "Document length model  must be either 'dlnormal', 'dlsegs' (adaptive piecewise) or 'dlgamma'\n"
    unless $dlmodel eq "dlsegs" || $dlmodel eq "dlgamma" 
    || $dlmodel eq "dlnormal" || $dlmodel eq "dlhisto";


$perl = $^X;
$perl =~ s@\\@/@g;

$QB_subpath = "GIT/Qbasher";
$ix_subpath = "QBASHER/indexes";
@QB_paths = (
    "C:/Users/dahawkin/$QB_subpath",  # Dave Laptop
    "D:/dahawkin/$QB_subpath",  # Redsar
    "S:/dahawkin/$QB_subpath",    # HeavyMetal
    "F:/dahawkin/$QB_subpath",    # RelSci00
    );



print "\n*** $0 @ARGV\n\n";

$emu_opts = " -dl_adjust ";

$suppress_extras = 0;

$fa = 2;
for ($a = $fa; $a <=$#ARGV; $a++) {
    if ($ARGV[$a] eq "-rebuild") {
	$emu_opts = $ARGV[$a];
    } elsif ($ARGV[$a] eq "-suppress_extras") {
	$suppress_extras = 1;
    } else {
	die "Unrecognized option $ARGV[$a]\n";
    }
}

%corpus_specific_options = (
	"TREC-AP" => "-long_docs",
	"Indri-WT10g" => "-long_docs",
	clueWeb12BodiesLarge => "-long_docs",
    );


$collections_skipped = "";

if ($ARGV[1] eq "-test") {
    @collections = ("AS_top500k");
} elsif ($ARGV[1] eq "-quick") {
    @collections = (
	# Can be run on Dave's laptop
	"Top100M",
	"classificationPaper",
	"Wikipedia",
	"TREC-AP",
	);
} elsif ($ARGV[1] eq "-all") {
    @collections = (
	"Top100M",
	"classificationPaper",
	"Wikipedia",
	"AcademicID",
	"ClueWeb12Titles",
	"Tweets",
       	"TREC-AP",
	"Indri-WT10g",
	"clueWeb12BodiesLarge",
	);
} else {
    die $usage;
}

@scripts = (
    # Script_name, additional args
    "./emulate_a_real_collection.pl,$emu_opts",
    );

# Try prefixing indexpaths for Dave's Laptop, Redsar and HeavyMetal
undef $idxdir;
for $p (@QB_paths) {
    $t = "$p/$ix_subpath";
    print "Trying: $t\n";
    if (-d $t) {
	$idxdir = $t;
	last;
    }
}
    die "Couldn't locate index directory in any of the known places\n"
	unless defined($idxdir);


$idxname = $idxdir;
$idxname =~ s@.*/@@;  # Strip all but the last element of the path.


# Check that everything is in place

foreach $sa (@scripts) {
    @s = split /,/, $sa;
    die "Can't execute $sa\n" unless -e $s[0];
}

# Now run all the scripts against the defined collections if they exist
foreach $c (@collections) {
    if (! -d "$idxdir/$c") {
	print "\n---> SKIPPING $c --->\n";
	$collections_skipped .= "$c, ";
	next;
    }
    foreach $sa (@scripts) {
	@s = split /,/, $sa;
	$cmd = "$perl $s[0] $c $dlmodel $s[1]";
	$cmd .= $corpus_specific_options{$c}
	    if (defined($corpus_specific_options{$c}));
	if ($s[0] =~ /sampling_experiments/ 
	    && $suppress_extras) {
	    print  "-- bigrams data will not be generated for $c --\n";
	    $cmd .= " -suppress_extras" 
	}
	$cmd .= "\n";
	print $cmd;
	$rez = `$cmd`;
	die "Command $cmd failed with code $?\n$rez\n" if $?;
    }
}


if ($collections_skipped ne "") {
    print "Finished but these collections skipped:
$collections_skipped\n\n";
} else {
    print "\nAll done!\n\n";
}


exit(0);
