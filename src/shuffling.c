// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>  // For memcpy

#include "definitions.h"
#include "utils/general.h"
#include "utils/randomNumbers.h"
#include "corpusGenerator.h"
#include "shuffling.h"

long long random_long_long(long long min, long long max) {
  long long range = max - min, rezo;
  double r, drange = (double) (range + 1);
  if (max < min || range < 1) {
    printf("Error: invalid range %lld to %lld in random_long_long()\n",
	   min, max);
    exit(1);
  }
  r = rand_val(0);  //  0 <= r < 1, rand_val is a front end to TinyMT64
  rezo = min + floor(r * drange);
  if (0) printf("R_L_L: r = %.6f, drange = %.3f, min = %lld, max = %lld\n",
		r, drange, min, max);
  if (rezo < min || rezo > max) {
    printf("Error: result %lld out of range %lld to %lld in random_long_long()\n",
	   rezo, min, max);
  }
    return rezo;
}


void knuth_shuffle(void *array, size_t elt_size, long long num_elts) {
  // Shuffle an array of elements, where an element can be any 
  // size between 1 and 8 bytes.
  // https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
  if (elt_size > 8 || elt_size < 1) {
    printf("Error: Elt size must be between 1 and 8 in knuth_shuffle(); it was %zd\n",
	   elt_size);
    exit(1);
  }
  byte *bi, *bj, t;
  long long b, i, j, l, m;

  if (0) printf("Knuth_shuffle(elt_size = %zd, num_elts = %lld)\n", elt_size, num_elts);
  
  if (array == NULL || num_elts < 2) {
    printf("knuth_shuffle(): nothing to be done\n");
    return;
  }
  
  l = num_elts - 2;
  m = l + 1;
  for (i = 0;  i < l; i++) {
    // We're going to swap the i-th element with a random element whose index j > i
    j = random_long_long(i + 1, m);
    // Need to turn them into byte addresses because the entries may be of any length
    bi = (byte *)array + (i * elt_size);
    bj = (byte *)array + (j * elt_size);
    // swap byte at a time -  inefficient, particularly for elt_sizes 8 and 4
    if (0) printf("Swapping %lld with %lld. \n", i, j);
    for (b = 1; b <= elt_size; b++) {
      t = *bi;
      *bi = *bj;
      *bj = t;
      bi++;
      bj++;
    }
  }
}


void knuth_shuffle_uint(u_int *array, long long num_elts) {
  // Shuffle an array of elements, where an element is assumed to be a u_int
  // https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
  // For num_elts = 30 million on a high-end laptop, using this function
  // rather than the byte by byte one, reduced runtime of the shuffle from
  // around 4.7 sec to around 3.6 sec.
  long long i, j, l, m;
  u_int t;
  if (0) printf("Knuth_shuffle_uint(num_elts = %lld)\n",num_elts);
  
  if (array == NULL || num_elts < 2) {
    printf("knuth_shuffle(): nothing to be done\n");
    return;
  }
  
  l = num_elts - 2;
  m = l + 1;
  for (i = 0;  i < l; i++) {
    // We're going to swap the i-th element with a random element whose index j > i
    j = random_long_long(i + 1, m);
    t = array[i];
    array[i] = array[j];
    array[j] = t;
  }
}


void knuth_shuffle_uint_respect_phrases(u_int *array, long long num_elts) {
  // Shuffle an array of elements, where an element is assumed to be a u_int
  // https://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle
  //
  // In this version, we recognize that array to be shuffled may contain
  // phrases.  A phrase is a sequence of u_uints in which the first has
  // the SON_FLAG set and the rest have the CON_FLAG set. (SON - Start of Ngram,
  // CON - continuation of Ngram.)
  // 
  long long i, j, k, l, m;
  int gramlen;
  u_int t;
  if (0) printf("Knuth_shuffle_uint_respect_phrases(num_elts = %lld)\n",num_elts);
  
  if (array == NULL || num_elts < 2) {
    printf("knuth_shuffle(): nothing to be done\n");
    return;
  }
  
  l = num_elts - 2;
  m = l + 1;
  for (i = 0;  i < l; i++) {
    // We're going to swap the i-th element with a random element whose index j > i
    // unless the i-th element has the CON_FLAG set.   If it has the SON_flag set,
    // we have to swap a group of n u_ints after first checking whether the n bytes
    // from the j-th either constitute a phrase OR include no part of a phrase.
    if (array[i] & CON_FLAG) continue;  // ----------------->
    if (array[i] & SON_FLAG) {
      gramlen = 1;
      k = i + 1;
      while (array[k] & CON_FLAG) {k++; gramlen++;}
      // We have to swap the current gramlen bytes with gramlen bytes somehwere beyond the
      // end of this ngram (the swapper), but that may not be possible
      k = m - gramlen + 1;  // k is now the last point at which a swappee could start
      if (i + gramlen > k) break;      // ==================>  // Can't swap with part of itself or beyond limit
      if (i + gramlen == k) j = k;
      else j = random_long_long(i + gramlen, k);
      // Now check that none of the words in swappee has a flag set.
      for (k = 0; k < gramlen; k++) {
	if (array[j + k] & NGRAM_FLAGS) {
	  // Not sure whether to retry or to just give up
	  // For the moment, let's give up.
	  gramlen = 0;  // Signal the give-up
	  break;
	}
      }

      if (gramlen) {
	for (k = 0; k < gramlen; k++) {   
	  t = array[i + k];
	  array[i + k] = array[j];   // I think this is OK?? (Meddling with the outer loop index)
	  array[j++] = t;
	}
	i += (gramlen -1);  // Loop will increment it once more.
      }
    } else {
      // This is the normal case, but we must check that array[j] is not
      // part of an n-gram.  If it is, we just skip
      if (i + 1 == m) j = m;
      else j = random_long_long(i + 1, m);
      if (array[j] & NGRAM_FLAGS) continue;  // -------------->
      t = array[i];
      array[i] = array[j];
      array[j] = t;
    }
  }
}



void test_knuth_shuffle_uint_respect_phrases() {
  u_int ta[15] = {1|SON_FLAG, 2|CON_FLAG, 3|SON_FLAG, 4|CON_FLAG, 5|CON_FLAG, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  int i, j, k, gramlen, ngrams_found;

  printf("Test_ksurp_initial: ");
  for (j = 0; j < 15; j++) printf("%x, ", ta[j]);
  printf("\n");
// Shuffle many times and keep checking that the phrases are preserved
  for (i = 0; i < 50; i++) {
    knuth_shuffle_uint_respect_phrases(ta, 15);
    printf("Test_ks_%d: ", i);
    for (j = 0; j < 15; j++) printf("%x, ", ta[j]);
    printf("\n");
    ngrams_found  = 0;
    for (j = 0; j < 15; j++) {
      if (ta[j] & SON_FLAG) {
	ngrams_found++;
	k = j + 1;
	gramlen = 1;
	while (k < 15 && (ta[k] & CON_FLAG)) { k++; gramlen++;}
	if (((ta[j] & TERM_RANK_MASK) == 1) && gramlen != 2) {
	  printf("Error: 2-gram mucked up.\n");
	  exit(1);
	} else if (((ta[j] & TERM_RANK_MASK) == 3) && gramlen != 3) {
	  printf("Error: 3-gram mucked up.\n");
	  exit(1);
	}
      }
    }
    if (ngrams_found != 2) {
      printf("Error: SON_FLAG evaporated.\n");
      exit(1);
    }
  }
  
}






void test_knuth_shuffle() {
  int ta[15] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, i;
  byte tab[75], *bp, b;
  long long ll, ll2;
  knuth_shuffle(ta, 4, 15);
  printf("Test_ks_1: ");
  for (i = 0; i < 15; i++) printf("%d, ", ta[i]);
  printf("\n");

  bp = tab;
  for (i = 0; i < 15; i++) {
    // Represent fifteen integers in an array of 5-byte quantities
    ll = 100000000 + (long long)i;
    for (b = 0; b < 5; b++) {
      *bp++ = (byte)(ll & 0xFF);
      ll >>= 8;
    }
  }
  
  knuth_shuffle(tab, 5, 15);
  printf("Test_ks_2: ");
  
  bp = tab + 4;  // Need to reverse the order of the bytes
  for (i = 0; i < 15; i++) {
    ll = 0;
    for (b = 0; b < 5; b++) {
      ll <<= 8;
      ll2 = *bp--;
      ll |= ll2;
    } 
    bp += 10;
    printf("%lld, ", ll);
  }
  printf("\n");   
}


void light_shuffle(void *array, size_t elt_size, long long num_elts, int max_step) {
  // Shuffle an array of elements, where an element can be any 
  // size up to 1000 bytes.  In this funny version only a random fraction
  // of items are shuffled


  byte *bi, *bj, t;
  long long b, i, j, l, m;
  double start;
  start = what_time_is_it();
  
  if (array == NULL || num_elts < 2) {
    printf("funny_shuffle(): nothing to be done\n");
    return;
  }

  
  l = num_elts - 2;
  m = l + 1;
  if (max_step < 2) i = 0;
  else i = random_long_long(0, max_step - 1);
  while (i < l) {
    //for (i = s;  i < l; i += random_long_long(1, max_step)) {
    // We're going to swap the i-th element with a random element whose index j > i
    j = random_long_long(i + 1, m);
    // Need to turn them into byte addresses because the entries may be of any length
    bi = (byte *)array + (i * elt_size);
    bj = (byte *)array + (j * elt_size);
    // swap byte at a time -  inefficient, particularly for elt_sizes 8 and 4
    if (0) printf("Swapping %lld with %lld. \n", i, j);
    //memcpy(t, bi, elt_size);
    //memcpy(bi, bj, elt_size);
    //memcpy(bj, t, elt_size);
    // swap byte at a time -  inefficient, particularly for elt_sizes 8 and 4
    for (b = 1; b <= elt_size; b++) {
      t = *bi;
      *bi = *bj;
      *bj = t;
      bi++;
      bj++;
    }
    if (max_step < 2) i++;
    else i += random_long_long(1, max_step);
  }
  if (1) printf("Time taken for light shuffle: %.3f sec\n", what_time_is_it() - start);
}


