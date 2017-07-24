// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This is the main module for corpusPropertyExtractor.exe
// It's job is to read a text corpus from a specified input file and
// to generate a set of property files.
//
// CHARACTER SET: All internal processing and output use the UTF-8 character
// set.  Input is expected to be in that format too.
//
// LENGTH LIMITS: Words are stored in a fixed number MAX_WD_LEN of bytes.  
// If an input word is longer than this it will be truncated back to the
// end of a UTF-8 character.  MAX_WD_LEN is defined in definitions.h as
// are other limit definitions such as MAX_BIGRAM_LEN and MAX_NGRAM_LEN
//
// HASH TABLES: corpusPropertyExtractor uses a large global hash table
// gVocabHash to accumulate information about the vocabulary (df and
// total occurrence frequency).  While processing each document it
// creates, fills and then destroys a local hash table lVocabHash 
// representing the vocabulary of that document, transferring
// accumulated information into gVocabHash when scanning of that document
// is complete.  Global hash tables gNgramHash and gRepetitionsHash
// record occurrence frequencies of repeated terms.
//

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
#include "utils/dynamicArrays.h"
#include "utils/dahash.h"
#include "characterSetHandling/unicode.h"
#include "utils/general.h"
#include "corpusPropertyExtractor.h"
#include "utils/argParser.h"
#include "extractorArgTable.h"
#include "generateTFDFiles.h"
#include "ngramFunctions.h"


params_t params;

char *fileTypes[] = {
  "_vocab",
  "_bigrams",
  "_ngrams",
  "_cooccurs",
  "_repetitions",};



static void initialiseGlobals(globals_t *globals) {
  globals->startTime = what_time_is_it();
  globals->numDocs = 0;
  globals->numEmptyDocs = 0;
  // Set up global vocab hash with room for both df and overall occurrence frequency
  // and with an initial capacity of around 15 million  (90% of 2**24)
  globals->gVocabHash =
    dahash_create((u_char *)"globalVocab", 24, MAX_WORD_LEN, 2 * sizeof(wordCounter_t),
		  (double)0.9, FALSE);
  globals->gNgramHash =
    dahash_create((u_char *)"globalNgram", 24, MAX_NGRAM_LEN, 2 * sizeof(wordCounter_t),
		  (double)0.9, FALSE);  // By definition, occurrence freq and df are identical
  globals->gWordRepsHash =
    dahash_create((u_char *)"globalwordReps", 24, MAX_REPETITION_LEN, sizeof(wordCounter_t),
		  (double)0.9, FALSE);

  globals->docWords = dyna_create(256, sizeof(long long));
  globals->distinctDocWords = dyna_create(256, sizeof(double));
  globals->greatestDocLength = 0;
  globals->WelfordM_old = 0.0;
  globals->WelfordS_old = 0.0;
  globals->WelfordM_new = 0.0;
  globals->WelfordS_new = 0.0;
}


static void print_usage(char *progName, arg_t *args) {
  printf("Usage: %s <params>\n"
	 " - must specify at least inputFileName and outputStem.\n\n"
	 "Allowable parameters are: \n", progName);
	 
  print_args(TEXT, args);
}


static wordCounter_t *freqs = NULL;  // Used by the next two functions


static int cmp_freq(const void *ip, const void *jp) {
  int *i = (int *)ip, *j = (int *)jp;
  if (freqs[*i] < freqs[*j]) return 1;
  if (freqs[*i] > freqs[*j]) return -1;
  return 0;  // May need a tie breaker, but what to use
}


static int *CreateAlphaToFreqMapping(char **alphabeticPermutation,
					 int vocabSize) {
  // If we look up a term using alphabeticPermutation, we get its
  // index in the alphabetic ordering of terms.  This function gives
  // us a mapping which allows us to turn that index into a termid,
  // i.e. the term's rank within the descending frequency ordering.
  int *permute = NULL, *alphaToFreqMapping = NULL, t;
  double start;

  start = what_time_is_it();
  permute = cmalloc(vocabSize * sizeof(int), "CreateAlphaToFreqMapping1", FALSE);
  freqs = cmalloc(vocabSize * sizeof(wordCounter_t), "CreateAlphaToFreqMapping2", FALSE);
  alphaToFreqMapping = cmalloc(vocabSize * sizeof(int), "CreateAlphaToFreqMapping3", FALSE);
  for (t = 0; t < vocabSize; t++) {
    permute[t] = t;
    freqs[t] = *((wordCounter_t *)(alphabeticPermutation[t] + MAX_WORD_LEN + 1));
  }

  qsort(permute, vocabSize, sizeof(int), cmp_freq);   

  // permute is now a mapping from rank in the freq sorted list to alphabetic rank
  // Create alphaToFreqMapping by reversing this
  for (t = 0; t < vocabSize; t++) {
    alphaToFreqMapping[permute[t]] = t + 1;  // +1 so that the termids start at rank 1, not 0
  }

  free(permute);
  free(freqs);
  permute = NULL;
  freqs = NULL;
  printf("Created alphaToFreqMapping for vocab: %d entries.  Elapsed time %.3f sec.\n",
	 vocabSize, what_time_is_it() - start);
  return alphaToFreqMapping;
}




static char docCopy[MAX_DOC_LEN + 1], *docWords[MAX_DOC_WORDS];

static int processOneDoc(params_t *params, globals_t *globals, char *docText,
			  size_t docLen) {
  int numWords, w;
  wordCounter_t *counter, *repCounter, tf;
  dahash_table_t *lVocab = NULL;
  char *htEntry;
  off_t htOff;
  long long e, *lHistoEntry;
  double *dlHistoEntry;
  
  // If docLen is greater than MAX, truncate and make sure that the first byte
  // after the chop isn't a UTF-8 continuation byte, leading '10' bits.  If it is we have to chop
  // before the start of the sequence.

  if (docLen > MAX_DOC_LEN) docLen = MAX_DOC_LEN;
  if ((docText[docLen] & 0xC0) == 0x80) {
    do {
      docLen--;
    } while ((docText[docLen] & 0xC0) == 0x80);
    // We should now be positioned on the start byte of a UTF-8 sequence which
    // we also need to zap.
    docLen--;
  }
  // Copy document text so we can write on it
  memcpy(docCopy, docText, docLen);
  docCopy[docLen] = 0;  // Put a NUL after the end of the doc for luck.
  if (0) {
    printf("processOneDoc: len=%zd, start_offset = %zd, end_offset = %zd, limit = %zd docCopy=\n",
	   docLen, docText - globals->inputInMemory, docText + docLen - globals->inputInMemory - 1,
	   globals->inputSize - 1);
    put_n_chars(docCopy, 100);
    printf("\n\n\n");
  }
  numWords = utf8_split_line_into_null_terminated_words(docCopy, docLen, (byte **)(&docWords),
							MAX_DOC_WORDS, MAX_WORD_LEN,
							TRUE,  // case-fold line before splitting
							FALSE, // before splitting
							FALSE,  // Perform some heuristic substitutions 
							FALSE
							);

  if (numWords <= 0) {
    globals->numEmptyDocs++;
    return 0;  // ----------------------------->
  }
  
  // Create a local vocab hash table  (10 bits cos most docs have few distinct words).
  // Just count up the frequency of each distinct word
  lVocab = dahash_create("localVocab", 10, MAX_WORD_LEN, sizeof(wordCounter_t),
			 (double)0.9, FALSE);
  if (0) printf("Putting %d words into local hash.\n", numWords);
  for (w = 0; w < numWords; w++) {
    if (0) printf("Word %d: %s\n", w, docWords[w]);
    counter = (wordCounter_t *)dahash_lookup(lVocab, docWords[w], 1);   // 1 means add the key if it's not already there.
     (*counter)++;
  }


  fprintf(globals->docTable, "%zd\t%zd\t%d\t%zd\n",
	  docText - globals->inputInMemory,  // Offset in input of document start
	  docLen,                           // Length in bytes (possibly truncated)
	  numWords,                         // Number of word occurrences
	  lVocab->entries_used);            // Number of distinct words

  // Now transfer the data from the local hash to the global one.
  htOff = 0;
  for (e = 0; e < lVocab->capacity; e++) {
    htEntry = ((char *)(lVocab->table)) + htOff;
    if (htEntry[0]) {    // Entry is used if first byte of key is non-zero
      if (0) printf("transferring entry %lld/%zd\n", e, lVocab->capacity);
      tf = *((wordCounter_t *)(htEntry + lVocab->key_size));
      if (tf >= 2) {
	// store repetition as <word>@<tf>
	u_ll ltf = (u_ll)tf;
	char repBuf[MAX_REPETITION_LEN + 1], *r, *w;
	if (ltf > 9999) ltf = 9999;  // Restrict to 4 digits to avoid possible overflow
	r = htEntry;
	w = repBuf;
	while (*r) *w++ = *r++;
	*w++ = '@';
	writeUllToString(w, ltf);
	repCounter = (wordCounter_t *)dahash_lookup(globals->gWordRepsHash, repBuf, 1);
	(*repCounter)++;	
      }
      counter = (wordCounter_t *)dahash_lookup(globals->gVocabHash, htEntry, 1);   
      (*counter)+= tf;  // First counter is totaloccurrence frequency
      counter++;
      (*counter)++;     // Second counter is df.
     
    }
    htOff += lVocab->entry_size;
  }

  // Add information to the doclength histogram and to the ratio of distinct to total words
  if (numWords > globals->greatestDocLength) globals->greatestDocLength = numWords;
  lHistoEntry = (long long *)dyna_get(&(globals->docWords), (long long) numWords, DYNA_DOUBLE);
  (*lHistoEntry)++;
  dlHistoEntry = (double *)dyna_get(&(globals->distinctDocWords), (long long) numWords, DYNA_DOUBLE);
  (*dlHistoEntry) += (double)(lVocab->entries_used);

  // Deal with ngrams
  recordNgramsFromOneDocument(params, globals, docWords, numWords);
  
  // Accumulate stuff for computing mean and StDev of doc length (Welford's method)
  if (globals->numDocs == 1) {
    globals->WelfordM_old = (double)numWords;
    globals->WelfordM_new = (double)numWords;
    globals->WelfordS_old = 0.0;
  } else {
    globals->WelfordM_new = globals->WelfordM_old + ((double)numWords - globals->WelfordM_old)
      / (double)globals->numDocs;
    globals->WelfordS_new = globals->WelfordS_old + ((double)numWords - globals->WelfordM_old)
      * ((double)numWords - globals->WelfordM_new);
    globals->WelfordM_old = globals->WelfordM_new;
    globals->WelfordS_old = globals->WelfordS_new;
  }

  // Finally free the space from the local hash
  dahash_destroy(&lVocab);
  return numWords;
}


static void processTSVFormat(params_t *params, globals_t *globals) {
  // Input is assumed to be in TSV format with an arbitrary (positive) number of
  // columns (including one column), in which the document text is in
  // column one and the other columns are ignored.   (All LFs and TABs are assumed
  // to have been removed from the document.)
  char *lineStart, *p, *inputEnd;
  long long printerval = 1000;
  size_t docLen;

  inputEnd = globals->inputInMemory + globals->inputSize - 1;
  lineStart = globals->inputInMemory;
  globals->numDocs = 0; 
  p = lineStart;
  while (p <= inputEnd) {
    // Find the length of column 1, then process the doc.
    while (p <= inputEnd  && *p >= ' ') p++;  // Terminate with any ASCII control char
    docLen = p - lineStart;
    globals->numDocs++;
    processOneDoc(params, globals, lineStart, docLen);
    if (globals->numDocs % printerval == 0) {
      printf("   --- %s in TSV format: %lld records scanned @ %.3f msec per record---\n",
	     params->inputFileName, globals->numDocs,
	     (1000.0 * (what_time_is_it() - globals->startTime)) / (double)globals->numDocs);
      if (globals->numDocs % (printerval * 10) == 0) printerval *= 10;
    }
    // Now skip to end of line (LF) or end of input.
    while (p <= inputEnd  && *p != '\n') p++;  // Terminate with LineFeed only
    p++;
    lineStart = p;
  }
  
}


static void processSTARCFormat(params_t *params, globals_t *globals) {
  // Input is assumed to be records in in a very simple <STARC
  // header><content> format. The STARC header begins and ends with a
  // single ASCII space.  Following the leading space is the content
  // length in decimal bytes, represented as an ASCII string,
  // immediately followed by a letter indicating the type of record.
  // Record types are H - header, D - document, or T - Trailer.  The
  // decimal length is expressed in bytes and is represented in
  // ASCII. If the length is L, then there are L bytes of document
  // content following the whitespace character which terminates the
  // length representation.  For example: " 13D ABCDEFGHIJKLM 4D ABCD"
  // contains two documents, the first of 13 bytes and the second of 4
  // bytes.
  //
  // Although this representation is hard to view with editors and
  // simple text display tools, it completely avoids the problems with
  // TSV and other formats which rely on delimiters, that it's very
  // complicated to deal with documents which contain the delimiters.
  //
  // This function skips H and T records.
  size_t docLen;
  char *docStart, *p, *q, *inputEnd;
  long long printerval = 10;
  byte recordType;

  globals->numDocs = 0; 
  inputEnd = globals->inputInMemory + globals->inputSize - 1;
  p = globals->inputInMemory;
  while (p <= inputEnd) {
    // Decimal length should be encoded in ASCII at the start of this doc.
    if (*p != ' ') {
      fprintf(stderr, "processSTARCFile: Error: STARC header doesn't start with space at offset %zd\n",
	     p - globals->inputInMemory);
      exit(1);

    }
    errno = 0;
    docLen = strtol(p, &q, 10);  // Making an assumption here that the number isn't terminated by EOF
    if (errno) {
      fprintf(stderr, "processSTARCFile: Error %d in strtol() at offset %zd\n",
	     errno, p - globals->inputInMemory);
      exit(1);
    }
    if (docLen <= 0) {
      printf("processSTARCFile: Zero or negative docLen %zd at offset %zd\n",
	     docLen, p - globals->inputInMemory);
      exit(1);
    }

    recordType = *q;    
    if (recordType != 'H' && recordType != 'D' && recordType != 'T') {
      fprintf(stderr, "processSTARCFile: Error: STARC header doesn't start with space at offset %zd\n",
	     q - globals->inputInMemory);
      exit(1);
    }
    q++;
    if (*q != ' ') {
      fprintf(stderr, "processSTARCFile: Error: STARC header doesn't end with space at offset %zd\n",
	     q - globals->inputInMemory);
      exit(1);

    }
    
    docStart = q + 1;  // Skip the trailing space.
    if (0) printf(" ---- Encountered %c record ---- \n", recordType);
    if (recordType == 'D') {
      globals->numDocs++;
      processOneDoc(params, globals, docStart, docLen);
      if (globals->numDocs % printerval == 0) {
	printf("   --- %s in STARC format: %lld records scanned @ %.3f msec per record---\n",
	       params->inputFileName, globals->numDocs,
	       (1000.0 * (what_time_is_it() - globals->startTime)) / (double)globals->numDocs);
	if (globals->numDocs % (printerval * 10) == 0) printerval *= 10;
      }
    }
    p = docStart + docLen;  // Should point to the first digit of the next length, or EOF
  }
  
}

static int cmpPermuteAA(const void *ip, const void *jp) {
  // Used to qsort an array of pointers to strings into ascending
  // alphabetic order of the strings to which they point.
  char *cip = *((char **)ip), *cjp = *((char **)jp);
  int r = strcmp(cip, cjp);
  return r;
}


static int cmpPermuteDFV(const void *ip, const void *jp) {
  // Used to qsort an array of pointers to strings into descending
  // frequency order of the vocab hashtable entries to which they point.
  char *cip = *((char **)ip), *cjp = *((char **)jp);
  wordCounter_t *wip = (wordCounter_t *)(cip + MAX_WORD_LEN + 1),
    *wjp  = (wordCounter_t *)(cjp + MAX_WORD_LEN + 1);
  int r;
  if (*wip > *wjp) return -1;
  if (*wip < *wjp) return 1;
  r = strcmp(cip, cjp);  // Tie breaking
  return r;
}




static void writeVocabTSV(globals_t *globals, char ***alphabeticPermutation) {
  // vocab.tsv will be written in alphabetic order and an alphabetic
  // permutation will be returned, i.e. an array of pointers to hash
  // table entries ordered by alphabetic order of the keys of those
  // entries
  long long e, p = 0;
  wordCounter_t *valPtr, occFreq;
  off_t htOff;
  char *htEntry, **permute = NULL;
  double startTime;
  
  globals->totalPostings = 0;
  globals->longestPostingsListLength = 0;
  permute = (char **)cmalloc(globals->gVocabHash->entries_used
			     * sizeof(char *),
			     "write_vocab_tsv", FALSE);
  
  htOff = 0;
  for (e = 0; e < globals->gVocabHash->capacity; e++) {
    htEntry = ((char *)(globals->gVocabHash->table)) + htOff;
    if (htEntry[0]) {    // Entry is used if first byte of key is non-zero
      if (0) printf("transferring entry %lld/%zd\n", e, globals->gVocabHash->capacity);
      permute[p++] = htEntry;
    }
    htOff += globals->gVocabHash->entry_size;
  }

  startTime = what_time_is_it();
  printf("Qsorting %lld entries in global vocabulary in alphabetical order ... ", p);
  qsort(permute, p, sizeof(char *), cmpPermuteAA);
  printf("%.3f sec. elapsed.\n", what_time_is_it() - startTime);
  for (e = 0; e < p; e++) {
    valPtr = (wordCounter_t *)(permute[e] + MAX_WORD_LEN + 1);
    fprintf(globals->vocabTSV, "%s\t%lld\t%lld\n", permute[e],
	    *valPtr, *(valPtr + 1));
    occFreq = *valPtr;   // occurrence frequency comes first, then DF
    globals->totalPostings += occFreq;
    if (occFreq > globals->longestPostingsListLength)
      globals->longestPostingsListLength = occFreq;
  }
  fclose(globals->vocabTSV);

  // Set up the return values
  globals->vocabSize = p;
  *alphabeticPermutation = permute;
}


static void writeVocabByFreqTSV(params_t *params, globals_t *globals, char **alphabeticPermutation) {
  // vocab.tsv will be written in descending frequency order.
  // The alphabetic permutation computed by the WriteVocabTSV() function is
  // copied and the copy permuted.  The number of entries is assumed to be
  // given by globals->gVocabHash->entries_used;
  long long e;
  wordCounter_t *valPtr, occFreq;
  char **frequencyPermutation = NULL;
  FILE *VBDF;
  double startTime;
  
  globals->totalPostings = 0;
  globals->longestPostingsListLength = 0;
  frequencyPermutation = (char **)cmalloc(globals->gVocabHash->entries_used
			     * sizeof(char *),
			     "writeVocabByFreqTSV", FALSE);
  memcpy(frequencyPermutation, alphabeticPermutation,
	 globals->gVocabHash->entries_used * sizeof(char *));

  startTime = what_time_is_it();
  printf("Qsorting %zd entries in global vocabulary in descending frequency order ... ",
	 globals->gVocabHash->entries_used);
  qsort(frequencyPermutation, globals->gVocabHash->entries_used, sizeof(char *), cmpPermuteDFV);
  printf("%.3f sec. elapsed.\n", what_time_is_it() - startTime);

  VBDF = openFILE(params->outputStem, "_vocab_by_freq", ".tsv", "wb", TRUE);

  for (e = 0; e < globals->gVocabHash->entries_used; e++) {
    valPtr = (wordCounter_t *)(frequencyPermutation[e] + MAX_WORD_LEN + 1);
    fprintf(VBDF, "%s\t%lld\t%lld\n", frequencyPermutation[e],
	    *valPtr, *(valPtr + 1));
    occFreq = *valPtr;   // occurrence frequency comes first, then DF
    globals->totalPostings += occFreq;
    if (occFreq > globals->longestPostingsListLength)
      globals->longestPostingsListLength = occFreq;
  }
  fclose(VBDF);
  free(frequencyPermutation);
}





static void processDocumentLengths(params_t *params, globals_t *globals) {
  // Write _docLenHist.tsv and _termRatios.tsv files using information recorded
  // by processOneDoc() in the dynamic arrays in globals
  int len;
  FILE *DLH, *TRT;
  long long *lHistoEntry;
  double *ulHistoEntry;

  DLH = openFILE(params->outputStem, "_docLenHist", ".tsv", "wb", FALSE);
  TRT = openFILE(params->outputStem, "_termRatios", ".tsv", "wb", FALSE);
  fprintf(DLH, "#DocLength Frequency\n");
  fprintf(TRT, "#DocLength Ave_distinct_words_in_docs_of_this_length\n");
  for (len = 1; len <= globals->greatestDocLength; len++) {
    lHistoEntry = (long long *)dyna_get(&(globals->docWords), (long long) len, DYNA_DOUBLE);
    if (*lHistoEntry > 0) {
      fprintf(DLH, "%d\t%lld\n", len, *lHistoEntry);
      ulHistoEntry = (double *)dyna_get(&(globals->distinctDocWords), (long long) len, DYNA_DOUBLE);
      fprintf(TRT, "%d\t%.3f\n", len, *ulHistoEntry / (double)(*lHistoEntry));
    }
  }
  
  fclose(DLH);
  fclose(TRT);
  printf("processDocumentLengths: %s_docLenHist.tsv and %s_termRatios.tsv written\n",
	 params->outputStem, params->outputStem);
}


static void writeSummaryFile(params_t *params, globals_t *globals) {
  FILE *SUMRY;
  double N, mean = 0.0 , var = 0.0 , stdev = 0.0;
  
  SUMRY = openFILE(params->outputStem, "_summary", ".txt", "wb", FALSE);
  globals->numDocs -= globals->numEmptyDocs;  // Don't count zero-length documents.
  fprintf(SUMRY, "docs=%lld  # Excluding zero-length\n", globals->numDocs);
  printf("writeSummaryFile(%lld)\n", globals->numDocs);
  N = (double)globals->numDocs;
  if (N > 0) mean = globals->WelfordM_new;
  if (N > 1) var = globals->WelfordS_new / N;
  stdev = sqrt(var);
  
  fprintf(SUMRY, "doclen_mean=%.3f\n", mean);
  fprintf(SUMRY, "doclen_stdev=%.3f\n", stdev);
  fprintf(SUMRY, "vocab_size=%lld\n", globals->vocabSize);
  fprintf(SUMRY, "longest_list=%lld\n", globals->longestPostingsListLength);
  fprintf(SUMRY, "total_postings=%lld\n", globals->totalPostings);
  fclose(SUMRY);
}

int main(int argc, char **argv) {
  int a, error_code;
  char *ignore, ASCIITokenBreakSet[] = DFLT_ASCII_TOKEN_BREAK_SET,
    **alphabeticPermutation = NULL;
  globals_t globals;
  double timeTaken;
  int *alphaToFreqMapping;
 
  setvbuf(stdout, NULL, _IONBF, 0);
  initialise_unicode_conversion_arrays(FALSE);
  initialise_ascii_tables(ASCIITokenBreakSet, TRUE);
  if (0) display_ascii_non_tokens();
  
  initialiseParams(&params);
  printf("Params initialised\n");
  initialiseGlobals(&globals);   // Includes the hashtables as well as scalar values
  printf("Globals initialised\n");
  for (a = 1; a < argc; a++) {
    assign_one_arg(argv[a], (arg_t *)(&args), &ignore);
  }
  printf("Args assigned\n");

  if (params.inputFileName == NULL || params.outputStem == NULL) {
    print_usage(argv[0], (arg_t *)(&args));
    exit(1);
  }

  sanitiseParams(&params);

  printf("Parameters sanitised\n");
  
  // Set up the necessary files
  globals.docTable = openFILE(params.outputStem, "_doctable", ".tsv", "wb", TRUE);
  globals.vocabTSV = openFILE(params.outputStem, "_vocab", ".tsv", "wb", TRUE);

  // Memory map the whole input file
  if (! exists(params.inputFileName, "")) {
    printf("Error: Input file %s doesn't exist.\n", params.inputFileName);
    exit(1);
  }
  
  globals.inputInMemory = (char *)mmap_all_of(params.inputFileName, &(globals.inputSize),
					       FALSE, &(globals.inputFH), &(globals.inputMH),
					       &error_code);

  // If input file ends in TSV we assume that the document text is in
  // the first column of a tab-separated-value file, one document per
  // line, one line per document.  Otherwise for the moment we assume
  // that the input is in simple text archive format, in which each
  // document is preceded by a ten-digit length in ASCII.

  printf("Corpus memory mapped.  About to process.\n");
  if (tailstr(params.inputFileName, ".tsv") != NULL
      || tailstr(params.inputFileName, ".TSV") != NULL)
    processTSVFormat(&params, &globals);
  else
    processSTARCFormat(&params, &globals);


  // Now dump the vocab.tsv file in alphabetic order
  printf("About to dump _vocab.tsv file\n");
  
  writeVocabTSV(&globals, &alphabeticPermutation);
  writeVocabByFreqTSV(&params, &globals, alphabeticPermutation);
  writeSummaryFile(&params, &globals);
  processDocumentLengths(&params, &globals);

  // We need to filter and write the Ngrams and Repetitions files before we
  // frequency-sort the vocab.  Writing ngrams.termids relies on the
  // alphabeticPermutation of the vocab to map words to term ranks.

  filterCompoundsHash(&params, &globals, alphabeticPermutation, NGRAMS);

  alphaToFreqMapping = CreateAlphaToFreqMapping(alphabeticPermutation, globals.vocabSize); 
  writeTSVAndTermidsFiles(&params, &globals, alphabeticPermutation,
			  alphaToFreqMapping, NGRAMS);
  generateTFDFiles(&params, &globals, &(globals.gNgramHash), NGRAMS);
  // Filter down to bigrams and do the same thing again
  filterHigherOrderNgrams(&params, &globals, 2);
  writeTSVAndTermidsFiles(&params, &globals, alphabeticPermutation,
			  alphaToFreqMapping, BIGRAMS);
  generateTFDFiles(&params, &globals, &(globals.gNgramHash), BIGRAMS);
  dahash_destroy(&(globals.gNgramHash));

  // Deal with the Repetitions
  if (1) printf("About to start on repetitions\n");
  filterCompoundsHash(&params, &globals, alphabeticPermutation, TERM_REPS);
	       
  writeTSVAndTermidsFiles(&params, &globals, alphabeticPermutation,
			  alphaToFreqMapping, TERM_REPS);
  generateTFDFiles(&params, &globals, &(globals.gWordRepsHash), TERM_REPS);
  dahash_destroy(&(globals.gWordRepsHash));
  

  // Finally deal with the vocab.
  generateTFDFiles(&params, &globals, &(globals.gVocabHash), WORDS);
  dahash_destroy(&(globals.gVocabHash));


  free(alphabeticPermutation);
  free(alphaToFreqMapping);
  fclose(globals.docTable);
  unmmap_all_of(globals.inputInMemory, globals.inputFH, globals.inputMH,
		globals.inputSize);
  dahash_destroy(&(globals.gNgramHash));
  dahash_destroy(&(globals.gWordRepsHash));


  printf("%s: All done.  Output in %s_*\n", argv[0], params.outputStem);
  timeTaken = what_time_is_it() - globals.startTime;
  printf("Total elapsed time: %.3f sec.\n", timeTaken);
}
