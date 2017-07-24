// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "definitions.h"
#include "utils/general.h"
#include "bubbleBabble.h"


#define K 4   // Length of integers to be encoded, in bytes
#define INCLUDE_FILLERS 0

static u_char Vtable[] = "aeiouy", Ctable[] = "bcdfghklmnprstvzx";  //  Note that j,q and w are missing from the consonants and x and z are swapped

u_char *bubble_babble(int k) {
  // Algorithm from http://web.mit.edu/kenta/www/one/bubblebabble/spec/jrtrjwzi/draft-huima-01.txt
  int C[4] = { 0 }, T1[5], //T2[5],
    P[3], bi;
  u_char *D = (u_char *) &k;   // Going to depend on endianness
  static u_char buffer[18];


  if (0) printf("bubble_babble(%d) - ", k);
  C[1] = 1;
  D--;  //Numbering starts at 1
  C[2] = (C[1] * 5 + (D[1] * 7 + D[2])) % 36;
  C[3] = (C[2] * 5 + (D[3] * 7 + D[4])) % 36;
	
  T1[0] = (((D[1] >> 6) & 3) + C[1]) % 6;
  T1[1] = (D[1] >> 2) & 15;
  T1[2] = (((D[1] & 3))) % 6;
  T1[3] = (D[2] >> 4) & 15;
  T1[4] = (D[2]) & 15;

  //T2[0] = (((D[3] >> 6) & 3) + C[1]) % 6;
  //T2[1] = (D[3] >> 2) & 15;
  //T2[2] = (((D[3] & 3))) % 6;
  //T2[3] = (D[4] >> 4) & 15;
  //T2[4] = (D[4]) & 15;

  // K is even
  P[0] = C[3] % 6;
  P[1] = (D[4] >> 2) & 15;

  P[2] = ((D[4] & 3) + C[3] / 6) % 6;
	
  bi = 0;

  if (INCLUDE_FILLERS) buffer[bi++] = 'x';
  buffer[bi++] = Vtable[T1[0]];
  buffer[bi++] = Ctable[T1[1]];
  buffer[bi++] = Vtable[T1[2]];
  buffer[bi++] = Ctable[T1[3]];
  if (INCLUDE_FILLERS) buffer[bi++] = '-';
  buffer[bi++] = Ctable[T1[3]];
  buffer[bi++] = Vtable[T1[0]];
  buffer[bi++] = Ctable[T1[1]];
  buffer[bi++] = Vtable[T1[2]];
  buffer[bi++] = Ctable[T1[3]];
  if (INCLUDE_FILLERS) buffer[bi++] = '-';
  buffer[bi++] = Ctable[T1[3]];
  buffer[bi++] = Vtable[P[0]];
  buffer[bi++] = Ctable[P[1]];
  buffer[bi++] = Vtable[P[2]];
  if (INCLUDE_FILLERS) buffer[bi++] = 'x';
  buffer[bi] = 0;

  if (0) printf("%s\n", buffer);
  return buffer;
}
