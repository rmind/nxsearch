/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <math.h>

#define __NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "index.h"
#include "utils.h"

/*
 * Ranking algorithm.
 *
 * It is used to order documents (matching the search criteria) based
 * on the score it produces.  The algorithm assigns a score based on its
 * estimation of document *relevance*.
 *
 * References:
 *
 *	Hans Peter Luhn, 1957, "A Statistical Approach to Mechanized
 *	Encoding and Searching of Literary Information"
 *
 *	Karen Spärck Jones, 1972, "A Statistical Interpretation of Term
 *	Specificity and Its Application in Retrieval"
 *
 *	S E Robertson, S Walker, S Jones, M M Hancock-Beaulieu, M Gatford,
 *	1994, "Okapi at TREC-3"
 */

float
tf_idf(const nxs_index_t *idx, const idxterm_t *term, const idxdoc_t *doc)
{
	/*
	 * TF-IDF intuition:
	 *
	 * 1) Term Frequency: "The weight of a term that occurs in a document
	 * is simply proportional to the term frequency" (H P Luhn, 1957).
	 *
	 * 2) Inverse Document Frequency: "The specificity of a term can be
	 * quantified as an inverse function of the number of documents in
	 * which it occurs" (K S Jones, 1972).
	 *
	 * Formula:
	 *
	 *	tf-idf(t, d, D) = tf(t, d) * idf(t, D)
	 *
	 *	t - term, d - document, D - all documents,
	 *	N = len(D) - the number of documents
	 *
	 * There are different weighting schemes in literature, e.g. what
	 * logarithm to apply on IDF, whether smoothen TF with square root
	 * or logarithm, etc.  We will use:
	 *
	 *	TF <- tf(t, d) = log(term_freq(t, d) + 1)
	 *	IDF <- idf(t, D) = log(N / doc_freq(t)) + 1
	 *
	 * We sum the scores of multiple terms, e.g. "cats AND dogs",
	 * if they are in the given document.
	 */

	int term_freq;
	unsigned doc_freq, doc_count;
	float tf, idf;

	term_freq = idxdoc_get_termcount(idx, doc, term->id);
	ASSERT(term_freq > 0);
	tf = log(term_freq + 1);

	doc_count = idxdoc_get_totalcount(idx);
	doc_freq = roaring_bitmap_get_cardinality(term->doc_bitmap);
	idf = log((float)doc_count / doc_freq) + 1;

	app_dbgx("term_freq %d, doc_freq %u, tf %f, idf %f, score %f",
	    term_freq, doc_freq, tf, idf, tf * idf);

	return tf * idf;
}
