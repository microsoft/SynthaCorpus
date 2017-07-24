// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Ngrams_from_TSV.c
//
// Functions to record n-grams in a hash table and build up their
// frequencies while scanning a corpus.  Once the data scan is
// finished other functions in this module may be used to calculate
// the expected frequencies of each ngram and to make a list in
// blah_ngrams.tsv of those n-grams whose ratio of observed to
// expected exceeds a threshold.
//
// The following logic is used to calculate which n-grams occur
// significantly more often than would be expected by random chance.
//
// It is easy to estimate the probability that a particular term
// occurrence is term T -- just divide the overall number of
// occurrences of T by the total number of occurrences of all terms.
// The probability of occurrence of "T U V" as an ordered 3-gram, is
// given by the product of the probabilities: P = Pr("T U V") = Pr(T)
// x Pr(U) x Pr(V).
//
// If we falsely assumed replacement, then the number of occurrences
// of the 3-gram "T U V" observed over an infinite number of trials
// would follow a binomial distribution.  Without replacement the
// distribution is hypergeometric, but maybe a binomial approximation
// is good enough.  The cumulative binomial we need in order to decide
// how likely it would be to observe a co-occurrence frequency of k
// would be incredibly time consuming to compute so, as per the
// Wikipedia page, we can in turn approximate the binomial by a normal
// distribution with mean NP (where N is the total number of
// postings), and variance NP(1 – P).
//
// In a normal distribution 95% of all observations fall below a Z
// score of + 1.6449 (table I, Appendix C in Hays, p. 672). Therefore
// our criterion value is NP + 1.6449 * sqrt(NP(1 -P)).  We can say
// with 95% confidence that any sequence of terms occurring more than
// this criterion are positively associated.  We are effectively using
// a one-tailed test and ignoring negative associations (i.e. terms
// which occur significantly less often as n-grams than would be
// expected under random scatter). Note that the criterion value
// computed in this way may be <= 1.  Should it count as significant
// if a bigram occurs just once?
//
// The Z score criterion is configurable -- parameter Zscore_criterion.



#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#ifdef WIN64
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "characterSetHandling/unicode.h"
#include "definitions.h"
#include "utils/general.h"
#include "utils/dahash.h"
#include "utils/dynamicArrays.h"
#include "utils/argParser.h"
#include "corpusPropertyExtractor.h"
#include "generateTFDFiles.h"
#include "ngramFunctions.h"


static long long truncations[MAX_NGRAM_WORDS + 1] = {0};


#if 0
static void report_truncations() {
  int l;
  BOOL there_were_truncations = FALSE;
  for (l = 0; l <= MAX_NGRAM_WORDS; l++) {
    if (truncations[l] > 0) {
      if (!there_were_truncations)
	printf("Some candidate n-grams were truncated and ignored. Unfortunately,\n"
	       "the following counts are occurrence counts not counts of distinct\n"
	       "truncated n-grams.\n");
      printf("   %d-grams: %lld\n", l, truncations[l]);
      there_were_truncations = TRUE;
    }
  }
  if (!there_were_truncations)
    printf("No candidates were truncated.\n");
}
#endif


static int cmpBsearch(const void *ip, const void *jp) {
  // Used by bsearch to find a (pointer to a) string in an array of pointers to strings assumed to be in ascending
  // alphabetic order of the strings to which they point.
  char *cip = *((char **)ip), *cjp = *((char **)jp);
  int r = strcmp(cip, cjp);
  return r;   // In this case we absolutely don't want tie breaking.
}


static wordCounter_t getWordFreqAndId(u_char *wd, char **alphabeticPermutation, int vocabSize, int *termid) {
  // Search for wd in the alphabetic permutation of vocab using binary search.
  // Return the frequency from the vocab entry, 0 if not found and -1 on error
  wordCounter_t freak = 0, *occFreqP;  // Return this if lookup fails
  char *p, **foundItem;

  *termid = -1;   // Assume the worst....
  
  foundItem = (char **)bsearch((char *)&wd, (char *)alphabeticPermutation, vocabSize, sizeof(char *),
			       cmpBsearch);
  if (foundItem != NULL) {
     *termid = (int)(foundItem - alphabeticPermutation);
     p = *foundItem;   //Pointer to the first byte of a hashtable entry
     occFreqP = (wordCounter_t *)(p + MAX_WORD_LEN + 1);
     freak = *occFreqP;
  }


  if (foundItem == NULL) {
      printf("getWordFreqAndId:   NOT FOUND: '%s'\n", wd);
  }
   return freak;
}


#define TERMID_STRING_LEN ((MAX_NGRAM_WORDS + 2) * 20)
static byte termid_string[TERMID_STRING_LEN + 1] = {0};

byte *makeStringOfTermidsFromCompound(char **alphabeticPermutation, int vocabSize,
					  int *alphaToFreqMapping, u_char *ngram,
				     u_ll ngramFreq, BOOL verbose, termType_t termType) {
  // The main purpose of this function is split up a compound term
  // into individual words and to convert the words into termids,
  // where the termid corresponds to the rank in a descending
  // frequency ordering.
  //
  // For NGRAMs, BIGRAMs, and COOCURs the words are guaranteed to be
  // separated by a single space, so we can use a simple FSM to split
  // them out.  In the case of TERM_REPS, the string is of the form
  // <word>@<freq>.  In this case we split out the word, convert it
  // to a termid and repeat the termid freq times, so the format is
  // consistent with other types of compound.
  //
  // Uses static storage: NOT THREAD-SAFE
  //
  // Format of string is N(t1,t2,...):f, where ti are the Zipf ranks of the
  // terms forming the n-gram and f is the frequency with which the n-gram
  // occurred.  The consumer of lines in this format will also be able to
  // handle C(t1,t2,...):f representing co-occurrences and R(t1,t1,t1,t1):f
  // representing repetitions.  In the C format, the terms may be N or R
  // groups, e.g. "C(17,N(23,34,45),R(5,5,5)):700", meaning that the cooccurrence
  // of: word 17, an n-gram comprising words 23, 34, and 45, and the three-fold
  // repetition of word 5, occurs 700 times.

  // In case of error, print a message and return NULL
  
  byte *q, saveq = 0, *wd = NULL, *v, *w = termid_string;
  int state = 0, nwds = 0;
  int termid, numTermidDigits;
  int r, repCount;
  
  if (termType == NGRAMS) *w++ = 'N';
  else if (termType == BIGRAMS) *w++ = 'N';
  else if (termType == COOCCURS) *w++ = 'C';
  else if (termType == TERM_REPS) *w++ = 'R';
  else *w++ = 'C';
  
  *w++ = '(';

  if (termType == TERM_REPS) {
    // special case first.  Because a term could be repeated very many times,
    // there is the potential to overflow storage.  Hence tests, based on the
    // fact that a termid won't have more than 9 digits.
    wd = ngram;
    q = strchr(wd, '@');
    if (q == NULL) {
      printf("Error: makeStringOfTermidsFromCompound(): Missing @ in TERM_REP\n"
	     "This is a serious internal error.\n");
      exit(1);
    }
    *q = 0;
    repCount = strtol(q + 1, NULL, 10);
    getWordFreqAndId(wd, alphabeticPermutation, vocabSize, &termid);
    if (termid < 0) {
      printf("For some peculiar reason, lookup of '%s' in ", wd);
      printf("'%s' failed.\n", ngram);
      return NULL;
    }
    termid = alphaToFreqMapping[termid];
    v = writeUllToString(w, (u_ll)termid);
    numTermidDigits = v - w;
    w = v;
    for (r = 1; r < repCount; r++) {
      // Truncate rather than risk overflow
      if (w - termid_string > (TERMID_STRING_LEN - numTermidDigits - 5)) break;
      *w++ = ',';
      w = writeUllToString(w, (u_ll)termid);
    }
    *q = '@';  // Put it back where we found it.   
  } else {
    q = ngram;
    while (*q >= ' ') {
      if (state == 0 && *q > ' ') {
	// Start of a word
	state = 1;
	wd = q;
	nwds++;
      }
      else if (state == 1 && *q <= ' ') {
	// We've come to the end of wd - 1, let's look up its frequency
	saveq = *q;
	*q = 0;
	if (verbose) printf("Looking up '%s'\n", wd);
      

	///wordCounter_t 
	getWordFreqAndId(wd, alphabeticPermutation, vocabSize, &termid);
	if (termid < 0) {
	  printf("For some peculiar reason, lookup of '%s' in ", wd);
	  *q = saveq;
	  printf("'%s' failed.\n", ngram);
	  return NULL;
	}
	*q = saveq;
	if (verbose) printf(" ... Termid = %d\n", termid);
	if (0 && (alphaToFreqMapping[termid] == 1))
	  printf(" ---- Inverted termid == 1 for termid %d in Ngram '%s'\n", termid, ngram);
	termid = alphaToFreqMapping[termid];
	w = writeUllToString(w, (u_ll)termid);
	*w++ = ',';
	state = 0;
      }
      if (*q >= ' ') q++;  // Don't advance if we're at NUL or TAB
    }  // End of word loop


    // Now deal with the last word.
    saveq = *q;
    *q = 0;
    if (verbose) printf("Looking up '%s'\n", wd);
    getWordFreqAndId(wd, alphabeticPermutation, vocabSize, &termid);  // End of the last word
    if (termid < 0) {
      printf("For some peculiar reason, lookup of '%s' in ", wd);
      printf("'%s' failed.\n", ngram);
      return NULL;
    }
    *q = saveq;
    if (verbose) printf(" ... Termid = %d\n", termid);
    if (0 && (alphaToFreqMapping[termid] == 1)) {
      printf(" Inverted termid == 1 for termid %d LAST in Ngram '%s'\n",
	     termid, ngram);
    }
    termid = alphaToFreqMapping[termid];
    w = writeUllToString(w, (u_ll)termid);
  }

  // Finish up is common to all termTypes.
  
  *w++ = ')';
  *w++ = ':';
  w = writeUllToString(w, (u_ll)ngramFreq);
  *w = 0;
  if (verbose) printf("  returning termid_string\n");
  return termid_string;
}

#if 0
static void testMakeStringOfTermidsFromNgram(char **alphabeticPermutation, int vocabSize,
					  int *alphaToFreqMapping){
  // Unfortunately, this relies on "the" and "and" being in the
  // vocab which probably won't be the case for synthetic collections.
  char in[] = "the and", *out;
  out = makeStringOfTermidsFromNgram(in, 999, FALSE);
  printf("msotfn(%s) -> %s\n", in, out);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// External API functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////


static byte *ngram = NULL, *limit = NULL;  // Used in the next function.

void recordNgramsFromOneDocument(params_t *params, globals_t *globals, char **docWords,
				int numWords) {
  // Takes an array docWords of size numWords, extracts Ngrams and stores them
  // in a local hash prior to transferring them to the global one at the end.
  int maxNgramWords = params->maxNgramWords; // Local copy allows us to change it.
  int ngramLen, sp, wd;
  long long e;
  byte *r, *w;  // read and write pointers
  BOOL truncated, verbose = FALSE;
  dahash_table_t *lNgrams = NULL;
  wordCounter_t *bvp, tf, *counter;
  off_t htOff;
  char *htEntry;

  if (ngram == NULL) {
    ngram = cmalloc(MAX_NGRAM_LEN + 1, (u_char *)"ngram", FALSE);
    limit = ngram + MAX_NGRAM_LEN;
  }

  if (numWords < params->minNgramWords) return;
  // Create a local Ngrams hash table  (10 bits cos most docs have few distinct nGrams).
  // Just count up the frequency of each distinct Ngram
  lNgrams = dahash_create("localNgrams", 10, MAX_NGRAM_LEN, sizeof(wordCounter_t),
			 (double)0.9, FALSE);


  if (maxNgramWords > numWords) maxNgramWords = numWords;
  // Loop over different ngram lengths.
  for (ngramLen = params->minNgramWords; ngramLen <= maxNgramWords; ngramLen++) {
    // Loop over different starting points for ngrams of this length
    for (sp = 0; sp <= (numWords - ngramLen); sp++) {
      w = ngram;
      truncated = FALSE;
      // Loop over the words of the candidate n-gram and copy into ngram. 
      for (wd = sp; wd < sp + ngramLen; wd++) {
	r = docWords[wd];
	while (*r && w < limit) *w++ = *r++;
	if (*r && w >= limit) {
	  // It looks as though this word has been truncated.
	  truncated = TRUE;
	  *limit = 0;
	  if (verbose) printf("TRUNCATED: '%s'\n", ngram);
	  truncations[ngramLen]++;
	  break;
	}
	*w++ = ' ';
      }
      if (truncated) continue;
      // NUL-terminate and remove the final space
      w--;
      *w = 0;
      // if (ngram[0] && ngram_passes_muster(ngram)) {  // Will stuff up corpus emulation experiments
      if (ngram[0]) {
	bvp = (wordCounter_t *)dahash_lookup(globals->gNgramHash, (byte *)ngram, 1);
	(*bvp) += 1;   
	if (verbose) printf("OK candidate %d-gram: '%s' has freq. %lld.\n",
				 ngramLen, ngram, *bvp);
      }
    }
  }

  // Now transfer the data from the local hash to the global one.
  htOff = 0;
  for (e = 0; e < lNgrams->capacity; e++) {
    htEntry = ((char *)(lNgrams->table)) + htOff;
    if (htEntry[0]) {    // Entry is used if first byte of key is non-zero
      if (0) printf("transferring entry %lld/%zd\n", e, lNgrams->capacity);
      tf = *((wordCounter_t *)(htEntry + lNgrams->key_size));
      counter = (wordCounter_t *)dahash_lookup(globals->gVocabHash, htEntry, 1);   
      (*counter)+= tf;  // First counter is total occurrence frequency
      counter++;
      (*counter)++;     // Second counter is df.
     
    }
    htOff += lNgrams->entry_size;
  }

  dahash_destroy(&lNgrams);
}


void filterCompoundsHash(params_t *params, globals_t *globals, char **alphabeticPermutation,
			 termType_t termType) {
  // Operates on a global hashtable, like globals->gNgramsHash, designed to record
  // frequency data for compound terms
  //   a. depending upon params->ngramsObsThresh, to filter out low frequency Ngrams
  //   b. if params->zScoreCriterion is > 0.0, to filter out Ngrams likely resulting from random scatter.
  //
  // Filtering out an item involves setting the entire entry to zero and reducing the
  // count of entries_used.
  //
  // alphabeticPermutation with vocabSize entries is an array of (char *) pointers
  // to entries in the vocabulary hash.  It is used to facilitate lookups of term frequency.
  int nwds, state = 0;   // state = 0 => scanning non-word, 1 => scannning word
  int ignore;
  byte *entryP = NULL;
  off_t htOff;
  wordCounter_t *occFreqP, freq;
  BOOL verbose = FALSE;
  long long e;
  double expected_prob, criterion, NP, stdev;
  u_char *q, *wd;
  dahash_table_t *ht;
  double start = what_time_is_it();

  if (termType == TERM_REPS) ht = globals->gWordRepsHash;
  else if (termType == NGRAMS || termType == BIGRAMS) ht = globals->gNgramHash;
  else{
    printf("Error: filterCompoundsHash() - unexpected term type %d\n", termType);
    exit(1);
  }

  printf("filterCompoundsHash(%s): Entries before filtering: %zd\n",
	 fileTypes[termType], ht->entries_used);
  printf("filterCompoundsHash: obsThresh, zScoreCriterion: %d, %.3f\n",
	 params->ngramObsThresh, params->zScoreCriterion);
  if (params->zScoreCriterion <= 0.0
      || params->ngramObsThresh <= 0) return;  // No filtering to be done ------>
  
  htOff = 0;

  for (e = 0; e < ht->capacity; e++) {
    if (0) printf("%9lld\n", e);
    if (((byte *)(ht->table))[htOff]) {  // Test is if the first byte of the key isn't NUL
      entryP = ((byte *)(ht->table)) + htOff;  // This is an entry in the n-grams hash
      occFreqP = (wordCounter_t *)(entryP + ht->key_size);
      if (*occFreqP < params->ngramObsThresh) {
	memset(entryP, 0, ht->entry_size);
	(ht->entries_used)--;
      } else if (params->zScoreCriterion > 0) {
	double prob;
	int r, reps;
	expected_prob = 1.0;
	q = entryP;
	if (verbose) printf("Candidate is '%s'\n", q);
	if (termType == TERM_REPS) {   // ----------- Repeated Terms ----------
	  // This compound term is something like walrus@87 i.e. Walrus occurred 87 times
	  // in the same document.
	  wd = entryP;
	  q = strchr(wd, '@');
	  if (q == NULL) {
	    printf("Error: No @ in repeated term (%s). Should not happen!\n", wd);
	    exit(1);
	  }
	  *q = 0;  // Null out the @
	  freq = getWordFreqAndId(wd, alphabeticPermutation, globals->vocabSize, &ignore);
	  if (0) printf("Prob of '%s' is %lld / %lld = %.7f\n", wd, freq,
			  globals->totalPostings, (double)freq / globals->totalPostings);
	  *q = '@';
	  prob = (double)freq / globals->totalPostings;
	  reps = strtol(q + 1, NULL, 10);
	  for (r = 0; r < reps; r++) expected_prob *= prob;
	} else {
	  //  ---------------  All other Compound Terms =--------------
	  // Find all the word starts
	  q = entryP;
	  if (verbose) printf("Candidate is '%s'\n", q);
	  wd = NULL;
	  nwds = 0;
	  state = 0;
	  while (*q) {
	    if (state == 0 && *q > ' ') {
	      // Start of a word
	      state = 1;
	      wd = q;
	      nwds++;
	    }
	    else if (state == 1 && *q <= ' ') {
	      // We've come to the end of wd - 1, let's look up its frequency
	      *q = 0;
	      if (0) printf("About to look up '%s'\n", wd);

	    
	      freq = getWordFreqAndId(wd, alphabeticPermutation, globals->vocabSize, &ignore);
	      if (0) printf("Prob of '%s' is %lld / %lld = %.7f\n", wd, freq,
			    globals->totalPostings, (double)freq / globals->totalPostings);
	      *q = ' ';
	      expected_prob *= (double)freq / globals->totalPostings;
	      state = 0;
	    }
	    q++;
	  }
	  if (0) printf("Checking frequency of last word '%s'\n", wd);
	  freq = getWordFreqAndId(wd, alphabeticPermutation, globals->vocabSize, &ignore);  // Another word end


	  // We decide whether to use this n-gram either on the basis of Zscores
	  // or on a raw frequency threshold.  Accepted candidates only are referenced
	  // in elements of ngramPermute[]
	  expected_prob *= (double)freq / globals->totalPostings;
	  // See head comment for this module for the maths behind this.
	  // Criterion is NP + Zscore_criterion * sqrt(NP(1 -P))
	}

	// ----------- Common to all compound terms ----------------
	NP = globals->totalPostings * expected_prob;
	stdev = sqrt(NP * (1.0 - expected_prob));
	criterion = NP + params->zScoreCriterion * stdev;
	if (criterion < 2.0) {  // Special case if threshold is less than 2
	  criterion = 2.0;
	}
	  
	if ((double)*occFreqP < criterion) {
	  memset(entryP, 0, ht->entry_size);
	  (ht->entries_used)--;
	}
      }  // --- end of conditional on Zscore criterion 
    } // --- end of conditional on non-empty hash table entry
    htOff += ht->entry_size;
  } // --- end of loop over hash table

  printf("filterCompoundsHash: Entries after filtering: %zd. Elapsed time: %.1f\n",
	 ht->entries_used, what_time_is_it() - start);
}


void filterHigherOrderNgrams(params_t *params, globals_t *globals, int N) {
  // Zap out all the entries in the Ngrams hash which correspond to higher order
  // Ngrams.  I.e those with more than N words

  long long e;
  off_t htOff;
  byte *entryP, *p;
  int spaceCount;
  dahash_table_t *ht = globals->gNgramHash;
  double start = what_time_is_it();

  printf("filterHigherOrderNgrams: Entries before filtering: %zd\n", ht->entries_used);
  printf("filterHigherOrderNgrams:  N = %d, \n", N);
  
  htOff = 0;
  
  for (e = 0; e < ht->capacity; e++) {
    if (((byte *)(ht->table))[htOff]) {  // Test is if the first byte of the key isn't NUL
      entryP = ((byte *)(ht->table)) + htOff;  // This is an entry in the Ngrams hash
      p = entryP;
      spaceCount = 0;
      while (*p) {
	if (*p++ == ' ') spaceCount++;
      }

      // Numner of words is spaceCount + 1
      if (spaceCount >= N) {
	memset(entryP, 0, ht->entry_size);
	(ht->entries_used)--;
      }
    }
    htOff += ht->entry_size;
  }

  printf("filterHigherOrderNgrams: Entries after filtering: %zd. Elapsed time: %.1f\n",
	 ht->entries_used, what_time_is_it() - start);
}





