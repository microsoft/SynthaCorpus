// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#ifndef MAX_TERM_LEN
#define MAX_TERM_LEN 15   
#endif

#define NUM_RANK_BUCKETS 9
#define TERM_ENTRY_LEN (MAX_TERM_LEN + 2)    // including terminating null and length byte
#define TERM_LENGTH_INDEX (MAX_TERM_LEN + 1) // length byte is last

typedef struct {
  int next_rank;  // What rank to assign to the next term allocated to this bucket.
  int max_rank;   // if next_rank > max_rank, this bucket is full.
} rank_bucket_entry_t;

extern BOOL markov_model_word_lens;

extern double base_counts[], base_means[], base_stdevs[],
  mimic_counts[], mimic_means[], mimic_stdevs[];

int strlenp(u_char *s);

void calculate_word_length_distribution(double *counts, double *means,
					double *stdevs);

void compare_word_length_distributions(double *base_counts, double *base_means,
				       double *base_stdevs, double *mimic_counts,
				       double *mimic_means, double *mimic_stdevs);

void fill_in_term_repn_table_tnum(termp term_storage, u_int vocab_size,
				 size_t max_term_len);

void fill_in_term_repn_table_base26(termp term_storage, u_int vocab_size,
				 size_t max_term_len);

void fill_in_term_repn_table_bubble_babble(termp term_storage, u_int vocab_size,
					   size_t max_term_len);

void fill_in_term_repn_table_simpleWords(termp term_storage, u_int vocab_size,
				       size_t max_term_len);

void fill_in_term_repn_table_from_tsv(termp term_storage, u_int *vocab_size,
				       size_t max_term_len, u_char *input_vocabfile);

void fill_in_term_repn_table_markov(termp *term_storage, u_int vocab_size,
				    size_t max_term_len, int Markov_order,
				    u_char *input_vocabfile);
