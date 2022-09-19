/*
 * Unit test helpers.
 * This code is in the public domain.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <err.h>

#include "helpers.h"
#include "utils.h"

static char *		tmpdir_list[256];
static unsigned		tmpdir_count = 0;

static char *		tmpfile_list[256];
static unsigned		tmpfile_count = 0;

static void
cleanup(void)
{
	for (unsigned i = 0; i < tmpfile_count; i++) {
		char *file = tmpfile_list[i];
		unlink(file);
		free(file);
	}
	tmpfile_count = 0;

	for (unsigned i = 0; i < tmpdir_count; i++) {
		char *dir = tmpdir_list[i];
		rmdir(dir);
		free(dir);
	}
	tmpdir_count = 0;
}

char *
get_tmpdir(void)
{
	char *path, tpl[256];

	assert(tmpdir_count < __arraycount(tmpdir_list));
	strncpy(tpl, "/tmp/t_nxsearch_base.XXXXXX", sizeof(tpl));
	tpl[sizeof(tpl) - 1] = '\0';

	path = strdup(mkdtemp(tpl));
	tmpdir_list[tmpdir_count++] = path;

	atexit(cleanup);
	return path;
}

char *
get_tmpfile(const char *dir)
{
	char *path;
	int ret;

	if (!dir) {
		const unsigned c = tmpdir_count;
		dir = c ? tmpdir_list[c - 1] : get_tmpdir();
	}

	assert(tmpfile_count < __arraycount(tmpfile_list));
	ret = asprintf(&path, "%s/%u.db", dir, tmpfile_count);
	assert(ret > 0 && path != NULL);
	tmpfile_list[tmpfile_count++] = path;
	return path;
}

int
mmap_cmp_file(const char *path, const unsigned char *buf, size_t len)
{
	void *addr;
	struct stat st;
	int fd, ret;

	fd = open(path, O_RDONLY);
	assert(fd > 0);

	ret = fstat(fd, &st);
	assert(ret == 0);
	assert(len <= (size_t)st.st_size);

	addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE | MAP_FILE, fd, 0);
	assert(addr != MAP_FAILED);
	ret = memcmp(buf, addr, len);
	munmap(addr, st.st_size);
	return ret;
}

tokenset_t *
get_test_tokenset(const char *values[], size_t count)
{
	tokenset_t *tokens;

	tokens = tokenset_create();
	assert(tokens != NULL);

	for (unsigned i = 0; i < count; i++) {
		const char *val = values[i];
		const size_t len = strlen(val);
		token_t *t = token_create(val, len);
		tokenset_add(tokens, t);
	}

	/* Stage all tokens, as we will be adding them. */
	TAILQ_CONCAT(&tokens->staging, &tokens->list, entry);
	tokens->staged = tokens->count;

	return tokens;
}

void
run_with_index(const char *terms_testdb_path, const char *dtmap_testdb_path,
    bool sync, test_func_t testfunc)
{
	nxs_index_t idx;
	int ret;

	memset(&idx, 0, sizeof(idx));
	ret = idxterm_sysinit(&idx);
	assert(ret == 0);

	// Open the terms and dtmap indexes.
	ret = idx_terms_open(&idx, terms_testdb_path);
	assert(ret == 0);

	ret = idx_dtmap_open(&idx, dtmap_testdb_path);
	assert(ret == 0);

	if (sync) {
		ret = idx_terms_sync(&idx);
		assert(ret == 0);

		ret = idx_dtmap_sync(&idx);
		assert(ret == 0);
	}

	// Run the test function
	testfunc(&idx);

	// Cleanup
	idx_dtmap_close(&idx);
	idx_terms_close(&idx);
	idxterm_sysfini(&idx);
}

static void
print_search_results(const char *query,
    const nxs_index_t *idx, const nxs_results_t *results)
{
	printf("ALGO %u QUERY [%s] DOC COUNT %u\n",
	    idx->algo, query, results->count);
	nxs_result_entry_t *entry = results->entries;
	while (entry) {
		printf("DOC %lu, SCORE %f\n", entry->doc_id, entry->score);
		entry = entry->next;
	}
}

static void
check_doc_score(const char *query, const nxs_index_t *idx,
    const nxs_results_t *results, const nxs_doc_id_t doc_id,
    const float score)
{
	nxs_result_entry_t *entry = results->entries;

	while (entry) {
		if (entry->doc_id != doc_id) {
			entry = entry->next;
			continue;
		}
		if (fabsf(entry->score - score) < 0.0001) {
			return;
		}
		break;
	}
	print_search_results(query, idx, results);
	if (entry) {
		errx(EXIT_FAILURE, "doc %"PRIu64" score is %f (expected %f)",
		    doc_id, entry->score, score);
	}
	errx(EXIT_FAILURE, "no doc %"PRIu64" in the results", doc_id);
}

void
test_index_search(const test_score_case_t *test_case)
{
	char *basedir = get_tmpdir();
	const char *q = test_case->query;
	nxs_results_t *results;
	nxs_index_t *idx;
	nxs_t *nxs;
	unsigned i;

	nxs = nxs_create(basedir);
	assert(nxs);

	idx = nxs_index_create(nxs, "test-idx");
	assert(idx);

	for (i = 0; i < test_case->doc_count; i++) {
		const nxs_doc_id_t doc_id = test_case->docs[i].id;
		const char *text = test_case->docs[i].text;
		int ret;

		ret = nxs_index_add(idx, doc_id, text, strlen(text));
		assert(ret == 0);
	}

	for (ranking_algo_t algo = TF_IDF; algo <= BM25; algo++) {
		idx->algo = algo;
		results = nxs_index_search(idx, q, strlen(q));

		for (i = 0; ; i++) {
			const test_score_t *score = &test_case->scores[i];
			if (score->id == 0) {
				break;
			}
			check_doc_score(q, idx, results, score->id,
			    score->value[idx->algo]);
		}
		assert(results->count == i);
		nxs_results_release(results);
	}

	nxs_index_close(nxs, idx);
	nxs_destroy(nxs);
}
