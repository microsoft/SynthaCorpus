// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//

typedef struct {
  char *corpusFileName;
  char *propertiesStem;
  int numQueries;
  double meanQueryLength;
  int minWordsInTargetDoc;
  BOOL verbose;
} params_t;

extern params_t params;

typedef enum {
  CORPUS_TSV,
  CORPUS_STARC,
} corpusFormat_t;


typedef struct {
  long long numDocs;
  char *corpusInMemory, *dtInMemory, *vocabInMemory;
  char **docTable;  // An ASCII version of a document table, whose columns are as
                    // follows:  (1) decimal offset within the input file,
                    // (2) length in bytes, (3) length in words, (4) number of distinct
                    // words.
  char **vocabTSV;  // A UTF-8 version of the vocabulary table, sorted in ascending
                    // alphabetic order, and containing word, total occurrence frequency,
                    // and df.
  size_t corpusSize, dtSize, vocabSize;
  CROSS_PLATFORM_FILE_HANDLE corpusFH, dtFH, vocabFH;
  HANDLE corpusMH, dtMH, vocabMH;
  int dtLines, vocabLines;
  corpusFormat_t corpusFormat;
  FILE *queryOutfile;
} globals_t;
