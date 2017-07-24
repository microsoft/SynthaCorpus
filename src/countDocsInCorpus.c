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
#include "utils/general.h"



static void printUsage(char *progName) {
  printf("Usage: %s <corpusFileName>\n", progName);
  exit(1);
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



static u_ll countSTARCDocs(byte *fileInMem, size_t fileSize) {
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
  // The count returned is a count of the number of documents

  byte *docStart, *actualRecord, *nextRec, *EOFIM = fileInMem + fileSize,
    recType, startRecType;
  u_ll docCount = 0, printerval = 10, recLen;


  // Get the type of the first record, it will determine which records are considered
  // to be the start of a document group.
  docStart = fileInMem;
  recLen = getSTARCLengthAndType(docStart, &startRecType, &actualRecord);
  nextRec = actualRecord + recLen;
  docCount = 1;
 
  while (nextRec < EOFIM) {
    recLen = getSTARCLengthAndType(nextRec, &recType, &actualRecord);
    if (recType == startRecType) {
      // We've found the start of a new document, time to roll the dice
      docCount++;
      if (docCount % printerval == 0) {
	printf("   --- count STARC documents: Input doc %10llu ---\n",
	       docCount);
	if (docCount % (printerval * 10) == 0) printerval *= 10;
      }
      docStart = nextRec;
    }
    nextRec = actualRecord + recLen;
  }
  
  return docCount;
}


static u_ll countLines(byte *fileInMem, size_t fileSize) {
  // This function is used for corpora represented in TSV or other
  // "one document per line" formats
  byte *lineStart, *p;
  u_ll lineCount = 0, printerval = 100, scanned = 0;
  
  lineStart = fileInMem;
  p = lineStart;
  do {
    lineCount++;
    if (lineCount % printerval == 0) {
      printf("   --- Counting lines: Input line %10lld ---\n", lineCount);
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
   lineStart = p;
  } while (scanned < fileSize);

  return lineCount;
}



int main(int argc, char *argv[]) {
  byte *fileInMem;
  size_t fileSize;
  CROSS_PLATFORM_FILE_HANDLE fh;
  HANDLE h;
  int errorCode;
  u_ll recsIn = 0;
  double startTime;


  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc != 2) printUsage(argv[0]);

  startTime = what_time_is_it();

  fileInMem = (byte *)mmap_all_of((u_char *)argv[1], &fileSize, FALSE, &fh, &h,
				  &errorCode);
  if (fileInMem == NULL) {
    printf("Error:  Failed to mmap %s, errorCode was %d\n", argv[1], errorCode);
    exit(1);
  }

  if (tailstr(argv[1], ".starc") != NULL || tailstr(argv[1], ".STARC") != NULL) {
    recsIn = countSTARCDocs(fileInMem, fileSize);
  } else {
    recsIn = countLines(fileInMem, fileSize);
  }

  unmmap_all_of(fileInMem, fh, h, fileSize);
  printf("Documents: %llu\nTime taken: %.3f\n", recsIn, what_time_is_it() - startTime);

  return 0;
}

