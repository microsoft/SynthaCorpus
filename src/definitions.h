// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.



typedef enum {   // Types of output allowed by the arg_parser (both qbashi and qbashq)
        TEXT,
        TSV,
        HTML
} format_t;



//----------------------------------------------------------------------------------------
// Providing alternate definitions for items pre-declared in a Windows environment.

#ifndef HANDLE
typedef void *HANDLE;
#endif
//----------------------------------------------------------------------------------------

#if defined(WIN32) || defined(WIN64)
typedef HANDLE CROSS_PLATFORM_FILE_HANDLE;
#else
typedef int CROSS_PLATFORM_FILE_HANDLE;
#endif

#ifndef BOOL
typedef int BOOL;
#endif

#ifndef u_char
typedef unsigned char u_char;
#endif

#ifndef byte
typedef unsigned char byte;
#endif

#ifndef u_short
typedef unsigned short u_short;
#endif


#ifndef u_int
typedef unsigned int u_int;
#endif

#ifndef u_ll
typedef unsigned long long u_ll;
#endif

#ifndef LPCSTR
typedef const char *LPCSTR;
#endif

#ifndef DWORD
typedef unsigned long DWORD;
typedef DWORD *LPDWORD;
#endif


#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif




typedef long long wordCounter_t;
typedef long long docnum_t;






#define MAX_WORD_LEN 15
#define MAX_REPETITION_LEN 20   // Word, @ sign and up to 4 digits  e.g. silver@5
#define MAX_BIGRAM_LEN 31
#define MAX_NGRAM_LEN 47
#define MAX_NGRAM_WORDS 6  // To avoid massive explosion in hash table size
#define MAX_QUERY_BYTES 10240

#define MAX_DOC_LEN 10485760   // 10 MB
#define MAX_DOC_WORDS 1048576    // 1 M
#define DFLT_BUF_SIZE 52428800 // 50 MB

#define DFLT_ASCII_TOKEN_BREAK_SET "%\"[]~/ &'( ),-.:;<=>?@\\^_`{|}!"
