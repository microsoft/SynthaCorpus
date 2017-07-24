// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

void setup_transition_matrices(int k, u_char *training_tsv, double **lp);

void decommission_transition_matrices_etc();

void store_unique_markov_word(u_char *where, u_int term_rank);


extern BOOL USE_MODIFIED_MARKOV_WITH_END_SYMBOL;
extern BOOL markov_use_within_vocab_probs; 
extern BOOL markov_full_backoff;
extern BOOL markov_assign_reps_by_rank;
extern BOOL markov_favour_pronouncable;
