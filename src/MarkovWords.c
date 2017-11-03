// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This module includes the functions to:
//
// A. build Markov transition matrices of up to a specified order from a vocab.tsv file, and
// B. to generate a random word of specified length, using the transition matrix
// C. to free the memory used by the transition matrices and other necessary structures.
//
// Much of the code is derived from SyntheticVocabGenerator.c
//
// The following notation applies to the below:
//     A - the size of the actual printable alphabet, possibly extended by an
//         end-of-word symbol EOW which may be printed as '$'
//     E - A + 1, the alphabet extended by one to allow for a start symbol, which
//         is always represented by SOW, and printed as '^'.
//     In the extended alphabet, SOW is always first at position zero and EOW
//     last, with the highest position number.

// A transition matrix is a rectangular matrix whose number of columns is A,
// the size of the real alphabet.  The number of rows is E^k.
// E.g. for order 0, there is only one row.  For order 1, the rows are indexed
// by the ordinal value of the symbols in E.  For higher k, the rows are logically indexed
// by a sequence of letters from the alphabet.
//
// The values across a row represent the probability of transitioning from the length-k
// row index to each of the possible next symbols.  The probabilities in each row are
// kept in cumulative form.
//
// Two distinct versions of the model are supported:  In the first, transition probabilities
// are calculated and recorded for an explicit END-OF-WORD symbol, extending
// the alphabet size by one.  This model is likely to provide natural word endings
// but we will have to resort to sorting and partial shuffling to achieve the desired
// correlation between term rank and term length, i.e. that more frequent words tend
// to be shorter.
//
// In the second version of the method, there is no END-OF-WORD symbol.  Instead,
// a rank-correlated random length is generated for each word. Letter-by-letter
// generation proceeds until that length is reached.  As the training vocabulary
// is scanned, counts are maintained .............
//
// The reason for generating all the transition matrices from order 0 to order k
// is that a prefix of length k (where k is large) is very constraining.   In order
// to be able to generate unique words and potentially length k+1 sequences which
// were not observed in training, we must allow for back-off to lower k.  Backing
// off from, say order 5, to order 0 results in unnatural looking words.  The theory
// is that cascading back-off will do better.

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "definitions.h"
#include "utils/general.h"
#include "imported/Fowler-Noll-Vo-hash/fnv.h"
#include "utils/dahash.h"
#include "imported/TinyMT_cutdown/tinymt64.h"
#include "corpusGenerator.h"
#include "termRepMethods.h"
#include "utils/randomNumbers.h"
#include "MarkovWords.h"


#define CHAR_NOT_IN_ALPHABET -1
#define BMP_SIZE 65536   // Size of Basic MultiLingual Plane (Unicode)
#define MAX_MARKOV_K 7
#define DLM_ADJUSTMENT 1.0 // 0.875  // These adjustments to length means and standard deviations are used to compensate
#define DLS_ADJUSTMENT 1.0 // 1.175  //  to some extent for the left truncation of the length distribution

#define BUF_LEN (MAX_MARKOV_K + MAX_TERM_LEN + 1)  // Enough room for the longest possible word (null-term) plus
                                                   // the maximum possible number of SOW symbols.
#define EOW '$'
#define SOW '^'
#define HASH_BITS 27  // You need this many for ClueWeb12  (89 million terms)

static long long  E_alphabet_size = 0, A_alphabet_size = 0;
static double *transition_matrices =  NULL, // This is the storage for the transition matrices up to order k
  *Markov[MAX_MARKOV_K + 1] = {NULL},  // These are pointers to the relevant parts of the storage.
  *letter_frequencies = NULL;

static int col_map[BMP_SIZE];   // Maps character codes from within the basic multilingual plane
                                // into a column within a Markov transition matrix
static int row_map[BMP_SIZE];   // Maps character codes from within the basic multilingual plane
                                // into a row within a Markov-1 transition matrix
							
static int *freq_sorted_alphabet, Markov_order = -1;
static u_short rev_col_map[BMP_SIZE] = { 0 }; // Character to output for col index in alphabet.
static u_short rev_row_map[BMP_SIZE] = { 0 }; // Character to output for row index in alphabet.

BOOL USE_MODIFIED_MARKOV_WITH_END_SYMBOL = FALSE,
  markov_full_backoff = TRUE,   // Whenever backing off to a background model, go all the way
                                // to level zero.
  markov_use_within_vocab_probs = TRUE,   // If true, when counting letter and transition frequencies
                                          // just add one each time.  Otherwise add the occurence
                                          // frequency of the vocabulary word.
  markov_assign_reps_by_rank = TRUE,
  markov_favour_pronouncable = TRUE;

dahash_table_t *words_generated;


/////////////////////////////////////////////////////////////////////////////////////////
//                                                                                     //
// Internal helper functions
//                                                                                     //
/////////////////////////////////////////////////////////////////////////////////////////

static void define_alphabet_lc_ascii() {
  // Load col_map with mappings for the lower-case ASCII letters after initializing everything to 
  // CHAR_NOT_IN_ALPHABET.   Set the in and out alphabet_sizes.
  int i;
  int c, r, rrm;
  for (i = 0; i < BMP_SIZE; i++) col_map[i] = CHAR_NOT_IN_ALPHABET;

  // Make a zero-th entry in the row_map for the start symbol SOW
  row_map[0] = 0;
  rev_row_map[0] = SOW;
  rrm = 1;
  c = 0;
  r = 1;
  // Now fill in entries for all of the actual letters
  for (i = 'a'; i <= 'z'; i++) {
    rev_col_map[c] = (u_short)i;  // Map from a column index, e.g. 0 .. 25 into a printable character, e.g. 'a' .. 'z'
    rev_row_map[rrm++] = (u_short)i;
    col_map[i] = c++;  // Map the letter to a column index  (No column for SOW)
    row_map[i] = r++;  // Map the letter to a row index     (No row corresponding to EOW)
}

  
  if (USE_MODIFIED_MARKOV_WITH_END_SYMBOL) {
    // In this case there's an additional column corresponding to End-of-word
    rev_col_map[c] = (u_short)EOW;
    col_map[EOW] = (int)c++;
    // There's no row_map  for EOW because it can never appear in context.
    if (c != r) {
      printf("Error: when using EOW, the two alphabet sizes should be the same (%d, %d)\n",
	     c, r);
      exit(1);
    }
  } else {
    if (r != (c + 1)) {
      printf("Error: when not using EOW, one alphabet size should one greater than the other (%d, %d)\n",
	     c, r);
      exit(1);
    }
  }

  A_alphabet_size = c;
  E_alphabet_size = r;


  letter_frequencies = (double *)cmalloc(A_alphabet_size *sizeof(double),
					 "letter frequencies", FALSE);
  freq_sorted_alphabet = (int *)cmalloc(A_alphabet_size *sizeof(int),
					"freq_sorted_alphabet", FALSE);

  printf("Alphabet sizes: in %lld,  out %lld\n", E_alphabet_size, A_alphabet_size);
}


static u_char *printable_context(u_char *pcbuf, long long row_index, int k) {
  // Given the number of a row in a transition matrix, convert it to the
  // corresponding context string.
  // pcbuf must have capacity for at least 12 bytes
  u_char *p = pcbuf;
  long long apower = 1;
  int d,  i;

  if (k == 0) {
    strcpy(pcbuf, "No Context");
    return pcbuf;
  }
  
  for (i = 1; i < k; i++) apower *= E_alphabet_size;  // The value of 1 in the most significant digit

  for (i = 1; i <= k; i++) {  // There are k letters of context.
    d = (int)(row_index / apower);
    *p++ = rev_row_map[d];
    row_index %= apower;
    apower /= E_alphabet_size;
  }
  *p = 0;
  return pcbuf;
}


static void print_row(double *row, u_char *label) {
  int c;
  printf("Row probabilities for %s\n", label);
  for (c = 0; c < A_alphabet_size; c++) {
    printf("%8d - %.4f\n", c, row[c]);
  }
  printf("\n");
}




static long long calculate_row_index(u_char *p, int k) {
  // Calculate a row index from the n bytes starting at p
  // This is the index of the element in the single-dimensional array
  // representing the start of a simulated row.   Each row contains
  // A_alphabet_size elements, there are E_alphabet_size^k rows.
  int  i, rm;
  long long index = 0;
  long long sanity_check_row_numbers[MAX_MARKOV_K + 1] = {1};

  if (k == 0) return 0;
  for (i = 1; i <= MAX_MARKOV_K; i++) {
    sanity_check_row_numbers[i] = sanity_check_row_numbers[i - 1] * E_alphabet_size;
  }
 
  for (i = 0; i < k; i++) {
    index *= E_alphabet_size;   
    rm = row_map[p[i]];
    if (rm < 0 || rm >= E_alphabet_size) {
      printf("Calculate_row_index error:  rm = %d, p[i] = %d i = %d\n", rm, p[i], i);
      exit(1);
    }
    index += rm;
  }

  if (index > sanity_check_row_numbers[k]) {
    printf("Erroneous row number calculated from %d bytes = %lld > %lld: '",
	   k, index, sanity_check_row_numbers[k]);
    for (i = 0; i < k; i++) putchar(p[i]);
    putchar('\n');
  }

  index *= A_alphabet_size;  // Turn from row number into element number in one-d array
  return index;  // result is in doubles, not in bytes
}


static int cmp_letter_freqs(const void *ip, const void *jp) {
  int ixi = *(int *)ip, ixj = *(int *)jp;
  if (letter_frequencies[ixj] > letter_frequencies[ixi]) return 1;
  if (letter_frequencies[ixi] > letter_frequencies[ixj]) return -1;
  return 0;
}

static void check_transition_matrices() {
  int k, r;
  long long num_rows = 1;
  double *end_of_row = transition_matrices + (A_alphabet_size - 1);


  // Check that every A_alphabet_size-th element is 1.0
  for (k = 0; k <= Markov_order; k++) {
    for (r = 0; r < num_rows; r++) {
      if (*end_of_row < 0.999999 || *end_of_row > 1.000001) {
	printf("Error in transition matrix %d, row %d:  %.5f\n",
	       k, r, *end_of_row);
	exit(1);
      }
      end_of_row += A_alphabet_size;
    }
    num_rows *= E_alphabet_size;
    printf("Check_transition_matrices(%d): PASS\n", k);
  }
}

static void convert_matrix_rows_to_cumprobs(double *M, int rows, int columns) {
  int i, j, start_of_row;
  double row_sum, prob, cumprob;
  
  for (i = 0; i < rows; i++) {
    if (markov_assign_reps_by_rank)
      printf("CMRTC[rank bucket %d]:", i + 1);
    else
      printf("CMRTC[word length %d]:", i + 1);
    row_sum = 0.0;
    cumprob = 0.0;
    start_of_row = i * columns;
    for (j = 0; j < columns; j++) row_sum += M[start_of_row + j];
    if (row_sum > 0.0) {
      for (j = 0; j < columns; j++){
	prob = M[start_of_row + j] / row_sum;
	cumprob += prob;
	M[start_of_row + j] = cumprob;
	printf(" %.4f", cumprob);
      }
    // Rows with no observations have been initialised to zero already.
    } else printf(" Zero Row");
    printf("\n");
  }
}


static void convert_transition_matrices_to_probs() {
  // Note that the transition matrix is kept in the form of cumulative
  // probabilities across the rows.  Each row is of length A_alphabet_size
  // markov0 is a pre-computed array of basic letter probabilities.
  int k, j; 
  long long i, start_of_row, backstart = 0, num_rows = 1, backoff_row = 0, backoff_power = 1;
  double row_sum, prob, cumprob, *transition_matrix = NULL, *backoff_matrix = NULL;
  u_char pcbuf1[20], pcbuf2[20], label[150];

  for (k = 0; k <= Markov_order; k++) {
    // Have to do it for each of the matrices up to order K.  Do it in
    // order of increasing k to facilitate backoff.
    transition_matrix = Markov[k];
    if (k > 0) num_rows *= E_alphabet_size;

    // Prepare backoff_power for use in filling in from backoff_matrix
    // It's dependent on k
    for (j = 1; j < k; j++) backoff_power *= A_alphabet_size;

    for (i = 0; i < num_rows; i++) {
      cumprob = 0.0;
      start_of_row = i * A_alphabet_size;
      row_sum = 0.0;
      for (j = 0; j < A_alphabet_size; j++) row_sum += transition_matrix[start_of_row + j];

      if (row_sum <= 0.000001 && k > 0) {
	// If there are no observed probabilities in this row, back off to a row in
	// the transition matrix of next lowest order.  It's obvious which matrix we
	// should use, but we need to do a bit of calculation to find the row number.
	// Say this row corresponds to a context of 'abcde', the backoff context should
	// be 'bcde'.  Treating letters as digits in a high base number system, we have
	// calculated the 'abcde' row index as r = e + Ed + E^2c + E^3b + E^4a
	// whereas the 'bcde' row index is r' = e + Ed + E^2c + E^3b.  To obtain r'
	// from r, we use: r' = r % (E^4);
	if (markov_full_backoff) {  // Back off all the way to zero.
	  backoff_matrix = Markov[0];
	  backoff_row = 0;
	} else {
	  backoff_matrix = Markov[k - 1];   // Back off to the one below.
	  backoff_row = i % backoff_power;
	}
	if (0) {
	  printf(" for row %lld (%s) to %lld (%s) in order k = %d\n",
		 i, printable_context(pcbuf1, i, k), backoff_row,
		 printable_context(pcbuf2, backoff_row, k - 1), k);
	}
	backstart = backoff_row * A_alphabet_size;
	if (0) print_row(backoff_matrix + backstart, "back off");
      }


      // Filling in the cumulative probabilities in the 
      for (j = 0; j < A_alphabet_size; j++) {
	if (row_sum > 0.000001) {
	  prob = transition_matrix[start_of_row + j] / row_sum;
	  cumprob += prob;
	} else {
	  cumprob = backoff_matrix[backstart + j];  // Already normalized and accumulated
	}
	transition_matrix[start_of_row + j] = cumprob;  // Have to write into the target trans mat.
      }
      if (debug) printf("Row sum: %.5f\n", row_sum);
      if (cumprob < 0.00001) {
	printf("Line with zero sum. [%lld, %d]: %.7f\n", i, j, cumprob);
   	sprintf(label, "Order %d, row %s", k, printable_context(pcbuf2, i, k));
	print_row(transition_matrix + start_of_row, label);
      } 

    }
    printf("Convert_transition_matrices_to_probs(): Order %d set up. num_rows = %lld.\n", k, num_rows);
  }
  check_transition_matrices();
}



/////////////////////////////////////////////////////////////////////////////////////////
//                                                                                     //
// Externally visible functions
//                                                                                     //
/////////////////////////////////////////////////////////////////////////////////////////


void setup_transition_matrices(int K, u_char *training_tsv, double **lp) {
  // Allocate space for a transition matrices of sizes appropriate
  // for all orders from zero up to K (see 
  // this module's head comment) then scan the training TSV file
  // (wordTABfrequencyEOL format) accumulating frequencies in the elements
  // of the transition matrices, before finally converting them all to
  // probabilities.

  // ***Now*** we assume that the words in the TSV file are sorted by decreasing
  // frequency of occurrence (rank). While reading the file we build up a two
  // dimensional array of probabilities (lp) in which element lp[i,j] represents
  // the probability that a word of length i will fall in rank bucket j.  Rank
  // buckets are logarithmic, i.e bucket 0 comprises ranks 1 - 10, bucket 1 comprises
  // 11 - 100, and so on up to bucket NUM_RANK_BUCKETS - 1.  NUM_RANK_BUCKETS is set to 9,
  // allowing for ranks up to one billion.  Lengths range from 1 to MAX_TERM_LEN 
  size_t elements_of_current_matrix = 0, total_elements = 0;
  off_t offie = 0;
  int i, l, k, trank = 0, col_index, rankbuk;
  long long row_index;
  double freak, total_freak = 0, *transition_matrix = NULL, *lenprob_matrix = NULL;
  u_char linebuf[1000], wd_buf[BUF_LEN], printable_wd_buf[BUF_LEN],
    *real_buf, *bp, *p, *q, *tabptr;
  FILE *VF;
  u_ll wds_read = 0;

  if (K < 0 || K > MAX_MARKOV_K) {
    printf("Error: Markov methods are only supported for  0<=K<=%d. %d was specified.\n",
	   MAX_MARKOV_K, K);
    exit(1);
  }
  
  Markov_order = K;
  
  define_alphabet_lc_ascii();

  // Allocate and zero the length probability matrix.
  lenprob_matrix = (double *)cmalloc(NUM_RANK_BUCKETS * MAX_TERM_LEN * sizeof(double),
				     "lenprob_matrix", FALSE);
  
 // Calculate the number of elements in all the transition matrices, then malloc and zero
  // an appropriate amount of storage.
  elements_of_current_matrix = A_alphabet_size;
  for (i = 0; i <= K; i++) {
    printf("Elements in order %d matrix: %zd\n",i,  elements_of_current_matrix);
     total_elements += elements_of_current_matrix;   // No. elements in all the matrices up to i
     elements_of_current_matrix *= E_alphabet_size;  // The number of elements in the matrix for order i
  }

  printf("Total elements in all matrices: %zd\n", total_elements);
  transition_matrices = (double *)cmalloc(total_elements * sizeof(double),
					  "transition matrices", TRUE);

  // Now set up the individual matrices.  (Unused elements have been initialised to NULL)
  elements_of_current_matrix = A_alphabet_size;
  offie = 0;
  for (i = 0; i <= K; i++) {
    Markov[i] = transition_matrices + offie;
    offie += elements_of_current_matrix;  // Offsets are doubles not bytes.
    elements_of_current_matrix *= E_alphabet_size;  // The number of elements in the matrix for order-i
  }


  // Set up the wd_buf to start with MAX_MARKOV_K default word_start symbols (represented by zeros)
  // Make real_buf point at the part where the real word starts
  memset(wd_buf, 0, MAX_MARKOV_K);
  real_buf = wd_buf + MAX_MARKOV_K;



  VF = fopen(training_tsv, "rb");
  if (VF == NULL) {
    printf("Error: fill_in_term_repn_table_from_tsv(): can't open %s\n",
	   training_tsv);
    exit(1);
  }

  if (0) printf("Starting to process training data\n");

  while (fgets(linebuf, 1000, VF) != NULL) {
    wds_read++;
    if (0) printf("%9lld\n", wds_read);
    trank++;
    p = linebuf;
    while (*p >= ' ') p++;  // Skip to null tab or any ASCII control char
    if (*p != '\t') {
      printf("Error: setup_transition_matrices(): TAB not found in input line %d\n",
	     trank);
      exit(1);
    }

    tabptr = p;
    // Squeeze out non-letters and lower case any uppers
    q = linebuf;
    p = linebuf;
    while (q < tabptr) {
      if (isalpha(*q)) *p++ = tolower(*q);
      q++;
    }
    *p = 0;  // Null terminate the result
    
    l = (int)(p - linebuf);
    if (l == 0) continue;  // May be empty as a result of squeezing
    
    if (l > MAX_TERM_LEN) {
      printf("Unexpectedly long word (%s, length %d) in vocab.tsv at line %llu\n",
	     linebuf, l, wds_read);
      exit(1);
    }

    rankbuk = (int)floor(log10((double)wds_read));
    // rankbuk identifies the logarithmic (base 10) rank bucket.  Rankbuk is used
    // to select the appropriate length distribution and also to drive the 
    // length-specified Markov method.
    if (rankbuk >= NUM_RANK_BUCKETS) {
      printf("rankbuk = %d implies that there are more than a billion words in the vocab.  That's ridiculous! (%lld)\n",
	     rankbuk, wds_read);
      exit(1);
    }
    // Accumulate data necessary to calculate mean and standard deviation
    // for each rank bucket.
    if (0) printf("Accumulating %d\n", rankbuk);
    base_counts[rankbuk]++;
    base_means[rankbuk] += (double)l;
    base_stdevs[rankbuk] += (double)(l * l);
    if (0) printf("Accumulated\n");

        
    if (markov_use_within_vocab_probs) {
      freak = 1;
    } else {
      p = tabptr + 1;
      errno = 0;
      freak = strtod(p, NULL);
      if (errno) {
	printf("Invalid frequency value in %s\n", training_tsv);
	exit(1);
      }
    }
    if (markov_assign_reps_by_rank) {   // Note that the array size is the same in either case
      // lenprob_matrix[i,j] represents the probability that a word in rank bucket i will have length j + 1
      lenprob_matrix[rankbuk * MAX_TERM_LEN + (l - 1)] += freak;
    } else {
      // lenprob_matrix[i,j] represents the probability that a word of length i + 1 will fall in rank bucket j
      lenprob_matrix[(l - 1) * NUM_RANK_BUCKETS + rankbuk] += freak;
    }

    
    total_freak += freak;
    if (0) printf("%.0f -+- %.0f\n", freak, total_freak);
    // ------------------ Deal with the evidence from this word, whose length in bytes is l ----------------

    // Basic letter frequencies
    for (i = 0; i < l; i++) {
      col_index = col_map[linebuf[i]];
      letter_frequencies[col_index] += freak;  
    }
    if (USE_MODIFIED_MARKOV_WITH_END_SYMBOL) {
      // Add the end-of-word symbol
      col_index = col_map[EOW];
      letter_frequencies[col_index] += freak;
    }

    // Now add frequency to each of the transition_matrices.
    strcpy(real_buf, linebuf);  // Have to copy to get the context
    for (k = 1; k <= Markov_order; k++) {
      transition_matrix = Markov[k];
      bp = real_buf - k;  // bp starts by pointing to the k-th start symbol to the left of the word
                          // we're examining.
      for (i = 0; i < l; i++) {  // Loop over each of the k-word contexts.  E.g. if the
                                 // the word is chook and k is three, then the buffer will
                                 // contain ^^^chook, where ^ is a start symbol.  We want
                                 // to record the frequency of c following ^^^, h following
                                 // ^^c, o following ^ch, o following ch, and k following
                                 // cho.  (And maybe EOW following ook) 
	row_index = calculate_row_index(bp + i, k);
	col_index = col_map[bp[i + k]];
	if (col_index < 0 || col_index >= A_alphabet_size) {
	  printf("Markov-K error col_index = %d, char was %d\n", col_index, bp[i + K]);
	  exit(1);
	}
	if (0) printf("  Order %d. i/l = %d/%d:   row %lld, col %d\n",
				       k, i, l, row_index, col_index);
	transition_matrix[row_index + col_index] += freak;
	if (0) printf("Set transition_matrix[%lld + %d] to %.0f\n",
		      row_index, col_index, transition_matrix[row_index + col_index]);
      }
      if (USE_MODIFIED_MARKOV_WITH_END_SYMBOL) {
	if (0) {
	  // Just testing for an erroneous condition which could cause empty words
	  // Note that printable_wd_buf is only needed for this test.
	  for (i = 0; i < BUF_LEN; i++) {
	    if (i > MAX_MARKOV_K && wd_buf[i] == 0) {
	      printable_wd_buf[i] = 0;
	      break;
	    }
	    if (wd_buf[i] == 0) printable_wd_buf[i] = SOW;
	    else printable_wd_buf[i] = wd_buf[i];
	  }
	  if (strlen(printable_wd_buf + MAX_MARKOV_K - K) <= K)
	    printf("Adding freq for EOW after '%s'\n", printable_wd_buf + MAX_MARKOV_K - K);
	}

	// Add in the frequency of transition to EOW
	row_index = calculate_row_index(bp + l, k);  // I'm pretty sure this is safe and sensible
	col_index = col_map[EOW];
	transition_matrix[row_index + col_index] += freak;
				
	if (0) printf("Set Markov-K[%lld + %d] to %.0f (freak was %.0f)\n",
		      row_index, col_index, transition_matrix[row_index + col_index], freak);
      }  // -----------------End of dealing with order k evidence from this word
    }
     // ------------------ End of dealing with evidence from this word ----------------
  }


  printf("%s fully processed: %llu words considered.  Last one was %s\n\n",
	 training_tsv, wds_read, linebuf);

  calculate_word_length_distribution(base_counts, base_means, base_stdevs);

  printf("\n\nCorpus letter frequencies:\n");
  double sum = 0.0, perc, prob, cumprob = 0.0;
  for (i = 0; i < A_alphabet_size; i++)  sum += letter_frequencies[i];
  transition_matrix = Markov[0];
  for (i = 0; i < A_alphabet_size; i++) {  // A_alphabet_size includes EOW if being used
    prob = letter_frequencies[i] / sum;
    cumprob += prob;
    transition_matrix[i] = letter_frequencies[i];
    perc = 100.0 * letter_frequencies[i] / sum;
    printf("'%c' - %.0f (%.2f%%)\n", rev_col_map[i], letter_frequencies[i], perc);
  }
  printf("\n");

  for (i = 0; i < A_alphabet_size; i++) freq_sorted_alphabet[i] = i;
  qsort(freq_sorted_alphabet, A_alphabet_size, sizeof(int), cmp_letter_freqs);
  printf("Letters in descending freq. order: ");
  for (i = 0; i < A_alphabet_size; i++)
    printf("%c ", rev_col_map[freq_sorted_alphabet[i]]);
  printf("\n\n");


  
  convert_transition_matrices_to_probs();

  // Set up a hash table to allow avoidance of repeated generation of the same word
  words_generated = dahash_create((u_char *)"Words generated", HASH_BITS, MAX_TERM_LEN + 1,
				  sizeof(int), (double)0.9, TRUE);
  printf("\nMarkov-%d and below models trained on %.0f word instances\n",
	 Markov_order, total_freak);
  if (markov_assign_reps_by_rank) {
    convert_matrix_rows_to_cumprobs(lenprob_matrix, NUM_RANK_BUCKETS, MAX_TERM_LEN);
    printf("\nRank-bucket-specific length probability matrix set up.\n");
  } else {
    convert_matrix_rows_to_cumprobs(lenprob_matrix, MAX_TERM_LEN, NUM_RANK_BUCKETS);
    printf("\nLength-specific rank bucket probability matrix set up.\n");
  }
  *lp = lenprob_matrix;
 
}

static long long max_tries[MAX_TERM_LEN + 1] = {0};
 
void store_unique_markov_word(u_char *where, u_int term_rank) {
  // Using the pre-built Markov model of order k, generate a random word of 
  // length l.  Buffer must be at least of size l + 1;
  // Currently assuming single byte characters but that's ratty
  int i, j, alphabet_index, l, k = Markov_order, *count, rankbuk;
  long long row_index, tries = 0;
  u_char context[MAX_MARKOV_K] = { 0 }, *bp = where;
  double *row, randy, *background = NULL, *transition_matrix = NULL;

  transition_matrix = Markov[k];
  if (USE_MODIFIED_MARKOV_WITH_END_SYMBOL) {
    l = MAX_TERM_LEN;
  } else {
    // Derive length l of word from term_rank
    rankbuk = (int)(floor(log10((double)term_rank + 1)));
    if (0) printf("Term %d, rankbuk = %d\n", term_rank, rankbuk);
    while (1) {
      l = (int)ceil(rand_normal(base_means[rankbuk]
				* DLM_ADJUSTMENT, base_stdevs[rankbuk])
		    * DLS_ADJUSTMENT);
      if (l > 0) break;  // reject zero and negative lengths.
    }
    if (l > 15) l = 15;
    if (0) printf("Term %d, l = %d\n", term_rank, l);
  }

  // If this is the first time we've been called, initialise the
  // max_tries array.
  if (max_tries[0] == 0) {
    long long t;
    t = 1;
    for (i = 0; i <= MAX_TERM_LEN; i++) {
      max_tries[i] = t;
      if (t <= 10000000000) t *= A_alphabet_size;  // Condition to prevent overflow
    }
  }
  
  if (0) printf("store_unique_markov_word(..., %d, %d)\n", l, k);

  do {  // Need to repeatedly try until we generate a previously unseen word.
    tries++;
    bp = where;
    for (i = 0; i < l; i++) {  // Generate required no. of char.s, one at a time.
      BOOL use_background = FALSE;
      if (k > 0 && markov_lambda > 0 && rand_val(0) < markov_lambda) use_background = TRUE;
      randy = rand_val(0);
      if (!use_background) {
	// Using the order-k model
	row_index = calculate_row_index(context, k);
	row = transition_matrix + row_index;
	if (0) {
	  printf("  Letter %d for k = %d row_index = %lld. Context is: ", i, k, row_index);
	  for (j = 0; j < k; j++) printf("'%c' ", context[j]);
	  printf("\n");
	}
      }  else {   // ----------- Smoothing to background -----------
	if (markov_full_backoff) {
	  // Falling back to order zero
	  background = Markov[0];
	  row_index = 0;
	} else {
	  // Falling back to using the order k -1 model
	  background = Markov[k - 1];
	  row_index = calculate_row_index(context, k - 1);
	}
	row = background + row_index;
	if (0) {
	  printf("  %i-th letter generated from background: for k = %d row_index = %lld. Context is: ",
		 i, k, row_index);
	  for (j = 0; j < k; j++) printf("'%c' ", context[j]);
	  printf("\n");
	}
     }


      // Linear search through the cumulative probabilities in this row to find
      // which letter the random number picked
      if (i == 0 && USE_MODIFIED_MARKOV_WITH_END_SYMBOL) {
	// Make sure we can't generate an EOW symbol in the first position (empty word)
	// by scaling down the random numbers.
	randy *= row[A_alphabet_size -2];  // The cumulative probability of all letters up to EOW
      }
      alphabet_index = -1;  
      for (j = 0; j < A_alphabet_size; j++) {
	if (0)printf("   Row %lld, col %1d: Comparing %.5f with %.5f [realalphsv = %lld]\n",
		     row_index, j, randy, row[j], A_alphabet_size);
	if (randy <= row[j]) {
	  alphabet_index = j;
	  break;
	}
      }

      if (0) printf("Row offsets: Relative to Markov[k] = %zd, Relative to Markov[0] = %zd\n",
		    row - Markov[k], row - Markov[0]);

      if (alphabet_index < 0) {
	printf("\nError:  Unable to assign a letter due to all-zero row, row %lld in Markov[%d]\n",
	       row_index / A_alphabet_size, Markov_order);
	check_transition_matrices();
	exit(1);
      }
      if (0)
	printf("     Alphabet_index = %d, char=%c\n",
	       alphabet_index, rev_col_map[alphabet_index]);
      *bp = (u_char)rev_col_map[alphabet_index];
      if (USE_MODIFIED_MARKOV_WITH_END_SYMBOL && *bp == EOW) {
	if (i > 0) {
	  *bp = 0;
	  break;    // ---------------------------------------->
	}
	else {
	  printf("Generated an EOW symbol as first character.  How can that be?  Leaving the $ and continuing\n");
	  printf("Context was: %s\n", context);
	}
      }


      // Now shift context unless there is none
      if (k > 0) {
	for (j = 1; j < k; j++) context[j - 1] = context[j];  // Shift the k characters of context 1 to the left
	context[k - 1] = *bp;  // Insert the character we just emitted at the right
      }
      if (0) printf("Inserted '%c' at position %d in context.\n", *bp, k - 1);
      bp++;
    }
    *bp = 0;

    if (0) printf("    generated %s\n", where);
    if (*where == 0) {
      printf("Warning: empty word generated but ignored\n");
    } else {    
      // Have we already generated this word?
      count = (int *)dahash_lookup( words_generated, where, 1);
      if (count == NULL) {
	printf("Error inserting into  words_generated hash.\n");
	exit(1);
      }
      if (*count == 0) {
	// It's original
	if (0) printf("  My sin's original: %s\n", where);
	*count = 1;
	tries = 0;
	break;
      }
    }

    if (0) printf("Trying again (%lld/%lld).  %s already used ...\n", tries, max_tries[l], where);
    if (tries > max_tries[l]) {
      if (max_tries[l] > 1) {
	printf("Note:  After %lld/%lld unsuccessful attempts at length %d for term %u will increase length by 1\n",
	       tries, max_tries[l], l, term_rank);
	printf(" ... setting max_tries[%d] to zero\n", l);
      }
      
      max_tries[l] = 0;  // Future attempts at this length are guaranteed to fail too.
 
      l++;
      if (l > MAX_TERM_LEN) {
	printf("Error: term length has increased above %d due to retries.\n", MAX_TERM_LEN);
	exit(1);
      }
     }
    // Have to reset bp and clear context before retrying
    bp = where;
    memset(context, 0, MAX_MARKOV_K);
  } while (1); // End of retry-to-get-unique loop.
}


void decommission_transition_matrices_etc() {
  int k;
  dahash_destroy(&words_generated);
  free(transition_matrices);
  transition_matrices = NULL;
  for (k = 0; k <= MAX_MARKOV_K; k++) Markov[k] = NULL;
  free(letter_frequencies);
  letter_frequencies = NULL;
  free(freq_sorted_alphabet);
  freq_sorted_alphabet = NULL;
}
