/*
 * Unit test helpers.
 * This code is in the public domain.
 */

#ifndef _TESTS_HELPERS_H_
#define _TESTS_HELPERS_H_

#include "tokenizer.h"

char *		get_tmpdir(void);
char *		get_tmpfile(const char *);
int		mmap_cmp_file(const char *, const unsigned char *, size_t);

tokenset_t *	get_test_tokenset(const char *[], size_t);

#endif
