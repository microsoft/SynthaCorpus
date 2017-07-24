// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>  // for malloc
#include <string.h>  // for memset and memcpy
#include <math.h>    // for floor

#include "definitions.h"
#include "utils/general.h"
#include "shuffling.h"
#include "bubbleBabble.h"
#include "simpleWords.h"
#include "MarkovWords.h"
#include "corpusGenerator.h"
#include "wordFeatures.h"
#include "utils/randomNumbers.h"
#include "termRepMethods.h"

// Machinery for accumulating rank-correlated word length parameters
double base_counts[NUM_RANK_BUCKETS + MAX_WORD_LEN] = { 0 },
  base_means[NUM_RANK_BUCKETS + MAX_WORD_LEN] = { 0 },
  base_stdevs[NUM_RANK_BUCKETS + MAX_WORD_LEN] = { 0 },
  mimic_counts[NUM_RANK_BUCKETS + MAX_WORD_LEN] = { 0 },
  mimic_means[NUM_RANK_BUCKETS + MAX_WORD_LEN] = { 0 },
  mimic_stdevs[NUM_RANK_BUCKETS + MAX_WORD_LEN] = { 0 };



BOOL markov_model_word_lens = TRUE; // When generating EOWs, we get a distribution of
                                    // word lengths which doesn't correlate word length
                                    // with word rank.  If TRUE, this attempts to fix that.

int word_length_histo[MAX_TERM_LEN + 1] = {0};


void calculate_word_length_distribution(double *counts, double *means,
					double *stdevs) {
  int lbuk;
  double mean = 0.0, stdev = 0.0;
  // Calculate the means and standard deviations of rank-bucketed word lengths
  for (lbuk = 0; lbuk < NUM_RANK_BUCKETS; lbuk++) {
    if (counts[lbuk] > 0) {
      mean = means[lbuk] / counts[lbuk];
      stdev = sqrt((stdevs[lbuk] / counts[lbuk]) - mean * mean);
      means[lbuk] = mean;
      stdevs[lbuk] = stdev;
    }
    else {
      means[lbuk] = mean;    // Just inherit from the last bucket
      stdevs[lbuk] = stdev;  // which had observations.
    }
  }
}


void compare_word_length_distributions(double *base_counts, double *base_means,
				       double *base_stdevs, double *mimic_counts,
				       double *mimic_means, double *mimic_stdevs) {
    int lbuk;
    printf("\nMean word lengths for logarithmic rank buckets\n"
	   "----------------------------------------------\n\n"
	   "            Base corpus                   Mimic corpus\n"
	   "---------------------------------------------------------------\n"
	   "Bucket         Mean       St.dev.  |         Mean       St.dev.\n");
  for (lbuk = 0; lbuk < NUM_RANK_BUCKETS; lbuk++) {
    printf("%6d   %10.3f    %10.3f  |   %10.3f    %10.3f\n", lbuk,
	   base_means[lbuk], base_stdevs[lbuk],
	   mimic_means[lbuk], mimic_stdevs[lbuk]);
  }
  printf("---------------------------------------------------------------\n\n"); 
}

				   
#if 0
static int sort_term_array_by_pronouncability(byte **terms, int num_terms) {
  // In decreasing order and return the number of pronouncable terms.
  int p_histo[MAX_PRONOUNCABILITY_SCORE + 1] = {0};
  int i, p, num_pronouncables = 0, count, sum = 0, print_interval;
  long long slot;
  byte *old = *terms, *new_terms = NULL, *new;

  new_terms = malloc((size_t)num_terms * (size_t)TERM_ENTRY_LEN);
  if (new_terms == NULL) {
    printf("Error: malloc failed in sort_term_array_by_pronouncability()\n");
    exit(1);
  }

  // 1. Build histogram
  for (i = 0; i < num_terms; i++) {
    p = pronouncability(old);
    if (0) printf("  pronouncability(%s) = %d\n", old, p);
    if (p > 0) num_pronouncables++;
    p_histo[p]++;
    old += TERM_ENTRY_LEN;
  }
  if (0) printf("Pronouncability histogram built.\n");
  
  // 2. Make sumulative from top
  for (p = MAX_PRONOUNCABILITY_SCORE; p >= 0; p--) {
    count = p_histo[p];
    p_histo[p] = sum;
    sum += count;
  }
  if (0) printf("Pronouncability histogram made cumulative.\n");

  // 3. Now sort
  old = *terms;
  print_interval = 1;
  for (i = 0; i < num_terms; i++) {
    if ((i % print_interval) == 0) {
      printf("%10d\n", i);
      if (i % (10 * print_interval) == 0) print_interval *= 10;
    }
    p = pronouncability(old);
    slot = (long long)p_histo[p];
    p_histo[p]++;
    new = new_terms + (slot * TERM_ENTRY_LEN);
    memcpy(new, old, TERM_ENTRY_LEN);
    old += TERM_ENTRY_LEN;
  }  
  printf("Pronouncability histogram sorted.\n");

  free(*terms);
  *terms = new_terms;
  
  printf("First fifty words after sorting by pronouncability...\n");
  for (i = 0; i < 50; i++) printf("  %s\n", *terms + (i * TERM_ENTRY_LEN));
  printf("Pronouncables: %d / %d\n", num_pronouncables, num_terms);
  return num_pronouncables;
}
#endif


#define UNPRONOUNCABLE_PENALTY 2
int strlenp(u_char *s) {
  // return the length of s in bytes with the possibility of adding a penalty for
  // unpronounceable words.
  int l = (int)strlen(s), p = 0;
  if (markov_favour_pronouncable) {
    p = pronouncability(s);
    if (p == 0) l += UNPRONOUNCABLE_PENALTY;
    if (l > MAX_TERM_LEN) l = MAX_TERM_LEN;
  }
  return l;
}


static void sort_term_array_by_length(byte **terms, int num_terms) {
  // Sort num_terms terms by increasing length.  Note that length of unpronouncable words
  // may be penalised.  (Use of strlenp() rather than strlen()
  // Length of entries is TERM_ENTRY_LEN
  int i, l, sum = 0, count;
  long long slot;
  byte *new_terms, *old = *terms, *new, len;
  new_terms = malloc((size_t)num_terms * (size_t)TERM_ENTRY_LEN);
  if (new_terms == NULL) {
    printf("Error: malloc failed in sort_term_array_by_length()\n");
    exit(1);
  }
  
  // 1. Build histogram
  for (i = 0; i < num_terms; i++) {
    l = strlenp(old);
    old[TERM_LENGTH_INDEX] = (byte) l;
    word_length_histo[l]++;
    old += TERM_ENTRY_LEN;
  }
  
  // Turn word_length_histo into a cumulative version.
  for (i = 0; i <= MAX_TERM_LEN; i++) {
    count = word_length_histo[i];
    word_length_histo[i] = sum;
    sum += count;
  }

  printf("Grand total in word length histogram: %d\n", sum);

  // Now scan terms and copy entries into their proper place in new_terms
  old = *terms;
  for (i = 0; i < num_terms; i++) {
    len = old[TERM_LENGTH_INDEX];
    slot = (long long) word_length_histo[len];
    word_length_histo[len]++;
    new = new_terms + (slot * TERM_ENTRY_LEN);
    memcpy(new, old, TERM_ENTRY_LEN);
    if (0) printf("%s copied into slot %lld\n", new, slot);
    old += TERM_ENTRY_LEN;
  }

  
  free(*terms);
  *terms = new_terms;

  if (markov_favour_pronouncable)
    printf("First fifty words after sorting by pronouncable length...\n");
  else
    printf("First fifty words after sorting by length in bytes...\n");
  
  for (i = 0; i < 50; i++) printf("  %s\n", *terms + (i * TERM_ENTRY_LEN));
}


static void check_for_null_words(termp term_storage, u_int vocab_size) {
  int i, null_words = 0;
  byte *bp = term_storage;
  for (i = 0; i < vocab_size; i++) {
    if (*bp == 0) {
      if (null_words < 10) printf("Null word at rank %d\n", i);
      null_words++;
    }
    bp += TERM_ENTRY_LEN;
  }
  printf("Null words found: %d\n", null_words);
  if (null_words) {
    printf("Error: Taking exit because there shouldn't be any null words!\n");
    exit(1);
  }
}


void fill_in_term_repn_table_tnum(termp term_storage, u_int vocab_size,
				 size_t max_term_len) {
  // t is the rank of a term, starting from 0.
  // These terms are just the letter 't' followed by the decimal term number
  int t, trank, len = 0;
  termp wp = term_storage;

  for (t = 0; t < vocab_size; t++) {
    trank = t;
    len = 0;
    wp[len++] = 't';
    do  {
      if (len >= max_term_len) break;  // Storage capacity reached.
      wp[len++] = '0' + (trank % 10);
      trank /= 10;   
    } while (trank > 0);
    wp[len] = 0;
    wp[TERM_LENGTH_INDEX] = len;
    wp += TERM_ENTRY_LEN;
  }
}



void fill_in_term_repn_table_base26(termp term_storage, u_int vocab_size,
				 size_t max_term_len) {
  // t is the rank of a term, starting from 0.
  int t, trank, len = 0;
  termp wp = term_storage;

  for (t = 0; t < vocab_size; t++) {
    trank = t;
    len = 0;
    do  {
      if (len >= max_term_len) break;  // Storage capacity reached.
      wp[len++] = 'a' + (trank % 26);
      trank /= 26;   
    } while (trank > 0);
    wp[len] = 0;
    wp[TERM_LENGTH_INDEX] = len;
    wp += TERM_ENTRY_LEN;
  }
}


void fill_in_term_repn_table_bubble_babble(termp term_storage, u_int vocab_size,
				 size_t max_term_len) {
  // t is the rank of a term, starting from 0.
  int t, trank, len = 0;
  termp wp = term_storage;

  for (t = 0; t < vocab_size; t++) {
    trank = t;
    strncpy(wp, bubble_babble(trank), MAX_TERM_LEN);
    wp[MAX_TERM_LEN] = 0;
    len  = (byte)strlen(wp);
    wp[TERM_LENGTH_INDEX] = len;
    wp += TERM_ENTRY_LEN;
  }
}


void fill_in_term_repn_table_simpleWords(termp term_storage, u_int vocab_size,
				 size_t max_term_len) {
  // t is the rank of a term, starting from 0.
  int t, trank, len = 0;
  termp wp = term_storage;

  for (t = 0; t < vocab_size; t++) {
    trank = t;
    simpleWords((char *)wp, (u_ll)trank);
    wp[MAX_TERM_LEN] = 0;
    len  = (byte)strlen(wp);
    wp[TERM_LENGTH_INDEX] = len;
    wp += TERM_ENTRY_LEN;
  }
}


void fill_in_term_repn_table_from_tsv(termp term_storage, u_int *vocab_size,
				      size_t max_term_len, u_char *input_vocabfile) {
  // t is the rank of a term, starting from 0.
  int trank = 0, len = 0;
  termp wp = term_storage;
  u_char linebuf[1000], *p;
  FILE *VF;

  VF = fopen(input_vocabfile, "rb");
  if (VF == NULL) {
    printf("Error: fill_in_term_repn_table_from_tsv(): can't open %s\n",
	   input_vocabfile);
    exit(1);
  }
  while (fgets(linebuf, 1000, VF) != NULL) {
    p = linebuf;
    while (*p >= ' ') p++;  // Skip to null tab or any ASCII control char
    if (*p != '\t') {
      printf("Error: fill_in_term_repn_table_from_tsv(): TAB not found in input line %d\n",
	     trank);
      exit(1);
    }
    *p = 0;
    strncpy(wp, linebuf, MAX_TERM_LEN);
    wp[MAX_TERM_LEN] = 0;
    len  = (byte)strlen(wp);
    wp[TERM_LENGTH_INDEX] = len;
    wp += TERM_ENTRY_LEN;
    trank++;
    if (trank >= *vocab_size) break;
  }

  if (trank < *vocab_size) {
    printf("Warning: fill_in_term_repn_table_from_tsv(): requested vocab_size reduced to %u\n",
	   trank);
    *vocab_size = trank;
  }
  
}


static void setup_rank_buckets(rank_bucket_entry_t *rb) {
  int b, f = 1;
  for (b = 0; b < NUM_RANK_BUCKETS; b++) {
    rb[b].next_rank = f;   // e.g.  1, 10, 100
    f *= 10;
    rb[b].max_rank = f - 1; // e.g.  9, 99, 999
    printf("Rankbuck %d: %d, %d\n", b, rb[b].next_rank, rb[b].max_rank);
  }
}



static void setup_length_buckets(rank_bucket_entry_t *rb, termp term_array, u_int vocab_size) {
  // The term array is assumed sorted by length, with possible length penalty.
  int b, r, prevlen = 0, len;
  u_char *term = term_array;

  // Many buckets will not be used.  Set them to full
  for (b = 0; b < MAX_TERM_LEN; b++) {
    rb[b].next_rank = vocab_size;
    rb[b].max_rank = vocab_size;
  }
   
  
  for (r = 1; r <= vocab_size; r++) {
    len = strlenp(term);
    if (len < prevlen) {
      printf("Error: setup_length_buckets() - term array must be sorted by increasing length\n");
      exit(1);
    }
    if (len > prevlen) {      
      rb[len - 1].next_rank = r;
      if (len > 1) rb[len - 2].max_rank = r - 1;
      prevlen = len;
    }
    term += TERM_ENTRY_LEN;
  }

  printf("Length buckets set up ....\n\n"
	 "Len Nextrank Maxrank\n__________________\n");
  for (b = 0; b < MAX_TERM_LEN; b++) {
    printf("%2d %8d - %8d\n", b + 1, rb[b].next_rank, rb[b].max_rank);
  }
}



int biased_random_pick(double *probvec, int veclen) {
  // probvec is a vector of cumulative probabilities whose length is veclen
  int e;
  double r;
  r = rand_val(0);
  for (e = 0; e < veclen; e++) {
    if (r < probvec[e]) return e;
  }
  // If we get here, it may be because probvec is all zeroes, in which
  // case choose a random element.
  return (int)floor(r * veclen);
}



static int find_a_better_rank_bucket(int useless_lbuk, rank_bucket_entry_t *rank_buckets, u_int num_terms) {
  // The currently selected lbuk is no good, try to find a better value.
  int lbuk;

  // First try going to a higher bucket.
  lbuk = useless_lbuk + 1;
  while (rank_buckets[lbuk].next_rank > rank_buckets[lbuk].max_rank) lbuk++;  //Find one with room
  if (rank_buckets[lbuk].next_rank <= num_terms) return lbuk;

  // That didn't work, try going smaller
  lbuk = useless_lbuk + 1;
  while (lbuk >= 0
	 && (rank_buckets[lbuk].next_rank > rank_buckets[lbuk].max_rank
	     || rank_buckets[lbuk].next_rank > num_terms)) lbuk--;  //Find one with room
  if (lbuk < 0 || rank_buckets[lbuk].next_rank > num_terms) {
    int r;
    printf("Error: Assignment of lbuk in find_a_better_rank_bucket(%d) failed.\n",
	   useless_lbuk);
    for (r = 0; r < NUM_RANK_BUCKETS; r++)
      printf("Rankbuck %d: %d, %d\n", r, rank_buckets[r].next_rank, rank_buckets[r].max_rank);
    exit(1);
  }
  return lbuk;
}



static int find_a_better_length_bucket(int useless_len, rank_bucket_entry_t *rank_buckets,
				       u_int num_terms) {
  // The currently selected len is no good, try to find a better value. Note that this logic
  // is really identical to find_a_better_rank_bucket()
  int len;

  // First try going to a higher length.
  len = useless_len + 1;
  while (rank_buckets[len - 1].next_rank > rank_buckets[len - 1].max_rank
	 && len <= MAX_WORD_LEN) len++;  //Find one with room
  if (0) printf("Came in with useless %d, suggesting %d  (%d)\n", useless_len,
		len, rank_buckets[len - 1].next_rank);
  if (rank_buckets[len - 1].next_rank <= num_terms
      && len <= MAX_WORD_LEN) return len;

  // That didn't work, try going smaller
  len = useless_len - 1;
  while (len > 0
	 && (rank_buckets[len - 1].next_rank > rank_buckets[len - 1].max_rank
	     || rank_buckets[len - 1].next_rank > num_terms)) len--;  //Find one with room
  if (len <= 0 || rank_buckets[len - 1].next_rank > num_terms) {
    int r;
    printf("Error: Assignment of len in find_a_better_rank_bucket(%d) failed.\n",
	   useless_len);
    for (r = 0; r < NUM_RANK_BUCKETS; r++)
      printf("Lengthbuck %d: %d, %d\n", r, rank_buckets[r].next_rank, rank_buckets[r].max_rank);
    exit(1);
  }
   if (0) printf("Came in with useless %d, Decided upon %d  (%d)\n",
		 useless_len, len, rank_buckets[len - 1].next_rank);
 
  return len;
}



static void randomly_assign_terms_to_ranks_based_on_length(byte **terms,
							   u_int num_terms,
							   double *lenprob_matrix) {
  byte *new_terms, *old = *terms, *new;
  double *row;
  int i, l, lbuk;
  // The following is a bit weird because rank_buckets are used as length_buckets
  // too, but the array dimensions needed are different.  This expression
  // should guarantee enough room for either.
  rank_bucket_entry_t rank_buckets[NUM_RANK_BUCKETS + MAX_TERM_LEN] = {0};

  if (markov_assign_reps_by_rank) {
    printf("Assigning word representations by rank(%d).  Setting up length buckets\n",
	   num_terms);
    setup_length_buckets(rank_buckets, *terms, num_terms);
  } else
    setup_rank_buckets(rank_buckets);

  new_terms = malloc((size_t)num_terms * (size_t)TERM_ENTRY_LEN);
  if (new_terms == NULL) {
    printf("Error: malloc failed in sort_term_array_by_length()\n");
    exit(1);
  }
  new = new_terms;

  // Whiz through the array and assign terms to the next available
  // slot in a randomly assigned rank bucket.  If that bucket is full,
  // move to the next etc.
  for (i = 1; i <= num_terms; i++) {
    if (markov_assign_reps_by_rank) {
      // NEW METHOD:  Seems to work better than the old one.
      //  -- Randomly choose a word to put at rank i - 1
      long long chosen;  
      lbuk = (int)log10((double)i);
      row = lenprob_matrix + lbuk * MAX_TERM_LEN;
      // Row is an array of length probabilities for words in the rank bucket appropriate
      // to this rank.
      l = biased_random_pick(row, MAX_TERM_LEN) + 1; 
      if (0) printf("Trank %d, lbuk = %d, l = %d\n", i, lbuk, l);
      // Just make sure this value of lbuk makes sense.  It's very hard to
      // follow because rank_buckets are actually length buckets.
      while (rank_buckets[l - 1].next_rank > num_terms) l--;
      // Otherwise we'll get a segfault.
    
      if (rank_buckets[l - 1].next_rank > rank_buckets[l - 1].max_rank) {  // bucket full
	l = find_a_better_length_bucket(l, rank_buckets, num_terms);
	if (0) printf("Find_a_better_length_bucket returned %d\n",
				l);
      }
      chosen = rank_buckets[l - 1].next_rank - 1;
      if (chosen >=  num_terms)  {
	printf("Error: ridiculous choice %lld for term rank\n", chosen);
	exit(1);
      }
      old = *terms + (chosen * TERM_ENTRY_LEN);  // This overflows if chosen is 32 bit.
      memcpy(new, old, TERM_ENTRY_LEN);
      rank_buckets[l - 1].next_rank++;
      new += TERM_ENTRY_LEN;
      if (0) printf("RATTRBOL(%s) l = %d, chosen = %lld\n", old, l, chosen);

   } else {
      long long slot;
      // OLD METHOD:  Doesn't achieve the desired relationship between term freq
      // and term length
      // -- Randomly find a rank at which to put the i-th word
      l = strlenp(old);
      row = lenprob_matrix + (l - 1) * NUM_RANK_BUCKETS;
      lbuk = biased_random_pick(row, NUM_RANK_BUCKETS);
      // Just make sure this value of lbuk makes sense
      while (rank_buckets[lbuk].next_rank > num_terms) lbuk--;
      // Otherwise we'll get a segfault.
    
      if (rank_buckets[lbuk].next_rank > rank_buckets[lbuk].max_rank) {  // bucket full
	lbuk = find_a_better_rank_bucket(lbuk, rank_buckets, num_terms);  
      }
      slot = rank_buckets[lbuk].next_rank - 1;
      if (l < 3) printf("  %d/%u: length %d Bucket %d: slot is %lld.\n", i, num_terms, l, lbuk, slot);
      new = new_terms + (slot * TERM_ENTRY_LEN);
      memcpy(new, old, TERM_ENTRY_LEN);
      rank_buckets[lbuk].next_rank++;
      old += TERM_ENTRY_LEN;
    }
   }

  free(*terms);
  *terms = new_terms;
}


void accumulate_bucketed_length_counts(termp term_storage, u_int vocab_size) {
  int t, l, lbuk;
  u_char *term = term_storage;
  if (1) printf("vocab_size = %u\n", vocab_size);
  for (t = 1; t <= vocab_size; t++) {
    l = (int)strlen(term);
    if (l > 0) {  
      lbuk = (int)floor(log10((double)t));
 
      mimic_counts[lbuk]++;
      mimic_means[lbuk] += (double)l;
      mimic_stdevs[lbuk] += (double)(l * l);
    }
    term += TERM_ENTRY_LEN;
  }
}


void fill_in_term_repn_table_markov(termp *term_storage, u_int vocab_size,
				    size_t max_term_len, int Markov_order,
				    u_char *input_vocabfile) {
  int t, trank, len = 0;
  termp wp = *term_storage;
  double *lenprob_matrix = NULL;
  
  setup_transition_matrices(Markov_order, input_vocabfile, &lenprob_matrix);
  
  printf("Filling in the synthetic vocabulary\n\n      words\n      -----\n");
  for (t = 0; t < vocab_size; t++) {
    trank = t + 1;
    store_unique_markov_word(wp, (u_ll)trank);
    wp[MAX_TERM_LEN] = 0;
    len  = (byte)strlen(wp);
    if (len < 0 || len > MAX_TERM_LEN) {
      printf("Length error: %d\n", len);
      exit(1);
    }
    wp[TERM_LENGTH_INDEX] = len;
    wp += TERM_ENTRY_LEN;
    if (trank%10000 == 0) printf("%11d\n", trank);
  }
  printf("%11d\n\n", vocab_size);

  decommission_transition_matrices_etc();
  
  printf("First fifty words before sorting ...\n");
  for (t = 0; t < 50; t++) printf("    %s\n", *term_storage + (t * TERM_ENTRY_LEN));

  check_for_null_words(*term_storage, vocab_size);

  if (USE_MODIFIED_MARKOV_WITH_END_SYMBOL) {
    if (markov_model_word_lens) {
      sort_term_array_by_length(term_storage, vocab_size);
      check_for_null_words(*term_storage, vocab_size);
      randomly_assign_terms_to_ranks_based_on_length(term_storage, vocab_size, lenprob_matrix);
      printf("First fifty words after random assignment to rank buckets ...\n");
      for (t = 0; t < 50; t++) printf("    %s\n", *term_storage + t * TERM_ENTRY_LEN);
      check_for_null_words(*term_storage, vocab_size);     
    }
  }
  free(lenprob_matrix);
  accumulate_bucketed_length_counts(*term_storage, vocab_size);
  calculate_word_length_distribution(mimic_counts, mimic_means, mimic_stdevs);
  compare_word_length_distributions(base_counts, base_means, base_stdevs,
				    mimic_counts, mimic_means, mimic_stdevs);
}


								      
