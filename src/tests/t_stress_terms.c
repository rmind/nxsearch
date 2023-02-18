/*
 * Stress test: terms index concurrency.
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

#define	COUNT			(10 * 1000)

static pthread_barrier_t	barrier;
static unsigned			nworkers;
static char *			testdb_path;

static void *
conc_term_add(void *arg)
{
	const unsigned id = (uintptr_t)arg;
	uint64_t i = (UINT32_MAX / nworkers) * id;
	unsigned n = COUNT;
	tokenset_t *tokens;
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));
	ret = idx_terms_open(&idx, testdb_path);
	assert(ret == 0);

	pthread_barrier_wait(&barrier);
	while (n--) {
		char val[8 + 1];
		const char *vals[] = { val };

		/*
		 * Generate a rotating string (8 characters) using
		 * separate range for each worker thread (the thread
		 * ID serves as a numeric offset for the range).
		 */
		get_rot_string(i++, val, sizeof(val));
		tokens = get_test_tokenset(vals, 1, true);
		assert(tokens != NULL);

		// Add the string as a term.
		ret = idx_terms_add(&idx, tokens);
		assert(ret == 0);
		tokenset_destroy(tokens);
	}
	idx_terms_close(&idx);
	pthread_exit(NULL);
	return NULL;
}

static void
term_verify(void)
{
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));
	ret = idx_terms_open(&idx, testdb_path);
	assert(ret == 0);

	for (unsigned id = 0; id < nworkers; id++) {
		uint64_t i = (UINT32_MAX / nworkers) * id;
		unsigned n = COUNT;

		while (n--) {
			char val[8 + 1];
			idxterm_t *t;

			get_rot_string(i++, val, sizeof(val));
			t = idxterm_lookup(&idx, val, sizeof(val) - 1);
			assert(t != NULL);
		}
	}
	idx_terms_close(&idx);
}

static void
run_test(int c)
{
	pthread_t *thr;

	nworkers = c ? c : (sysconf(_SC_NPROCESSORS_CONF) + 1);
	thr = malloc(sizeof(pthread_t) * nworkers);
	testdb_path = get_tmpfile(NULL);

	/*
	 * Concurrent term addition.  Note: even though nxsearch as a
	 * whole is not considered multi-threading safe, many of its parts
	 * are and, in this case, we can use threads to stress test the
	 * index map structure.
	 */
	pthread_barrier_init(&barrier, NULL, nworkers);
	for (unsigned i = 0; i < nworkers; i++) {
		if ((errno = pthread_create(&thr[i], NULL,
		    conc_term_add, (void *)(uintptr_t)i)) != 0) {
			err(EXIT_FAILURE, "pthread_create");
		}
	}
	for (unsigned i = 0; i < nworkers; i++) {
		pthread_join(thr[i], NULL);
	}

	/*
	 * Verify all terms.
	 */
	term_verify();

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
