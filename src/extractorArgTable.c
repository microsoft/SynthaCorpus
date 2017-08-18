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
#include "utils/dynamicArrays.h"
#include "corpusPropertyExtractor.h"
#include "utils/argParser.h"

arg_t args[] = {
  { "inputFileName", ASTRING, (void *)&(params.inputFileName), "This is the file of text containing the corpus whose contents are to be extracted.  Currently it can be in TSV format (.tsv), or simple text archive format (.starc)."},
  { "outputStem", ASTRING, (void *)&(params.outputStem), "The names of all the files containing extracted properties will share this prefix."},
  { "headTerms", AINT, (void *)&(params.headTerms), "The number of terms which are explicitly modelled in the term frequency distribution (TFD) model."},
  { "piecewiseSegments", AINT, (void *)&(params.piecewiseSegments), "The number of linear segments in the middle section of the TFD model."},
  { "minNgramWords", AINT, (void *)&(params.minNgramWords), "Only record Ngrams with at least this many words."},
  { "maxNgramWords", AINT, (void *)&(params.maxNgramWords), "Only record Ngrams with at least this many words."},
  { "zScoreCriterion", AFLOAT, (void *)&(params.zScoreCriterion), "If greater than 0.0, only 'significant' Ngrams - those whose Zscore exceeds the criterion - will be written."},
  { "ignoreDependencies", ABOOL, (void *)&(params.ignoreDependencies), "If TRUE time-consuming extraction of word compounds and repetitions will be skipped and those files not written."},  
  { "separatelyReportBigrams", ABOOL, (void *)&(params.separatelyReportBigrams), "If TRUE the _bigrams.* files will be written (provided that minNgramWords is 2)."},
  { "ngramObsThresh", AINT, (void *)&(params.ngramObsThresh), "Only record Ngrams which occur at least this many times."},
  { "", AEOL, NULL, "" }
  };


void initialiseParams(params_t *params) {
  params->inputFileName = NULL;
  params->outputStem = NULL;
  params->headTerms = 10;
  params->piecewiseSegments = 10;
  params->ignoreDependencies = FALSE;
  params->minNgramWords = 2;
  params->maxNgramWords = 3;
  // By default record all Ngrams
  params->zScoreCriterion = 1.6449;  
  params->ngramObsThresh = 10;
}


void sanitiseParams(params_t *params) {
  // Make sure the Ngram length parameters make some kind of sense
  if (params->minNgramWords < 1) params->minNgramWords = 1;
  if (params->maxNgramWords < 1) params->maxNgramWords = 1;
  if (params->maxNgramWords > MAX_NGRAM_WORDS) params->maxNgramWords = MAX_NGRAM_WORDS;
  if (params->minNgramWords > params->maxNgramWords) params->minNgramWords = params->maxNgramWords;
}




