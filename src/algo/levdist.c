/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Levenshtein distance.
 *
 * It is a for metric measuring the difference between two strings,
 * defined as: the minimum number of mutations required to change one
 * string into the other.  Levenshtein distance mutations are removal,
 * insertion and substitution.
 *
 * Simple recursive algorithm (in Python):
 *
 *	def lev_dist(a, b):
 *	    if len(a) == 0: return len(b)
 *	    if len(b) == 0: return len(a)
 *	    cost = int(a[0] != a[1])
 *	    return min(
 *	        lev_dist(a[1:], b) + 1),
 *	        lev_dist(b[1:], a) + 1),
 *	        lev_dist(a[1:], b[1:]) + cost),
 *	    )
 *
 * The actual implementation is based on the optimized version of the
 * Wagner–Fischer algorithm which, instead of the full matrix, uses only
 * the relevant row and two variables necessary to compute the final value.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <err.h>

#include "levdist.h"
#include "utils.h"

struct levdist {
	uint16_t *	vrow;
	unsigned	vlen;
};

static inline unsigned
min_u3(const unsigned x, const unsigned y, const unsigned z)
{
	return MIN(MIN(x, y), z);
}

levdist_t *
levdist_create(void)
{
	return calloc(1, sizeof(levdist_t));
}

void
levdist_destroy(levdist_t *ctx)
{
	free(ctx->vrow);
	free(ctx);
}

int
levdist(levdist_t *ctx, const char *s1, size_t n, const char *s2, size_t m)
{
	unsigned vlen, prev_diag, prev_above;
	uint16_t *vrow;

	if (n == 0)
		return m;
	if (m == 0)
		return n;

	vlen = m + 1;
	if (vlen > ctx->vlen) {
		/*
		 * Maintain a large enough memory buffer to avoid
		 * allocating all the time.
		 */
		vrow = realloc(ctx->vrow, sizeof(uint16_t) * vlen);
		if (!vrow) {
			return -1;
		}
		ctx->vrow = vrow;
		ctx->vlen = vlen;
	} else {
		vrow = ctx->vrow;
	}

	for (unsigned i = 0; i < vlen; i++) {
		vrow[i] = i;
	}
	prev_above = 0;

	for (unsigned i = 0; i < n; i++) {
		const char s1c = s1[i];

		vrow[0] = i + 1;

		for (unsigned j = 0; j < m; j++) {
			const char s2c = s2[j];
			const unsigned cost = !(s1c == s2c);

			prev_diag = prev_above;
			prev_above = vrow[j + 1];

			vrow[j + 1] = min_u3(
			    vrow[j] + 1,	// insertion cost
			    prev_above + 1,	// deletion cost
			    prev_diag + cost	// substitution cost
			);
		}
	}

	return vrow[m];
}
