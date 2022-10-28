/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Burkhard-Keller tree (simply, BK-tree).
 *
 * The tree basically indexes a metric space, where Levenshtein distance
 * provides the metric.  A metric must satisfy several axioms, but the
 * key one relevant in this context is the _triangle inequality_:
 *
 *	d(a,b) <= d(a,c) + d(b,c)
 *
 * The main observation by the BK-tree authors is that the Levenshtein
 * distance matches the metric criteria.  Think of the following example
 * as points of a triangle:
 *
 *	levdist("kitten", "sitting") == d(a,b) == 3
 *	levdist("kitten", "sittin")  == d(a,c) == 2
 *	levdist("sitting", "sittin") == d(b,c) == 1
 *
 * Let the distance between the given word and the target node be D, and
 * the level of tolerance i.e. maximum allowed distance for the approximate
 * matching be N.  Therefore, deducting from the above formula, matching
 * nodes must have a distance between D − N and D + N, inclusively.
 *
 * This implementation uses Bagwell's (2001) bit trick to compress the
 * sparse array of branches.  The use of the 64-bit bitmap sets the
 * limitation of the overall maximum distance of 64 and we just choose
 * to not support larger words (there is arguably little value in that).
 *
 * References:
 *
 *	W. Burkhard and R. Keller, 1973,
 *	Some approaches to best-match file searching.
 *	https://dl.acm.org/doi/pdf/10.1145/362003.362025
 *
 *	Phil Bagwell, 2001, Ideal Hash Trees.
 */

#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>
#include <strings.h>
#include <errno.h>

#include "deque.h"
#include "bktree.h"
#include "utils.h"

typedef struct bknode {
	const void *	obj;
	uint64_t	bitmap;
	struct bknode *	map[];
} bknode_t;

struct bktree {
	bknode_t *		root;
	bktree_distfunc_t	distfunc;
	void *			distctx;
};

////////////////////////////////////////////////////////////////////////////

static void *
bknode_new(const void *obj)
{
	bknode_t *node;

	if ((node = calloc(1, sizeof(bknode_t))) == NULL) {
		return NULL;
	}
	node->obj = obj;
	return node;
}

static void *
bknode_get(bknode_t *node, unsigned i)
{
	const uint64_t bitmap = node->bitmap;
	const uint64_t bit = UINT64_C(1) << i;

	if ((bitmap & bit) == 0) {
		return NULL;
	}

	/*
	 * From Bagwell (2001), page 2:
	 *
	 *	"Finding the arc for a symbol s, requires finding its
	 *	corresponding bit in the bitmap and then counting the one
	 *	bits below it in the map to compute an index into the
	 *	ordered sub-trie."
	 */
	i = popcount64(bitmap & (bit - 1));
	return node->map[i];
}

static inline bknode_t **
bknode_get_slotptr(bknode_t *node, unsigned i)
{
	const uint64_t bitmap = node->bitmap;
	const uint64_t bit = UINT64_C(1) << i;
	const unsigned slot = popcount64(bitmap & (bit - 1));
	return &node->map[slot];
}

static uint64_t
bknode_set_slot(bknode_t *node, unsigned i, void *val)
{
	const uint64_t bitmap = node->bitmap;
	const uint64_t bit = UINT64_C(1) << i;
	const unsigned slot = popcount64(bitmap & (bit - 1));

	ASSERT(val != NULL);
	node->map[slot] = val;
	return bit;
}

static bknode_t *
bknode_set(bknode_t *curnode, unsigned i, void *val)
{
	const uint64_t bit = UINT64_C(1) << i;
	uint64_t bitmap = curnode->bitmap;
	const unsigned nitems = popcount64(bitmap);
	const size_t nlen = offsetof(bknode_t, map[nitems + 1]);
	bknode_t *node;

	ASSERT((bitmap & bit) == 0);

	if ((node = malloc(nlen)) == NULL) {
		return NULL;
	}
	node->obj = curnode->obj;
	node->bitmap = bitmap | bit;
	bknode_set_slot(node, i, val);

	while ((i = ffs64(bitmap)) != 0) {
		void *ival = bknode_get(curnode, --i);
		bitmap &= ~bknode_set_slot(node, i, ival);
	}

	free(curnode);
	return node;
}

static uint64_t
bknode_get_range(bknode_t *node, unsigned start, unsigned end)
{
	const uint64_t lo_mask = ~UINT64_C(0) << start;
	const uint64_t hi_mask = ~UINT64_C(0) >> (64 - end);
	return node->bitmap & (lo_mask & hi_mask);
}

////////////////////////////////////////////////////////////////////////////

int
bktree_insert(bktree_t *bkt, const void *obj)
{
	bknode_t *new_node, *node, *child, **pp;
	int d;

	if ((new_node = bknode_new(obj)) == NULL) {
		return -1;
	}
	if ((node = bkt->root) == NULL) {
		bkt->root = new_node;
		return 0;
	}
	pp = &bkt->root;

	/*
	 * Insertion: the tree is built by computing D and branching at
	 * this value thus descending the tree until the leaf is reached
	 * where the new node is added.
	 */
desc:
	d = bkt->distfunc(bkt->distctx, obj, node->obj);
	if (__predict_false(d <= 0)) {
		free(new_node);
		if (d == 0) {
			/* Duplicate. */
			errno = EEXIST;
		}
		return -1;
	}

	/*
	 * Everything above the limit just goes into a single bucket.
	 * This may result in O(n) scan for all long strings but one may
	 * produce synthetic data to trigger such behaviour anyway.
	 */
	d = MIN((unsigned)d, BKT_DIST_LIMIT);

	/*
	 * Check if there is a child node at this distance.
	 */
	if ((child = bknode_get(node, d)) != NULL) {
		/* Descend: the child is now a new node. */
		pp = bknode_get_slotptr(node, d);
		node = child;
		goto desc;
	}

	/*
	 * Insert the new node into the current leaf.
	 */
	if ((node = bknode_set(node, d, new_node)) == NULL) {
		free(new_node);
		return -1;
	}
	*pp = node;
	return 0;
}

int
bktree_search(bktree_t *bkt, unsigned tolerance,
    const void *obj, deque_t *results)
{
	bknode_t *node;
	deque_t *dq;
	int ret = -1;

	if ((node = bkt->root) == NULL) {
		return 0;
	}

	/* Accumulator deque for the nodes to process. */
	if ((dq = deque_create(64)) == NULL) {
		return -1;
	}
	deque_push(dq, node);

	/*
	 * Search: compute D and look for nodes matching D - N and D + N,
	 */
	while ((node = deque_pop_front(dq)) != NULL) {
		unsigned i, min_d, max_d;
		uint64_t bitmap;
		int d;

		/*
		 * Compute the distance.
		 */
		d = bkt->distfunc(bkt->distctx, obj, node->obj);
		if (__predict_false(d < 0)) {
			goto out;
		}
		if ((unsigned)d <= tolerance) {
			deque_push(results, (void *)(uintptr_t)node->obj);
		}

		/*
		 * Get the boundaries, the bitmap representing the range
		 * and inspect the child nodes.
		 */
		min_d = MAX((int)d - (int)tolerance, 0);
		max_d = MIN(d + tolerance, BKT_DIST_LIMIT);

		/* Iterate the nodes in the range .. */
		bitmap = bknode_get_range(node, min_d, max_d);
		while ((i = ffs64(bitmap)) != 0) {
			bknode_t *child = bknode_get(node, --i);
			deque_push(dq, child);
			bitmap &= ~(UINT64_C(1) << i);
		}
	}
	ret = 0;
out:
	deque_destroy(dq);
	return ret;
}

bktree_t *
bktree_create(bktree_distfunc_t func, void *ctx)
{
	bktree_t *bkt;

	if ((bkt = calloc(1, sizeof(bktree_t))) == NULL) {
		return NULL;
	}
	bkt->distfunc = func;
	bkt->distctx = ctx;
	return bkt;
}

void
bktree_destroy(bktree_t *bkt)
{
	bknode_t *node;
	deque_t *dq;

	if ((node = bkt->root) == NULL) {
		free(bkt);
		return;
	}

	dq = deque_create(64);
	deque_push(dq, node);

	while ((node = deque_pop_front(dq)) != NULL) {
		const unsigned nitems = popcount64(node->bitmap);

		for (unsigned i = 0; i < nitems; i++) {
			deque_push(dq, node->map[i]);
		}
		free(node);
	}
	deque_destroy(dq);
	free(bkt);
}
