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



static void print_usage(char *progname) {
  printf("Usage: %s <STARCFile>\n", progname);
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



static void checkSTARCInMem(byte *fileInMem, size_t fileSize) {
  // Check that the sequence of record lengths makes sense and
  // takes you to end of file, and that all the records are
  // of type H, D, or T.  Print counts of all the records of each
  // type
  byte *actualRecord, *nextRec, *EOFIM = fileInMem + fileSize,
    recType;
  u_ll docCount = 0, printerval = 100, HCount = 0, DCount = 0, TCount = 0, recLen;

  nextRec = fileInMem;
  
  while (nextRec < EOFIM) {
    recLen = getSTARCLengthAndType(nextRec, &recType, &actualRecord);
    if (recType == 'H') HCount++;
    else if (recType == 'D') DCount++;
    else if (recType == 'T') TCount++;
    else {
      printf("Error: Record type '%c' is invalid at offset %zd\n", recType,
	     (actualRecord - fileInMem - 1));
      exit(1);
    }
    
    docCount++;
    if (docCount % printerval == 0) {
      printf("   --- checkSTARCfile: Input record number %10lld ---\n", docCount);
      if (docCount % (printerval * 10) == 0) printerval *= 10;
    }
    nextRec = actualRecord + recLen;
  }
  if (nextRec != EOFIM) {
    printf("Error: Last record in file extends beyond EOF.\n");
    exit(1);
  }

  printf("\nChecks passed: Record counts: H:%llu, D: %llu, T: %llu\n",
	 HCount, DCount, TCount);
}


int main(int argc, char *argv[]) {
  byte *fileInMem;
  size_t fileSize;
  CROSS_PLATFORM_FILE_HANDLE fh;
  HANDLE h;
  int errorCode;
  double startTime;


  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc != 2) print_usage(argv[0]);

  startTime = what_time_is_it();

  fileInMem = (byte *)mmap_all_of((u_char *)argv[1], &fileSize, FALSE, &fh, &h,
				  &errorCode);
  if (fileInMem == NULL) {
    printf("Error:  Failed to mmap %s, error_code was %d\n", argv[1], errorCode);
    exit(1);
  }

  checkSTARCInMem(fileInMem, fileSize);

  unmmap_all_of(fileInMem, fh, h, fileSize);

  printf("Check of %s completed in %.3f sec.\n", argv[1], what_time_is_it() - startTime);
  return 0;
}

