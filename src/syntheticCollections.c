// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#ifdef WIN64
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>  //strlen() etc.
#include <ctype.h>  // isspace() etc.
#include <math.h>

#include "definitions.h"
#include "utils/general.h"
#include "corpusGenerator.h"
//#include "../../Qbasher/src/utils/linked_list.h"
#include "utils/randomNumbers.h"
#include "utils/dynamicArrays.h"
#include "syntheticCollections.h"


// Variables used in piecewise linear model of document lengths
static int num_dl_segs = 0;
double *dl_cumprobs = NULL, *dl_lengths = NULL;
midseg_desc_t *mid_seg_defns = NULL;
int head_terms = 0, mid_segs = 0;


dyna_t fakedoc_len_histo = NULL;   // A dynamic array, see ../../Qbasher/src/utils/dynamic_arrays.

int generate_fakedoc_len_histo_from_doclenhist(docnum_t *num_docs) {
  // Read a document length histogram from a QBASH.doclenhist file and load into the
  // fakedoc_len_histo dynamic array.
  //
  // doclenhist contains a couple of comment lines, then lines with <length>TAB<count>
  // for each length from zero to longest observed.
  // We ignore documents of zero length and create a dynamic array in which the i-th
  // element after the 16 byte header, numbering from 0 is the count of documents of length
  // i + 1.   Note that the dynamic array may have unused elements at the end which are
  // included in the element count in the header -- these unused elements
  // are all set to zero when the DA is created or expanded.
    
  FILE *inhist;
  double total_length = 0, dfreq, scaling_factor, scaled, scaled_total = 0.0;
  char inbuf[1000], *p, *q;
  int len, max_len = 0;
  long long freq, *freqp, *histofreq, totdocs = 0;
  double start;

  start = what_time_is_it();

  printf("Reading document length histogram from %s\n", synth_dl_read_histo);

  inhist = fopen((char *)synth_dl_read_histo, "rb");
  if (inhist == NULL) {
    printf("Can't read %s\n", synth_dl_read_histo);
    exit(1);
  }
  while (fgets(inbuf, 1000, inhist) != NULL) {
    p = inbuf;
    while (*p && isspace(*p)) p++;
    if (*p == '#') continue;  // Ignore comment lines
    len = strtol(p, &q, 10);
    if (len == 0) continue;  // Ignore zero length documents  
    if (len > max_len) max_len = len;
    p = q;
    freq = strtoll(p, &q, 10);
    dfreq = (double)freq;
    total_length += (dfreq * len);
    freqp = (long long *)dyna_get(&fakedoc_len_histo, len - 1, DYNA_DOUBLE);  
    *freqp = freq;  // Store the unscaled number
  }
  fclose(inhist);

  // We may have to scale the number of postings so that the number of postings 
  // requested via -synth_postings is achieved

  scaling_factor = (double)synth_postings / total_length;
  printf("   Maximum length: %d.  Total_postings represented by input histogram: %.0f, c.f.\n"
	 "   %.0f requested.  Scaling factor: %.5f\n",
	 max_len, total_length, synth_postings, scaling_factor);

  histofreq = (long long *)(fakedoc_len_histo + DYNA_HEADER_LEN);
  for (len = 1; len <= max_len; len++) {    
    // Write into element len - 1, so that first elt corresponds to length 1.
    scaled = round((double)histofreq[len - 1] * scaling_factor);
    scaled_total += scaled * len;
    histofreq[len - 1] = (long long)scaled;
    totdocs += (long long)scaled;
  }

  printf("Document length histogram read and scaled: %lld docs, max_length = %d\n", totdocs, max_len);
  printf("Total postings requested: %.0f, achieved: %.0f\n", synth_postings, scaled_total);
  printf("Doc length histogram reading and scaling: elapsed time %.1f sec.\n", what_time_is_it() - start);
  *num_docs = totdocs;
  return max_len;
}


int generate_fakedoc_len_histo(docnum_t *num_docs) {
  // Sets up a histogram of doc lengths in a dynamic array and returns the maximum length
  long long total_length = 0, postings_required = (long long)ceil(synth_postings),
    *freqp, docs_generated = 0;
  int max_len = 0, length;
  double start, rl;
  start = what_time_is_it();

  fakedoc_len_histo = dyna_create(1000, sizeof(long long));   // Start with 1000 lengths
  if (synth_dl_read_histo != NULL) {
    max_len = generate_fakedoc_len_histo_from_doclenhist(num_docs);
    if (1) printf("Returned from GFLHFD.  num_docs = %lld, max_len = %d\n",
		  *num_docs, max_len);
    return max_len;
  }

  do {
    // Note: rand_normal() may lead to a negative or zero length; rand_gamma() may be zero.  Just ignore those
    if (num_dl_segs > 0) {
      // Use piecewise linear model of document lengths
      rl = rand_cumdist(num_dl_segs, dl_cumprobs, dl_lengths);
      length = (int)(ceil(rl));
      if (rl <=  2) printf("GFLH:  Chose length %d, given rl = %.6f\n", length, rl);
    }
    else if (synth_dl_gamma_shape != UNDEFINED_DOUBLE) {
      length = (int)(round(rand_gamma(synth_dl_gamma_shape, synth_dl_gamma_scale)));
    }
    else {
      length = (int)(round(rand_normal(synth_doc_length, synth_doc_length_stdev)));
    }
    // Make sure the length is sensible 
    if (length < 1) continue;
    if (length > MAX_DOC_WORDS) length = MAX_DOC_WORDS;  
    if (length > max_len) max_len = length;
    freqp = (long long *)dyna_get(&fakedoc_len_histo, length - 1, DYNA_DOUBLE);
    (*freqp)++;
    total_length += length;
    docs_generated++;
  } while (total_length < postings_required);
  printf("Document length histogram generated: %lld docs, max_length = %d\n", docs_generated, max_len);
  printf("Doc length histogram generation: elapsed time %.1f sec.\n", what_time_is_it() - start);
  *num_docs = docs_generated;
  return max_len;
}




double setup_for_explicit_headterm_percentages() {
  u_char *p = head_term_percentages, *next;
  double dubl, toil = 0.0;
  int ht;
  head_terms = 1;
  // Count commas to set a maximum on the number of head_terms
  while (*p) if (*p++ == ',') head_terms++;
  // malloc memory to store cumulative probabilities converted from percentages.
  head_term_cumprobs = (double *)malloc(head_terms * sizeof(double));
  if (head_term_cumprobs == NULL) {
    printf("Malloc failure:  for %d head terms.\n", head_terms);
    exit(1);
  }
  // Now read the values and convert to cumulative probabilities
  p = head_term_percentages;
  ht = 0;
  while (1) {
    if (!isdigit(*p) && *p != '.') {
      printf("Error1 in format of head_term_percentages string: '%s'\n", p);
      exit(1);
    }
    dubl = strtod((char *)p, (char **)&next);
    if (p == next) {
      // No conversion happened.
      printf("Error2 in format of head_term_percentages string: '%s'\n", p);
      exit(1);
    }
    toil += dubl / 100.0;  // Convert percentage to fraction and add to cumulative total
    head_term_cumprobs[ht++] = toil;
    if (ht >= head_terms) break;
    p = next;
    if (*p++ != ',') {
      printf("Error3 in format of head_term_percentages string: '%s'\n", p);
      exit(1);
    }
  }

  printf("Head term probabilities explicitly defined: %d.  Proportion of occurrences in head terms = %.3f\n",
	 head_terms, head_term_cumprobs[ht - 1]);
  return (head_term_cumprobs[ht - 1]);
}




void setup_for_piecewise_linear() {
  // The numbers supplied via the -zipf_middle_pieces allow us to do 
  // piecewise linear approximation of the middle section of the TPD.
  // If the middle section consists of terms ranked from r0 to r1, and there are
  // k tuples specified in the zipf_middle_pieces string then we assume 
  // that the range from log(r0) to log(r1) is divided into k segments and that
  // the five comma-separated numbers in each tuple describe the segment.  The 
  // five numbers are:
  //      alpha - slope of the line segment in log-log space
  //          F - rank of first term covered by segment
  //          L - rank of last term covered by segment
  //  probrange - sum of all the term probabilities within this segment
  //    cumprob - the sum of all the term probabilities from rank 1 to rank L
  // Tuples are terminated by '%'
  u_char *p = zipf_middle_pieces, *next;
  double dubl;
  uint32_t ms;
  mid_segs = 0;
  // Count percents to get the number of piecewise segments
  while (*p) if (*p++ == '%') mid_segs++;
  // malloc memory to store cumulative probabilities converted from percentages.
  mid_seg_defns = (midseg_desc_t *)malloc(mid_segs * sizeof(midseg_desc_t));
  if (mid_seg_defns == NULL) {
    printf("Malloc failure:  for %d head terms.\n", mid_segs);
    exit(1);
  }
  // Now read the values and convert to cumulative probabilities
  p = zipf_middle_pieces;
  ms = 0;
  while (1) {	// Read one tuple per iteration of this loop
    // ------------------ Read alpha
    if (!isdigit(*p) && *p != '.' && *p != '-') {
      printf("Error1 in alpha for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    dubl = strtod((char *)p, (char **)&next);
    mid_seg_defns[ms].alpha = dubl;
    if (p == next) {
      // No conversion happened.
      printf("Error2 in alpha for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    p = next;
    if (*p++ != ',') {
      printf("Error3 in alpha for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }

    // ----------------- Read F
    if (!isdigit(*p)) {
      printf("Error1 in F for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    dubl = strtod((char *)p, (char **)&next);
    mid_seg_defns[ms].F = dubl;
    if (p == next) {
      // No conversion happened.
      printf("Error2 in F for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    p = next;
    if (*p++ != ',') {
      printf("Error3 in F for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }

    // ----------------- Read L
    if (!isdigit(*p)) {
      printf("Error1 in L for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    dubl = strtod((char *)p, (char **)&next);
    mid_seg_defns[ms].L = dubl;
    if (p == next) {
      // No conversion happened.
      printf("Error2 in L for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    p = next;
    if (*p++ != ',') {
      printf("Error3 in L for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }

    // ------------------ Read probrange
    if (!isdigit(*p) && *p != '.' && *p != '-') {
      printf("Error1 in probrange for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    dubl = strtod((char *)p, (char **)&next);
    mid_seg_defns[ms].probrange = dubl;
    if (p == next) {
      // No conversion happened.
      printf("Error2 in probrange for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    p = next;
    if (*p++ != ',') {
      printf("Error3 in probrange for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }

    // ------------------ Read cumprob
    if (!isdigit(*p) && *p != '.' && *p != '-') {
      printf("Error1 in cumprob for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    dubl = strtod((char *)p, (char **)&next);
    mid_seg_defns[ms].cumprob = dubl;
    if (p == next) {
      // No conversion happened.
      printf("Error2 in cumprob for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }
    p = next;
    if (*p  && *p != '%') {
      printf("Error3 in cumprob for segment %d of zipf_middle_pieces string: '%s'\n", ms, p);
      exit(1);
    }

    setup_linseg_derived_values(mid_seg_defns + ms);

    //  Move on to the next tuple
    ms++;
    p++;
    if (*p == 0) break;
  }

  printf("Middle segments defined: %d.\n", mid_segs);
}



void setup_for_dl_piecewise() {
  // The numbers supplied via the -synth_dl_segments allow us to do 
  // piecewise linear approximation of the document length distribution.
  // Format should be e.g. "4:1,0.333333;10,0.500000;200,0.6666667;5000,1.000000"
  u_char *p = synth_dl_segments, *q;
  int i;

  // Extract the number of segments
  num_dl_segs = strtol((char*)p, (char **)&q, 10);
  if (num_dl_segs < 2) {
    printf("Error: Must be at least 2 points in -synth_dl_segments arg.\n");
    exit(1);
  }
  p = q;
  if (*p != ':') {
    printf("Error1: Invalid format in -synth_dl_segments arg.\n");
    exit(1);
  }
  p++;

  dl_cumprobs = (double *)malloc(num_dl_segs * sizeof(double));
  dl_lengths = (double *)malloc(num_dl_segs * sizeof(double));
  if (dl_cumprobs == NULL || dl_lengths == NULL) {
    printf("Error: memory allocation failure in processing synth_dl_segments.\n");
    exit(1);
  }
  for (i = 0; i < num_dl_segs; i++) {
    dl_lengths[i] = strtod((char *)p, (char **)&q);
    p = q;
    if (*p != ',') {
      printf("Error2: Invalid format in -synth_dl_segments arg at '%s'\n", p);
      exit(1);
    }
    p++;
    dl_cumprobs[i] = strtod((char *)p, (char **)&q);
    p = q;
    if (i < (num_dl_segs - 1) && *p != ';') {
      printf("Error3: Invalid format in -synth_dl_segments arg at '%s'\n", p);
      exit(1);
    }
    p++;

    if (i > 0) {
      // Check for ascending order
      if (dl_lengths[i] < dl_lengths[i - 1] || dl_cumprobs[i] < dl_cumprobs[i - 1]) {
	printf("Error: Values in synth_dl_segments argument are not in ascending order.\n");
	exit(1);
      }
    }
  }

  if (dl_cumprobs[num_dl_segs - 1] < 1.0) {
    printf("Error: Last cumulative probability in  synth_dl_segments argument must be 1.0 but is %.5f.\n",
	   dl_cumprobs[num_dl_segs - 1]);
    exit(1);
  }

  printf("%d Piecewise segments set up for document length histogram.\n", num_dl_segs);
  for (i = 0; i < num_dl_segs; i++) {
    printf("     %3d %.0f  %.5f\n", i, dl_lengths[i], dl_cumprobs[i]);
  }
}


double find_alpha(double ox, double vs) {
  // Given a corpus with a vocabulary size of vs and a total number of term occurrences of ox,
  // estimate the value of alpha assuming a Zipf law.   I.e freq = c * vs^alpha
  // Integrating we get ox = vs^(alpha + 1) / (alpha + 1), but I can't solve that for
  // alpha so do it iteratively, using a binary chop.   Let q = alpha + 1;
  double hiq, loq, q, alpha, c, diff, estimox;
  int cnt = 0;
  printf("Automatic calculation of Zipf alpha for N = %.0f and |V| = %.0f\n", ox, vs);

  hiq = -0.001;
  loq = -5;
  do {
    if (cnt > 100) {
      printf("\nAutomatic calculation of Zipf alpha failed to terminate.  Taking emergency exit.\n");
      printf("  - Please try again with different values of synth_postings and/or synth_vocab_size.\n\n");
      exit(1);
    }
    q = (hiq + loq) / 2;
    alpha = q - 1;
    // Scale factor so that  last point = 1
    c = -1 / pow(vs, alpha);

    estimox = c * pow(vs, q) / q;
    diff = estimox - ox;
    printf("   Estimated alpha= %.5f  Estimated c= %.5f Estimated N= % .3f Diff= %.3f\n", alpha, c, estimox, diff);
    if (diff < 0) loq = q;
    else if (diff > 0) hiq = q;
    cnt++;
  } while (fabs(diff) > 0.001);
  return alpha;
}



void set_up_for_term_generation() {
  // Set up the necessary parameters for synthetic term generation.  For all of the possible combinations
  // of head_term_percentages defined or not, midlle segments defined or not and tail_percentages zero
  // or non-zero.  If middle segments are not defined, then we need to create a single midseg descriptor
  // and deduce its values to fit in with the head and tail values, specified or set by default.

  double head_term_prob = 0.0;

  // First, explicitly set hashbits so that the hash table never doubles (unless hashbits was explicitly set)
  if (hashbits == 0) {   // 0 is the default value
    double target_hashsize = 1.11 * synth_vocab_size, power_of_2 = 1024;
    hashbits = 10;
    while (power_of_2 <= target_hashsize) {
      hashbits++;
      power_of_2 *= 2;
    }
  }

  if (!tfd_use_base_vocab) {
    if (head_term_percentages != NULL) {
      head_term_prob = setup_for_explicit_headterm_percentages();
    } else head_terms = 0;  // Necessary?

    if (zipf_tail_perc == UNDEFINED_DOUBLE) {
      zipf_tail_perc = 33;
      printf("Set zipf_tail_perc to %.2f. (It wasn't explicitly defined.)\n", zipf_tail_perc);
    }
  
    if (zipf_middle_pieces != NULL) {
      setup_for_piecewise_linear();
    }
    else {
      // Middle pieces are not explicitly defined .. just set up one segment and use properties
      // derived from other options.
      mid_seg_defns = (midseg_desc_t *)malloc(sizeof(midseg_desc_t));
      mid_segs = 1;
      if (mid_seg_defns == NULL) {
	printf("Error: malloc of mid_seg_defns failed\n");
	exit(1);
      }
      if (zipf_alpha == UNDEFINED_DOUBLE) {
	zipf_alpha = find_alpha(synth_postings, synth_vocab_size);
	printf("Set zipf_alpha to %.4f. (It wasn't explicitly defined.)\n", zipf_alpha);
      }
      mid_seg_defns->alpha = zipf_alpha;
      mid_seg_defns->F = head_terms + 1;
      mid_seg_defns->L = synth_vocab_size * (1.0 - (zipf_tail_perc / 100.0));
      mid_seg_defns->cumprob = 1.0 - ((synth_vocab_size * (zipf_tail_perc / 100.0)) / synth_postings);
      mid_seg_defns->probrange = mid_seg_defns->cumprob - head_term_prob;

      printf("Set up for a single middle segment:  alpha= %.4f, F= %.0f, L=%.0f, cumprob= %.4f, probrang %.4f\n",
	     mid_seg_defns->alpha, mid_seg_defns->F, mid_seg_defns->L, mid_seg_defns->cumprob, mid_seg_defns->probrange);
      setup_linseg_derived_values(mid_seg_defns);
    }
  }
  
  if (synth_dl_read_histo != NULL) {
    printf("Document lengths will be read from %s. Freq.s will be scaled if nec.\n",
	   synth_dl_read_histo);
  } else if (synth_dl_segments != NULL) {
    setup_for_dl_piecewise();
  } else {
    if (synth_doc_length_stdev == UNDEFINED_DOUBLE) {
      synth_doc_length_stdev = synth_doc_length / 2.0;
      printf("Set synth_doc_length_stdev to %.4f. (It wasn't explicitly defined.)\n", synth_doc_length_stdev);
    }

    if (synth_dl_gamma_shape == UNDEFINED_DOUBLE)
      printf("Document length generation model is Gaussian: Mean, St. Dev. = %.4f, %.4f\n\n", synth_doc_length, synth_doc_length_stdev);
    else
      printf("Document length generation model is Gamma: Shape, Scale. = %.4f, %.4f\n\n", synth_dl_gamma_shape, synth_dl_gamma_scale);

  }
}



