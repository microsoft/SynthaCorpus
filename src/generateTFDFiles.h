// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

//

typedef enum {  // Make sure that the fileTypes[] array has an element for each of these.
	WORDS,
	BIGRAMS,
	NGRAMS,
	COOCCURS,
	TERM_REPS
} termType_t;



void generateTFDFiles(params_t *params, globals_t *globals, dahash_table_t **htp,
		      termType_t termType);

void writeTSVAndTermidsFiles(params_t *params, globals_t *globals, char **alphabeticPermutation,
			     int *alphaToFreqMapping, termType_t termType);

