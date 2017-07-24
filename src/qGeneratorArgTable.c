// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Table of command line argument definitions for the corpus
// property extractor.  The functions in argParser.c operate
// on this array.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "definitions.h"
#include "utils/dahash.h"
#include "queryGenerator.h"
#include "utils/argParser.h"

arg_t args[] = {
  { "corpusFileName", ASTRING, (void *)&(params.corpusFileName), "This is the file of text comprising the corpus from which known items and queries are to be generated.  Currently it can be in TSV format (.tsv), or simple text archive format (.starc)."},
  { "propertiesStem", ASTRING, (void *)&(params.propertiesStem), "The names of all the properties files containing extracted properties and generated queries file will share this prefix."},
  { "numQueries", AINT, (void *)&(params.numQueries), "How many queries to generate."},
  { "meanQueryLength", AFLOAT, (void *)&(params.meanQueryLength), "Mean of truncated normal distribution of query lengths. Standard deviation will be half this value."},
   { "minWordsInTargetDoc", AINT, (void *)&(params.minWordsInTargetDoc), "Known-item targets must have at least this number of distinct words (and at least as many as the query)."},
 
  { "", AEOL, NULL, "" }
  };


void initialiseParams() {
  params.corpusFileName = NULL;
  params.propertiesStem = NULL;
  params.numQueries = 10;
  params.meanQueryLength = 5;
  params.minWordsInTargetDoc = 5;
}



