// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

extern long long random_long_long(long long min, long long max);

extern void knuth_shuffle(void *array, size_t elt_size, long long num_elts);

extern void knuth_shuffle_uint(u_int *array, long long num_elts);

extern void knuth_shuffle_uint_respect_phrases(u_int *array, long long num_elts);

extern void light_shuffle(void *array, size_t elt_size, long long num_elts, int max_step);

extern void test_knuth_shuffle();

extern void test_knuth_shuffle_uint_respect_phrases();
