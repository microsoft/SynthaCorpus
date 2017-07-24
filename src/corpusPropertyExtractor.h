// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//

typedef struct {
  char *inputFileName;
  char *outputStem;
  int headTerms, piecewiseSegments, minNgramWords, maxNgramWords;
  double zScoreCriterion;
  BOOL separatelyReportBigrams;
  int ngramObsThresh;
} params_t;

extern params_t params;

extern char *fileTypes[];


typedef struct {
  double startTime;
  long long vocabSize, numDocs, numEmptyDocs, totalPostings, longestPostingsListLength;
  dahash_table_t *gVocabHash, *gNgramHash, *gWordRepsHash;
  char *inputInMemory;
  size_t inputSize;
  CROSS_PLATFORM_FILE_HANDLE inputFH;
  HANDLE inputMH;
  FILE *docTable;  // An ASCII version of a document table, whose columns are as
                   // follows:  (1) decimal offset within the input file,
                   // (2) length in bytes, (3) length in words, (4) number of distinct
                   // words.
  FILE *vocabTSV;  // A UTF-8 version of the vocabulary table, sorted in ascending
                   // alphabetic order, and containing word, total occurrence frequency,
                   // and df.
  dyna_t docWords, distinctDocWords;  // Doc length histogram and ratio of unique to total
  int greatestDocLength;
  double WelfordM_old, WelfordS_old,
    WelfordM_new, WelfordS_new;  // Welford's incremental method for computing variance
                                 // and stdev (for document lengths).
                                 // See https://www.johndcook.com/blog/standard_deviation/
} globals_t;
