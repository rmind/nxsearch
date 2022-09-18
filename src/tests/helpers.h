/*
 * Unit test helpers.
 * This code is in the public domain.
 */

#ifndef _TESTS_HELPERS_H_
#define _TESTS_HELPERS_H_

#include "tokenizer.h"
#include "index.h"

typedef void (*test_func_t)(nxs_index_t *);

char *		get_tmpdir(void);
char *		get_tmpfile(const char *);
int		mmap_cmp_file(const char *, const unsigned char *, size_t);

tokenset_t *	get_test_tokenset(const char *[], size_t);

void		run_with_index(const char *, const char *, bool, test_func_t);

void		print_search_results(const char *, nxs_results_t *);
void		check_doc_score(nxs_results_t *, nxs_doc_id_t, float);

#endif
