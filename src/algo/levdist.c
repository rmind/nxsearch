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
 *	    cost = int(a[0] != b[0])
 *	    return min(
 *	        lev_dist(a[1:], b) + 1,         # removal
 *	        lev_dist(a, b[1:]) + 1,         # insertion
 *	        lev_dist(a[1:], b[1:]) + cost,  # substitution
 *	    )
 *
 * The actual implementation is based on the optimized version of the
 * Wagner–Fischer algorithm which.  Instead of the full matrix, it uses
 * only the relevant row and two variables necessary to compute the
 * final value.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <err.h>

#include "levdist.h"
#include "utils.h"

struct levdist {
	uint16_t *	row;
	unsigned	rlen;
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
	free(ctx->row);
	free(ctx);
}

int
levdist(levdist_t *ctx, const char *s1, size_t n, const char *s2, size_t m)
{
	unsigned rlen, prev_diag, prev_above;
	uint16_t *row;

	if (n < m) {
		return levdist(ctx, s2, m, s1, n);
	}
	if (n == 0)
		return m;
	if (m == 0)
		return n;

	/*
	 * The matrix rows represent the second string.  The +1 is for
	 * the element of the column representing the initial distances
	 * against an empty string.  Note the strings start at index 1.
	 */
	rlen = m + 1;

	if (rlen > ctx->rlen) {
		/*
		 * Maintain a large enough memory buffer to avoid
		 * allocating all the time.
		 */
		row = realloc(ctx->row, sizeof(uint16_t) * rlen);
		if (!row) {
			return -1;
		}
		ctx->row = row;
		ctx->rlen = rlen;
	} else {
		row = ctx->row;
	}

	/*
	 * The relevant row is the one above the matrix element (the cell)
	 * we are computing.  The very first row is filled with sequential
	 * numbers as it represents the distance against an empty string.
	 */
	for (unsigned j = 0; j < rlen; j++) {
		row[j] = j;
	}

	for (unsigned i = 0; i < n; i++) {
		const char s1c = s1[i];

		/*
		 * Compute the values for the "new" row.
		 *
		 * The first element represents the values of the first
		 * column which are also sequential (see the comment above).
		 *
		 * Reset the prev_above variable as we need to initialize
		 * diagonal value to the first cell on the first iteration.
		 */
		row[0] = i + 1;
		prev_above = i;

		for (unsigned j = 1; j <= m; j++) {
			const char s2c = s2[j - 1];
			const unsigned cost = !(s1c == s2c);

			/*
			 * - The new diagonal cell value becomes the value
			 * of the above cell as we are shifting right.
			 *
			 * - The new above value is contained in the cell
			 * we are about to compute.
			 */
			prev_diag = prev_above;
			prev_above = row[j];

			row[j] = min_u3(
			    row[j - 1] + 1,	// insertion cost (left cell)
			    prev_above + 1,	// removal cost (cell above)
			    prev_diag + cost	// substitution cost
			);
		}
	}

	return row[m];
}
