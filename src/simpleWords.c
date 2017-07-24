// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.


#include <ctype.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "definitions.h"
#include "simpleWords.h"
 
#define WORD_BUFFER_SIZE 16   

char *simpleWords(char  *buffer, u_ll termNumber) {
  // buffer must be at least of size WORD_BUFFER_SIZE
  termNumber++;
  char *wordPointer = buffer, *EOB = buffer + WORD_BUFFER_SIZE - 1;
  int remainder;
  int i;
  const int alphabet_size = 24;

  int prefixLength = 2;
  int postfixLength1 = 2;
  int postfixLength2 = 2;
  const int prefixDeterminingPrimeNumber = 4;
  const int postfixDeterminingPrimeNumber1 = 2;
  const int postfixDeterminingPrimeNumber2 = 6;
  int primeNumbers[8] = { 2, 3, 5, 7, 11, 13, 17, 19 };
  int primesThatApply[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  int termNumberCopy = (int)termNumber;
  for (i = 0; i < 8; i++) {
    int currentPrimeNumber = primeNumbers[i];
    int remainderOriginal = termNumber % currentPrimeNumber;
    remainder = termNumberCopy % currentPrimeNumber;
    if (remainderOriginal == 0) {
      primesThatApply[i] = 1;
    }
    if (remainder == 0) {
      termNumberCopy /= currentPrimeNumber;
      //primesThatApply[i] += 10;
    }
  }
  if (primesThatApply[prefixDeterminingPrimeNumber] == 1) {
    prefixLength = 0;
  }
  if (prefixLength > 0) {
    wordPointer += prefixLength;
  }
  while (termNumber > 0) {
    remainder = termNumber % alphabet_size;
    *wordPointer++ = (char)(remainder) + 'a';
    termNumber /= alphabet_size;
    if (wordPointer >= EOB) {
      printf("Error: word buffer overflow in simpleWords()\n");
      exit(1);
    }
  }
  if (primesThatApply[postfixDeterminingPrimeNumber1] == 1) {
    if (primesThatApply[postfixDeterminingPrimeNumber2] == 1) {
      postfixLength1 += postfixLength2;
    }
    char *wordPointer2 = buffer + prefixLength;
    *wordPointer++ = 'y';
    postfixLength1--;
    while (postfixLength1-- > 0) {
      if (wordPointer >= EOB) {
	printf("Error: word buffer overflow2 in simpleWords()\n");
	exit(1);
      }
      *wordPointer = *wordPointer2;
      wordPointer++;
      wordPointer2++;
    }
  }
  *wordPointer = 0;
  if (prefixLength > 0) {
    wordPointer = buffer;
    char *wordPointer2 = buffer + prefixLength - 1;
    wordPointer += prefixLength;
    *wordPointer2 = 'z';
    wordPointer2--;
    while (wordPointer2 >= buffer) {
      *wordPointer2 = *wordPointer;
      wordPointer2--;
      if (*(wordPointer + 1) != 0) {
	wordPointer++;
      }
    }
  }

  return buffer;
}
