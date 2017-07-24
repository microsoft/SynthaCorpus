// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

int generate_fakedoc_len_histo_from_doclenhist();

int generate_fakedoc_len_histo(docnum_t *docs_generated);

void process_fake_records(docnum_t *doccount, u_ll *max_plist_len, size_t *infile_size, u_char *fname_synthetic_docs);

double setup_for_explicit_headterm_percentages();

void set_up_for_term_generation();

void setup_for_piecewise_linear();

void setup_for_dl_piecewise();

double find_alpha(double ox, double vs);




extern int head_terms, mid_segs;

extern midseg_desc_t *mid_seg_defns;

extern dyna_t fakedoc_len_histo;
