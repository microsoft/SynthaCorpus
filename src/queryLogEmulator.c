// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This is the main module for queryLogEmulator.exe which attempts to produce an
// emulated query log from a base query log.  The emulated query log must be
// compatible with an emulated corpus and the combination of emulated query log
// and emulated corpus should allow accurate simulation of running the base
// query log against an index of the base corpus.
//
// <baseStem> and <emuStem> must be supplied on the command line.  They are used
// to specify the input and output files:
//
// Inputs:
//    - TQR - base query log  <baseStem>.qlog
//    - WC  - word frequency distribution of the base corpus      <baseStem>_vocab.tsv
//    - EWC - word frequency distribution of the emulated corpus  <emuStem>_vocab_by_freq.tsv
//
// Outputs:
//    - TQE - the emulated query log. <emuStem>.qlog
//
// Algorithm:
//    1. For each query Q in TQR
//       2. For each word W in Q
//          3. Calculate the rank R of W in WC
//          4. Emit the word W' at rank R in EWC
//       5. Emit a newline.

// Note that the _vocab.tsv file is assumed to have been produced by a version of
// corpusPropertyExtractor.exe which lists the vocab in alphabetic order (that's
// always been the case) and includes a fourth column which specifies the rank of
// the word in a frequency ordering (that was added around 20 Oct 2017).


#ifdef WIN64
#include <windows.h>
#include <WinBase.h>
#include <strsafe.h>
#include <Psapi.h>  // Windows Process State API
#else
#include <errno.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>

#include "definitions.h"
#include "utils/dahash.h"
#include "characterSetHandling/unicode.h"
#include "utils/general.h"
#include "utils/argParser.h"
#include "utils/randomNumbers.h"
#include "qleArgTable.h"
#include "queryLogEmulator.h"


params_t params;

static void initialiseGlobals(globals_t *globals) {

}


static int vocabCmp(const void *ip, const  void *jp) {
  u_char *i = *((u_char**)ip), *j = *((u_char**)jp);
  // Note:  MUST use u_char otherwise comparisons fail
  // String comparison where the string is terminated by any ASCII control including NUL

  if (0) {
    printf("Comparing '");
    show_string_upto_nator_nolf(i, '\t');
    printf("' and '");
    show_string_upto_nator_nolf(j, '\t');
    printf("'\n");
  }
   
  while (*i > ' ' && *j > ' ' && (*i == *j)) {
    i++;  j++;
  }

  if (0) printf("  Loop end: *i = %d, *j = %d\n", *i, *j);
  if (*i <= ' ' && *j <= ' ') return 0;  // Terminators may differ but both ended at the same time
  if (*j <= ' ') return 1;  // j finished before i did
  if (*i <= ' ') return -1;  // i finished before j did

  if (*i < *j) return -1;
  return 1;
}


int getRankInBase(globals_t *globals, char *inWord) {
  u_char **vocabEntryP, *p, *q;
  int rank, field;

  if (0) printf("About to look up %s among %d entries\n",
		inWord, globals->baseVocabLineCount);
  vocabEntryP = (u_char **)bsearch(&inWord, globals->baseVocabLines,
				 globals->baseVocabLineCount,
				 sizeof(char *), vocabCmp);
  if (vocabEntryP == NULL) {
    printf("Lookup of '%s' failed.  Returning 777\n", inWord);
    return 777;
  }

  p = *vocabEntryP;
  while (*p > ' ') p++;
  p++;  // Skip the tab

  // There should be three numeric fields: occFreq, DF, and rank.  We only want the
  // third one.
  for (field = 2; field < 5; field++) {
    errno = 0;
    rank = strtod(p, (char **)&q);
    if (errno) printf("Error reading field %d in Base vocab\n", field);
    if (*q == '\t') p = q + 1;
    else if (*q < ' ' && field !=4) {
      printf("Error: missing field in base vocab.tsv\n");
      show_string_upto_nator(*vocabEntryP, '\n');
      return 1818;
    } else if (*q && *q != '\r' && *q != '\n') {
      printf("Error: unexpected format found in base vocab.tsv *q is %dc\n", *q);
      show_string_upto_nator(*vocabEntryP, '\n');
      return 2929;
    }
  }

  if (0) printf("Rank(%s) = %d\n", inWord, rank);
  return rank;
}


static void printUsage(char *progName, char *msg, arg_t *args) {
  printf("%s", msg);
  printf("Usage: %s baseStem=<blah> emuStem=<blah>\n"
	 "\n     <baseStem>_vocab.tsv, <baseStem>.qlog and <emuStem>_vocab_by_freq.tsv must"
	 "\n     all exist.  <emuStem>.qlog will be created.\n\n", progName);
  print_args(TEXT, args);
  exit(1);
}


int main(int argc, char **argv) {
  int a, q, queryLength, rank, qCount = 0, printerval = 10;
  double aveQueryLength = 0.0, startTime, generationStarted, generationTime, overheadTime;
  char *outWord, *fnameBuffer, ASCIITokenBreakSet[] = DFLT_ASCII_TOKEN_BREAK_SET,
    *p, *ignore;
  size_t lineLen, stemLen;
  globals_t globals;
  char fgetsBuf[1000];
  byte *wordStarts[500];

  startTime = what_time_is_it();
  setvbuf(stdout, NULL, _IONBF, 0);
  initialise_unicode_conversion_arrays(FALSE);
  initialise_ascii_tables(ASCIITokenBreakSet, TRUE);
  if (0) display_ascii_non_tokens();

  initialiseParams();
  printf("Params initialised\n");
  initialiseGlobals(&globals);   // Includes the hashtables as well as scalar values
  printf("Globals initialised\n");
  for (a = 1; a < argc; a++) {
    assign_one_arg(argv[a], (arg_t *)(&args), &ignore);
  }
  printf("Args assigned\n");

  if (params.baseStem == NULL || params.emuStem == NULL)
    printUsage(argv[0], "\n -- Missing argument(s) --\n", (arg_t *)(&args));   // Exits;

  if (!(exists(params.baseStem, "_vocab.tsv")))
      printUsage(argv[0], "\n -- Base is missing _vocab.tsv -- \n", (arg_t *)(&args));  // Exits;
  if (!(exists(params.baseStem, ".qlog")))
      printUsage(argv[0], "\n -- Base is missing _.qlog -- \n", (arg_t *)(&args));  // Exits;
  if (!(exists(params.emuStem, "_vocab_by_freq.tsv")))
      printUsage(argv[0], "\n -- Emu is missing _vocab_by_freq.tsv -- \n", (arg_t *)(&args));  // Exits;


  // Map the vocab.tsv files as arrays of lines 
  stemLen = strlen(params.emuStem);
  if (strlen(params.baseStem) > stemLen) stemLen = strlen(params.baseStem);
  fnameBuffer = (char *)cmalloc(stemLen + 100, "fnameBuffer", FALSE);
  
  strcpy(fnameBuffer, params.baseStem);
  strcpy(fnameBuffer + strlen(params.baseStem), "_vocab.tsv");
  globals.baseVocabLines = (char **)load_all_lines_from_textfile(fnameBuffer, &(globals.baseVocabLineCount),
								 &(globals.bvFH), &(globals.bvMH),
								 (u_char **)&(globals.baseVocabInMemory),
								 &(globals.bvSize));
  // Open the query infile
  strcpy(fnameBuffer, params.baseStem);
  strcpy(fnameBuffer + strlen(params.baseStem), ".qlog");
  globals.queryInfile = fopen(fnameBuffer, "rb");
  if (params.verbose) printf("Input file = %s\n", fnameBuffer);

  strcpy(fnameBuffer, params.emuStem);
  strcpy(fnameBuffer + strlen(params.emuStem), "_vocab_by_freq.tsv");
  globals.emuVocabLines = (char **)load_all_lines_from_textfile(fnameBuffer, &(globals.emuVocabLineCount),
								 &(globals.evFH), &(globals.evMH),
								 (u_char **)&(globals.emuVocabInMemory),
								 &(globals.evSize));

  // Open the query outfile
  strcpy(fnameBuffer + strlen(params.emuStem), ".qlog");
  globals.queryOutfile = fopen(fnameBuffer, "wb");

  generationStarted = what_time_is_it();
  overheadTime = what_time_is_it() - startTime;
  printf("Setup complete:  Elapsed time: %.3f sec.\n", overheadTime);

  qCount = 0;
  while (fgets(fgetsBuf, 1000, globals.queryInfile) != NULL) {  // Loop over input queries
    lineLen = strlen(fgetsBuf);
    while (fgetsBuf[lineLen -1] < ' ') lineLen--;  // Strip trailing newlines, CRs etc
    fgetsBuf[lineLen] = 0;
    if (params.verbose) printf("Input query: %s\n", fgetsBuf);
    qCount++;
    if (qCount % printerval == 0) {
      printf("   --- Progress %s: %d queries generated ---  Average time per query: %.3f sec.\n",
	     fnameBuffer, qCount, (what_time_is_it() - generationStarted) / (double)qCount);
      if (qCount % (printerval * 10) == 0) printerval *= 10;
    }
    queryLength = utf8_split_line_into_null_terminated_words((byte *)fgetsBuf, lineLen,
							     (byte **)&wordStarts,
							     500, MAX_WORD_LEN,
							     TRUE,  FALSE, FALSE, FALSE);
    
    for (q = 0; q < queryLength; q++) {  // Loop over words in query
      // Look up wordStarts[q] in base vocab and extract its rank r
      // print the word at rank r in the emu vocab.
      if (params.verbose) printf("   --- looking at word %s\n", wordStarts[q]);
      rank = getRankInBase(&globals, wordStarts[q]);
      if (params.verbose) printf("   --- it's at rank %d\n", rank);
      if (rank >= globals.emuVocabLineCount) {
	printf("Error:  rank %d too high (> %d)\n", rank, globals.emuVocabLineCount);
	exit(1);
      }
      outWord = globals.emuVocabLines[rank];
      if (q >0) fputc(' ', globals.queryOutfile);
      p = outWord;
      while (*p > ' ') {
	fputc(*p, globals.queryOutfile);
	p++;
      }
      aveQueryLength++;
    }
    fputc('\n', globals.queryOutfile);
  }
    
  generationTime = what_time_is_it() - generationStarted;

  fclose(globals.queryInfile);
  fclose(globals.queryOutfile);
  unmmap_all_of(globals.baseVocabInMemory, globals.bvFH, globals.bvMH,
		globals.bvSize);
  unmmap_all_of(globals.emuVocabInMemory, globals.evFH, globals.evMH,
		globals.evSize);
  if (qCount > 0) aveQueryLength /= (double)qCount;
  printf("Number of queries: %d\nAve. query length: %.2f\nQuery file %s.qlog\n",
	 qCount, aveQueryLength, params.emuStem);
  overheadTime = (what_time_is_it() - startTime) - generationTime;
  printf("Total time taken: %.1f sec. startup/shutdown + %.1f sec. generation time\n"
	 "Average generation time per query: %.4f sec\n",
	 overheadTime, generationTime,  generationTime / (double)qCount);
  printf("\nEmulated query log is in %s.qlog\n", params.emuStem);
}

