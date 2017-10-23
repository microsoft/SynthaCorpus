// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This is the main module for queryGenerator.exe which takes files produced by
// corpusPropertyExtractor.exe and generates a batch of random known item queries
// (and answers).  The algorithm used is the one proposed by Azzopardi, de Rijke,
// and Balog (SIGIR 2007), "Building Simulated Queries for Known-Item Topics".
// Our method is based on the one in Equation 4 of that paper (Discriminative
// Selection.  Essentially:
//
//    1. Randomly pick a target document, making a replacement selection if the
//       target is unsuitable.
//    2. Randomly pick a query length L from a distribution of query lengths.
//    3. Repeat L times:
//         a. Randomly pick a term from the document, according to the probabilities
//            given by Equation 4, and making a replacement selection if the
//            term has already been selected in this query.
//    4. Emit the query


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
#include "qGeneratorArgTable.h"
#include "queryGenerator.h"


params_t params;

static char docCopy[MAX_DOC_LEN + 1], *docWords[MAX_DOC_WORDS];

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



static void assign_wordScores(globals_t *globals, params_t *params, char **docWords,
			      double *wordScores, wordCounter_t *tf, int numWords) {
  // globals->vocabTSV is an array of globals->vocabLine strings sorted in alphabetic order
  //  - each string includes tab-separated total occurrence frequency and DF.
  // docWords is an array of size numWords, contain all of the distinct words in the document
  // A companion array contains the tf of the corresponding word (term frequency in this
  // document.
  // Output is in the pre-allocated array of wordScores.
  int w;
  u_char **vocabEntryP, *p, *q;  // MUST be unsigned!!
  double occFreq, sumProbs = 0.0, sumScores = 0.0, this, thusFar;
#if 0
  double df;
#endif
  // look up each document word in the vocabulary table and get its df.
  for (w = 0; w < numWords; w++) {
    if (params->verbose) printf("Looking up '%s' %d among %d words\n", docWords[w], w, numWords);
    vocabEntryP = (u_char **)bsearch(docWords + w, globals->vocabTSV, globals->vocabLines,
				 sizeof(char *), vocabCmp);
    if (vocabEntryP == NULL) {
      printf("Lookup of '%s' failed\n", docWords[w]);
      exit(1);
    }
   
    p = *vocabEntryP;
    while (*p > ' ') p++;
    p++;  // Skip the tab
    errno = 0;
    occFreq = strtod(p, (char **)&q);
    if (errno  || occFreq <= 0.0) {
      printf("Error reading occFreq for '%s'\n", docWords[w]);
      exit(1);
    }

#if 0
    // Leif's method doesn't use DF or TF 
    p = q + 1;
    errno = 0;
    df = strtod(p, &q);
    if (params->verbose) printf("occFreq = %.0f, df = %.0f, tf = %lld\n", occFreq, df, tf[w]);
    if (errno) printf("Error reading df\n");
#endif
   

    // 
    wordScores[w] = 1.0 / occFreq;  // Partially computed value
    sumProbs += wordScores[w];
  }

  if (sumProbs <= 0.0) {
    printf("Error: zero sumProbs for '%s'\n", docWords[w]);
    exit(1);
  }

  // We should multiply the interim wordScores by P / sumOccFreqs, where P is the total number
  // of word occurrences in the corpus, but since we're then going to normalise we can
  // forget P.
  for (w = 0; w < numWords; w++) {
    wordScores[w] /= sumProbs;   
    sumScores += wordScores[w];
  }

  if (params->verbose) printf("SumScores = %.4f\n", sumScores);
  // Now turn the wordScores into cumulative probabilities
  thusFar = 0.0;
  for (w = 0; w < numWords; w++) {
    this = wordScores[w] / sumScores;
    thusFar += this;
    wordScores[w] = thusFar;
  }
}



static void pickTargetAndOutputAQuery(globals_t *globals, params_t *params, int queryLen) {
  int chosenDoc, w;
  wordCounter_t *counter;
  long long docOff, docBytes, numWords;
  char *docText;
  dahash_table_t *lVocab = NULL;
  BOOL happy = FALSE;
  
  do {  // Loop until we're happy with the selection
    chosenDoc = (int)floor(rand_val(0) * globals->dtLines);
    if (params->verbose) printf("Chose document %d out of %d\n", chosenDoc, globals->dtLines);
    sscanf(globals->docTable[chosenDoc], "%lld %lld %lld", &docOff, &docBytes, &numWords);
    if (params->verbose) printf("  offset = %lld, bytes = %lld, words = %lld\n", docOff, docBytes, numWords);
    docText = globals->corpusInMemory + docOff;
    memcpy(docCopy, docText, docBytes);
    docCopy[docBytes] = 0;
    if (0) printf("  document is: \n%s\n\n", docCopy);
    numWords = utf8_split_line_into_null_terminated_words(docCopy, docBytes, (byte **)(&docWords),
							MAX_DOC_WORDS, MAX_WORD_LEN,
							TRUE,  // case-fold line before splitting
							FALSE, // before splitting
							FALSE, // Perform some heuristic substitutions 
							FALSE
							);
    // Make a vocabulary hash of the words in this document.  
    lVocab = dahash_create("localVocab", 10, MAX_WORD_LEN, sizeof(wordCounter_t),
			   (double)0.9, FALSE);
    for (w = 0; w < numWords; w++) {
      counter = (wordCounter_t *)dahash_lookup(lVocab, docWords[w], 1);   // 1 means add key if not already there.
      (*counter)++;
      if (params->verbose) printf("Word %d: %s  %lld\n", w, docWords[w], *counter);
    }

    
    if (params->verbose) printf("  word occurrences found: %lld, distinct words: %zd\n", numWords, lVocab->entries_used);

    // If this doc has enough distinct words, choose it as the target and generate a
    // query
    if (lVocab->entries_used > params->minWordsInTargetDoc
	&& lVocab->entries_used >= queryLen) {
      int e, qw, u = 0, dw, numDocWords;
      char **docWords = cmalloc(lVocab->entries_used * sizeof(char *), "docWords", FALSE);
      double *wordScores = cmalloc(lVocab->entries_used * sizeof(double), "wordScores", FALSE);
      wordCounter_t *tf = cmalloc(lVocab->entries_used * sizeof(wordCounter_t), "tf", FALSE);
      byte *used = cmalloc(lVocab->entries_used * sizeof(byte), "used", FALSE);
      double randy;
      char *htEntry;
      off_t htOff = 0;

      for (e = 0; e < lVocab->capacity; e++) {
	htEntry = ((char *)(lVocab->table)) + htOff;
	if (htEntry[0]) {    // Entry is used if first byte of key is non-zero
	  docWords[u] = htEntry;
	  tf[u++] = *((wordCounter_t *)(htEntry + MAX_WORD_LEN + 1));
	}
	htOff += lVocab->entry_size;
      }
      numDocWords = u;
      if (params->verbose) printf("DocWords array has %d entries. %d query words will be generated\n",
				  numDocWords, queryLen);

      assign_wordScores(globals, params, docWords, wordScores, tf, numDocWords);


      // Now pick query words based on docScores cumulative probabilities
      qw = 0;
      if (params->verbose) printf("Query: ");
      while (qw < queryLen) {
 	randy = rand_val(0);
	// Slow linear search - speed up later if nec.
	for (dw = 0; dw < numDocWords; dw++) {
	  if (params->verbose) printf(" comparing %.5f v. %.5f\n", wordScores[dw], randy);
	  if (wordScores[dw] >= randy) break;
	}

	if (params->verbose) printf("randy= %.4f, dw = %d, used[dw] = %d\n",
		      randy, dw, used[dw]);
	// Now check that we haven't picked this query word before.
	if (!used[dw]) {
	  used[dw] = 1;
	  fprintf(globals->queryOutfile, "%s ", docWords[dw]);
	  if (params->verbose) printf("%s ", docWords[dw]);
	  qw++;
	}
      }

      memcpy(docCopy, docText, docBytes);  // Do again to avoid all the NULs
      docCopy[docBytes] = 0;
      fprintf(globals->queryOutfile, "\tDoc%d\n", chosenDoc);
      if (params->verbose) printf("\tAnswer: Doc%d\n", chosenDoc);
      happy = TRUE;
      free(docWords);
      free(wordScores);
      free(tf);
      free(used);
    }
    // And free the space from the hashtable
    dahash_destroy(&lVocab);
		  
  } while (!happy);
}

static void printUsage(char *progName, arg_t *args) {
  printf("Usage: %s corpusFileName=<blah> propertiesStem=<blah>\n", progName);
  print_args(TEXT, args);
  exit(1);
}


int main(int argc, char **argv) {
  int a, error_code, q, queryLength, printerval = 10;
  double aveQueryLength = 0.0, startTime, generationStarted, generationTime, overheadTime;
  char *ignore, *fnameBuffer, ASCIITokenBreakSet[] = DFLT_ASCII_TOKEN_BREAK_SET;
  size_t stemLen;
  globals_t globals;

  startTime = what_time_is_it();
  rand_val(5);  // Seed the random generator.
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

  if (params.corpusFileName == NULL || params.propertiesStem == NULL) {
    printUsage(argv[0], (arg_t *)(&args));
    exit(1);
  }

  // Memory map the whole corpus file
  globals.corpusInMemory = (char *)mmap_all_of(params.corpusFileName, &(globals.corpusSize),
                                               FALSE, &(globals.corpusFH), &(globals.corpusMH),
                                               &error_code);
  // Map the properties files as arrays of lines 
  stemLen = strlen(params.propertiesStem);
  fnameBuffer = (char *)cmalloc(stemLen + 100, "fnameBuffer", FALSE);
  strcpy(fnameBuffer, params.propertiesStem);

  strcpy(fnameBuffer + stemLen, "_doctable.tsv");
  globals.docTable = (char **)load_all_lines_from_textfile(fnameBuffer, &(globals.dtLines),
							   &(globals.dtFH), &(globals.dtMH),
							   (u_char **)&(globals.dtInMemory),
							   &(globals.dtSize));

  strcpy(fnameBuffer + stemLen, "_vocab.tsv");
  globals.vocabTSV = (char **)load_all_lines_from_textfile(fnameBuffer, &(globals.vocabLines),
							   &(globals.vocabFH), &(globals.vocabMH),
							   (u_char **)&(globals.vocabInMemory),
							   &(globals.vocabSize));


  // Open the query outfile
  strcpy(fnameBuffer + stemLen, ".q");
  globals.queryOutfile = fopen(fnameBuffer, "wb");



  // If input file ends in TSV we assume that the document text is in
  // the first column of a tab-separated-value file, one document per
  // line, one line per document.  Otherwise for the moment we assume
  // that the input is in simple text archive format, in which each
  // document is preceded by a ten-digit length in ASCII.
  //
							   
							   
  if (tailstr(params.corpusFileName, ".tsv") != NULL) globals.corpusFormat = CORPUS_TSV;
  else globals.corpusFormat = CORPUS_STARC;

  generationStarted = what_time_is_it();
  printf("Data structures loaded in %.3f sec.:  Query generation commencing....\n",
	 generationStarted - startTime);

  for (q = 1; q <= params.numQueries; q++) {
    do {
      queryLength = (int)(round(rand_normal(params.meanQueryLength, params.meanQueryLength)) / 2.0);
    } while (queryLength < 1);
    pickTargetAndOutputAQuery(&globals, &params, queryLength);
    aveQueryLength += (double)queryLength;
    if (q % printerval == 0) {
      printf("   --- Progress %s: %d queries generated ---  Average time per query: %.3f sec.\n",
	     params.propertiesStem, q, (what_time_is_it() - generationStarted) / (double)q);
      if (q % (printerval * 10) == 0) printerval *= 10;
    }
  }

  generationTime = what_time_is_it() - generationStarted;

  unmmap_all_of(globals.corpusInMemory, globals.corpusFH, globals.corpusMH,
		globals.corpusSize);
  fclose(globals.queryOutfile);
  aveQueryLength /= (double)params.numQueries;
  printf("Number of queries: %d\nAve. query length: %.2f\nQuery file %s\n",
	 params.numQueries, aveQueryLength, fnameBuffer);
  overheadTime = (what_time_is_it() - startTime) - generationTime;
  printf("Total time taken: %.1f sec. startup/shutdown + %.1f sec. generation time\n"
	 "Average generation time per query: %.4f sec\n",
	 overheadTime, generationTime,  generationTime / (double)params.numQueries);
}

