// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//

typedef struct {
  char *baseStem, *emuStem;
  BOOL verbose;
} params_t;

extern params_t params;

typedef struct {
  long long numDocs;
  char *baseVocabInMemory, *emuVocabInMemory;
  char **baseVocabLines, **emuVocabLines;  
  size_t bvSize, evSize;
  CROSS_PLATFORM_FILE_HANDLE bvFH, evFH;
  HANDLE bvMH, evMH;
  int baseVocabLineCount, emuVocabLineCount;
  FILE *queryInfile, *queryOutfile;
} globals_t;
