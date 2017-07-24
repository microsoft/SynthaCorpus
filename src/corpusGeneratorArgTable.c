// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Table of command line argument definitions for the corpus
// property extractor.  The functions in argParser.c operate
// on this array.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "definitions.h"
#include "utils/dahash.h"
#include "utils/argParser.h"
#include "corpusGenerator.h"
#include "MarkovWords.h"
#include "termRepMethods.h"

arg_t args[] = {
        { "debug", AINT, (void *)&debug, "Activate debugging output.  0 - none, 1 - low, 4 - highest. (Not implemented.)" },
        { "rand_seed", AINT, (void *)&rand_seed, "If non-zero will allow for reproducible output.  Otherwise seed is based on time." },
        { "synth_postings", AFLOAT, (void *)&synth_postings, "The number of random word occurrences to generate." },
        { "synth_vocab_size", AFLOAT, (void *)&synth_vocab_size, "The number of distinct random words to generate." },
        { "synth_doc_length", AFLOAT, (void *)&synth_doc_length, "The average length of generated document.  Gaussian distributed."  },
        { "synth_doc_length_stdev", AFLOAT, (void *)&synth_doc_length_stdev, "The standard dev. of the dist. of document lengths. Defaults to half the mean."},
        { "synth_dl_gamma_shape", AFLOAT, (void *)&synth_dl_gamma_shape, "Shape param for a gamma distribution - a better way of modelling document lengths." },
        { "synth_dl_gamma_scale", AFLOAT, (void *)&synth_dl_gamma_scale, "Scale param for the gamma distribution of document lengths." },
        { "tfd_use_base_vocab", ABOOL, (void *)&tfd_use_base_vocab, "If TRUE, use exact term frequencies from synth_input_vocab" },
        { "zipf_alpha", AFLOAT, (void *)&zipf_alpha, "Parameter used in generating Zipf synthetic collections. Usually between -0.1 and -2.0 but NOT -1.0." },
        { "zipf_tail_perc", AFLOAT, (void *)&zipf_tail_perc, "The desired percentage of terms in a Zipf simulation which occur only once." },
        { "file_synth_docs", ASTRING, (void *)&fname_synthetic_docs, "The name of file to contain output from synthetic collection generation." },
        { "head_term_percentages", ASTRING, (void *)&head_term_percentages, "A comma sep.d list of %s of total term instances made up by an arbitrary no. of head terms. Descending order, please." },
        { "zipf_middle_pieces", ASTRING, (void *)&zipf_middle_pieces, "A list of %s line segment tuples for the middle segment of the \"Zipf\" curve. Desc. order." },
        { "synth_dl_segments", ASTRING, (void *)&synth_dl_segments, "A list of line segment points for the doclength model. Asc. order." },
        { "synth_dl_read_histo", ASTRING, (void *)&synth_dl_read_histo, "File path to .doclenhist file used as input"},
        { "synth_input_vocab", ASTRING, (void *)&synth_input_vocab, "File path to vocab.tsv file used as input in generating term repns"},
        { "synth_input_ngrams", ASTRING, (void *)&synth_input_ngrams, "File path to ngrams.termids file containing n-grams represented as termid tuples. Currently bigrams only."},
        { "synth_term_repn_method", ASTRING, (void *)&synth_term_repn_method, "Method to use when generating term representations.  E.g. markov-2e, base26"},
        { "markov_lambda", AFLOAT, (void *)&markov_lambda, "Markov smoothing parameter. The probability that the nextletter will be generated from the background model (unigram)" },
        { "markov_use_vocab_probs", ABOOL, (void *)&markov_use_within_vocab_probs, "How transition prob.s are modeled.  TRUE - from vocab, FALSE - from corpus." },
        { "markov_model_word_lens", ABOOL, (void *)&markov_model_word_lens, "Attempt to shuffle words to achieve correlation of length with rank." },
        { "markov_full_backoff", ABOOL, (void *)&markov_full_backoff, "Whenever falling back to a background model, drop back to order zero." },
        { "markov_assign_reps_by_rank", ABOOL, (void *)&markov_assign_reps_by_rank, "When matching Markov term representations to ranks, do it in rank rather than length order" },
        { "markov_favour_pronouncable", ABOOL, (void *)&markov_favour_pronouncable, "When sorting Markov term representations by length, add a penalty for unpronouncability" },
        { "include_docnums", ABOOL, (void *)&include_docnums, "When writing synthetic docs, include a third column with Doc1234 where 1234 is the number of the document, starting from 0." },
        { "", AEOL, NULL, "" }
};




