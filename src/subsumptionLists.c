// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// This module implements the machinery necessary to quickly find the ngrams which
// are subsumed by a higher-order ngram.  Ngrams are represented by the ngrams array
// which has one row for each ngram.  The number of rows is vl. The zero-th column
// contains the value of n (arity) for this n-gram, then there are N columns containing
// termids (or zero) where N is the maximum allowed arity (value of n).  Finally,
// the last column contains the frequency of occurrence of this ngram.  The ngrams array
// is sorted first by decreasing arity and then by increasing termids.
//
// The first implementation of subsumption did a linear scan of the array from the
// row being considered to the end.  That's essentially n-squared, impractical if there
// are millions of records.  This module provides functions to build an inverted file
// in which the words are termids and the postings lists are implemented as simple
// linked lists.

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

#include "corpusGenerator.h"
#include "subsumptionLists.h"


void slist_append(slist_head_t *slist, int rowno, slist_elt_t *subsumption_memory, int *next_free) {
  int new_item = *next_free;
  (*next_free)++;
  subsumption_memory[new_item].next = -1;
  subsumption_memory[new_item].row = rowno;
  subsumption_memory[slist->tail].next = new_item;
  slist->tail = new_item;
  if (slist->head < 0) slist->head = new_item;
}



static BOOL subsumes(int *super, int *sub) {
  // super and sub are pointers to rows within the ngrams array
  // See whether sub is a subsequence of super. Each of the arrays starts
  // with their arity.
  int sp, ix;
  BOOL mismatch;

  for (sp = 1; sp <= (super[0] - sub[0] + 1); sp++) {  // Check each possible match starting point in super
    mismatch = FALSE;
    for (ix = 1; ix <= sub[0]; ix++) {
      if (sub[ix] != super[sp + ix - 1]) {
	mismatch = TRUE;
	break;
      }
    }
    if (!mismatch) return TRUE;
  }
  return FALSE;
}


static int *arity_first_line = NULL, *arity_count = NULL, sm_next_free;
slist_elt_t *subsumption_memory = NULL;
slist_head_t *vocab = NULL;

void set_up_for_subsumption(int *ngrams, int vl) {
  // ngrams is the sorted (arity, termids) array of vl n-gram descriptors
  // We won't be called if the highest arity is < 3
  // Our job is to set up the arity-first_line and arity_count arrays
  // to describe the iso-arity segments.
  int l, m, n, r, start_row, arity, last_arity = 999, a_count = 0, num_cols = (MAX_DEPEND_ARITY + 2);
  int highest_termid = 0, highest_arity = ngrams[0];
  long long total_postings = 0;

  // Phase 1: determine memory requirements and vocab sizes from the ngrams array.
  // Note that the inverted files we build exclude the rows which have the highest
  // arity.

  arity_first_line = cmalloc((MAX_DEPEND_ARITY + 1) * sizeof(int),
			     "arity first line", FALSE);
  arity_count = cmalloc((MAX_DEPEND_ARITY + 1) * sizeof(int), "arity count",
			FALSE);
  for (l = 0; l < vl; l++) {
    arity = ngrams[l * num_cols];
    if (arity != last_arity) {
      arity_first_line[arity] = l;
      if (a_count > 0) arity_count[last_arity] = a_count;
      a_count = 0;
      last_arity = arity;
    }
    a_count++;

    if (arity < highest_arity) {
      total_postings += arity;
      for (m = 1; m <= arity; m++) {
	if (ngrams[l * num_cols + m] > highest_termid)
	  highest_termid = ngrams[l * num_cols + m];
      }
    }
  }
  if (a_count > 0) arity_count[last_arity] = a_count;

  // Phase 2:  Set up storage.
  vocab = cmalloc((highest_termid + 1) * sizeof(slist_head_t), "subsumption vocab",
		  TRUE);
  for (n = 0; n <= highest_termid; n++) {
    vocab[n].head = -1;
    vocab[n].tail = -1;
  }

  subsumption_memory = cmalloc(total_postings * sizeof(slist_elt_t),
			       "subsumption memory", TRUE);
  sm_next_free = 0;

  // Phase 3: Set up the postings lists.
  start_row = arity_first_line[highest_arity - 1];
  for (r = start_row; r< vl; r++) {
    arity = ngrams[r * num_cols];
    for (m = 1; m <= arity; m++) {
      // Create a posting for each of the arity termids
      if (0) printf("Append for row %d, term %d\n", r, m);
      slist_append(vocab + ngrams[r * num_cols + m], r, subsumption_memory, &sm_next_free);
    }
  }
  if (1) printf("set_up_for_subsumption() finished.\n");
}


void find_all_subsumptions_of_an_ngram(int arity, int *termids, int *ngrams, int line,
					      int vl, dyna_t *ngram_refs, int *num_ngram_refs) {

  // Given an ngram represented by arity and termids, find references to all the n-grams
  // it subsumes and return them in the dynamic array.  We do this by intersecting the
  // slists for the termids.  Should be far faster than the previous linear scan.
  //
  
  int verbose = 0, ngr = 0, subarity, num_cols = (MAX_DEPEND_ARITY + 2);
  int a, termid;
  int slist_curpos[MAX_DEPEND_ARITY] = {0};  // Pointers to the current position in each list
  BOOL exhausted[MAX_DEPEND_ARITY];
  int sle, highest, oldhighest, count;

  // If necessary, set up iso-arity descriptors
  *num_ngram_refs = 0;
  if (arity == 2) return;  // Bigrams can't subsume anything but unigrams (shouldn't be called)
  if (arity_count == NULL) set_up_for_subsumption(ngrams, vl);

  // Initialise the slist_curpos and exhausted arrays.
  for (a = 0; a < arity; a++) {
    termid = termids[a];
    sle = vocab[termid].head;
    if (sle == -1) exhausted[a] = TRUE;
    else {
      exhausted[a] = FALSE;
      slist_curpos[a] = sle;
    }
  }

  // find the highest row number of the terms
  highest = -1;
  for (a = 0; a < arity; a++) {
    if (!exhausted[a]) {
      if (subsumption_memory[slist_curpos[a]].row > highest)
	highest = subsumption_memory[slist_curpos[a]].row;
    }
  }
  
  // Now do the intersections
  while (1) {
    // Try to advance the lists to highest.
    count = 0;
    for (a = 0; a < arity; a++) {
      while (!exhausted[a] && subsumption_memory[slist_curpos[a]].row < highest) {
	if (subsumption_memory[slist_curpos[a]].next == -1) exhausted[a] = TRUE;
	else slist_curpos[a] = subsumption_memory[slist_curpos[a]].next;
      }
      if (!exhausted[a] && subsumption_memory[slist_curpos[a]].row == highest) count++;
    }
    if (1 && highest == 1200676) verbose = TRUE;

    if (count >= 2) {
      // We only get here if at least two of the component terms intersect.  Check for actual subsumption
      if (verbose) printf("  Found an intersection on row %d\n", highest); 
      if (subsumes(termids - 1, ngrams + highest * num_cols)) {  // Each array starts with its arity
	if (verbose) {
	  int y;
	  subarity = ngrams[highest * num_cols];
	  for (y = 0; y < arity; y++) printf("  %7d", termids[y]);
	  printf(" subsumes");
	  for (y = 1; y <= subarity; y++) printf("  %7d", ngrams[highest * num_cols + y]);
	  printf("\n");
	}
	dyna_store(ngram_refs, ngr++, &highest, sizeof(int), DYNA_DOUBLE);  // Storing line num.
      } else {
	if (0 && verbose) {
	  int y;
	  subarity = ngrams[highest * num_cols];
	  for (y = 0; y < arity; y++) printf("  %7d", termids[y]);
	  printf(" DOES NOT SUBSUME ");
	  for (y = 1; y <= subarity; y++) printf("  %7d", ngrams[highest * num_cols + y]);
	  printf("\n");
	}
      }
    }
 
    // Now advance all the slists which aren't exhausted and which are set to the oldhighest,
    // while choosing a new highest
    count = 0;
    oldhighest = highest;
    for (a = 0; a < arity; a++) {
      if (!exhausted[a]) {
	count++;
	while (subsumption_memory[slist_curpos[a]].row == oldhighest) {
	  if (subsumption_memory[slist_curpos[a]].next == -1) {
	    exhausted[a] = TRUE;
	    count--;
	    break;
	  } else {
	    slist_curpos[a] = subsumption_memory[slist_curpos[a]].next;
	  }
	}
	if (subsumption_memory[slist_curpos[a]].row > highest) 
	  highest = subsumption_memory[slist_curpos[a]].row;
      }
    }
    if (0) printf("  Highest = %d, oldhighest = %d, count = %d\n", highest, oldhighest, count);
    if (count < 2) break;
  }

  if (ngr >= arity) {
    int a;
    printf("Suspicious number %d of subsumptions found for:", ngr);
    for (a = 0; a < arity; a++) printf(" %d", termids[a]);
    printf(". Rows were:\n");
    for (a = 0; a < ngr; a++) printf("%d ", *((int *)dyna_get(ngram_refs, a, DYNA_DOUBLE)));
    printf("\n");
					      
    exit(1);
  }
  *num_ngram_refs = ngr;
}


