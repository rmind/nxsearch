/*
 * Stress test: dtmap index concurrency.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <err.h>

#include "nxs.h"
#include "strbuf.h"
#include "index.h"
#include "tokenizer.h"
#include "helpers.h"
#include "utils.h"

#define	COUNT			(50 * 1000)

static const char *		terms[] = { "test" };

static pthread_barrier_t	barrier;
static unsigned			nworkers;
static char *			terms_testdb_path;
static char *			dtmap_testdb_path;

static void
terms_init(nxs_index_t *idx)
{
	tokenset_t *tokens;
	int ret;

	tokens = get_test_tokenset(terms, __arraycount(terms), true);
	assert(tokens != NULL);
	ret = idx_terms_add(idx, tokens);
	assert(ret == 0);
	tokenset_destroy(tokens);
}

static void *
conc_doc_add(void *arg)
{
	const unsigned id = (uintptr_t)arg;
	uint64_t i = (UINT32_MAX / nworkers) * id;
	unsigned n = COUNT;
	tokenset_t *tokens;
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));

	ret = idx_terms_open(&idx, terms_testdb_path);
	assert(ret == 0);

	ret = idx_dtmap_open(&idx, dtmap_testdb_path);
	assert(ret == 0);

	tokens = get_test_tokenset(terms, __arraycount(terms), false);
	assert(tokens != NULL);
	tokenset_resolve(tokens, &idx, 0);

	pthread_barrier_wait(&barrier);
	while (n--) {
		ret = idx_dtmap_add(&idx, 1U + i++, tokens);
		assert(ret == 0);
	}
	idx_dtmap_close(&idx);
	idx_terms_close(&idx);
	tokenset_destroy(tokens);
	pthread_exit(NULL);
	return NULL;
}

static void
docs_verify(nxs_index_t *idx)
{
	for (unsigned id = 0; id < nworkers; id++) {
		uint64_t i = (UINT32_MAX / nworkers) * id;
		unsigned n = COUNT;

		while (n--) {
			nxs_doc_id_t doc_id = 1U + i++;
			idxdoc_t *doc;

			doc = idxdoc_lookup(idx, doc_id);
			assert(doc != NULL);

			assert(idxdoc_get_doclen(idx, doc) == 1);
			assert(idxdoc_get_termcount(idx, doc, 1) == 1);
		}
	}
	assert(idx_get_doc_count(idx) == nworkers * COUNT);
}

static void
run_test(int c)
{
	pthread_t *thr;

	nworkers = c ? c : (sysconf(_SC_NPROCESSORS_CONF) + 1);
	thr = malloc(sizeof(pthread_t) * nworkers);

	terms_testdb_path = get_tmpfile(NULL);
	dtmap_testdb_path = get_tmpfile(NULL);
	run_with_index(terms_testdb_path, dtmap_testdb_path, true, terms_init);

	/*
	 * NOTE: See the comments in t_strss_terms.c test.
	 */
	pthread_barrier_init(&barrier, NULL, nworkers);
	for (unsigned i = 0; i < nworkers; i++) {
		if ((errno = pthread_create(&thr[i], NULL,
		    conc_doc_add, (void *)(uintptr_t)i)) != 0) {
			err(EXIT_FAILURE, "pthread_create");
		}
	}
	for (unsigned i = 0; i < nworkers; i++) {
		pthread_join(thr[i], NULL);
	}
	run_with_index(terms_testdb_path, dtmap_testdb_path, true, docs_verify);

	pthread_barrier_destroy(&barrier);
	free(thr);
}

int
main(int argc, char **argv)
{
	int c = 0;

	if (argc > 1) {
		c = atoi(argv[1]);
	}
	(void)get_tmpdir();
	run_test(c);
	puts("OK");
	return 0;
}
