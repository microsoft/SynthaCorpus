// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#define DEBUG 0

#define MAX_NGRAM_WORDS 6
typedef unsigned char *doh_t;

extern double ass_thresh;
extern u_ll obs_thresh, stop_after;
extern BOOL use_Zscores;
extern int min_ngram_words, max_ngram_words;
extern double Zscore_criterion;


void recordNgramsFromOneDocument(params_t *params, globals_t *globals, char **docWords,
				 int numWords);

void filterCompoundsHash(params_t *params, globals_t *globals, char **alphabeticPermutation,
			 termType_t termType);

void filterHigherOrderNgrams(params_t *params, globals_t *globals, int N);

byte *makeStringOfTermidsFromCompound(char **alphabeticPermutation, int vocabSize,
				     int *alphaToFreqMapping, u_char *ngram,
				     u_ll ngramFreq, BOOL verbose, termType_t termType);
