// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#ifdef WIN64
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#endif

#include "definitions.h"
#include "imported/TinyMT_cutdown/tinymt64.h"
#include "utils/general.h"


#define BUFSIZE (50 * 1048576)   // X * MB
static byte *writeBuf;
static size_t bytesInWriteBuf = 0;
static BOOL randomSelection;
static u_ll headCount;

static void printUsage(char *progname) {
  printf("Usage: %s <infile> <outfile> (random <proportion_to_select> |head <number_of_lines>)\n"
	 "  In random mode, a die is rolled for each document to decide whether it is selected.\n"
	 "  In head mode, the specified number of records at the head of the file are selected\n"
	 "  (if possible).\n\n",
	 progname);
  exit(1);
}


static double randVal(u_ll seed) {
  // Returns a random number in the range 0 to 1.
  // If seed is non-zero, initialises the generator.
  static tinymt64_t tinymt;
  double rez;
  //static uint64_t seed_array[5];
  // Set the seed if argument is non-zero and then return zero
  if (seed != 0) {
    tinymt.mat1 = 0Xfa051f40;
    tinymt.mat2 = 0Xffd0fff4;
    tinymt.tmat = 0X58d02ffeffbfffbcULL;
    //seed_array[0] = seed;
    //tinymt64_init_by_array(&tinymt, seed_array, 1);
    tinymt64_init(&tinymt, seed);
    if (0) printf("Initialized.\n");
    return 0.0;
  }

  // Use the function which generates doubles in the range 0.0 <= r < 1.0
  rez = tinymt64_generate_double(&tinymt);
  if (0) printf("Returning %.8f\n", rez);
  return rez;
}

static u_ll getSTARCLengthAndType(byte *str, byte *recType, byte **startOfRecord) {
  
  u_ll rslt;
  byte *oneAfter = NULL;
  errno = 0;
  rslt = strtoll((char *)str, (char **)&oneAfter, 10);
  if (errno) {
    printf("Error in reading STARC file record introducer\n");
    *recType = 0;
    *startOfRecord = str;
    exit(1);
  }
  *recType = *oneAfter;
  *startOfRecord = oneAfter + 2;  // Skip the record type marker plus the space
  return rslt;
}



static u_ll selectFromSTARC(byte *fileInMem, size_t fileSize, double proportion,
			CROSS_PLATFORM_FILE_HANDLE out, u_ll *docsIn) {
  // This function is used for corpora represented in STARC (Simple Text ARChive)
  // format.  There are three types of STARC record: H - Header, D - Document,
  // and T - Trailer.  This function assumes the following:
  //  1. There is a D record for every document in the file
  //  2. If a document has a header, the D record is immediately preceded by
  //     the corresponding H record.
  //  3. If a document has a trailer, the D record is immediately followed by
  //     the corresponding T record.
  //  4. If any document has a header, all documents do
  //  5. If any document has a trailer, all documents do
  //  6. The file contains no H or T records which are not associated with
  //     a D record.
  //
  // When a document is randomly selected for output, all its records are
  // emitted.
  //

  byte *docStart, *actualRecord, *nextRec, *EOFIM = fileInMem + fileSize,
    recType, startRecType;
  u_ll docCount = 0, printerval = 10, docsOut = 0, recLen;
  double r;


  // Get the type of the first record, it will determine which records are considered
  // to be the start of a document group.
  docStart = fileInMem;
  recLen = getSTARCLengthAndType(docStart, &startRecType, &actualRecord);
  nextRec = actualRecord + recLen;
 
  while (nextRec < EOFIM) {
    recLen = getSTARCLengthAndType(nextRec, &recType, &actualRecord);
    if (recType == startRecType) {
      // We've found the start of a new document, time to roll the dice
      docCount++;
      if (docCount % printerval == 0) {
	printf("   --- Select_random_documents: Input doc %10llu (Output: %llu)---\n",
	       docCount, docsOut);
	if (docCount % (printerval * 10) == 0) printerval *= 10;
      }
      if (randomSelection) {
	r = randVal(0);
	if (r <= proportion) {
	  // Output the previous document and update docStart
	  buffered_write(out, &writeBuf, BUFSIZE, &bytesInWriteBuf,
			 docStart, nextRec - docStart, "STARC write");
	  docsOut++;
	}
      } else {
	buffered_write(out, &writeBuf, BUFSIZE, &bytesInWriteBuf,
		       docStart, nextRec - docStart, "STARC write");
	docsOut++;
	if (docsOut >= headCount) {
	  *docsIn = docCount;
	  return docsOut;    //  ----------------------->
	}
      }

      docStart = nextRec;
    }
    nextRec = actualRecord + recLen;
  }

  if (randomSelection) {
    // Roll the dice for the last document.
    r = randVal(0);
    if (r <= proportion) {
      // Output the previous document and update docStart
      buffered_write(out, &writeBuf, BUFSIZE, &bytesInWriteBuf,
		     docStart, nextRec - docStart, "STARC write");
      docsOut++;
    }
  } else {
    buffered_write(out, &writeBuf, BUFSIZE, &bytesInWriteBuf,
		   docStart, nextRec - docStart, "STARC write");
    docsOut++;
    if (docsOut > headCount) {
      *docsIn = docCount;
      return docsOut;    //  ----------------------->
    }
  }

  *docsIn = docCount;
  return docsOut;
}


static u_ll selectLines(byte *fileInMem, size_t fileSize, double proportion,
			CROSS_PLATFORM_FILE_HANDLE out, u_ll *linesIn) {
  // This function is used for corpora represented in TSV or other
  // "one document per line" formats
  byte *lineStart, *p;
  u_ll lineCount = 0, printerval = 100, scanned = 0, linesOut = 0;
  double r;
  
  lineStart = fileInMem;
  p = lineStart;
  do {
    lineCount++;
    if (lineCount % printerval == 0) {
      printf("   --- Select_random_lines: Input line %10lld ---\n", lineCount);
      if (lineCount % (printerval * 10) == 0) printerval *= 10;
    }

    // Now skip to the next linefeed
    while (scanned < fileSize && *p != '\n') {
      p++;
      scanned++;
    }

    // Line extends from lineStart to p, inclusive
    p++;
    scanned++;
    if (randomSelection) {
      // Roll the dice
      r = randVal(0);
      if (r <= proportion) {

	buffered_write(out, &writeBuf, BUFSIZE, &bytesInWriteBuf, lineStart, p - lineStart, "randywrite");
	linesOut++;
      }
    } else {
      buffered_write(out, &writeBuf, BUFSIZE, &bytesInWriteBuf, lineStart, p - lineStart, "randywrite");
      linesOut++;
      if (linesOut >= headCount) break;
    }
    lineStart = p;
  } while (scanned < fileSize);

  *linesIn = lineCount;
  return linesOut;
}



int main(int argc, char *argv[]) {
  byte *fileInMem;
  size_t fileSize;
  CROSS_PLATFORM_FILE_HANDLE fh, out;
  HANDLE h;
  int error_code;
  u_ll randseed, recsIn = 0, recsOut = 0;
  double startTime;
  double proportion = 1.0, actualProportion;
  char *nxt;


  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc != 5) printUsage(argv[0]);

  
  out = open_w(argv[2], &error_code);
  if (error_code < 0) {
    printf("Error: Failed to open %s for writing\n", argv[2]);
    exit(1);
  }
  if (0) printf("File %s open for writing....\n", argv[2]);

  if (!strcmp(argv[3], "random") || !strcmp(argv[3], "RANDOM")) {
    randomSelection = TRUE;
    proportion = strtod(argv[4], &nxt);
    if (proportion > 1.0 || proportion < 0) {
      printf("Error: Proportion %s should have been a decimal fraction between 0 and 1 inclusive\n",
	     argv[4]);
      exit(1);
    }    
  } else if (!strcmp(argv[3], "head") || !strcmp(argv[3], "HEAD")) {
    randomSelection = FALSE;
    errno = 0;
    headCount = strtoull(argv[4], NULL, 10);
    if (errno) {
      printf("Error: Problem with format of specified number of documents: %s\n", argv[4]);
      exit(1);
    }
  } else {
    printf("Error: selection method must be either 'random' or 'head'\n");
    printUsage(argv[0]);
  }



  startTime = what_time_is_it();

  randseed = (u_ll)fmod(startTime, 100000.0);
  randVal(randseed);

  fileInMem = (byte *)mmap_all_of((u_char *)argv[1], &fileSize, FALSE, &fh, &h,
				  &error_code);
  if (fileInMem == NULL) {
    printf("Error:  Failed to mmap %s, error_code was %d\n", argv[1], error_code);
    exit(1);
  }

  if (tailstr(argv[1], ".starc") != NULL || tailstr(argv[1], ".STARC") != NULL) {
    recsOut = selectFromSTARC(fileInMem, fileSize, proportion, out, &recsIn);
  } else {
    recsOut = selectLines(fileInMem, fileSize, proportion, out, &recsIn);
  }

  unmmap_all_of(fileInMem, fh, h, fileSize);
  actualProportion = (double)recsOut / (double)recsIn;
  buffered_flush(out, &writeBuf, &bytesInWriteBuf, "randywrite", TRUE);

  if (randomSelection) 
    printf("SelectRandomRecords: %llu / %llu lines output. %.4f v. %.4f requested. ",
	   recsOut, recsIn, actualProportion, proportion);
  else
    printf("SelectHeadRecords: %llu lines output. ", recsOut);
  
  printf(" Time taken: %.2f sec\n", what_time_is_it() - startTime);

  return 0;
  }

