// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <inttypes.h>           // Needed for u_ll

#define UNDEFINED_DOUBLE  999999999999.9
#define WORD_BUFFER_SIZE 100

// The midseg_desc_t struct is a descriptor for the (assumed) linear segment of the term probability distribution
// from the term at rank f to the term at rank l (inclusive).   An array of such descriptors supports piecewise
// linear modeling.

typedef struct{
	double cumprob;  // The cumulative term probability to the end of this segment of the middle
	double probrange;  // The range of probabilitities covered by this sgement
	double F, L;  // The ranks of the first and last terms in this segment
	double alpha;  // The slope of the probability line connecting terms f and l.
	// The following derived variables are computed once at initialisation
	double area_scale_factor, ap1, rap1, area_toF;
} midseg_desc_t;

void setup_linseg_derived_values(midseg_desc_t *linseg);
void setup_linseg_full(midseg_desc_t *linseg, double alpha, double F, double L, double probrange, double cumprob);

double rand_val(u_ll seed);
double rand_normal(double, double);
double rand_gamma(double alpha, double lambda);
double rand_cumdist(int num_segs, double *cumprobs, double *lengths);
void test_rand_gamma();
void test_rand_cumdist();
