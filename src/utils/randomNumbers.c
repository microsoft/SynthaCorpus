// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>              // Needed for printf()
#include <stdlib.h>             // Needed for exit() and ato*()
#include <math.h>               // Needed for sqrt()
#include <string.h>             // Needed for strcpy()


#include "../definitions.h"
#include "../imported/TinyMT_cutdown/tinymt64.h"
#include "randomNumbers.h"




void test_random_number_generation(double alpha, double N) {
  double r, expected;
  int buckets[100] = { 0 }, i, bucket, iters = 1000000;
  // First check uniformity of rand_val(0);
  for (i = 0; i < iters; i++) {
    r = rand_val(0) * 100.0;
    bucket = (int)floor(r);
    buckets[bucket]++;
  }

  expected = (double)iters / 100.00;

  printf("test_random_number_generation: 100 buckets, %d trials, expected frequency: %.0f Testing for freq. deviations > 2%%\n", iters, expected);
  for (i = 0; i < 100; i++) {
    double dev = fabs((double)buckets[i] - expected);
    if (dev / expected > 0.02) printf("  Bucket %3d: %d v. %.0f\n", i, buckets[i], expected);
  }
  printf("\n");
}


void setup_linseg_derived_values(midseg_desc_t *linseg) {
  double AFL;

  linseg->ap1 = linseg->alpha + 1.0;  // alpha plus 1
  linseg->rap1 = 1.0 / linseg->ap1;   // reciprocal of alpha + 1
  AFL = (pow(linseg->L, linseg->ap1) - pow(linseg->F, linseg->ap1)) / linseg->ap1;   // AFL is initially the calculated area between F and L
  linseg->area_scale_factor = 1.0 / AFL;  // Now c is the scale up factor to ensure that the random numbers in 0-1 map to areas under the curve between F and L
  linseg->area_toF = linseg->area_scale_factor * pow(linseg->F, linseg->ap1) / linseg->ap1;  // I think it makes sense that this area must also be scaled up.
}


void setup_linseg_full(midseg_desc_t *linseg, double alpha, double F, double L, double probrange, double cumprob) {
  // From slope alpha and term rank range f - l, set up linseg to
  // record those values and all the other fixed constants which will
  // be needed by getZipfianTermRandomNumber()

  // This is the revised maths from Guy Fawkes Day 2015 -- "Guy
  // Fawkes, the only man ever to enter parliament with honest
  // intentions."
  //
  // logic is as follows: Basic function is x ** alpha whose integral
  // is x ** (alpha + 1) / (alpha + 1).  The area under the curve
  // between x = F and x = L is A = (L ** (alpha + 1) - F ** (alpha +
  // 1)) / (alpha + 1) but the area is a sum of all probabilities so
  // we must scale up by c = 1/A to ensure that the total area is 1.0
  //
  // Given a uniform random number r in the range 0 - 1, we treat it
  // as an area, beyond x = F and solve for x Let area_toF be the area
  // under the curve between x = 0 and x = F: area_toF = c * (F **
  // (alpha + 1) / (alpha + 1)) Let r' = r + area_toF; r' = c * ((x **
  // (alpha + 1)) / (alpha + 1)) Thus x ** (alpha + 1) = r' * (alpha +
  // 1) / c; and x = pow(r' * (alpha + 1) / c, 1/(alpha + 1))

  if (0) printf("setup linseg\n");

  linseg->alpha = alpha;
  linseg->F = F;
  linseg->L = L;
  linseg->probrange = probrange;
  linseg->cumprob = cumprob;

  setup_linseg_derived_values(linseg);

}



double rand_val(u_ll seed) {
  // Get a random double r in 0.0 <= r < 1.0.  If seed != 0, initialise the
  // underlying generator first.
  // Calls the cache-friendly Tiny version of the Mersenne Twister. See
  // ../imported/TinyMT_cutdown/tinymt64.[ch]
  //
  // Note that the parameter array values are hard-coded rather than settable
  static tinymt64_t tinymt;
  double rez;
  if (seed != 0) {
    tinymt.mat1 = 0Xfa051f40;
    tinymt.mat2 = 0Xffd0fff4;
    tinymt.tmat = 0X58d02ffeffbfffbcULL;
    tinymt64_init(&tinymt, seed);
    if (0) printf("Initialized.\n");
    return 0.0;
  }

  rez = tinymt64_generate_double(&tinymt);
  return rez;
}


double rand_normal(double mean, double stddev) {
  // Generate a random number from a normal distribution
  // with specified mean and standard deviation.  The
  // method used is Marsaglia's polar method.  See
  // https://en.wikipedia.org/wiki/Marsaglia_polar_method
  // When the loop finishes, we calculate a common expression
  // from which we derive a pair of results.  We return
  // one and save the second for the next call.
  static double r2 = 0.0;
  static BOOL r2_ready = FALSE;
  double ce, r1, result;
  if (!r2_ready) {
    double x, y, s;
    do {
      x = 2.0 * rand_val(0) - 1;
      y = 2.0 * rand_val(0) - 1;
      s = x*x + y*y;
    } while ((s == 0.0) || (s > 1.0));
    ce = sqrt(-2.0 * log(s) / s);
    r1 = x * ce;
    r2 = y * ce;
    result = r1 * stddev + mean;
    r2_ready = TRUE;  // Indicate that r2 is ready for the next call.
    return result;
  } else {
    r2_ready = FALSE;
    return r2 * stddev + mean;
  }
}


static double prev_alpha = -1;

double rand_gamma(double alpha, double lambda) {
  // Gamma(alpha, lambda) generator, where alpha is the shape
  // parameter and lambda is the scale.  using Marsaglia and Tsang
  // (2000) method.
  // @article{MarsagliaTsangGamma2000,
  // author = {Marsaglia, George and Tsang, Wai Wan},
  // title = {A Simple Method for Generating Gamma Variables},
  // journal = {ACM Transactions on Mathematical Software},
  // volume = {26},
  // number = {3},
  // year = {2000},
  // pages = {363--372},
  // url = {http://doi.acm.org/10.1145/358407.358414},
  // }

  double u, v, x;
  static double c, d;
  if (alpha != prev_alpha) {
    // Set up c and d (only needed if alpha changes.
    d = alpha - 1 / 3;
    c = 1.0 / sqrt(9.0 * d);
    prev_alpha = alpha;
  }

  if (alpha >= 1) {
    while (1) {
      do {

	x = rand_normal(0.0, 1.0);   // Should think about using the Ziggurat inline
	                             // method from Marsaglia and Tsang
	v = (1.0 + c * x);
      } while (v <= 0.0);  // According to M & T looping is rare
      v *= (v * v);  // Fastest way to do a cube?
      u = rand_val(0);

      if (u < 1.0 - 0.0331*(x*x)*(x*x)) return d * v * lambda;
      if (log(u) < 0.5 * x * x + d * (1.0 - v + log(v))) return d * v * lambda;
    }  // End of infinite loop;
  }
  else {
    // See the note in Marsagia and Tsang p. 371
    double res;
    res = rand_gamma(alpha + 1, lambda);
    res *= pow(rand_val(0), (1.0  / alpha));
    return res * lambda;
  }
}



void test_rand_gamma() {
  int i, b, trials = 10000000;	
  double alpha, lambda, x, *buckets, unit_weight = 1.0 / (double)trials;
  buckets = (double *)malloc(200 * sizeof(double));
  for (i = 0; i < 200; i++) buckets[i] = 0.0;
  rand_val(53);

  alpha = 5.0;
  lambda = 1.0;
  for (i = 0; i < trials; i++) {
    x = rand_gamma(alpha, lambda);
    b = (int)floor(x * 10.0);
    if (b < 0) b = 0;
    if (b >= 200) b = 199;
    buckets[b] += unit_weight;
  }

  printf("GAMMA(5.0, 1.0) #x y\n");
  for (i = 0; i < 200; i++) {
    printf("GAMMA(5.0, 1.0) %.3f %.5f\n", (double) i / 10.0, buckets[i]);
  }
  free(buckets);
  printf("test_rand_gamma(): %d trials\n", trials);
}


double rand_cumdist(int num_segs, double *cumprobs, double *xvals) {
  // If there were a lot of segments we should implement something
  // like a binary search.  Let's assume that there aren't that many
  // and that the shape of the cumulative distribution is biased
  // toward the early segments which will tend to reduce the number of
  // steps necessary It is assumed that cumprobs[num_segs - 1] = 1.0
  // and that the values are in ascending order;
  double unirand = rand_val(0);  // Uniform in range 0 -1
  double probstep, loprob, frac, xvalstep, loxval, rslt;
  int s;   // s - segment number

  for (s = 0; s < num_segs; s++) {
    if (unirand <= cumprobs[s]) {
      if (s == 0) {
	loprob = 0.0;
	loxval = 1;
      }
      else {
	loprob = cumprobs[s - 1];
	loxval = xvals[s - 1];
      }
      probstep = cumprobs[s] - loprob;  // This is the range of probabilities covered by this segment
      frac = (unirand - loprob) / probstep;
      xvalstep = xvals[s] - loxval;
      rslt = loxval + frac * xvalstep;
      return rslt;
    }
  }
  printf("Error: in rand_cumdist. (Fell off the end.)\n");
  exit(1);
}


void test_rand_cumdist() {
  // Simulate a uniform distributions between lengths 1 and 100 using 10 unequal segments.
  int i, b, trials = 100000000;
  double x, buckets[101], 
    lengths[10] = { 1,2,3,10,20,30,40,80,99,100 },
    cumprobs[10] = {0.01, 0.02, 0.03, 0.10, 0.20, 0.30, 0.40, 0.80, 0.99, 1.00},
    unit_weight = 1.0 / (double)trials;
  for (i = 0; i <= 100; i++) buckets[i] = 0.0;
  rand_val(53);

  for (i = 0; i < trials; i++) {
    x = rand_cumdist(10, cumprobs, lengths);
    b = (int)round(x);
    if (b < 1 || b > 100) {
      printf("Error in test_rand_cumdist() - value %.5f is assigned to out-of-range bucket %d\n", x, b);
      exit(1);
    }
    buckets[b] += unit_weight;
  }

  printf("Rand_cumdist #x y\n");
  for (i = 0; i <= 100; i++) {
    if (fabs(buckets[i] - 0.0100)> 0.005) printf("** ");
    else if (fabs(buckets[i] - 0.0100)> 0.001) printf("*  ");
    else printf("   ");
    printf("CUMDIST %d %.5f\n", i, buckets[i]);
  }
  printf("test_rand_cumdist(): %d trials\n", trials);
}

