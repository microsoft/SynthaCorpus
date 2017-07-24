// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Randomly generate a text corpus with properties fed in as
// parameters.  Several alternative options are provided for modelling
// word frequency distribution, generation of compound terms, document
// length distribution, word length distribution, and the string
// representation of randomly generated words.

// The first version of this program worked by generating an array of
// term occurrences in termid order, then shuffling them, then marking
// document boundaries.

// The second version (late 2016) was designed to handle bigrams,
// repetitions and co-occurrences. It created a document table first,
// with a pointer into the occurrences array and a count of empty
// positions.  Terms (be they unigrams, or compounds) are then
// assigned to a randomly chosen element of the document table.  If
// that document has insufficient room, another random choice must be
// made. To avoid the need for retries when allocating words to
// documents, a pointer is retained to the highest numbered doctable
// entry which is not yet full.  When a document fills, it is swapped
// with the highest non-full one and the pointer is adjusted.  Random
// selections are made among the non-full documents only.  All the
// full ones sit at the top of the table.  Note that compound terms
// are assigned first to reduce the chance of failed allocations terms
// into docuements with few unallocated postions. Once all terms have
// been assigned to documents, each document is independently
// shuffled, taking care not to break up or reorder Ngrams.

// In the second version, unigrams were assigned in order of
// decreasing frequency.  The motivation was to ensure that stopwords
// were scattered over all documents.  However, the following problem
// was observed: Because the probability of being assigned to a
// document is the same for all non-full documents, regardless of
// length, words with very high frequency can totally dominate a short
// document.  Twenty occurrences of the equivalent of "the" is
// unremarkable in a document of 1000 words, but is ridiculous in a
// document of length 20.

// To address this, the algorithm was changed in April 2017 as
// follows:
//
//  1. Assign compound terms as per second version, reducing the
//     unigram frequencies as necessary.  2. Allocate the unigrams
//     into an intermediate array and shuffle it, as per the original
//     version.  3. Allocate the terms from the intermediate array
//     into documents as per the second version.
//
// This seemed to solve the problem, at the cost of a significant
// increase in memory requirements and an increase in time taken. 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "characterSetHandling/unicode.h"
#include "definitions.h"
#include "utils/general.h"
#include "imported/TinyMT_cutdown/tinymt64.h"
#include "utils/dynamicArrays.h"
#include "utils/argParser.h"
#include "utils/randomNumbers.h"
#include "syntheticCollections.h"
#include "shuffling.h"
#include "corpusGeneratorArgTable.h"
#include "corpusGenerator.h"
#include "termRepMethods.h"
#include "MarkovWords.h"
#include "subsumptionLists.h"


int debug;
u_ll rand_seed = 0;

double synth_postings = 10000000, synth_vocab_size = 1000000,
  synth_doc_length = UNDEFINED_DOUBLE, synth_doc_length_stdev = UNDEFINED_DOUBLE,
  synth_dl_gamma_shape = UNDEFINED_DOUBLE, synth_dl_gamma_scale = UNDEFINED_DOUBLE,
  zipf_alpha=-0.9, zipf_tail_perc=0, markov_lambda = 0.0;

u_char *synth_term_repn_method = "base26", 
  *fname_synthetic_docs = "QBASH.forward", *head_term_percentages = NULL,
  *zipf_middle_pieces = NULL, *synth_dl_segments = NULL,
  *synth_dl_read_histo = NULL, *synth_input_vocab = NULL,
  *synth_input_ngrams = NULL;

BOOL tfd_use_base_vocab = FALSE, include_docnums = TRUE;
// ----

double *head_term_cumprobs = NULL;
int hashbits;

// ----
static long long num_postings;
static int vocab_size;

static void print_usage(u_char *progname, arg_t *args) {
  printf("\nUsage: %s <options>\n\n", progname);
  print_args(TSV, args);
  exit(1);
}






termp make_term_rep_table(u_int vocab_size, u_char *method){
  termp a, wp;
  int Markov_order = 0;
  size_t term_table_size = (size_t)(vocab_size + 1) * (size_t)TERM_ENTRY_LEN;
  a = malloc(term_table_size);
  if (a == NULL) {
    printf("Malloc failed in make_term_rep_table()\n");
    exit(1);
  } 
  printf("Term representation method: %s.  Term table size: %zd\n",
	 method, term_table_size);
  
  if (!strcmp(method, "tnum")) {
    fill_in_term_repn_table_tnum(a, vocab_size, MAX_TERM_LEN);
  } else if (!strcmp(method, "base26")) {
    fill_in_term_repn_table_base26(a, vocab_size, MAX_TERM_LEN);
  } else if (!strcmp(method, "bubble_babble")) {
    fill_in_term_repn_table_bubble_babble(a, vocab_size, MAX_TERM_LEN);
  } else if (!strcmp(method, "simpleWords")) {
    fill_in_term_repn_table_simpleWords(a, vocab_size, MAX_TERM_LEN);
  } else if (!strcmp(method, "from_tsv")) {
    fill_in_term_repn_table_from_tsv(a, &vocab_size, MAX_TERM_LEN,
				     synth_input_vocab);
    // Markov method specifiers match the following regex 'markov-[0-9]e?'
    // The digit gives the value of k, the order of the Markov method.
    // If present 'e' requests the use of an end-of-word symbol
    // to determine the length of words.  I.e. EOW is treated as an
    // additional letter and its emission is determined by normal
    // transition probabilities.  Otherwise a semi-random length
    // correlated with term rank is used to choose when to stop
    // a word.
  } else if (!strncmp(method, "markov-", 7)  && isdigit(method[7])) {
    Markov_order = (int)(method[7] - '0');
    if (method[8] == 'e') {
      USE_MODIFIED_MARKOV_WITH_END_SYMBOL = TRUE;
      printf("Using Markov EOW symbol\n");
    }
    fill_in_term_repn_table_markov(&a, vocab_size, MAX_TERM_LEN, Markov_order,
				   synth_input_vocab);    
  } else {
    printf("Unrecognized term representation method %s\n", method);
    exit(1);
  }

   if (1) printf("\n term_rep_table filled in\n");
 // Always fill in a last UNKNOWN word
  wp = a + (vocab_size * TERM_ENTRY_LEN);
  strcpy(wp, "UNKNOWN");

  if (1) printf("\n UNKNOWN filled in\n");

  return a;
}


static void shuffle_terms_within_docs(u_int *term_occurrence_array, long long num_postings) {
  // Using the FINAL_POSTING flags identify the sequence of occurrences constituting a document
  // and shuffle it.  For the moment, use knuth_shuffle, but eventually we will need to respect
  // n-grams, and not split them up.
  long long start = 0, end = 0;
  int doc_ends_found = 0;
  while (start < num_postings) {
    end = start;
    while (end < num_postings && !(term_occurrence_array[end] & FINAL_POSTING_IN_DOC)) end++;
    if (term_occurrence_array[end] & FINAL_POSTING_IN_DOC) {
      if ((end - start) > 2) {
	if (0) printf("Shuffling %lld to %lld\n", start, end);
	// Clear the FINAL_POSTING flag because END may be shuffled.
	term_occurrence_array[end] &= MASK_ALL_BUT_FINAL_POSTING_FLAG;
	knuth_shuffle_uint_respect_phrases((void *)(term_occurrence_array + start), end - start + 1);
	// Put the FINAL_POSTING flag on the new last term
	term_occurrence_array[end] |= FINAL_POSTING_IN_DOC;
      } 
      doc_ends_found++;
    }
    start = end + 1;
  }
  printf("shuffle_terms_within_docs():  %d doc ends found.\n", doc_ends_found);
}


static doctable_entry_t *create_doctable_from_histo(dyna_t histo, long long requested_postings,
						    docnum_t *num_docs) {
  // Allocate an array of doctable entries, sorted by increasing length, according to the
  // length histogram histo.  Terminate creation once *num_docs elements have been created.
  // Modify num_docs if necessary to reflect the actual number created.  In this function
  // the doctable entries just contain the doc length.  Pointers into the term occurrence
  // array are inserted.
  //
  // Note that there is a tension between the goals of creating the right number of
  // documents and the right number of postings.  We give priority to the number of
  // postings and stop generating documents when the requested number of postings
  // has been reached.   This same prioritisation is also applied in
  // generate_fakedoc_len_histo() which will have been used to generate the input histo.
  
  long long *histoptr = (long long *)histo, i, count, j, entries_created = 0,
    postings_created = 0;
  u_ll len3b, max_len_with_non_zero_freq = 0;
  doctable_entry_t *rezo;

  printf("create_doctable_from_histo(%lld)\n", *num_docs);

  rezo = malloc(*num_docs * sizeof(doctable_entry_t));
  if (rezo == NULL) {
    printf("Malloc failed in create_doctable_from_histo\n");
    exit(1);
  }
  
  // First two elements of dyna_t are 8 byte elt_count and 8-byte elt_size
  // Fake histos have long longs in them
  count = *histoptr;  // Count is a count of the number of distinct lengths recorded in histo
  histoptr += 2;      // Skip to the first element after the header.  It's a count of documents
                      // of length one.
  for (i = 1; i <= count; i++) {  // For each document length ...  Lengths start at one in the  
                                  // dynamic array and are contiguous.
    if (*histoptr > 0) {  // Ignore lengths with zero counts. 
      len3b = i & doctable_slots_avail_mask;
      max_len_with_non_zero_freq = len3b;
      printf("           Generating %lld copies of doc length %llu\n", *histoptr, len3b);
      // Need to store 3 least significant bytes of i *histoptr times into rezo
      for (j = 0; j < *histoptr; j++) {
	if ((long long)len3b > (requested_postings - postings_created))  // Shorten the last doc if nec.
	  len3b = (u_int)(requested_postings - postings_created);
	rezo[entries_created++] = len3b;
	postings_created += len3b;
	if (postings_created >= requested_postings || entries_created >= *num_docs) {
	  *num_docs = entries_created;
	  printf("create_doctable_from_histo: Returning a doctable of %lld docs.  Max \n"
		 "len: %lld, total_postings: %lld \n",
		 *num_docs, max_len_with_non_zero_freq, postings_created);
	  return rezo;    // ------------------------------->
	}
      }
    }

    histoptr++;
  }
  *num_docs = entries_created;
  printf("create_doctable_from_histo: Returning a doctable of %lld docs.  Max \n"
	 "len: %lld, total_postings: %lld \n",
	 *num_docs, max_len_with_non_zero_freq, postings_created);
  return rezo;
}


static void plug_in_dt_pointers(doctable_entry_t *doctable,  docnum_t num_docs) {
  // The doctable has been shuffled but each entry has only a length (in words).
  // Our job is to convert the sequence of lengths into offsets (indexes) into the
  // word occurrences array.
  docnum_t d;
  u_ll len3b, index = 0, pointer;
  for (d = 0; d < num_docs; d++) {
    len3b = doctable[d] & doctable_slots_avail_mask;
    pointer = (index & doctable_pointer_mask2);
    doctable[d] = (pointer << doctable_pointer_shift) | len3b;
    if (d < 10) printf("  %lld: index = %llu, len3b = %llu: %llX\n", d, index, len3b, doctable[d]);
    index += len3b;
  }
  printf("Pointers plugged in for %lld doctable entries.  Highest index = %llu\n",
	 num_docs, index);
}

static u_ll num_full = 0, print_interval = 1;

typedef enum {
  TT_WORD,
  TT_NGRAM,
  TT_COOC,
  TT_BURST,
} term_type_t;


static int place_one_word_instance_in_a_random_document(u_int *termids, int term_len, term_type_t term_type,
					      doctable_entry_t *doctable, long long *number_of_non_full_docs,
					      u_int *term_occurrence_array, long long num_postings) {
  // Hasty pre-keynote coding ....  We should probably change the next function to call this 
  long long i, j, k, nonfulls = *number_of_non_full_docs;
  u_ll pointer, t;
  u_int count;
  int outcome = 0;
  BOOL success = FALSE;
  
  for (i = 0; i < MAX_RANDOM_RETRIES; i++) {
    // Randomly pick a doc for this to go in
    if (nonfulls < 1) {
      printf("  --- All documents are full ---\n");
      success = TRUE;
      outcome = 1;
      break;
    } else if (nonfulls == 1) j = 0;
    else j = random_long_long(0, nonfulls - 1);
    if (0) printf("[Term staring with %u] Picked entry %lld in doctable.\n",
		  termids[0], j);
    // pointer is an index into the term_occurrences array, starting from zero
    pointer = (doctable[j] & doctable_pointer_mask) >> doctable_pointer_shift;
    count = (u_int)(doctable[j] & doctable_slots_avail_mask);
    // does the chosen document have room?  (Only an issue when we deal with mult-word
    // terms)
    if (count >= term_len) {
      // Allocation succeeded, unless ...
      success = TRUE;
      for (k = 0; k < term_len; k++) {
	if (pointer >= num_postings) {
	  printf("Error!   Out of range pointer into term occurrences array\n"
		 "  pointer = %llu/%lld, count = %u \n"
		 "  Doctable entry was %lld, out of %lld non-fulls.\n",
		 pointer, num_postings, count, j,  nonfulls);
	  exit(1);
	}
	term_occurrence_array[pointer] = termids[k];
	if (term_type == TT_NGRAM) {
	  if (k == 0) term_occurrence_array[pointer] |= SON_FLAG;   // Start of n-gram
	  else term_occurrence_array[pointer] |= CON_FLAG;   // Continuation of n-gram
	}
	pointer++;
	count--;
      }
      doctable[j] = ((pointer & doctable_pointer_mask2) << doctable_pointer_shift) | count;
      if (count == 0) {
	if (nonfulls > 1) {
	  // Swap this doctable entry with the last non-full item and reduce the nonfulls count
	  t = doctable[j];
	  doctable[j] = doctable[nonfulls - 1];
	  nonfulls--;
	  doctable[nonfulls] = t;
	} else {
	  printf(" ... we've got to the last non-full document\n");
	  nonfulls--;
	}
	term_occurrence_array[pointer - 1] |= FINAL_POSTING_IN_DOC;  // -1 cos pointer has been incremented
	num_full++;
	if (num_full % print_interval == 0) {
	  if (0) printf("Documents which have become full: %lld.\n", num_full);
	  if (num_full % (10 * print_interval) == 0) print_interval *= 10;
	}
      }
      break;
    } else {
      if (0) printf(" ... no good... retrying\n");
    }
  } // End of retry loop.  Retries shouldn't be necessary

  if (!success) {
    // "Can't happen"
    printf("Warning:  Random retry limit of %d exceeded for termid %d.\n",
	   MAX_RANDOM_RETRIES, termids[0]);
    outcome = 2;
  }
  *number_of_non_full_docs = nonfulls;
  return outcome;
}


static int place_postings_in_random_documents(u_int *termids, int term_len, term_type_t term_type,
					      long long num2generate, doctable_entry_t *doctable,
					      long long *number_of_non_full_docs,
					      u_int *term_occurrence_array, long long num_postings) {
  
  // Scatter num2generate occurrences of the term comprising across the documents described by
  // doctable. The termids are written into elements of term_occurrence_array.
  // This function will be called once for each termid.

  // As the process of assigning termid instances to documents proceeds documents will
  // become full. Each time a doc fills, it is swapped down to the end of the doctable.

  // On return, a number of term_occurrence_array elements will have been filled in,
  // a number of doctable entry counts will have been decremented, and elements of
  // the doctable may have been swapped.  Finally, an outcome is returned:
  //
  //  0 - successful placement of all occurrences
  //  1 - There are no more free spots in the term occurrence array, all docs are full
  //  2 - Retry limit exceeded
  
  long long h, i, j, k, nonfulls = *number_of_non_full_docs;
  u_ll pointer, t;
  u_int count;
  int outcome = 0;
  BOOL success;

  if (0) printf("Term starting with %u: Attempting to place %lld occurrences in random documents\n",
		termids[0], num2generate);

  for (h = 0; h < num2generate; h++) {
    success = FALSE;
    for (i = 0; i < MAX_RANDOM_RETRIES; i++) {
      // Randomly pick a doc for this to go in
      if (nonfulls < 1) {
	printf("  --- All documents are full ---\n");
	success = TRUE;
	outcome = 1;
	break;
      } else if (nonfulls == 1) j = 0;
      else j = random_long_long(0, nonfulls - 1);
      if (0) printf("[Term staring with %u, %lld/%lld] Picked entry %lld in doctable.\n",
		    termids[0], h, num2generate, j);
      // pointer is an index into the term_occurrences array, starting from zero
      pointer = (doctable[j] & doctable_pointer_mask) >> doctable_pointer_shift;
      count = (u_int)(doctable[j] & doctable_slots_avail_mask);
      // does the chosen document have room?  (Only an issue when we deal with mult-word
      // terms)
      if (count >= term_len) {
	// Allocation succeeded, unless ...
	success = TRUE;
	for (k = 0; k < term_len; k++) {
	  if (pointer >= num_postings) {
	    printf("Error!   Out of range pointer into term occurrences array\n"
		   "  pointer = %llu/%lld, count = %u \n"
		   "  Doctable entry was %lld, out of %lld non-fulls.\n",
		   pointer, num_postings, count, j,  nonfulls);
	    exit(1);
	  }
	  term_occurrence_array[pointer] = termids[k];
	  if (term_type == TT_NGRAM) {
	    if (k == 0) term_occurrence_array[pointer] |= SON_FLAG;   // Start of n-gram
	    else term_occurrence_array[pointer] |= CON_FLAG;   // Continuation of n-gram
	  }
	  pointer++;
	  count--;
	}
	doctable[j] = ((pointer & doctable_pointer_mask2) << doctable_pointer_shift) | count;
	if (count == 0) {
	  if (nonfulls > 1) {
	    // Swap this doctable entry with the last non-full item and reduce the nonfulls count
	    t = doctable[j];
	    doctable[j] = doctable[nonfulls - 1];
	    nonfulls--;
	    doctable[nonfulls] = t;
	  } else {
	    printf(" ... we've got to the last non-full document\n");
	    nonfulls--;
	  }
	  term_occurrence_array[pointer - 1] |= FINAL_POSTING_IN_DOC;  // -1 cos pointer has been incremented
	  num_full++;
	  if (num_full % print_interval == 0) {
	    if (0) printf("Documents which have become full: %lld.\n", num_full);
	    if (num_full % (10 * print_interval) == 0) print_interval *= 10;
	  }
	}
	break;
      } else {
	if (0) printf(" ... no good... retrying\n");
      }
    } // End of retry loop.  Retries shouldn't be necessary
    if (!success) {
      // "Can't happen"
      printf("Warning:  Random retry limit of %d exceeded for occurrence %lld of termid %d.\n",
	     MAX_RANDOM_RETRIES, h, termids[0]);
      outcome = 2;
    }
  }

 
  if (0 && outcome == 0)
    printf("place_postings_in_r_ds()placed %lld occurrences of term starting with %d. nonfulls = %lld\n",
	   h, termids[0], nonfulls);
  *number_of_non_full_docs = nonfulls;
  return outcome;
}


static double calculate_middle_fudge_factor(double middle_postings, double dnum_postings,
					    double initial_fudge) {
  // Slight inaccuracies in the TFD model may result in generation of
  // substantially too many or too few postings in the middle segments.
  // This function does a trial calculation of how many would be generated.
  // The ratio of this number to the required number (middle_postings)
  // is returned as a fudge factor to be used in actual generation.
  
  // Loop through each of the middle segments (should be at least one.)
  // The maths (and the structure of the mid_seg_defns) are described in
  // the head comment of setup_linseg_full() in get_random_numbers.c

  int ms; 
  u_int F, L, trank;
  double postings = 0.0, tf, carry = 0.0, fudge;
  long long tf0;

  printf("calculate_middle_fudge_factor(): mid_segs = %d, middle_postings = %.0f\n",
	 mid_segs, middle_postings);
  
  for (ms = 0; ms < mid_segs; ms ++) {
    F = (u_int)mid_seg_defns[ms].F;
    L = (u_int)mid_seg_defns[ms].L;
    printf("calculate_middle_fudge_factor(): F, L = %d, %d\n", F,L);
    
		  
    for (trank = F; trank <= L; trank++) {
      // We want to calculate the area of the integral x ** (alpha + 1) / (alpha + 1)
      // between x = trank - 1 and x = trank .... I think.  But wait!  If alpha < -1
      // then the denomintor and consequently the area comes out as negative.  But we
      // can't handle that.  
      double x0 = (double)(trank -1), x1 = (double)(trank), area = 0.0, p0, p1;
      if (trank == 1) p0 = 0.0;
      else p0 = pow(x0, mid_seg_defns[ms].ap1);
      p1 = pow(x1, mid_seg_defns[ms].ap1);
      area = (p0 - p1) / mid_seg_defns[ms].ap1;
      // The area_scale_factor brings the area under this segment up to 1.0
      // We then multiply by the probability range of this segment to give the
      // area we want.
      if (0) printf("Area = %.10f. x0 = %.0f, x1 = %.0f, p0 = %.10f, p1 = %.10f, AP1 = %.10f\n",
	     area, x0, x1, p0, p1, mid_seg_defns[ms].ap1);
      area *=  mid_seg_defns[ms].area_scale_factor * mid_seg_defns[ms].probrange;
      area *= initial_fudge;
      tf = dnum_postings * area + carry;  // Carry forward the remainder from rounding
      if (tf < 0) {
	if (0) printf("calc_mid_fudge_fac(): tf %.10f) was -ve for trank %d, flip ap1. Scale_fac = %.10f, %.10f\n",
	       tf, trank, mid_seg_defns[ms].ap1, mid_seg_defns[ms].area_scale_factor);
	tf = -tf;
      }
      tf0 = (long long) floor(tf);
      if (0) printf("TF0 = %lld\n", tf0);
      carry = tf - (double)tf0;
      postings += (double)tf0;
    }
  }
  fudge = middle_postings / postings;
  printf("Middle fudge factor: %.10f  (%.1f / %.1f)\n", fudge, postings, middle_postings);
  return fudge;
}



static u_ll *read_TOFS_array_from_file(char *vocab_filename, long long *num_postings, int *vocab_size) {
  // This fn is to support the case where we the emulated corpus uses exactly the same
  // word frequency histogram as the base corpus.  We read that in from vocab.tsv.
  // Because I anticipate that we'll only use this in rare circumstances, a couple of restrictions
  // are imposed which could be removed if desired:
  //
  // 1. We need to know the vocab_size in advance
  // 2. It is assumed that the vocab.tsv file has been sorted in order of descending frequency.
  u_ll *TOFS;
  u_char linebuf[1000], *p;
  int trank = 0;
  long long freq, tot_freq = 0, thresh = 2;
  FILE *VF;

  if (1) printf("Reading TOFS from %s\n", vocab_filename);
  TOFS = (u_ll *)malloc(*vocab_size * sizeof(u_ll));
  if (TOFS == NULL) {
    printf("Error: malloc failed in read_TOFS_array_from_file()\n");
    exit(1);
  }


  VF = fopen(vocab_filename, "rb");
  if (VF == NULL) {
    printf("Error: read_TOFS_array_from_file(): can't open %s\n",
	   vocab_filename);
    exit(1);
  }
  while (fgets(linebuf, 1000, VF) != NULL) {
    p = linebuf;
    while (*p >= ' ') p++;  // Skip to null tab or any ASCII control char
    if (*p != '\t') {
      printf("Error: read_TOFS_array_from_file(): TAB not found in input line %d\n",
	     trank);
      exit(1);
    }
    p++;
    freq = strtoll(p, NULL, 10);
    TOFS[trank] = freq;
    tot_freq += freq;
    trank++;
    if (freq <= thresh) {
      printf("Rank %3d: Freq: %7llu\n", trank, freq);
      thresh--;
    }
    if (trank > *vocab_size) {
      printf("Error: read_TOFS_array_from_file(): %s has more than %d lines\n",
	     vocab_filename, *vocab_size);
      exit(1);
    }
  }

  if (trank < *vocab_size) {
    printf("Error: read_TOFS_array_from_file(): %s has fewer than %d lines\n",
	   vocab_filename, *vocab_size);
    exit(1);
  }

  if (tot_freq != *num_postings) {
    printf("Error: read_TOFS_array_from_file(): %s has the wrong number of postings. (%lld v. %lld) \n",
	   vocab_filename, tot_freq, *num_postings);
    exit(1);
  }

  fclose(VF);
  printf("TOFS array loaded from %s\n", vocab_filename);
  return TOFS;
}


static u_ll *create_and_fill_TOFS_array(long long *num_postings, int *vocab_size) {
  // The goal of this function is to create and fill in an array of term occurrence frequencies
  // (TOFs) for each of the terms in the vocabulary.
  // 1. Create a TOFS array with one 64 bit element for each of the terms from rank 1 to rank vocab_size
  // 2. Calculate and store term occurrence frequency (TOF) values for each of the terms, using
  // whichever term frequency distribution model is in use.
  // 3. Return the array
  //
  // In full glory, this function will generate head terms, middle terms, and tail terms. In the 
  // minimal case there will be no head terms, no tail terms and only a single middle-term segment.
  //
  // We always face problems due to the fact that middle segments are continuous functions (whose
  // parameters are only estimated) but we need to generate discrete values for the occurrence
  // frequency of each of the term ranks falling within the domain of the function.
  //
  // The tail segment is specified effectively as a percentage of the total number of postings
  // which are due to singleton terms.  It is quite a challenge to achieve all of:
  //
  // A - the specified number of postings,
  // B - the specified vocabulary size, and
  // C - the specified number of singletons.
  //
  // We must give priority to A and B over C, to avoid overflows or underfills of
  // vocabulary and term_occurrence arrays which are sized according to the request.

  // There are two pre-requisites to achieving C:
  // 
  // 1: The generation of head and middle terms must not create any singletons, and
  // 2: The number of term occurrences in the head and middle must generate exactly
  //    num_postings - tail_postings_required.
  //
  // It's hard to achieve either of these.


  u_ll *TOFS = NULL;
  u_int trank = 0,  F, L;   // term_rank;
  int i,ht,  ms, outcome = 0;  // Zero outcome means nothing has (yet) gone wrong
  long long posting = 0, limit = *num_postings, middle_postings_generated = 0,
    non_tail_singletons = 0, tail_singletons = 0, tf0, posting_limit;
  double dnum_postings = (double)*num_postings;
  double head_postings = 0.0, middle_postings = 0.0, tail_postings = 0.0;
  double middle_fudge_factor0 = 1.0, middle_fudge_factor = 1.0, tf, carry = 0.0;  
  double total_seg_prob = 0.0, total_prob = 0.0;
  
  TOFS = (u_ll *)cmalloc(*vocab_size * sizeof(u_ll), "TOFS", 0);
  if (TOFS == NULL) {
    printf("Error: malloc failed in create_and_fill_TOFS_array()\n");
    exit(1);
  }

  printf("C_and_F_TOFS(%lld, %d)\n", *num_postings, *vocab_size);

  // Calculate how many postings there should be in head, middle and tail segments
  if (head_terms > 0) head_postings = head_term_cumprobs[(int)head_terms - 1] * dnum_postings;
  if (zipf_tail_perc > 0.0) tail_postings = zipf_tail_perc * (double)(*vocab_size) / 100.0;
  middle_postings = dnum_postings - head_postings - tail_postings;

  printf("Aiming for\n"
	 "  Head postings: %.1f\n"
	 "  Middle postings: %.1f\n"
	 "  Tail postings: %.1f\n"
	 "  Total: %.1f\n\n",
	 head_postings, middle_postings, tail_postings, dnum_postings);
  
  // --------------- Generate the head terms, if any --------------------------
  if (head_terms > 0) {
    for (ht = 0; ht < (int)head_terms; ht++) {
      posting_limit = (long long) floor(head_term_cumprobs[ht] * dnum_postings);
      if (posting_limit > limit) posting_limit = limit;  // Make sure we don't overflow the total number of postings
      tf0 = posting_limit - posting;
      if (tf0 <= 0) {
	outcome = 1;  // Premature end.
	break;
      }
      TOFS[ht] = tf0;
      posting += tf0;
      if (tf0 == 1) non_tail_singletons++;
   
      if (0) {
	printf("Generated occurrences for head term %u up to posting %lld\n", ht, posting);
	printf("   Head term[%u] cumprob = %.4f. Limit = %lld\n",
	       ht, head_term_cumprobs[ht], limit);
	printf("    Term %u: tf0 = %lld\n", ht + 1, tf0);
      }
    }
    total_prob += head_term_cumprobs[(int)(head_terms - 1)];
    
    printf("Generated a total of %lld occurrences for %d head terms.  Total_prob = %.6f\n",
	   posting, head_terms, total_prob);
    if (posting != (long long)floor(head_postings)) {
      printf("\nWarning: Head term generation created %lld, different from %.3f requested.\n",
	     posting, head_postings);
      // should we do something here.
    }
  }

  if (outcome == 0) {
    // --------------------- Generate the middle segment TOFs --------------------------
    printf("Middle segments: %d\n", mid_segs);
    // Calculate the middle fudge factor, basically by doing a pretend allocation
    // and calculating a scale factor necessary to align the total allocation with
    // the allocation requested.
    middle_fudge_factor = calculate_middle_fudge_factor(middle_postings, dnum_postings, 1.0);

    // Do it again a few times to refine
    for (i = 0; i < 20; i++) {
      middle_fudge_factor0 = calculate_middle_fudge_factor(middle_postings, dnum_postings,
							   middle_fudge_factor);
      middle_fudge_factor *= middle_fudge_factor0;
    }
    
    // Loop through each of the middle segments (should be at least one.)
    // The maths (and the strutcure of the mid_seg_defns) are described in
    // the head comment of setup_linseg_full() in get_random_numbers.c

    for (ms = 0; ms < mid_segs; ms++) {
      F = (u_int)mid_seg_defns[ms].F;
      L = (u_int)mid_seg_defns[ms].L;
      total_seg_prob = 0.0;
     
      printf("Middle segment %d: alpha=%.4f, F=%u, L=%u, scale_factor=%.4f, fudge_factor=%.10f,\n"
		    "  probrange=%.4f, cumprob=%.4f\n",
		    ms, mid_seg_defns[ms].alpha, F, L,
		    mid_seg_defns[ms].area_scale_factor, middle_fudge_factor,
		    mid_seg_defns[ms].probrange, mid_seg_defns[ms].cumprob);
		  
      for (trank = F; trank <= L; trank++) {
	// We want to calculate the area of the integral x ** (alpha + 1) / (alpha + 1)
	// between x = trank - 1 and x = trank .... I think.
	double x0 = (double)(trank - 1), x1 = (double)trank, p0, p1, area;
	if (trank == 1) p0 = 0.0;
	else p0 = pow(x0, mid_seg_defns[ms].ap1);
	p1 = pow(x1, mid_seg_defns[ms].ap1);
	area = (p0 - p1) / mid_seg_defns[ms].ap1;
	// The area_scale_factor brings the area under this segment up to 1.0
	// We then multiply by the probability range of this segment to give the
	// area we want.
	area *=  mid_seg_defns[ms].area_scale_factor * mid_seg_defns[ms].probrange;
	area *= middle_fudge_factor;
	tf = dnum_postings * area + carry;  // Carry forward the remainder from rounding
	if (tf < 0) {
	  if (0) printf("create_and_fill_TOFS_array(): tf %.10f) was -ve for trank %d, flip ap1. Scale_fac = %.10f, %.10f\n",
			tf, trank, mid_seg_defns[ms].ap1, mid_seg_defns[ms].area_scale_factor);
	  tf = -tf;
	}
	tf0 = (long long) floor(tf);
	carry = tf - (double)tf0;
	total_seg_prob += area;
	if (posting + tf0 > limit) tf0 = limit - posting;
	TOFS[trank - 1] = tf0;
	if (tf0 == 0) printf("Warning: tf0 == 0 for rank %d\n", trank);
	if (tf0 == 1) non_tail_singletons++;
	middle_postings_generated += tf0;
	posting += tf0;
	if (0) printf("Generated occurrences for term %u in middle segment %d up to posting %lld\n",
		      trank, ms + 1, posting);  
      } // End of loop over terms in one middle seg
      total_prob += total_seg_prob;
      if (0) printf("\\\\\\\\ %.6f -- %.6f\n", total_seg_prob, total_prob);
      if (outcome > 0) break;
    }  // End of loop over middle segs

    printf("Generated a total of %lld occurrences up to term rank %u,\n"
		  "of which %lld were non-tail singletons and %lld were middlers\n",
		  posting, trank, non_tail_singletons, middle_postings_generated);
    
    if (posting != (long long)floor(head_postings + middle_postings)) {
      printf("\nWarning: Head + middle term generation created %lld, different from %.3f requested.\n\n",
	     posting, head_postings + middle_postings);
      // should we do something here.
    }
  }


  if (outcome == 0) {
    // ---------------  Generate the tail.  Note that they mightn't all be singletons.  -----------
    long long tail_postings_needed, tail_vocab_needed;
    double ave_tail_tf; 

    tail_postings_needed = *num_postings - posting;
    tail_vocab_needed = *vocab_size - trank;

    if (tail_vocab_needed > tail_postings_needed) {
      // This is a serious situation because unless we take corrective action, we'll end up
      // with a smaller vocabulary than we want.  What we do is take some postings away from
      // the first thousand or so terms
      long long total_adjustment_required = tail_vocab_needed - tail_postings_needed, adjustment;
      int t, terms_to_alter = 1000;
      printf("\nWarning:  Tail segment adjustment of %lld needed to achieve required vocabulary size.\n",
	     total_adjustment_required);
      if (terms_to_alter > *vocab_size / 100) terms_to_alter = *vocab_size / 100 + 1;
      adjustment = total_adjustment_required / terms_to_alter + 1;
      for (t = 0; t < terms_to_alter; t++) {
	TOFS[t] -= adjustment;
	tail_postings_needed += adjustment;
	total_adjustment_required -= adjustment;
	posting -= adjustment;
	if (total_adjustment_required <= 0) break;
      }
    }
  
    printf("Tail segment.  We need to generate %lld more postings, and %lld new words.\n",
	   tail_postings_needed, tail_vocab_needed);

    ave_tail_tf = (double)tail_postings_needed / (double)tail_vocab_needed;
  
    trank++;  // Start one after the middle segment
    carry = 0.0;
    while (trank <= *vocab_size) {
      tf = ave_tail_tf + carry;  // Carry forward the remainder from rounding
      tf0 = (long long) floor(tf);
      carry = tf - (double)tf0;
      if (posting + tf0 > limit) {
	tf0 = limit - posting;
	printf("\n\nThrottling back tf0 to stay within limit %lld\n\n", limit);
      }
      if (tf0 == 0) printf("Warning: tf0 == 0 for rank %d\n", trank);
      TOFS[trank - 1] = tf0;
      posting += tf0;
      if (tf0 == 1) tail_singletons++;
      trank++;
    }
    if (*num_postings > posting) {
      printf("\nInfo: Final tail patch-up by %lld!\n\n", *num_postings - posting);
      TOFS[trank - 2] += (*num_postings - posting);
      posting = *num_postings;
    }
  }
  
  printf("Postings generated: %lld cf. %lld\n"
	 "Vocab size: %u cf %u\n"
	 "Singletons:  tail %lld + non-tail %lld = %lld, %.1f%% v. %.1f%%\n",
	 posting, *num_postings, trank - 1, *vocab_size,
	 tail_singletons, non_tail_singletons, tail_singletons + non_tail_singletons,
	 (double)(tail_singletons + non_tail_singletons) * 100.0 / (double)(*vocab_size),
	 zipf_tail_perc);  

    
  // *num_postings = posting;
  // *vocab_size = trank;
  return TOFS;
}



static long long count_valid_lines_in_file(FILE *fyle) {
  byte fbuf[MAX_DEPEND_LINE_LEN];
  int err;
  long long cnt = 0;
  
  err = fseek(fyle, 0, SEEK_SET);
  if (err) {
    printf("Error: count_valid_lines_in_file(1): Can't fseek \n");
    exit(1);
  }

  while (fgets(fbuf, MAX_DEPEND_LINE_LEN, fyle) != NULL) {
    if ((fbuf[0] == 'N' || fbuf[0] == 'C' || fbuf[0] == 'B')
	&& fbuf[1] == '(' && isdigit(fbuf[2])) cnt++;  // Assume OK.
  }

  err = fseek(fyle, 0, SEEK_SET);
  if (err) {
    printf("Error: count_valid_lines_in_file (2): Can't fseek \n");
    exit(1);
  }
  return cnt;
}


static u_ll sum_of_ull_array(u_ll *TOFS, int vocab_size) {
  // Note that the TOFS array is indexed numbering from one.
  int i;
  u_ll sum = 0;
  for (i = 0; i < vocab_size; i++) {
    sum += TOFS[i];
  }
  return sum;
}


static int repetitions[MAX_DEPEND_ARITY];

int *count_term_repetitions(int arity, int *termids) {
  // In an n-gram one or more words may be repeated one or more times
  // Return a companion array in which element i counts the repetitions of
  // term i.  Note that the repetition count is only accurate for the first
  // occurrence of a repeated term.  This is perfectly OK for the way
  // the repetitions array is used.
  int a, b;
  for (a = 0; a < arity; a++) {
    repetitions[a] = 1;
    for (b = 1; b < arity; b++) if (termids[b] == termids[a]) repetitions[a]++;
  }
  return repetitions;
}


static int multicol_cmp(const void *ip, const void *jp) {
  // Items to be compared are integer arrays with MAX_DEPEND_ARITY + 2 elements
  // First element is the actual arity, last is frequency (not compared) and
  // the others are termids.
  
  int *i = (int *)ip, *j = (int *)jp;
  int k;
  // First column is sorted descending, others ascending
  if (i[0] < j[0]) return 1;
  else if (i[0] > j[0]) return -1;

  for (k = 1; k <= MAX_DEPEND_ARITY; k++) {
    if (i[k] > j[k]) return 1;
    else if (i[k] < j[k]) return -1;
  }

  return 0;
}


static int *load_ngrams_file_and_sort(u_char *synth_input_ngrams, int *num_rows) {
  // Read the non-comment content of a file in ngram.termids format, whose name is
  // synth_input_ngrams, into an array.  Sort the array using degree (the value of n)
  // as the primary key, and the termids as secondary and subsequent keys.
  // Return the sorted array as the result of the function and a count of the number
  // of rows via the second parameter.
  //
  FILE *infile;
  int valid_lines, vl = 0, *ngrams, column, freq, num_cols = (MAX_DEPEND_ARITY + 2),
    termid;
  byte fbuf[MAX_DEPEND_LINE_LEN], *p, *q;
  double start;

  infile = fopen(synth_input_ngrams, "rb");
  if (infile == NULL) {
    printf("Error: Can't read %s\n", synth_input_ngrams);
    exit(1);
  }
  valid_lines = count_valid_lines_in_file(infile);
  if (1) printf("Valid lines in %s: %d\n", synth_input_ngrams, valid_lines);

  // Malloc an array big enough to hold up to MAX_DEPEND_ARITY + 2 ints for each valid line
  // what will be stored is actual arity, arity termids, zero or more zeros then frequency.
  ngrams = cmalloc(valid_lines * num_cols * sizeof(int), "ngrams", FALSE);
  if (ngrams == NULL) {
    printf("Error: Malloc failed for %d ngrams\n", valid_lines);
    exit(1);
  }

  while (fgets(fbuf, MAX_DEPEND_LINE_LEN, infile) != NULL) {
    
    if ((fbuf[0] == 'N' ) && fbuf[1] == '('  && isdigit(fbuf[2])) {
      // Assume OK.
      p = fbuf + 2;
      column = 1;
      while (*p != ')' && *p >= ' ') {
	termid = strtol((char *)p, (char **)&q, 10);
	if (0) printf("Read termid %d into column %d\n", termid, column);
	ngrams[vl * num_cols + column] = termid;
	column++;
	if (column >= num_cols) {
	  // Arity is too high! Ignore extra termids.  Skip forward to close paren
	  while (*p != ')' && *p >= ' ') p++;
	  break;
	}
	    
	p = q;
	if (*p == ',') p++;
      }

      if (*p++ == ')' && *p++ == ':') {
	// Read the frequency of this n-gram
	freq = strtol(p, NULL, 10);
	ngrams[vl * num_cols + num_cols - 1] = freq;  // Store the freq
      }
      ngrams[vl * num_cols] = column - 1;  // Store the arity
      vl++;  // Valid line number count
    }
  }

  fclose(infile);
  printf(" ... sorting the ngrams array\n");
  // Now sort the array using all the columns in row order.
  start = what_time_is_it();
  qsort((void *)ngrams, (size_t) vl, (size_t)num_cols * sizeof(int), multicol_cmp);
  printf("Time for ngrams sort was %.3f sec.\n", what_time_is_it() - start);

  *num_rows = valid_lines;
  return ngrams;
}


static long long process_ngrams_file(u_char *synth_input_ngrams, u_ll *TOFS, u_int *rezo,
				int vocab_size, long long num_postings, u_int *term_occurrences,
				doctable_entry_t *doctable, docnum_t *number_of_non_full_docs) {

  int i, j, vl = 0, *ngrams = NULL, arity, printerval, freq, line, outcome,
    num_cols = (MAX_DEPEND_ARITY + 2), *termids, *repetitions;
  BOOL finished, verbose = FALSE;
  dyna_t ngram_refs = NULL;
  int subsumed_ngrams;  
  long long ngram_instances_not_emitted = 0, ngram_instances_emitted = 0,
    postings_placed = 0, TOFs_subtracted = 0, total_subsumptions = 0;


  ngrams = load_ngrams_file_and_sort(synth_input_ngrams, &vl);
  
  ngram_refs = dyna_create(20, sizeof(int));


  // Now generate the n-grams in array order and adjust subsidiary counts.
  printerval = 100;

  for (line = 0; line < vl; line++) {
    arity = ngrams[line * num_cols];
    termids = ngrams + line * num_cols + 1;
    freq = ngrams[line * num_cols + num_cols - 1];
    if (line && (line % printerval) == 0) {
      int y;
      printf("Processing ngrams line %d/%d.  arity %d::: ", line + 1, vl, arity);
      for (y = 0; y < arity; y++) printf("  %7d(%llu)", termids[y], TOFS[termids[y] - 1]);
      printf(".   Freq: %d.  Tot. subsumptions: %lld\n", freq, total_subsumptions);
      if (line && (line % (printerval * 10)) == 0) printerval *= 10;
    }

    if (arity > 2) {
      find_all_subsumptions_of_an_ngram(arity, termids, ngrams, line, vl,
					&ngram_refs, &subsumed_ngrams);
      total_subsumptions += subsumed_ngrams;
    }

    // There's a special case when a word is repeated within an n-gram, e.g.
    // 'wagga wagga'.  When testing for frequency under-run it's not sufficient
    // to test for frequency == 0, we have to test for frequency < repetitions.
    repetitions = count_term_repetitions(arity, termids);

    finished = FALSE;
   
    for (i = 0; i < freq; i++) {
      // Don't emit this instance until we've checked that none of the components have
      // zero counts.

      // Check subsumed k-grams
      for (j = 0; j < subsumed_ngrams; j++) {
	int *row_startp;
	row_startp = (int *)dyna_get(&ngram_refs, (long long)j, DYNA_DOUBLE);
	if (ngrams[*row_startp * num_cols + num_cols -1] == 0) {
	  if (verbose) printf(" Subsumed k-gram exhausted.\n");
	  finished = TRUE;
	  break;
	}
      }
      
      if (!finished) {
	// Check subsumed words
	for (j = 0; j < arity; j++) {
	  if (0) printf("Checking(%d) termid %d = %llu\n", j, termids[j], TOFS[termids[j] - 1]);
	  if (TOFS[termids[j] - 1] < repetitions[j]) {
	    if (verbose) printf(" Term %d exhausted.  j was %d\n", termids[j], j);
	    finished = TRUE;
	    break;
	  }
	}	
      }


      if (finished) {
 	if (verbose) printf(" ** We're broken[At line %d, freq %d]\n", line , i);
	ngram_instances_not_emitted += (freq -i);
	break;  // Have to stop top level generation if any component freqs hit zero ----->
      }

      // --------------------- All good to emit -----------------

      // Try to emit first.  If success then subtract from the subsumption counts.
      if (0) printf(" ....  placing one instance.\n");
      outcome = place_postings_in_random_documents(termids, arity, TT_NGRAM, 1,
						   doctable, number_of_non_full_docs,
						   term_occurrences, num_postings);
      if (outcome != 0) {
	printf("Warning: placement of %d-gram with frequency: %d and first term %u failed.\n",
	       arity, freq, termids[0]);
      } else {
	if (verbose) printf(" !!! %d-gram emitted\n", arity);
	postings_placed += arity;

      
	// Now decrement the frequency of all the subsumptions
	// First subsumed k-grams
	for (j = 0; j < subsumed_ngrams; j++) {
	  int *row_startp;
	  row_startp = (int *)dyna_get(&ngram_refs, (long long)j, DYNA_DOUBLE);
	  ngrams[*row_startp * num_cols + num_cols -1]--;
	}

	// Now individual words
	for (j = 0; j < arity; j++) {
	  if (termids[j] < 1 || termids[j] > vocab_size) {
	    printf("Error: termids[%d] = %d [At line %d, freq %d]\n",
		   j, termids[j], line , i);
	    break;
	  }
	  if (TOFS[termids[j] - 1] == 0) {
	    printf("Error: (How can it be? Already checked!)  [At line %d, freq %d]. j=%d, tid=%d, subsumptions = %d\n",
		   line , i, j, termids[j], subsumed_ngrams);
	    printf("   Arity = %d\n", arity);
	    break;
	  }
	
	  TOFS[termids[j] - 1]--;
	  TOFs_subtracted++;
	}
 

	if (postings_placed != TOFs_subtracted) {
	  printf("Error: postings_placed (%lld) != TOFs_subtracted (%lld)\n",
		 postings_placed, TOFs_subtracted);
	  exit(1);
	}

	ngram_instances_emitted++;
      }
    }
  }


  
  free(ngrams);
  free(ngram_refs);
  printf("\nNgrams instances emitted: %lld\n", ngram_instances_emitted);
  printf("Ngrams instances suppressed due to overlap: %lld\n", ngram_instances_not_emitted);
  printf("Total subsumptions found: %lld\n", total_subsumptions);
  return postings_placed;
}


static void check_term_occurrence_array(u_int *term_occurrence_array, long long num_postings,
					docnum_t num_docs) {
  // Count zero termids and complain if any are present
  // Count end-of-doc markers and complain if it's not the same as num_docs

  long long i, zeroes = 0, end_of_doc_markers = 0;
  u_int tid;

  for (i = 0; i < num_postings; i++) {
    if (term_occurrence_array[i] & FINAL_POSTING_IN_DOC) end_of_doc_markers++;
    tid  = (term_occurrence_array[i] & MASK_ALL_BUT_FINAL_POSTING_FLAG);
    if (tid == 0) zeroes++;
  }

  if (zeroes)
    printf("Error: CTOA: %lld entries in term_occurrence array are zero.\n", zeroes);
  if (end_of_doc_markers != num_docs)
    printf("Error: CTOA: Incorrect end-of-doc-marker count %lld.  Should have been %lld\n",
	   end_of_doc_markers, num_docs);
  if ((zeroes == 0) &&  (end_of_doc_markers == num_docs))
      printf("Check_term_occurrence_array():  Found no problems.\n");
  
}


static u_int *create_and_fill_term_occurrence_array(u_ll *TOFS, doctable_entry_t *doctable,
						    docnum_t num_docs, int vocab_size,
						    long long num_postings) {
  // The goal of this function is to create and fill in an array of term occurrences using frequency
  // data in TOFS.  The term occurrence array holds one u_int for every term occurrence.  The u_int
  // contains a term rank and some flags.  The term occurrences
  // The result array is filled in as follows.
  // 1. Create an empty term occurrences array with *num_postings elements
  // 2. f we have a file of significant co-occurrences containing an entry "Malcolm Turnbull  27" then
  // we would put 27 occurrences of the term rank corresponding to Malcolm and 27 occurrences of
  // that for Turnbull into the term occurrences array, marked with flags to indicate that each
  // pair is cooccurrence related and we would reduce the TOF values for those individual 
  // terms each by 27.  Currently the term dependence information we have available comes from
  // the ngrams.termids files created by Ngrams_from_TSV.exe.  Records in this file are of
  // the form similar to this example: N(9464,56514):10665 -- "auto insurance",
  // where:  N indicates that this is an N-gram relationship,
  //         (9464,56514) gives the term ranks of the participating words,
  //         :10665 is the number of times this N-gram was observed.
  //         -- "auto insurance" identifies the words involved (for debugging, explanation, etc.)
  // 3. Run through the TOFs array. If the TF value in TOF[r - 1] is f then we we would
  // place f copies of the integer r in the next available slots of the term occurrences array.
  
  // Return a pointer to an array of term occurrences and update the actual num_postings count.
  u_int *rezo = NULL, *intermediate = NULL;
  size_t term_occurrence_array_size;
  int trank, outcome;
  long long number_of_non_full_docs = num_docs, total_postings_placed = 0,
    ngram_postings_placed = 0, postings_still_to_generate = 0, t;
  double start;
 
	 
  term_occurrence_array_size = num_postings * sizeof(u_int);
  rezo = cmalloc(term_occurrence_array_size, "term occurrences array", FALSE);
  printf("Term occurrences array of %zd bytes malloced\n", term_occurrence_array_size);


  // Process the term_dependence information, if any ...
  if (synth_input_ngrams == NULL) {
    printf("\nNo term dependence information available.\n\n");
    postings_still_to_generate = num_postings;
  } else {
    start = what_time_is_it();
    ngram_postings_placed = process_ngrams_file(synth_input_ngrams, TOFS, rezo, vocab_size, num_postings,
						rezo, doctable, &number_of_non_full_docs);
    printf("Time to deal with term dependence information for vocab_size %d: %.3f sec.\n",
	   vocab_size, what_time_is_it() - start);
    total_postings_placed += ngram_postings_placed;
    postings_still_to_generate = sum_of_ull_array(TOFS, vocab_size);

    if (postings_still_to_generate + ngram_postings_placed != num_postings) {
      printf("Error:  Posting counts don't add up after n-grams.\n"
	     "Will overgenerate by %lld\n",
	     postings_still_to_generate + ngram_postings_placed - num_postings);
      exit(1);
    } 
  }

#if 0   // This was the pre-keynote version
  for (trank = 1; trank <= vocab_size; trank++) {  
    outcome = place_postings_in_random_documents(&trank, 1, TT_WORD, TOFS[trank - 1], doctable,
						 &number_of_non_full_docs, rezo, num_postings);
    if (outcome != 0) {
      printf("Error: Unfortunate outcome from place_postings...() at rank %d\n", trank);
      printf("Total postings placed = %lld (including %lld from n-grams) out of %lld\n",
	     total_postings_placed, ngram_postings_placed, num_postings);				   
      break;
    }
    total_postings_placed += TOFS[trank - 1];
  }
#else   // Quick change for keynote.  See head comment in this module
  // Allocate an intermediate array and shuffle it.
  intermediate = cmalloc(sizeof(int) * postings_still_to_generate,
			 "intermediate array", FALSE);
  printf("Intermediate array of %llu bytes malloced\n", sizeof(int) * postings_still_to_generate);
  // Now fill with all the term occurrences, and shuffle
  long long occno = 0;
  for (trank = 1; trank <= vocab_size; trank++) {
    long long  i;
    for (i = 0; i < TOFS[trank - 1]; i++) {
      if (occno >= postings_still_to_generate) {
	printf("Error:  Internal sanity check failed:  occno = %lld, trank = %d, tOFS[trank - 1] = %lld, vocab_size = %d\n",
	       occno, trank, TOFS[trank - 1], vocab_size);
	exit(1);
      }
      intermediate[occno++] = trank;
    }
  }
  printf("Term instances placed in the intermediate array: %lld/%lld\n", occno, postings_still_to_generate);
  knuth_shuffle_uint(intermediate, postings_still_to_generate);
  printf("Intermediate array shuffled\n");
  for (t = 0; t < postings_still_to_generate; t++) {
    outcome = place_one_word_instance_in_a_random_document(intermediate + t, 1, TT_WORD, doctable,
						 &number_of_non_full_docs, rezo, num_postings);
    if (outcome != 0) {
      printf("Error: Unfortunate outcome from place_one_word_inst...() t = %lld\n", t);
      printf("Total postings placed = %lld (including %lld from n-grams) out of %lld\n",
	     total_postings_placed, ngram_postings_placed, num_postings);
      exit(1);
    }
    total_postings_placed ++;
  }
  free(intermediate);

#endif
  

  printf("Total postings placed = %lld, cf %lld requested.\n", total_postings_placed,
	 num_postings);

  if ((rezo[num_postings - 1] & FINAL_POSTING_IN_DOC) == 0) {
    printf("Minor sanity check failed in create_and_fill_term_occurrence_array() - FINAL_POSTING not set.\n");
    // For some reason, we may not end up with a FINAL_POSTING_IN_DOC on the last posting
    // Not sure why, but it means the last record in the .forward file may have incorrect format.
    // Hopefully this will avoid it.
    rezo[num_postings - 1] |= FINAL_POSTING_IN_DOC;
  }

  if (number_of_non_full_docs)
    printf("Error: %lld documents remain non-full.\n", number_of_non_full_docs);
  check_term_occurrence_array(rezo, num_postings, num_docs);
  return rezo;  // ------------------------------->
}


static void write_synthetic_docs_starc(u_char *fname_synthetic_docs,
				 u_int *term_occurrence_array, long long num_postings,
				 termp term_rep_table) {
  // Write the representations of each term in the term occurrence array to the output
  // file in STARC (Simple Text ARChive) format.  Each document record starts with a length
  // in bytes, represented as an ASCII string, and the code 'D'.  Optionally document
  // records may be preceded by a header record with code 'H'.  End of document is
  // signalled by a term with FINAL_POSTING_IN_DOC set.
  // In this case we need to build up each document in a document buffer before output,
  // in order to know the length in bytes.

  int p, trank, error_code;
  u_char *term_rep;
  long long doxWritten = 0;
  CROSS_PLATFORM_FILE_HANDLE CORPUS;
  byte *corpusBuffer = NULL, *docBuffer = NULL, headerBuffer[100], headerHeader[100];
  size_t bytes_in_corpusBuffer, bytes_in_docBuffer, wdlen;

  docBuffer = (byte *)cmalloc(MAX_DOC_LEN + 1, "docBuffer", FALSE);
  bytes_in_docBuffer = 0;
  
  CORPUS = open_w(fname_synthetic_docs, &error_code);
  
  if (error_code) {
    printf("Error: Can't fopen %s for writing\n", fname_synthetic_docs);
    exit(1);
  }
  for (p = 0; p < num_postings; p++) {
    trank = term_occurrence_array[p] & TERM_RANK_MASK;
    if (trank > vocab_size) trank = vocab_size;  // The unknown term
    term_rep = term_rep_table + trank * TERM_ENTRY_LEN;
    
    // Append term to docBuffer, preceded if appropriate by a space
    if (bytes_in_docBuffer > 0) docBuffer[bytes_in_docBuffer++] = ' ';
    wdlen = strlen(term_rep);
    strcpy(docBuffer + bytes_in_docBuffer, term_rep);    
    bytes_in_docBuffer += wdlen;

    if (term_occurrence_array[p] & FINAL_POSTING_IN_DOC
	|| p == (num_postings - 1)) {
      // We've come to the end of the document -- output a header?
      if (include_docnums) {
	// Write the H record.  (Its header first)
	sprintf((char *)headerBuffer, "Doc%08lld", doxWritten);
	sprintf((char *)headerHeader, " %zdH ", strlen(headerBuffer));
	buffered_write(CORPUS, &corpusBuffer, DFLT_BUF_SIZE, &bytes_in_corpusBuffer,
		   headerHeader, strlen(headerHeader), "STARC HeaderHeader");
	
	buffered_write(CORPUS, &corpusBuffer, DFLT_BUF_SIZE, &bytes_in_corpusBuffer,
		   headerBuffer, strlen(headerBuffer), "STARC HeaderBuffer");
      }
      // Now write the D record.  (Header first)
      docBuffer[bytes_in_docBuffer++] = '\n';
      sprintf((char *)headerHeader, " %zdD ", bytes_in_docBuffer);
      buffered_write(CORPUS, &corpusBuffer, DFLT_BUF_SIZE, &bytes_in_corpusBuffer,
		     headerHeader, strlen(headerHeader), "STARC DocHeader");
      buffered_write(CORPUS, &corpusBuffer, DFLT_BUF_SIZE, &bytes_in_corpusBuffer,
		     docBuffer, bytes_in_docBuffer, "STARC Doc");
      bytes_in_docBuffer = 0;
      
     doxWritten++;
    }
  }

  buffered_flush(CORPUS, &corpusBuffer, &bytes_in_corpusBuffer, "closing", TRUE);  // TRUE implies free buffer memory
  free(docBuffer);
  
  printf("%lld documents written to %s\n", doxWritten, fname_synthetic_docs);
}


static void write_synthetic_docs_tsv(u_char *fname_synthetic_docs, u_int *term_occurrence_array,
				       long long num_postings, termp term_rep_table) {
  
  // Write the representations of each term in the term occurrence array to the output
  // file in TSV format.  It the term just emitted has the FINAL_POSTING_IN_DOC flag
  // set, output a TAB, a static weight (always 1), and a newline.

  int p, trank, error_code;
  u_char *term_rep;
  long long doxWritten = 0;
  CROSS_PLATFORM_FILE_HANDLE CORPUS;
  byte *buffer = NULL, line_end1[] = "\t1\n", line_end2[30], space[] = " ";
  size_t bytes_in_buffer;
  
  CORPUS = open_w(fname_synthetic_docs, &error_code);
  
  if (error_code) {
    printf("Error: Can't fopen %s for writing\n", fname_synthetic_docs);
    exit(1);
  }
  for (p = 0; p < num_postings; p++) {
    trank = term_occurrence_array[p] & TERM_RANK_MASK;
    if (trank > vocab_size) trank = vocab_size;  // The unknown term
    term_rep = term_rep_table + trank * TERM_ENTRY_LEN;
    buffered_write(CORPUS, &buffer, DFLT_BUF_SIZE, &bytes_in_buffer,
		   term_rep, strlen(term_rep), "write_the_term");
    if (term_occurrence_array[p] & FINAL_POSTING_IN_DOC
	|| p == (num_postings - 1)) {
      // Make sure the last line is properly formatted (belt and braces)
      if (include_docnums) { 
	sprintf((char *)line_end2, "\t1\tDoc%lld\n", doxWritten);
	buffered_write(CORPUS, &buffer, DFLT_BUF_SIZE, &bytes_in_buffer,
		       line_end2, strlen(line_end2), "line_end2");
      } else {
	buffered_write(CORPUS, &buffer, DFLT_BUF_SIZE, &bytes_in_buffer,
		       line_end1, 3, "line_end");
      }
      doxWritten++;
    } else {
     buffered_write(CORPUS, &buffer, DFLT_BUF_SIZE, &bytes_in_buffer,
		   space, 1, "space");
    }
  }

  buffered_flush(CORPUS,&buffer, &bytes_in_buffer, "closing", TRUE);
  printf("%lld documents written to %s\n", doxWritten, fname_synthetic_docs);
}




int main(int argc, char **argv) {
  double very_start, start, et;
  int a, max_doc_len;
  u_char *ap;
  u_ll *TOFS = NULL;  // Allow for terms with occurrence frequencies greater than 4 billion
  u_int *term_occurrence_array = NULL;  // Allow 30 bits for term number
  doctable_entry_t *doctable = NULL;
  termp term_rep_table = NULL;
  docnum_t num_docs = 0;
  
  if (argc < 2) print_usage(argv[0], args);
  setvbuf(stdout, NULL, _IONBF, 0);

  if (sizeof(size_t) != 8) {
    printf("Error: %s must be compiled for 64 bit.\n", argv[0]);
    exit(1);
  }

  // Read and process the command line options.
  for (a = 0; a < argc; a++) {
    char *ignore;
    ap = (u_char *)argv[a];
    assign_one_arg(ap, args, &ignore);
  }

  initialise_unicode_conversion_arrays(FALSE);

  if (tfd_use_base_vocab) {
    if (synth_input_vocab == NULL) {
      printf("Error: tfd_use_base_vocab is TRUE but synth_input_vocab not specified.\n");
      exit(1);
    }
  }
  
  set_up_for_term_generation();

  num_postings = (long long) synth_postings;
  vocab_size = (int) synth_vocab_size;
  
  // Check validity of arguments and print'em
  // Check that .forward file is writable.
  very_start = what_time_is_it();
  if (rand_seed == 0) rand_seed = (u_ll)fmod(very_start, 100000.0);
  rand_val(rand_seed);
  printf("TinyMT random number generator seeded with %llu\n", rand_seed);
  test_knuth_shuffle_uint_respect_phrases();
  
  // Generate representations for each of the terms
  start = what_time_is_it();
  term_rep_table = make_term_rep_table((u_int) synth_vocab_size, synth_term_repn_method);
  printf("Time taken: %.3f sec. to generate representations for %.0f terms\n",
	 (what_time_is_it() - start), synth_vocab_size);
  // ... perhaps write to disk?

  // Read or fake document length histogram, convert into a doctable, shuffle
  // and then fill in the pointers into the term occurrence array
  start = what_time_is_it();
  // Note that generate_fakedoc...() will read from a histo depending upon
  // options.
  max_doc_len = generate_fakedoc_len_histo(&num_docs);
  printf("Fake doc len histogram read or generated. Max len: %d\n", max_doc_len);
  
  doctable = create_doctable_from_histo(fakedoc_len_histo, num_postings, 
						&num_docs);
  knuth_shuffle(doctable, sizeof(u_ll), num_docs);
  plug_in_dt_pointers(doctable, num_docs);
  
  printf("Time to generate, histogram, create, shuffle docs, plug in pointers. for %lld docs: %.1f sec.\n",
	 num_docs, what_time_is_it() - start);
  // ... perhaps write to disk?

  
  // Generate and an array of all term occurrence frequencies
  start = what_time_is_it();
  if (tfd_use_base_vocab)
    TOFS = read_TOFS_array_from_file(synth_input_vocab, &num_postings, &vocab_size);
  else
    TOFS = create_and_fill_TOFS_array(&num_postings, &vocab_size);
  printf("Time to create and fill term occurrence frequency array for %d terms: %.3f sec.\n",
	 vocab_size, what_time_is_it() - start);


  // Generate and an array of all term occurrences, taking account of term dependencies if applicable
  start = what_time_is_it();
  term_occurrence_array = create_and_fill_term_occurrence_array(TOFS, doctable, num_docs,
								vocab_size, num_postings);
  printf("Time to create and fill term occurrence array for %lld occurrences: %.3f sec.\n",
	 num_postings, what_time_is_it() - start);
  free(TOFS);
  TOFS = NULL;
  
  // Here's the place to do a within-document shuffle, when we start handling mult-word terms
  start = what_time_is_it();
  shuffle_terms_within_docs(term_occurrence_array, num_postings);
  printf("Time for within-document shuffling: %.3f sec.\n", what_time_is_it() - start);

  start = what_time_is_it();
  if (tailstr(fname_synthetic_docs, ".tsv") == NULL && tailstr(fname_synthetic_docs, ".TSV") == NULL) {
    write_synthetic_docs_starc(fname_synthetic_docs, term_occurrence_array, num_postings,
			 term_rep_table);
  } else {
    write_synthetic_docs_tsv(fname_synthetic_docs, term_occurrence_array,
				   num_postings, term_rep_table);
  }
  printf("Time to write synthetic docs into %s: %.1f sec.\n",
	 fname_synthetic_docs, what_time_is_it() - start);

  et = what_time_is_it() - very_start;
  printf("Total elapsed time: %.1f sec.  Postings generated: %lld.  Rate of generation: %.3f Mpostings/sec.\n",
	 et, num_postings, (double)num_postings / et / 1000000.0);

}
