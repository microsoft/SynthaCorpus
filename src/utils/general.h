// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.



double what_time_is_it();

BOOL is_a_directory(char *arg);

BOOL exists(char *fstem, char *suffix);

size_t get_filesize(char *fname, BOOL verbose, int *error_code);

CROSS_PLATFORM_FILE_HANDLE open_ro(const char *fname, int *error_code);

CROSS_PLATFORM_FILE_HANDLE open_w(const char *fname, int *error_code);

void close_file(CROSS_PLATFORM_FILE_HANDLE h);

void buffered_flush(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer,
			   size_t *bytes_in_buffer, char *label, BOOL cleanup);

void buffered_write(CROSS_PLATFORM_FILE_HANDLE wh, byte **buffer,
			   size_t buffer_size, size_t *bytes_in_buffer,
			   byte *data, size_t bytes2write, char *label);

FILE *openFILE(char *stem, char *middle, char *suffix, char *openOptions, BOOL useLargeBuffer);


void *mmap_all_of(char *fname, size_t *sighs, BOOL verbose,
			 CROSS_PLATFORM_FILE_HANDLE *H, HANDLE *MH, 
			 int *error_code);

void unmmap_all_of(void *inmem, CROSS_PLATFORM_FILE_HANDLE H,
			  HANDLE MH, size_t length);

byte **load_all_lines_from_textfile(char *fname, int *line_count,
					   CROSS_PLATFORM_FILE_HANDLE *H,
					   HANDLE *MH, byte **file_in_mem,
					   size_t *sighs);

void unload_all_lines_from_textfile(CROSS_PLATFORM_FILE_HANDLE H, HANDLE MH,
					   byte ***lines, byte **file_in_memory,
					   size_t sighs);

void *lp_malloc(size_t how_many_bytes, BOOL x_use_large_pages, size_t large_page_minimum);

void lp_free(void *memory_to_free, BOOL x_use_large_pages);

void *cmalloc(size_t s,  char *msg, BOOL verbose);

void show_string_upto_nator(char *str, char nator);

void show_string_upto_nator_nolf(char *str, char nator);

void put_n_chars(char *str, size_t len);

void strncasecpy(char *dest, char *src, size_t len);

char *tailstr(char *str, char *s);

char *make_a_copy_of(char *in);

char *make_a_copy_of_len_bytes(char *in, size_t len);

char *make_a_copy_of(char *in);

char *make_a_copy_of_len_bytes(char *in, size_t len);

char *writeUllToString(char *where, u_ll qty);

char *strstr_within_line(char *haystack, char *needle);

void test_strstr_within_line();

