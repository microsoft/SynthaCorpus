// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "definitions.h"
#include "utils/general.h"


static  byte **PGLines = NULL, *PGFileInMemory, *oneBeyondEnd, *afterBOM;
static int chapterCount = 0;

static void emitOneChapter(int chapStart, int chapEnd, char *fname, int chapterNumber) {
  char *headerBuf;
  size_t fnameLen = strlen(fname), headerLen;

  headerBuf = (char *) cmalloc(fnameLen + 100, "headerBuf", FALSE);
  sprintf(headerBuf, "%s Chapter %d\n", fname, chapterNumber);
  headerLen = strlen(headerBuf);
  printf(" %zdH %s", headerLen, headerBuf);
  printf(" %zdD ", PGLines[chapEnd] - PGLines[chapStart]);
  put_n_chars(PGLines[chapStart], PGLines[chapEnd] - PGLines[chapStart]);
  chapterCount++;
  free(headerBuf);
}


static void emitChapterByChapter(int endOfHeader, int startOfTrailer, char *fname) {
  // The content of the book starts at line endOfHeader and ends the line before
  // line startOfTrailer.  Chapters are assumed to start with line endOfHeader or
  // with a line starting with Chapter, CHAPTER, or ****
  int l, chapterNumber = 0;
  int chapStart = endOfHeader;

  for (l = endOfHeader; l < startOfTrailer; l++) {
    if (strstr_within_line(PGLines[l], "Chapter") == (char *)PGLines[l]
	|| strstr_within_line(PGLines[l], "CHAPTER") == (char *)PGLines[l]
	|| strstr_within_line(PGLines[l], "****") == (char *)PGLines[l]) {
      if (l > chapStart) {
	chapterNumber++;
	emitOneChapter(chapStart, l, fname, chapterNumber);
	chapStart = l;
      }
    }
  }

  // Output the last chapter
  if (startOfTrailer > (chapStart + 1)) {
    chapterNumber++;
    emitOneChapter(chapStart, l, fname, chapterNumber);
  }
}


void print_usage(char *progname) {
  printf("Usage: %s <Project_Gutenberg_Textfile> ...\n", progname);
  printf("\n - converts a list of PG files into a single STARC file\n"
	 "(Simple Text ARChive).  Each record in STARC format is\n"
	 "preceded by a single space, a length represented as a decimal string, \n"
	 "a letter: H, D, or T indicating the type of record, and a space. The\n"  
	 "spaces are just to aid human readability. The length is a count of the\n"
	 "bytes following the trailing space.\n\n"
	 "By default, Project Gutenberg headers and trailers are not output, and \n"
	 "books are broken up into chapters.  Each chapter results in a STARC\n"
	 "header record (filename and chapter number) and a STARC document record\n"
	 "containing the text of the chapter.  Output is to stdout.\n"
	 "\n"
	 "All files are assumed to be in the UTF-8 character set.\n");
  exit(1);
}


int main(int argc, char **argv) {
  int f, l, numLines, endOfHeader, startOfTrailer;
  HANDLE mh;
  CROSS_PLATFORM_FILE_HANDLE h;
  size_t PGFileSize;
  BOOL outputPGHeaderTrailer = FALSE;
  BOOL breakIntoChapters = TRUE;
  
  test_strstr_within_line();
  
  if (argc < 2) print_usage(argv[0]);

  for (f = 1; f <argc; f++) { // Loop over the input files
    // Make an array of pointers to all the lines in the file
    fprintf(stderr, "File %d: %s\n", f, argv[f]);
    PGLines = load_all_lines_from_textfile(argv[f], &numLines, &h,
                                           &mh, &PGFileInMemory,
                                           &PGFileSize);
    afterBOM = PGFileInMemory;
    if (*PGFileInMemory == 0xEF
	&& *(PGFileInMemory + 1) == 0xBB
	&& *(PGFileInMemory + 2) == 0xBF) {
      afterBOM += 3;
      PGLines[0] += 3;
    }
    
    oneBeyondEnd = PGFileInMemory + PGFileSize;
    endOfHeader = -1;
    startOfTrailer = -1;
    // Find the boundaries of the three sections of the file
    for (l = 0; l <numLines; l++) {
      if (strstr_within_line(PGLines[l], "PROJECT GUTENBERG EBOOK") != NULL) {
	if (strstr_within_line(PGLines[l], "START") != NULL) 
	  endOfHeader = l + 1;
	else if (strstr_within_line(PGLines[l], "END") != NULL)
	  startOfTrailer = l;
      }
    }

    if (endOfHeader == -1 || startOfTrailer == -1) {
      fprintf(stderr,"Error: didn't find header or trailer line in %s. Skipping.\n",
	    argv[f]);
    } else {
      if (outputPGHeaderTrailer) {
	printf(" %zdD ", PGLines[endOfHeader] - afterBOM);
	put_n_chars(afterBOM, PGLines[endOfHeader] - afterBOM);
      }

      if (breakIntoChapters) {
	emitChapterByChapter(endOfHeader, startOfTrailer, argv[f]);
      }  else {
	// Output the entire content part of the file as a single book preceded by
	// a simple filename header.
	size_t fnameLen = strlen(argv[f]);
	printf(" %zdH ", fnameLen +1);
	printf("%s\n", argv[f]);
	printf(" %zdD ", PGLines[startOfTrailer] - PGLines[endOfHeader]);
	put_n_chars(PGLines[endOfHeader], PGLines[startOfTrailer] - PGLines[endOfHeader]);
      }

      
      if (outputPGHeaderTrailer) {
	printf(" %zdD ", oneBeyondEnd - PGLines[startOfTrailer]);
	put_n_chars(PGLines[startOfTrailer], oneBeyondEnd - PGLines[startOfTrailer]);
      }
    }

    unload_all_lines_from_textfile(h, mh, &PGLines, &PGFileInMemory,
                                           PGFileSize);
    
  }
  fprintf(stderr, "Normal exit. Input Files: %d.  Output Chapters: %d\n",
	  argc - 1, chapterCount);
}
