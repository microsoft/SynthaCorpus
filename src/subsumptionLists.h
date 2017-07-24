// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#define BLAH 1

typedef struct {
  // indexes within the subsumption_memory array of head and tail of
  // a list.
  int head, tail;
} slist_head_t;

typedef struct {
  // Row is the row number within the ngrams array (the payload)
  // Next is a pointer (index within subsumbtion_memory array) to the next element
  // of the list, or -1
  int row, next;
} slist_elt_t;


void set_up_for_subsumption(int *ngrams, int vl);

void find_all_subsumptions_of_an_ngram(int arity, int *termids, int *ngrams, int line,
				       int vl, dyna_t *ngram_refs, int *num_ngram_refs); 

