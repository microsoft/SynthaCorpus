#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.



$|++;

die "Can't read UnicodeData.txt"
    unless open UD, "UnicodeData.txt";

die "Can't write to C-statements.txt\n"
    unless open CS, ">C-statements.txt";

$count_u = 0;
$count_l = 0;

while (<UD>) {
    $accent = "";
    if (/^([0-9A-F]+);LATIN (SMALL|CAPITAL) LETTER ([A-Z]+)( WITH [^;]+)?;/) {
	$code = "0x$1";
	$case = $2;
	$letter = $3;
	$accent = $4;
	$accent = "" unless defined($accent);

	#print "Accent $accent\n";

	if ($accent eq "") {
	    $t = "$case $letter";
	    $unaccented{$t} = $code;
	    #print "Unaccented entry for $t\n";
	    $count_u++;
	} 
	if ($case eq "SMALL") {
	    $t = "$letter $accent";
	    $lower{$t} = $code;
	    #print "Lower entry for $t\n";
	    $count_l++;
	}
    }
}

close UD;

print "First pass finished: $count_u unaccented + $count_l lower case\n";


die "Can't read UnicodeData.txt"
    unless open UD, "UnicodeData.txt";
$count_u = 0;
$count_l = 0;
while (<UD>) {
    $accent = "";
    if (/^([0-9A-F]+);LATIN (SMALL|CAPITAL) LETTER ([A-Z]+)( WITH [^;]+)?;/) {
	$code = "0x$1";
	$case = $2;
	$letter = $3;
	$accent = $4;
	$accent = "" unless defined($accent);

	if ($accent ne "") {
	    $t = "$case $letter";
	    if (defined($unaccented{$t})) {
		print CS "  map_unicode_to_unaccented[$code] = $unaccented{$t};  // $t$accent\n";
		$count_u++;
	    } else {
		print "No accent mapping found for $t\n";
	    }
	} 
	if ($case eq "CAPITAL") {
	    $t = "$letter $accent";
	    if (defined($lower{$t})) {
		print CS "  map_unicode_to_lower[$code] = $lower{$t};  // $case $t\n";
		$count_l++;
	    } else {
		print "No lower mapping found for $t\n";
	    }
	}
    }
}
close UD;
close CS;
print "Second pass finished: $count_u acccent mappings + $count_l case mappings\n";
   

exit 0;
