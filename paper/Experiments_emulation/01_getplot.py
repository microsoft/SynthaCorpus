#!c:/Anaconda3/python
#-*-mycompile-target:"./01_getplot.py"-*-

# Nick's script, modified by Dave to:
# 1. take the term_ratios.tsv file as an input argument
#    and print Usage message if there is no argument
# 2. Change the output format to <doc_length>\t<average_distinct_terms_in_docs_of_this_length>
#

import csv
import math
import sys
from collections import defaultdict

#fileprefix = "TREC-AP_term_ratios"
#fileprefix = "Real/AS_top500k_term_ratios"
#fileprefix = "academic_words_distinctwords_synthetic"
#/cygdrive/S/dahawkin/mlp/OSA/RelevanceSciences/Qbasher/QBASHER/indexes/TREC-AP/term_ratios.tsv
#/cygdrive/s/dahawkin/GIT/allThingsSynthetic/journal/Experiments_emulation/Piecewise/TREC-AP_term_ratios.tsv
#\\ccpsofshm\Data\dahawkin\GIT\allThingsSynthetic\journal\Experiments_emulation\Piecewise

if (len(sys.argv) < 2):
    print("Usage: ",__file__, "<name_of_term_ratios_file>\n");
    sys.exit();

filename = sys.argv[1];

# keycol = 0
# valcol = 1
# rowsize = 2
keycol = 1
valcol = 2
rowsize = 6

numdocs = defaultdict(int)
totaluniq = defaultdict(int)
with open(filename) as file:
    file.readline()
    tsvreader = csv.reader(file, delimiter='\t', quoting=csv.QUOTE_NONE)
    for row in tsvreader:
        if len(row) != rowsize:
            print( "Error:" )
            print( row )
            continue
        dictkey = int(row[keycol])
        # dictkey = math.floor(math.log10(float(row[keycol])))
        # dictkey is total number of words in a document
        numdocs[dictkey] += 1  # count of documents of length dictkey
        totaluniq[dictkey] += int(row[valcol])  # total num of distinct words
                                                # in documents of length dictkey

flout = filename.replace(".tsv", ".out");
with open(flout, "w") as file:
    for (dictkey,count) in numdocs.items():
        #file.write("%d\t%d\t%d\n" % (dictkey, count, totaluniq[dictkey]))
        file.write("%d\t%.3f\n" % (dictkey, totaluniq[dictkey]/count))


