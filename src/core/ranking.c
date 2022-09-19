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
 * This module implements two algorithms: TF-IDF and BM25.
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
	doc_count = idx_get_doc_count(idx);
	doc_freq = roaring_bitmap_get_cardinality(term->doc_bitmap);

	/*
	 * Verify: the index may be changed by concurrent requests.
	 * This would not affect results in real-world situations,
	 * but we must handle the document removal case.
	 */
	if (__predict_false(term_freq <= 0)) {
		return nanf("");
	}

	tf = log(term_freq + 1);
	idf = log((float)doc_count / doc_freq) + 1;

	app_dbgx("term_freq %d, doc_freq %u, tf %f, idf %f, score %f",
	    term_freq, doc_freq, tf, idf, tf * idf);

	return tf * idf;
}

float
bm25(const nxs_index_t *idx, const idxterm_t *term, const idxdoc_t *doc)
{
	/*
	 * BM25 can be seen as an evolution of TF-IDF.
	 *
	 * There are several adjustments:
	 *
	 * - It bounds TF using the tf / (tf + k) formula.  This addresses
	 * the term saturation problem (i.e. many occurrences of the same
	 * term in the document resulting in a greater score) and gives the
	 * greater weight for documents which match multiple terms rather
	 * than one term many times.
	 *
	 * - The algorithm weights in the document length in TF.  The weight
	 * is expressed by the ratio of the document length and the average
	 * document length in the whole document collection: dl / adl.  It
	 * is used as coefficient to k.  The longer the document, the lower
	 * weight it will have.  The importance of this weight is regulated
	 * by a constant b using the 1 – b + b * dl / adl formula, where b
	 * is between 0 and 1.
	 *
	 * - BM2 also uses probabilistic IDF, defined as:
	 *
	 *	log((N - doc_freq(t) + .5) / (doc_freq(t) + .5))
	 *
	 * This is tuned to speed up the score decline for the terms which
	 * are used in many documents.  Note: Lucene adds 1 to the expression
	 * in logarithm to avoid the negative scores.
	 *
	 * - Therefore:
	 *
	 *	TF <- tf(t, d) = log(term_freq(t, d) + 1)
	 *	BM25 = TF / (TF + k * (1 - b + b * dl / adl)) *
	 *	       log((N - doc_freq(t) + .5) / (doc_freq(t) + .5) + 1)
	 *
	 * We will use the fine tuned k and b constants from Lucene:
	 *
	 *	k = 1.2
	 *	b = 0.75
	 */

	static const double k = 1.2f;
	static const double b = 0.75f;

	int term_freq;
	double doc_freq, doc_count;
	double tf, dl, adl, tf_bm25, idf_bm25;

	term_freq = idxdoc_get_termcount(idx, doc, term->id);
	doc_count = idx_get_doc_count(idx);
	doc_freq = roaring_bitmap_get_cardinality(term->doc_bitmap);
	adl = idx_get_token_count(idx) / idx_get_doc_count(idx);

	/* Verify in case of concurrent document removals. */
	if (__predict_false(term_freq <= 0 || adl == 0)) {
		return nanf("");
	}

	tf = log(term_freq + 1);
	dl = idxdoc_get_doclen(idx, doc);
	tf_bm25 = tf / (tf + k * (1 - b + b * dl / adl));

	idf_bm25 = log(((doc_count - doc_freq + 0.5) / (doc_freq + 0.5)) + 1);

	return tf_bm25 * idf_bm25;
}
