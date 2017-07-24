// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// General utility functions

#ifdef WIN64
#include <tchar.h>
#include <strsafe.h>
#include <windows.h>
#include <Psapi.h>  
#else
#include <sys/mman.h>
#include <unistd.h>
#include <sys/time.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>

#include "../definitions.h"
#include "general.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timing functions
////////////////////////////////////////////////////////////////////////////////////////////////////////


double what_time_is_it() {
  // Returns current time-of-day in fractional seconds in a portable way
  // To calculate elapsed times, just subtract the results of two of these calls
#ifdef WIN64
  // gettimeofday is not available on Windows.  Use https://msdn.microsoft.com/en-us/library/windows/desktop/ms644904(v=vs.85).aspx
  LARGE_INTEGER now;
  static double QPC_frequency = -1.0;
  if (QPC_frequency < 0.0) {
    LARGE_INTEGER t;
    QueryPerformanceFrequency(&t);
    QPC_frequency = (double)t.QuadPart;
  }
  // We now have the elapsed number of ticks, along with the
  // number of ticks-per-second. We use these values
  // to convert to the fractional number of seconds since "the epoch", whatever that might be.

  QueryPerformanceCounter(&now);
  return (double)now.QuadPart / QPC_frequency;

#else
  struct timeval now;
  gettimeofday(&now, NULL);
  return (double)now.tv_sec + (double)(now.tv_usec) / 1000000.0;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Functions dealing with file i/o and memory mapping
////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BOOL is_a_directory(char *arg) {
  // Is arg the path to a directory?
#ifdef WIN64
  struct __stat64 fileStat;
  if (_stat64((char *)arg, &fileStat) == 0) {
	  if (fileStat.st_mode & _S_IFDIR) return TRUE;
	  else return FALSE;
  }
  else return FALSE;   // It doesn't even exist
#else
  struct stat statbuf;
  if (stat(arg, &statbuf) == 0) {
	  if (S_ISDIR(statbuf.st_mode)) return TRUE;
	  else return FALSE;
  }
  else return FALSE;   // It doesn't even exist
#endif
}


BOOL exists(char *fstem, char *suffix) {
  // Test for the existence of a file whose name is the concatenation of fstem and suffix
#ifdef WIN64
  struct __stat64 fileStat;
#else
  struct stat statbuf;
#endif
  char *fname;
  size_t l1 = strlen(fstem);

  fname = (char *)malloc(l1 + strlen(suffix) + 2);  // MAL2000
  if (fname == NULL) {
    fprintf(stderr, "Warning: Malloc failed in exists(%s, %s)\n", fstem, suffix);
    return FALSE;
  }
  strcpy(fname, fstem);
  strcpy(fname + l1, suffix);
#ifdef WIN64
  if (_stat64((char *)fname, &fileStat) == 0) {
    free(fname);								 //FRE2000
    return TRUE;
  }
#else
  if (stat(fname, &statbuf) == 0) {
    free(fname);								 //FRE2000
    return TRUE;
  }       
#endif
  free(fname);								     //FRE2000
  return FALSE;
}



size_t get_filesize(char *fname, BOOL verbose, int *error_code) {
#ifdef WIN64
  struct __stat64 fileStat;
#else
  struct stat statbuf;
#endif
  *error_code = 0;
#ifdef WIN64
  if (_stat64((char *)fname, &fileStat) != 0) {
    long long ser = GetLastError();
    // Error codes are listed via https://msdn.microsoft.com/en-us/library/windows/desktop/ms681381%28v=vs.85%29.aspx
    if (verbose) printf("Error %lld while statting %s\n", ser, fname);
    *error_code = -210007;
    return -1;
  }
  return fileStat.st_size;
#else 
  if (stat((char *)fname, &statbuf) != 0) {
    if (verbose) printf("Error %d while statting %s\n", errno, fname);
    *error_code = -210007;
    return -1;
  }
  return statbuf.st_size;
#endif

}


CROSS_PLATFORM_FILE_HANDLE open_ro(const char *fname, int *error_code) {
  // Open fname for read-only access and return an error code if this isn't
  // possible.
  CROSS_PLATFORM_FILE_HANDLE rslt;
  *error_code = 0;
#ifdef WIN64
  rslt = CreateFile((LPCSTR)fname,
		    GENERIC_READ,
		    FILE_SHARE_READ,
		    NULL,
		    OPEN_EXISTING,
		    //FILE_FLAG_SEQUENTIAL_SCAN,
		    FILE_ATTRIBUTE_READONLY,
		    NULL);

  if (rslt == INVALID_HANDLE_VALUE) {
    *error_code = -210006;
  }
#else
  rslt = open(fname, O_RDONLY);
  if (rslt < 0) {
    *error_code = -210006;
  }
#endif
  return(rslt);
}


CROSS_PLATFORM_FILE_HANDLE open_w(const char *fname, int *error_code) {
  // Open fname for write access and return an error code if this isn't
  // possible.
  CROSS_PLATFORM_FILE_HANDLE rslt;
  *error_code = 0;
#ifdef WIN64
  rslt = CreateFile((LPCSTR)fname,
		    GENERIC_WRITE,
		    FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		    NULL,
		    CREATE_ALWAYS,
		    FILE_FLAG_SEQUENTIAL_SCAN,
		    NULL);

  if (rslt == INVALID_HANDLE_VALUE) {
    *error_code = -210006;
    return NULL;
  }

#else
  // Create the file with mode 0777 so that anyone can re-create the index
  rslt = open(fname, O_CREAT|O_TRUNC|O_WRONLY, S_IRWXU|S_IRWXG|S_IRWXO);
  if (rslt < 0) {
    *error_code = -210006;
    return -1;
  }
#endif
  return(rslt);
}


void close_file(CROSS_PLATFORM_FILE_HANDLE h) {
#ifdef WIN64
  CloseHandle(h);
#else
  close(h);
#endif
      
}

void buffered_flush(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer, size_t *bytes_in_buffer, char *label, BOOL cleanup)  {
  // Write *bytes_in-buffer bytes to the program-maintained buffer to the file 
  // represented by wh and set bytes_in_buffer to zero.  If an error occurs
  // print label and take an abnormal exit.  If cleanup is set, free the buffer
  // close the handle.

  if (*buffer == NULL) {
    printf("Buffered_flush() - nothing to do: buffer is NULL\n");
    return;
  }
#ifdef WIN64
  BOOL ok;
  DWORD written;
  ok = WriteFile(wh, *buffer, (DWORD)*bytes_in_buffer, (LPDWORD)&written, NULL);
  if (!ok) {
    printf("Error code for %s: %u, trying to write %lld bytes\n", label,
	   GetLastError(), *bytes_in_buffer);
    printf("\n%s: ", label);
    printf("buffered_flush() write error %d\n", GetLastError());
    exit(1);
  }
#else
  ssize_t written;
  written = write(wh, *buffer, *bytes_in_buffer);
  if (written < 0) {
    printf("Error code for %s: %u, trying to write %zu bytes\n", label,
	   errno, *bytes_in_buffer);
    printf("\n%s: ", label);
    printf("buffered_flush() write error %d\n", errno);
    exit(1);
  }
#endif
  if (written < *bytes_in_buffer) {
    printf("\n%s: ", label);
    printf("buffered_flush() short write.\n");
    exit(1);
  }
  *bytes_in_buffer = 0;
  if (cleanup) {
    free(*buffer);
    *buffer = NULL;
    close_file(wh);
  }
}


void buffered_write(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer, size_t buffer_size, size_t *bytes_in_buffer,
		    byte *data, size_t bytes2write, char *label) {
  // Attempt to store bytes2write bytes from data in the buffer.  If during the 
  // process, the buffer fills, call buffer_flush to write a buffer full to the file 
  // represented by wh.  If bytes2write is larger than the buffer_size, loop until done.
  // If an error occurs, use label to help identify which file caused the prob.
  // If buffer is NULL, allocate one of the specified buffer_size.
  size_t b, bib;
  byte *buf;

  if (0) printf("Buffered write(%s) - %zu\n", label, bytes2write);
  if (*buffer == NULL) {
    *buffer = (byte *)malloc(buffer_size);
    if (*buffer == NULL) {
      printf("\n%s: ", label);
      printf("buffered_write() - malloc failed\n");
      exit(1);
    }
    *bytes_in_buffer = 0;
    if (0) printf("Buffered write(%s) - buffer of %zu malloced\n", label, buffer_size);

  }
  buf = *buffer;
  bib = *bytes_in_buffer;
  for (b = 0; b < bytes2write; b++) {
    if (bib >= buffer_size) {
      if (0) printf("Buffered write(%s) - about to flush %zu bytes\n",
			label, buffer_size);
      buffered_flush(wh, buffer, &bib, label, 0);
      bib = 0;
    }
    if (0) printf("Buffered write(%s) - b = %zu, bib = %zu\n", label, b, bib);
    buf[bib++] = data[b];
  }
  *bytes_in_buffer = bib;
  if (0) printf("Buffered write(%s) - write finished\n", label);
}



FILE *openFILE(char *stem, char *middle, char *suffix, char *openOptions, BOOL useLargeBuffer) {
  // Open a stream, check for errors, and optionally set a very large buffer size.
  FILE *rslt;
  static char fnameBuffer[1001];
  size_t fnameLength;
  fnameLength = strlen(stem) + strlen(middle) + strlen(suffix);
  if (fnameLength > 1000) {
    printf("Error: fname %s%s%s too long\n", stem, middle, suffix);
    exit(1);
  }
  strcpy(fnameBuffer, stem);
  strcat(fnameBuffer, middle);
  strcat(fnameBuffer, suffix);
  
  rslt = fopen(fnameBuffer, openOptions);
  if (rslt == NULL) {
    printf("Error: Can't write to %s\n", fnameBuffer);
    exit(1);
  }
  if (useLargeBuffer)
    setvbuf(rslt, NULL, _IOFBF, DFLT_BUF_SIZE);   // Use large buffer to speed writing on NAS

  return rslt;
}



void *mmap_all_of(char *fname, size_t *sighs, BOOL verbose, CROSS_PLATFORM_FILE_HANDLE *H, HANDLE *MH, 
		  int *error_code) {
  // Memory map the entire file named fname and return a pointer to mapped memory,
  // plus handles to the file and to the memory mapping.  We have to return the
  // handles to enable indexes to be properly unloaded.
  // 
  //
  void *mem;
  double MB;
  int ec;
  *error_code = 0;
  BOOL report_errors = TRUE;
  double start;
  start = what_time_is_it();

  if (verbose) fprintf(stderr, "Loading %s\n", fname);
  *sighs = get_filesize(fname, verbose, error_code);

  MB = (double)*sighs / 1048576.0;
  *H = open_ro((char *)fname, &ec);
  if (ec < 0) {
#ifdef WIN64
    long long ser = GetLastError();
    *error_code = ec;
    if (report_errors) printf("\nError %lld while opening %s\n", ser, fname);
#else
    if (report_errors) printf("\nError %d while opening %s\n", errno, fname);
#endif
    return NULL;
  }

  if (verbose) fprintf(stderr, "File %s opened.\n", fname);

#ifdef WIN64
  if ((*MH = CreateFileMapping(*H,
			       NULL,
			       PAGE_READONLY,
			       0,
			       0,
			       NULL)) < 0) {
    long long ser = GetLastError();
    *error_code = -210008;
    if (report_errors) printf("\nError %lld while Creating File Mapping for %s\n", ser, fname);
    return NULL;   // ----------------------------------------->
  }

  if (verbose) printf("File mapping created for %s\n", fname);

  if ((mem = (byte *)MapViewOfFile(*MH,
				   FILE_MAP_READ,
				   0,
				   0,
				   0)) == NULL) {
    long long ser = GetLastError();
    *error_code = -210009;
    if (report_errors) printf("\nError %lld while Mapping View of %s\n", ser, fname);
    return NULL;  // ----------------------------------------->
  }


  // Assertion:  We can close both handles here without losing the mapping.
  //CloseHandle(H);
  //CloseHandle(MH);
  // False.  Subsequent reopening of the file finds a lot of crap.
#else

  /*  Note: 
      MAP_HUGETLB (since Linux 2.6.32)
      Allocate the mapping using "huge pages."  See the Linux kernel
      source file Documentation/vm/hugetlbpage.txt for further
      information, as well as NOTES, below.

      MAP_HUGE_2MB, MAP_HUGE_1GB (since Linux 3.8)
      Used in conjunction with MAP_HUGETLB to select alternative
      hugetlb page sizes (respectively, 2 MB and 1 GB) on systems
      that support multiple hugetlb page sizes.
  */
  mem = mmap(NULL, *sighs, PROT_READ, MAP_PRIVATE, *H, 0);
  if (mem == MAP_FAILED) {
    *error_code = -210007;
    return NULL;
  }

#endif

  if (verbose) fprintf(stderr, "  - %8.1fMB mapped.\n", MB);
  if (verbose) fprintf(stderr, "  - elapsed time: %8.1f sec.\n", what_time_is_it() - start);
  return mem;
}


void unmmap_all_of(void *inmem, CROSS_PLATFORM_FILE_HANDLE H, HANDLE MH, size_t length) {
  // Note MH is only used on Windows and length is only used on Unix-like systems.
#ifdef WIN64
  UnmapViewOfFile(inmem);
  CloseHandle(MH);
  close_file(H);
#else
  munmap(inmem, length);
  close_file(H);
#endif
}


byte **load_all_lines_from_textfile(char *fname, int *line_count, CROSS_PLATFORM_FILE_HANDLE *H,
				    HANDLE *MH, byte **file_in_mem, size_t *sighs) {
  // memory map file fname, and return an array of pointers to every line in the in-memory
  // version
  int error_code;
  int cnt = 0;
  byte *p, *lastbyte;
  byte **lions = NULL;
  
  *file_in_mem = mmap_all_of(fname, sighs, FALSE, H, MH, &error_code);
  if (*file_in_mem == NULL) {
    printf("Error: Can't mmap %s.  Error_code %d.\n", fname, error_code);
    exit(1);
  }
  // Count the number of lines
  p = *file_in_mem;
  lastbyte = p + *sighs - 1;
  if (*lastbyte != '\n') cnt++;  // Last line ends with EOF rather than LF
  while (p <= lastbyte) {
    if (*p++ == '\n') cnt++;
  }
  *line_count = cnt;

  // Allocate storage for the strings
  lions = (byte **)malloc(cnt * sizeof(byte *));
  if (lions == NULL) {
    printf("Error: Malloc failed in load_all_lines_from_textfile()\n");
    exit(1);
  }

  // set up all the entries in lions[]
  p = *file_in_mem;
  lastbyte = p + *sighs - 1;
  cnt = 0;
  lions[cnt++] = p;
  while (p < lastbyte) {
     if (*p++ == '\n') {
       lions[cnt++] = p;  // Point to the character after each linefeed (except the one at EOF)
     }
  }
  return lions;
}

void unload_all_lines_from_textfile(CROSS_PLATFORM_FILE_HANDLE H, HANDLE MH, byte ***lines,
				    byte **file_in_memory, size_t sighs) {
  free(*lines);
  unmmap_all_of(*file_in_memory, H, MH, sighs);
  *lines = NULL;
  *file_in_memory = NULL;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory functions
////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef WIN64
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Code for setting up to use Virtual Memory Large Pages.  (Can reduce program runtime.)                               //
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//Code posted by 'jeremyb1' on https://social.msdn.microsoft.com/forums/windowsdesktop/en-us/09edf7b2-2ddc-44e5-9fd2-5d537b542051/trying-to-use-memlargepages-but-cant-get-selockmemoryprivilege
void Privilege(TCHAR* pszPrivilege, BOOL bEnable, BOOL *x_use_large_pages, size_t *large_page_minimum)
{
  HANDLE      hToken;
  TOKEN_PRIVILEGES tp;
  BOOL       status;
  u_long      error;

  printf("Attempting to set privileges\n");
		
  OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
  LookupPrivilegeValue(NULL, pszPrivilege, &tp.Privileges[0].Luid);
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  status = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)NULL, 0);
  error = GetLastError();
  if (error) {
    printf("\n\nAdjustTokenPrivileges error %u.  Giving up on using LARGE PAGES.\n\n", error);
    printf("Note that successful use of x_use_large_pages requires that QBASHI.exe is run with administrator privilege\n"
	   "by a user who has the lock-memory privilege bit set.   Setting this bit can be achieved by running secpol.msc,\n"
	   "navigating to Local Policies, User Rights, and setting the lock-memory right.  I believe you then have to\n"
	   "reboot.\n");
    x_use_large_pages = FALSE;
  }
  else {
    *large_page_minimum = GetLargePageMinimum();
    printf("Proceeding to use LARGE PAGES.  Large Page Minimum: %lld bytes.\n", *large_page_minimum);
  }

  CloseHandle(hToken);
}

#endif

void *lp_malloc(size_t how_many_bytes, BOOL x_use_large_pages, size_t large_page_minimum) {
  // Use either malloc or virtualalloc() (with LARGE PAGES) depending upon the setting of the global
  // x_use_large_pages

  void *rslt;
#ifdef WIN64
  if (x_use_large_pages) {
    // how_many_bytes must be a multiple of large_page_minimum
    if (how_many_bytes % large_page_minimum) {
      // round up
      how_many_bytes = ((how_many_bytes / large_page_minimum) + 1) * large_page_minimum;
    }
    rslt = VirtualAlloc(
			NULL,
			how_many_bytes,
			MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
			PAGE_READWRITE);

    if (rslt == NULL) {
      // Failed
      printf("VirtualAlloc failed: %u\n", GetLastError());
    }
    else {
      if (0) printf("VirtualAlloc succeeded!\n");
    }
  }
  else 
#endif	
    rslt = malloc(how_many_bytes);
  return rslt;
}


void lp_free(void *memory_to_free, BOOL x_use_large_pages) {
#ifdef WIN64
  if (x_use_large_pages) {
    BOOL success = VirtualFree(
			       memory_to_free,
			       (size_t)0,
			       MEM_RELEASE);
    if (!success) {
      printf("VirtualFree failed: %u\n", GetLastError());
    }
  }
  else 
#endif	
    free(memory_to_free);
}



void *cmalloc(size_t s,  char *msg, BOOL verbose) {
  // A front end to malloc() which exits on error, zeroes the allocated memory
  // and reports the numbe of MB allocated.
  void *vvv;
  double MB;
  vvv = malloc(s);
  if (vvv == NULL) {
    printf("Error:  CMALLOC(%s) failed for size %zu\n", msg, s);
    exit(1);
  }
  memset(vvv, 0, s);   // Make sure it's all zeroes
  MB = (double)s / (1024.0 * 1024.0);
  if (verbose) printf("CMALLOC(%s):  %.1fMB allocated.\n", msg, MB);
  return vvv;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////
// String functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////

void show_string_upto_nator(char *str, char nator) {
  // Print str up to the first occurrence of nator or NUL, followed by a linefeed.
  // Like fputs(str, stdio) except that:
  //   - str is terminated by nator or NUL rather than just NUL
  while (*str  && *str != nator) putchar(*str++);
  putchar('\n');
}


void show_string_upto_nator_nolf(char *str, char nator) {
  // Print str up to the first occurrence of nator or NUL.
  // Like fputs(str, stdio) except that:
  //   - str is terminated by nator or NUL rather than just NUL
  while (*str  && *str != nator) putchar(*str++);
}

void put_n_chars(char *str, size_t len) {
  while (len-- > 0) {
    putchar(*str++);
  }
}


#if !defined(strncasecpy) 
void strncasecpy(char *dest, char *src, size_t len) {
  // Just like strncpy but does ascii case folding
  int i;
  for (i = 0; i < len; i++) {
    if (*src == 0) {
      dest[i] = 0;
    }
    else {
      dest[i] = tolower(*src);
      src++;
    }
  }
}
#endif


char *tailstr(char *str, char *s) {
  // Returns NULL iff str doesn't end with a substring s.
  // Otherwise returns a pointer to the matching substring
  char *p;
  size_t l = strlen((char *)s);
  BOOL matched = FALSE;
  while ((p = (char *)strstr((char *)str, (char *)s)) != NULL) {
    str = p + l;
    matched = TRUE;
  }

  if (matched) {
    // There was at least one match and the last one started at str - l
    if (!strcmp((char *)str - l, (char *)s)) return str - l;
  }
  return NULL;
}

char *make_a_copy_of(char *in) {
  // Makes and returns a copy of string in in malloced storage
  size_t len;
  char *out;
  if (in == NULL) return NULL;
  len = strlen((char *)in);
  out = (char *)malloc(len + 1);
  if (out == NULL) return NULL;
  strcpy((char *)out, (char *)in);
  return out;
}


char *make_a_copy_of_len_bytes(char *in, size_t len) {
  // Makes and returns a null-terminated copy of len byte of in in malloced storage
  char *out;
  if (in == NULL) return NULL;
  out = (char *)malloc(len + 1);
  if (out == NULL) return NULL;
  strncpy((char *)out, (char *)in, len);
  out[len] = 0;
  return out;
}

char *writeUllToString(char *where, u_ll qty) {
  // Sometimes sprintf seems very slow.  Hopefully this
  // avoids the problem in this specific case.
  byte buf[24], *w = buf, *x = where;

  // Store the digits in buf (least to most significant order)
  if (qty == 0) {
    *w++ = '0';
  } else {
    while (qty > 0) {
      *w++ = (qty % 10) + '0';
      qty /= 10;
    }
  }

  // Transfer the digits from  buf to where (reversing the order).
  do {
    w--;
    *x++ = *w;
  } while (w > buf);
  *x = 0;
  return x;
}

char *strstr_within_line(char *haystack, char *needle) {
  // Like strstr but haystack may be terminated with a newline rather than a NUL
  char *h, *n;
  if (needle == NULL ||  !*needle) return NULL;
  while (*haystack && *haystack != '\n') {
    if (*haystack == *needle) {
      h = haystack + 1;
      n = needle + 1;
      while (*n && *n == *h) {
	n++;
	h++;
      }
      if (!*n) return haystack;
      if (!*h || *h == '\n') return NULL;
    }
    haystack++;
  }
  return NULL;
}


void test_strstr_within_line() {
  char haystack[1000] = "Now is the tttititimtime\nafter";
  if (strstr_within_line(haystack, "after") != NULL)
    fprintf(stderr, "Error: 'after' found\n");
  if (strstr_within_line(haystack, "Now is the") == NULL)
    fprintf(stderr, "Error: 'Now is the' not found\n");
   if (strstr_within_line(haystack, "ti") == NULL)
    fprintf(stderr, "Error: 'ti' not found\n");
   if (strstr_within_line(haystack, "time") == NULL)
    fprintf(stderr, "Error: 'time' not found\n");
   if (strncmp(strstr_within_line(haystack, "time"), "time", 4))
    fprintf(stderr, "Error: 'time' not found in right place\n");
   if (strstr_within_line(haystack, "imti") == NULL)
    fprintf(stderr, "Error: 'imti' not found\n");
}


