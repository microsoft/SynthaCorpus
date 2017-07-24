// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

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
#include <math.h>
#include <ctype.h>
#include <sys/types.h>

#include "definitions.h"
#include "utils/general.h"
#include "utils/dahash.h"
#include "utils/dynamicArrays.h"
#include "characterSetHandling/unicode.h"
#include "corpusPropertyExtractor.h"
#include "generateTFDFiles.h"
#include "ngramFunctions.h"


#define EPSILON 0.02



static size_t keySize;  // Sorry about this, Can't see how to pass the info to hashCmp() otherwise
static int hashCmp(const void *vip, const void *vjp) {
  // Note that because the hashtable is zeroed at creation, unused entries will
  // have an occurrence frequency of zero.  They'll end up bunched at the
  // tail of the table array.
  wordCounter_t *i = (wordCounter_t *)((char *)vip + keySize);
  wordCounter_t *j = (wordCounter_t *)((char *)vjp + keySize);
  if (*i > *j) return -1;  // Descending frequency
  if (*j > *i) return 1;
  // Use pointer values to break ties - otherwise qsort runs like a dog
  if (i > j) return 1;
  if (j < i) return -1;
  return 0;  // Shouldn't ever happen
}

static double getFreq(char *sortedHashTable, int distinctWords, size_t entryLen, size_t keyLen, int t) {
  // Find the t-th entry in the sorted hash table and return it's overall frequency
  // of occurrence, as a double.  Note t is a rank, i.e numbering is from 1.
  char *entryP;
  wordCounter_t *occFreqP;
  if (t < 0 || t > distinctWords) {
    printf("Error: getFreq(): t = %d c.f. distinctWds = %d\n", t, distinctWords);
    exit(1);
  }
  t--; // Convert rank to array index
  entryP = sortedHashTable + (t * entryLen);
  occFreqP = (wordCounter_t *)(entryP + keyLen);
  return (double)(*occFreqP);
}


static double getFreqForRange(char *sortedHashTable, int distinctWords, size_t entryLen, size_t keyLen,
			      int t1, int t2) {
  // Find the t1-th to t2-th entries in the sorted hash table and return the total of their
  // overall frequencies of occurrence, as a double.
  char *entryP;
  wordCounter_t *occFreqP;
  double total = 0.0;
  int t;
  
  if (t1 < 0 || t1 > distinctWords
      || t2 < 0 || t1 > distinctWords
      || t2 < t1) {
    printf("Error: getFreqForRange(%d, %d): parameters out of range.\n", t1, t2);
    exit(1);
  }
  
  t1--;  t2--; // Convert rank to array indices

  for (t = t1; t <= t2; t++) {
    entryP = sortedHashTable + (t * entryLen);
    occFreqP = (wordCounter_t *)(entryP + keyLen);
    total += (double)(*occFreqP);
  }
  return total;
}


static void writeTFDAndSegdatFiles(params_t *params, globals_t *globals,FILE *TFD, FILE *SDF,
				    char *sortedHashTable, int distinctWds, size_t singletons,
				    size_t entryLen, size_t keyLen, termType_t termType) {
  // Using the information passed in, generate the derived values and write to the
  // .tfd (Term Frequency Distribution) file.
  //
  // Also generate the data for fitting by piecewise segments and write to the .segdat file
  
  int h, s, f, l, middleHighest;  // f and l are the first and last ranks of a linear segment
  double dTotFreq = (double)(globals->totalPostings), dl, probf, probl, logprobf, logprobl, alpha, 
    domain, domainStep, probrange, cumprob, cumprobHead = 0.0, freqf, freql, log10freqf, log10freql; 

  fprintf(TFD, "#Type of file from which this was derived: %s\n", fileTypes[termType]);
  fprintf(TFD, "#Option names correspond to generateACorpus.exe\n"); 
  fprintf(TFD, "#Note:  zipf_alpha shown below is for the line connecting the extreme points of the middle segment - not for best fit.\n");
  fprintf(TFD, "#Head_terms: %d\n#Piecewise_segments: %d\n", params->headTerms, params->piecewiseSegments);
  fprintf(TFD, "-synth_postings=%.0f  # Total of all the frequencies\n", dTotFreq);
  fprintf(TFD, "-synth_vocab_size=%d\n",  distinctWds);
  fprintf(TFD, "-zipf_tail_perc=%.6f  # Number of terms with freq. 1\n", 
	  (double)singletons * 100.0 / (double)distinctWds);

  // Only try to do any of the below if there are more than params->headTerms terms

  // ------------ 1. Output Head Term Percentages -------------------
  
  if (distinctWds > params->headTerms) {
    fprintf(TFD, "-head_term_percentages=");

    for (h = 1; h <= params->headTerms; h++) {
      probf = getFreq(sortedHashTable, distinctWds, entryLen, keyLen, h) / dTotFreq;
      fprintf(TFD, "%.6f", probf * 100.0);
      cumprobHead += probf;
      if (h == params->headTerms) fputc('\n', TFD);
      else fputc(',', TFD);
    }

    fprintf(TFD, "#Combined_head_term_probability: %.10f\n", cumprobHead);

   // ------------ 2. Generate middle section descriptors -------------------
   
    // Calculate an overall Zipf Alpha for the middle part.
    f = (int)(params->headTerms + 1);
    l = (int)(distinctWds - singletons);
    middleHighest = l; 

    // Only try to do this if l is quite a bit larger than f
    if (l - f > 10) {
      freqf = getFreq(sortedHashTable, distinctWds, entryLen, keyLen, f);
      log10freqf = log10(freqf);
      probf = freqf / dTotFreq;
      logprobf = log(probf);
      freql = getFreq(sortedHashTable, distinctWds, entryLen, keyLen, l);
      log10freql = log10(freql);
      probl = freql / dTotFreq;
      logprobl = log(probl);
      domain = log((double)l) - log((double)f);
      alpha = (logprobl - logprobf) / domain;
      fprintf(TFD, "-zipf_alpha=%.4f\n", alpha);

      // Only try to do this if there are a lot of terms in the middle
      if (l - f > 1000) {
	// Now the middle segments.  Only try to do this if 
	// Each piecewise segment has five comma-separated values.   Segments are separated by a '%'
	// The five values are:
	//      alpha - the slope of the segment (in log-log space)
	//          f - the first rank in the segment
	//          l - the last rank in the segment
	//  probrange - the sum of all the term probabilities for ranks f..l (inclusive)
	//    cumprob - the sum of all the term probabilities for ranks 1..l (inclusive)
	fprintf(TFD, "-zipf_middle_pieces=");

	cumprob = cumprobHead;
	// Divide the domain f - l into equal segments in log space.
	domainStep = domain / (double)params->piecewiseSegments;
	dl = log((double)f);  // just to get started.  dl is maintained in fractional form.
	for (s = 0; s < params->piecewiseSegments; s++) {
	  dl = dl + domainStep;  // dl positions the end of this segment in log space
	  l = (int)trunc(exp(dl) + 0.5);  // use exp to convert dl to a rank in linear space
	  if (l > middleHighest) l = middleHighest;
	  freqf = getFreq(sortedHashTable, distinctWds, entryLen, keyLen, f);
	  probf = freqf / dTotFreq;
	  log10freqf = log10(freqf);
	  logprobf = log(probf);
	  freql = getFreq(sortedHashTable, distinctWds, entryLen, keyLen, l);
	  probl = freql / dTotFreq;
	  log10freql = log10(freql);
	  logprobl = log(probl);
	  domain = log((double)l) - log((double)f);
	  alpha = (logprobl - logprobf) / domain;  

	  probrange = getFreqForRange(sortedHashTable, distinctWds, entryLen, keyLen, f, l) / dTotFreq;
	  cumprob += probrange;
					
	  fprintf(TFD, "%.4f,%d,%d,%.10f,%.10f%%", alpha, f, l, probrange, cumprob);

	  // .segdat info has to be compatible with info in .plot (see below)
	  fprintf(SDF, "%.10f %.10f\n%.10f %.10f\n\n", log10((double)f), log10freqf,
		  log10((double)l), log10freql);
	  f = l + 1;  // Don't want f to be the same as the l for the last segment.
	}
	fputc('\n', TFD);
      }
    }
  }
}



void generateTFDFiles(params_t *params, globals_t *globals, dahash_table_t **htp,
		      termType_t termType) {
  // Given a hash table containing terms of type termType, e.e. Words, bigrams, ngrams,
  // term repetitions or cooccurrences, turn it into a Zipf-style term frequency
  // distribution and generate a number of files to assist in corpus emulation and
  // distribution plotting.  Generated files have the format <Stem>_<fileType>.suffix, where
  // Stem is params->outputStem and suffixes are:
  //
  // .plot    - sampled log(freq) v. log(rank) data for plotting with gnuplot
  // .segdat  - another gnuplot datafile showing the piecewise segments fitting the TFD
  // .tfd     - corpus generator options which could be used to emulate the TFD of this corpus.
  // .wdlens  - length distinct_prob occurrence_prob, suitable for gnuplotting
  // .wdfreqs - length mean-freq st.dev, suitable for gnuplotting
  //
  // First step is to copy the used entries in ht into a linear array and sort it by descending
  // frequency.  But WAIT, why not try sorting the hash table in place to avoid the extra
  // memory demands caused by copying.
  //
  // WARNING: On return, ht will no longer be a hash table!!!
  long long e, freq, maxFreq;
  wordCounter_t *occFreqP;
  off_t htOff = 0;
  dahash_table_t *ht = *htp;
  char *htEntry;
  FILE *tfdFile = NULL, *plotFile = NULL, *segDatFile = NULL,
       *wdLensFile = NULL, *wdFreqsFile = NULL;

  size_t singletons = 0;
  int distinctWords, wdLen;
  double lastLogRank = -1.0, logRank, logFreq, startTime;
  // The following are only used for WORDS, not for NGRAMS etc.
  double wdLengthCount[MAX_WORD_LEN + 1] = {0},
         freqWeightedWdLengthCount[MAX_WORD_LEN + 1] = {0},
         wdLenMeanFreq[MAX_WORD_LEN + 1] = {0},
         wdLenStDevFreq[MAX_WORD_LEN + 1] = {0};

  tfdFile = openFILE(params->outputStem, fileTypes[termType], ".tfd", "wb", FALSE);
  plotFile = openFILE(params->outputStem, fileTypes[termType], ".plot", "wb", FALSE);
  segDatFile = openFILE(params->outputStem, fileTypes[termType], ".segdat", "wb", FALSE);
  if (termType == WORDS) {
    wdLensFile = openFILE(params->outputStem, fileTypes[termType], ".wdlens", "wb", FALSE);
    wdFreqsFile = openFILE(params->outputStem, fileTypes[termType], ".wdfreqs", "wb", FALSE);
    fprintf(wdLensFile, "#Word length probability for %s.\n",
	    params->outputStem);

    fprintf(wdFreqsFile, "#Word frequency distributions for different word lengths for %s.\n",
	    params->outputStem);
  }

  // Record the outputStem in comments at the head of various files
  fprintf(plotFile, "#Log10(freq) v. Log10(rank) data for %s.\n#Log10(rank)  Log10(freq).\n",
	  params->outputStem);
  fprintf(segDatFile, "#Segments for fitting the data for %s.\n"
	  "# Consists of x0 y0NLx1 y1 pairs of lines interspersed with blank lines\n"
	  "# gnuplot interprets blank lines as meaning the end of a discrete line seg.\n",
	  params->outputStem);
 
  printf("Qsorting %zd entries in hash table %s by descending frequency: ", 
	 ht->entries_used, ht->name);
  startTime = what_time_is_it();
  keySize = ht->key_size;  // Using a global to allow hashCmp() to do the right thing.
  qsort(ht->table, ht->capacity, ht->entry_size, hashCmp);
  printf(" Completed in  %.3f sec.\n", what_time_is_it() - startTime);

  maxFreq  = *((wordCounter_t *)((char *)(ht->table) + ht->key_size));
  printf("Highest freq: %lld\n", maxFreq);
  distinctWords = (int)(ht->entries_used);
   
  for (e = 0; e < ht->entries_used; e++) {
    htEntry = ((char *)(ht->table)) + htOff;
    // -------------- Here we do everything that needs to be done on one term entry --------------
    occFreqP = (wordCounter_t *)(htEntry + ht->key_size);
    freq = *occFreqP;
    if (freq == 1) singletons++;
    //dfP = occFreqP + 1;  // Repetitions file doesn't have a DF, we don't use it here anyway
    //if (0) printf("%9lld:  %s\t%lld\t%lld\n", e + 1, htEntry, freq, *dfP);

    logRank = log10((double)(e + 1));
    if (logRank - lastLogRank > EPSILON) {
      logFreq = log10((double)(freq)); 
      fprintf(plotFile, "%.10f %.10f\n", logRank, logFreq);
      lastLogRank = logRank;
    }
    if (termType == WORDS) {
      wdLen = utf8_count_characters(htEntry);
      wdLenMeanFreq[wdLen] += freq;
      wdLenStDevFreq[wdLen] += (freq * freq);
      freqWeightedWdLengthCount[wdLen] += freq;
      wdLengthCount[wdLen]++;
    }

    // -------------------------------------------------------------------------------------------

    htOff += ht->entry_size;
  }

  printf("Scan of %s hash table finished.\n", ht->name);

  writeTFDAndSegdatFiles(params, globals, tfdFile, segDatFile, (char *)ht->table, 
			  distinctWords, singletons, ht->entry_size,
			  ht->key_size, termType);
  printf("TFD and Segdat files written for %s.\n", fileTypes[termType]);
  
  fclose(tfdFile);
  fclose(plotFile);
  fclose(segDatFile);
  if (termType == WORDS) {
    double aveLen  = 0, freqWeightedAveLen = 0,
      aveFreq = 0, stDevFreq = 0.0, count;
    int i;
    for (i = 1; i <= MAX_WORD_LEN; i++) {
      aveLen += (double)i * wdLengthCount[i];
      freqWeightedAveLen += (double)i * freqWeightedWdLengthCount[i];
      count = wdLengthCount[i];
      wdLengthCount[i] /= (double)distinctWords;
      freqWeightedWdLengthCount[i] /= globals->totalPostings;

      // Accumulating overall mean and standard deviations of word frequencies.
      aveFreq += wdLenMeanFreq[i];
      stDevFreq += wdLenStDevFreq[i];

      // Calculating mean and standard deviation for words of length i.
      // St.dev calculation uses old method, prone to numerical accuracy issues
      // .. hopefully OK
      // Var = (SumSq − (Sum × Sum) / n) / (n − 1)
      wdLenStDevFreq[i] = sqrt((wdLenStDevFreq[i]
				- (wdLenMeanFreq[i] * wdLenMeanFreq[i])
				/ count) / (count -1));
      wdLenMeanFreq[i] /= count;  
    }

    stDevFreq = sqrt((stDevFreq
		     - (aveFreq * aveFreq)
		     / (double)distinctWords) / ((double)distinctWords - 1));
			
    aveFreq /= (double)distinctWords;


    fprintf(wdLensFile, "# - lengths are measured in Unicode characters, not bytes.\n"
	    "#Average of distinct word lengths: %.3f\n",
	    aveLen /= (double)distinctWords);
    fprintf(wdLensFile, "#Average of word occurrence lengths in Unicode characters: %.3f\n",
	    freqWeightedAveLen /= globals->totalPostings);
    fprintf(wdLensFile, "#Length prob._for_distinct_wds  prob_for_wd_occurrences\n");


    fprintf(wdFreqsFile, "# Overall word frequency: Mean %.3f; St. Dev %.3f\n#\n"
	    "# Mean and st.dev of frequencies by word length.\n",
	    aveFreq, stDevFreq);
    fprintf(wdFreqsFile, "#Length Mean-freq.  St.dev\n");


    for (i = 1; i <= MAX_WORD_LEN; i++) {
      fprintf(wdLensFile, "%d\t%.6f\t%.6f\n",  i, wdLengthCount[i],  freqWeightedWdLengthCount[i]);
      fprintf(wdFreqsFile, "%d\t%.6f\t%.6f\n",  i, wdLenMeanFreq[i],  wdLenStDevFreq[i]);
    }

    fclose(wdLensFile);
    fclose(wdFreqsFile);
  }
}


static int cmpPermuteAA(const void *ip, const void *jp) {
  // Used to qsort an array of pointers to strings into ascending
  // alphabetic order of the strings to which they point.
  char *cip = *((char **)ip), *cjp = *((char **)jp);
  int r = strcmp(cip, cjp);
  if (r) return r;
  if (ip > jp) return 1;  // Never return zero.  Force tiebreaks
  return -1;
}


void writeTSVAndTermidsFiles(params_t *params, globals_t *globals, char **alphabeticPermutation,
			     int *alphaToFreqMapping, termType_t termType) {
  // assuming the relevant hash table has been filtered as desired, write a
  // TSV file in the form <term> TAB <occFreq> [TAB <df>] (alphabetic order)
  // If the termtype is NGRAMS, BIGRAMS or COOCCURS, the .termids file is written
  // in the following format:
  //   - records sorted in alphabetic order
  //   - each record in the form:  N(6388,3610):26 -- "absolute zero"
  //     where N indicates Ngram, 6388, 3610 are termids and 26 is the frequency of occurrence

  char **permute = NULL, *entryP, *termidsLine;
  dahash_table_t *ht;
  long long e, p, printerval;
  off_t htOff;
  FILE *termidsFile, *TSVFile;
  wordCounter_t *occFreqP, df;
  double start = what_time_is_it();
  char *middleName = fileTypes[termType];

  printf("writeNgramsTermidsFiles: -------------- %s ---------------\n", fileTypes[termType]);
  if (termType == TERM_REPS) ht = globals->gWordRepsHash;
  else if (termType == NGRAMS ||termType == BIGRAMS) ht = globals->gNgramHash;
  else {
    printf("Error: writeTSVAndTermidsFiles() unexpected termtype %d\n", termType);
    exit(1);
  }
  permute = (char **) cmalloc(ht->entries_used * sizeof(char *), "writeNgramsTermidsFile",
			      FALSE);

  htOff = 0;
  p = 0;
  for (e = 0; e < ht->capacity; e++) {
    if (((char *)(ht->table))[htOff]) {  // Test is if the first byte of the key isn't NUL
      entryP = ((char *)(ht->table)) + htOff;
      permute[p++] = entryP;
    }
    htOff += ht->entry_size;
  }
  

  printf("writeNgramsTermidsFiles: Alphabetically sorting %lld items...\n", p);
  qsort(permute, p, sizeof(char *), cmpPermuteAA);
  // NOTE: Could save a bit of memory using qsort_r() and defining permute
  // as an array of integers rather than an array of pointers.

  termidsFile = openFILE(params->outputStem, middleName, ".termids", "wb", TRUE);
  TSVFile = openFILE(params->outputStem, middleName, ".tsv", "wb", TRUE);

  printerval = 10;
  for (e = 0; e < p; e++) {
    if (e && e % printerval == 0) {
      printf("  %s(.termids|.tsv)  %9lld\n", middleName, e);
      if (e % (printerval *10) == 0) printerval *= 10;
    }
    if (permute[e] == NULL) {
      printf("NgramPermute[%lld] == NULL!\n", e);
      exit(1);
    }

    occFreqP = (wordCounter_t *)(permute[e] + ht->key_size);
    if (termType == TERM_REPS) {
      fprintf(TSVFile, "%s\t%lld\n", permute[e], *occFreqP);
    } else {
      df = *(occFreqP + 1);
      fprintf(TSVFile, "%s\t%lld\t%lld\n", permute[e], *occFreqP, df);
    }
    
    termidsLine = makeStringOfTermidsFromCompound(alphabeticPermutation, globals->vocabSize,
					       alphaToFreqMapping, permute[e],
					       *occFreqP, FALSE, termType);

    fprintf(termidsFile, "%s -- \"%s\"\n", termidsLine, permute[e]);
   
  }

  fclose(termidsFile);
  fclose(TSVFile);

  free(permute);
  printf("writeNgramsTermidsFiles: %lld Ngrams, elapsed time: %.1f sec.\n",
	 p, what_time_is_it() - start);
  
}


