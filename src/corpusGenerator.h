// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

extern int debug;
extern u_ll rand_seed;
extern double synth_postings, synth_vocab_size,
  synth_doc_length, synth_doc_length_stdev,
  synth_dl_gamma_shape, synth_dl_gamma_scale,
  zipf_alpha, zipf_tail_perc, markov_lambda;
extern u_char *synth_term_repn_method, 
  *fname_synthetic_docs, *head_term_percentages,
  *head_term_percentages, *synth_dl_segments,
  *zipf_middle_pieces, *synth_dl_read_histo,
  *synth_input_vocab, *synth_input_ngrams;

extern BOOL tfd_use_base_vocab, include_docnums;

//---
extern double *head_term_cumprobs;
extern int hashbits;

typedef u_char byte;
typedef long long docnum_t;
typedef int BOOL;
typedef byte *threebytep;
typedef byte *termp;

#define FINAL_POSTING_IN_DOC 0x80000000  // Most significant bit to signal End-of-Doc
#define MASK_ALL_BUT_FINAL_POSTING_FLAG 0x7FFFFFFF
// Next two flags facilitate retaining n-gram during within-document shuffling.
#define SON_FLAG 0x40000000              // Next most sig bit signals start-of-n-gram
#define CON_FLAG 0x20000000              // Next most sig bit signals continuation-of-n-gram
#define NGRAM_FLAGS 0x60000000           // Either CON or SON
#define TERM_RANK_MASK 0x1FFFFFFF        // Sets a limit of 2^29 (approx 500 million) on vocab_size

typedef u_ll doctable_entry_t;
#define doctable_pointer_mask     0xFFFFFFFFFF000000 // Allow 5 bytes for u_int * pointer  (1 trillion occurrences)
#define doctable_pointer_mask2    0xFFFFFFFFFF       // Allow 5 bytes for u_int * pointer  (1 trillion occurrences)
#define doctable_slots_avail_mask 0xFFFFFF           // Allow 3 bytes for remaining doc length (up to 16M words per doc)
#define doctable_pointer_shift    24

#define MAX_RANDOM_RETRIES 5000
#define MAX_DEPEND_LINE_LEN 1000
#define MAX_DEPEND_ARITY 6

