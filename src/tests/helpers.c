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

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
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

static char
rot(unsigned i, unsigned pos)
{
	const unsigned range = 'z' - 'a' + 1;
	unsigned d = 1;

	for (unsigned e = 0; e < pos; e++) {
		d *= range;
	}
	return 'a' + (i / d) % range;
}

ssize_t
get_rot_string(unsigned i, char *buf, size_t len)
{
	return snprintf(buf, len, "%c%c%c%c%c%c%c%c",
	    rot(i, 7), rot(i, 6), rot(i, 5), rot(i, 4),
	    rot(i, 3), rot(i, 2), rot(i, 1), rot(i, 0));
}

tokenset_t *
get_test_tokenset(const char *values[], size_t count, bool stage)
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

	if (stage) {
		/* Stage all tokens, as we will be adding them. */
		TAILQ_CONCAT(&tokens->staging, &tokens->list, entry);
		tokens->staged = tokens->count;
	}

	return tokens;
}

void
run_with_index(const char *terms_testdb_path, const char *dtmap_testdb_path,
    bool sync, test_func_t testfunc)
{
	nxs_index_t idx;
	nxs_t nxs;
	int ret;

	memset(&nxs, 0, sizeof(nxs));
	memset(&idx, 0, sizeof(idx));
	idx.nxs = &nxs;

	// Open the terms and dtmap indexes.
	ret = idx_terms_open(&idx, terms_testdb_path);
	assert(ret == 0);

	ret = idx_dtmap_open(&idx, dtmap_testdb_path);
	assert(ret == 0);

	if (sync) {
		ret = idx_terms_sync(&idx);
		assert(ret == 0);

		ret = idx_dtmap_sync(&idx, 0);
		assert(ret == 0);
	}

	// Run the test function
	testfunc(&idx);

	// Cleanup
	nxs_clear_error(&nxs);
	idx_dtmap_close(&idx);
	idx_terms_close(&idx);
}

static void
print_search_results(const char *query,
    const nxs_index_t *idx, nxs_resp_t *resp)
{
	nxs_doc_id_t doc_id;
	float score;

	printf("ALGO %u QUERY [%s] DOC COUNT %u\n",
	    idx->algo, query, nxs_resp_resultcount(resp));

	nxs_resp_iter_reset(resp);
	while (nxs_resp_iter_result(resp, &doc_id, &score)) {
		printf("DOC %lu, SCORE %f\n", doc_id, score);
	}
}

static void
check_doc_score(const char *query, const nxs_index_t *idx,
    nxs_resp_t *resp, nxs_doc_id_t target_doc_id,
    float expected_score)
{
	nxs_doc_id_t doc_id;
	float score;

	nxs_resp_iter_reset(resp);
	while (nxs_resp_iter_result(resp, &doc_id, &score)) {
		if (doc_id != target_doc_id) {
			continue;
		}
		if (expected_score < 0) {
			// Negative expected score means ignore the score.
			return;
		}
		if (fabsf(score - expected_score) < 0.0001) {
			return;
		}
		print_search_results(query, idx, resp);
		errx(EXIT_FAILURE, "doc %"PRIu64" score is %f (expected %f)",
		    doc_id, score, expected_score);
	}
	print_search_results(query, idx, resp);
	errx(EXIT_FAILURE, "no doc %"PRIu64" in the results", doc_id);
}

void
test_index_search(const test_search_case_t *test_case)
{
	const char *algos[] = {
		[TF_IDF] = "TF-IDF",
		[BM25] = "BM25",
	};
	char *basedir = get_tmpdir();
	const char *q = test_case->query;
	nxs_index_t *idx;
	nxs_resp_t *resp;
	nxs_t *nxs;

	nxs = nxs_open(basedir);
	assert(nxs);

	idx = nxs_index_create(nxs, "__test-idx-1", NULL);
	assert(idx);

	for (unsigned i = 0; i < test_case->doc_count; i++) {
		const nxs_doc_id_t doc_id = test_case->docs[i].id;
		const char *text = test_case->docs[i].text;
		int ret;

		ret = nxs_index_add(idx, NULL, doc_id, text, strlen(text));
		assert(ret == 0);
	}

	for (ranking_algo_t algo = TF_IDF; algo <= BM25; algo++) {
		nxs_params_t *params;
		unsigned i;

		params = nxs_params_create();
		assert(params);

		nxs_params_set_str(params, "algo", algos[algo]);
		resp = nxs_index_search(idx, params, q, strlen(q));
		assert(resp);

		nxs_params_release(params);

		/* Validate each test case. */
		for (i = 0; ; i++) {
			const test_score_t *score = &test_case->scores[i];
			if (score->id == 0) {
				break;
			}
			check_doc_score(q, idx, resp, score->id,
			    score->value[algo]);
		}
		if (nxs_resp_resultcount(resp) != i) {
			print_search_results(q, idx, resp);
			errx(EXIT_FAILURE, "got different number of results "
			    "than expected");
		}
		nxs_resp_release(resp);
	}

	nxs_index_close(idx);
	nxs_close(nxs);
}
