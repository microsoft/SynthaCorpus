// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// Functions for calculating features of generated words

#include <stdio.h>
#include <stdlib.h>  // for malloc
#include <string.h>  // for memset and memcpy

#include "characterSetHandling/unicode.h"
#include "definitions.h"
#include "utils/general.h"
#include "wordFeatures.h"

int pronouncability(byte *wd) {
  // Return a small integer indicating the degree of pronouncability of a
  // UTF-8 word.  Note that the initial implementation is very limited.
  //
  // Zero indicates unpronounceable
  u_int unicode;
  int score = 0, vowels = 0, consonants = 0;
  byte *p = wd, *q;

  while ((unicode = utf8_getchar(p, &q, FALSE))) {
    if (unicode_isvowel(unicode)) vowels++;
    else {consonants++;}
    p = q;
  }

  if (vowels) {
    score = 1;
    if (consonants && (consonants - vowels) <= 2) score++;
  }
  if (score > MAX_PRONOUNCABILITY_SCORE) score = MAX_PRONOUNCABILITY_SCORE;
  return score;
}


void test_pronouncability() {
  int errors = 0;
  if (pronouncability("dxq") != 0) errors++;
  if (pronouncability("x") != 0) errors++;
  if (pronouncability("A") != 1) errors++;
  if (pronouncability("axe") != 2) errors++;
  if (pronouncability("aardvark") != 2) errors++;
  if (pronouncability("do") != 1) errors++;
  if (pronouncability("odd") != 2) errors++;
}
