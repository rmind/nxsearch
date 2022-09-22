/*
 * Unit test: index limits.
 * This code is in the public domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nxs.h"
#include "strbuf.h"
#include "index.h"
#include "tokenizer.h"
#include "helpers.h"
#include "utils.h"

// FIXME: #define	TERM_TARGET	UINT16_MAX
#define	TERM_TARGET	(UINT16_MAX / 3)
#define	DOC_ID		1001

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

static tokenset_t *
generate_tokens(unsigned n)
{
	tokenset_t *tokens;

	tokens = tokenset_create();
	assert(tokens);

	for (unsigned i = 0; i < n; i++) {
		token_t *token;
		char val[4 + 1];

		snprintf(val, sizeof(val), "%c%c%c%c",
		    rot(i, 3), rot(i, 2), rot(i, 1), rot(i, 0));

		token = token_create(val, strlen(val));
		assert(token);
		tokenset_add(tokens, token);
	}
	return tokens;
}

static void
generate_terms_doc(nxs_index_t *idx)
{
	tokenset_t *tokens;
	int ret;

	tokens = generate_tokens(TERM_TARGET);
	tokenset_resolve(tokens, idx, true);

	ret = idx_terms_add(idx, tokens);
	assert(ret == 0);

	ret = idx_dtmap_add(idx, DOC_ID, tokens);
	assert(ret == 0);

	tokenset_destroy(tokens);
}

static void
verify_terms_doc(nxs_index_t *idx)
{
	idxdoc_t *doc;
	int count;

	doc = idxdoc_lookup(idx, DOC_ID);
	assert(doc);

	for (unsigned i = 0; i < TERM_TARGET; i++) {
		nxs_term_id_t term_id = i + 1;
		idxterm_t *term;
		char val[4 + 1];
		unsigned len;
		int c;

		len = (unsigned)snprintf(val, sizeof(val), "%c%c%c%c",
		    rot(i, 3), rot(i, 2), rot(i, 1), rot(i, 0));

		// Check the term ID and value
		term = idxterm_lookup(idx, val, len);
		assert(term && term->id == term_id);
		assert(strcmp(term->value, val) == 0);

		// Check that the term has the document associated.
		assert(roaring_bitmap_contains(term->doc_bitmap, DOC_ID));

		// Check the document term count.
		c = idxdoc_get_termcount(idx, doc, term_id);
		assert(c == 1);
	}

	count = idxdoc_get_doclen(idx, doc);
	assert(count == TERM_TARGET);

	count = idx_get_token_count(idx);
	assert(count == TERM_TARGET);
}

static void
run_many_terms_test(void)
{
	char *terms_p = get_tmpfile(NULL);
	char *dtmap_p = get_tmpfile(NULL);

	/*
	 * Add auto-generated tokens.
	 */
	run_with_index(terms_p, dtmap_p, true, generate_terms_doc);

	/*
	 * Validate using a different index descriptor.
	 */
	run_with_index(terms_p, dtmap_p, true, verify_terms_doc);
}

static void
add_large_term(nxs_index_t *idx)
{
	tokenset_t *tokens;
	token_t *token;
	idxterm_t *term;
	const char *errmsg;
	char *val;
	int ret;

	tokens = tokenset_create();
	assert(tokens);

	val = malloc(UINT16_MAX + 1);
	memset(val, 'a', UINT16_MAX + 1);

	/* Maximum length token. */
	token = token_create(val, UINT16_MAX);
	tokenset_add(tokens, token);
	tokenset_resolve(tokens, idx, true);

	ret = idx_terms_add(idx, tokens);
	assert(ret == 0);

	term = idxterm_lookup(idx, val, UINT16_MAX);
	assert(term && strncmp(term->value, val, UINT16_MAX) == 0);

	/* Too large token. */
	token = token_create(val, UINT16_MAX + 1);
	tokenset_add(tokens, token);
	tokenset_resolve(tokens, idx, true);

	ret = idx_terms_add(idx, tokens);
	assert(ret == -1);

	/* Check the error message. */
	errmsg = nxs_index_get_error(idx);
	assert(strcmp(errmsg, "term too long (65536)") == 0);

	free(idx->error);
	idx->error = NULL;

	tokenset_destroy(tokens);
	free(val);
}

static void
run_large_term_test(void)
{
	char *terms_p = get_tmpfile(NULL);
	char *dtmap_p = get_tmpfile(NULL);

	run_with_index(terms_p, dtmap_p, false, add_large_term);
}

int
main(void)
{
	(void)get_tmpdir();
	run_many_terms_test();
	run_large_term_test();
	puts("OK");
	return 0;
}
