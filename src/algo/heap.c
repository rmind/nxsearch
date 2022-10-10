/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Sorting algorithm with a cap on items ("Top N elements").
 *
 * Implements heapsort with the min-heap used to maintain the cap.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

#include "heap.h"
#include "utils.h"

#define	HEAP_PARENT(i)		(((i) - 1) / 2)
#define	HEAP_LEFT_NODE(i)	((i) * 2 + 1)
#define	HEAP_RIGHT_NODE(i)	((i) * 2 + 2)

struct heap {
	size_t		cap;
	size_t		nitems;
	heap_cmpfunc_t	cmpfunc;
	void *		items[];
};

heap_t *
heap_create(size_t cap, heap_cmpfunc_t func)
{
	const size_t len = offsetof(heap_t, items[cap]);
	heap_t *h;

	if ((h = calloc(1, len)) == NULL) {
		return NULL;
	}
	h->cmpfunc = func;
	h->cap = cap;
	return h;
}

void
heap_destroy(heap_t *h)
{
	free(h);
}

/*
 * heap_add: insert the item into the min-heap.
 *
 * => Returns true on success or false if the item is rejected,
 *    because it would exceed the cap and its value is too small.
 */
bool
heap_add(heap_t *h, void *item)
{
	size_t i;

	/*
	 * If we already reached the cap, then determine if this item
	 * should be added or dropped i.e. whether it has a greater value
	 * than the minimum-value item (root of the tree).
	 */
	if (h->nitems == h->cap) {
		void *root = h->items[0];

		/* If the new item is smaller, then drop it. */
		if (h->cmpfunc(item, root) <= 0) {
			return false;
		}

		/* Remove the min-value item to free up a slot. */
		heap_remove_min(h);
	}

	/*
	 * Add new element at the next slot at the lowest level.
	 *
	 * Note: heap is a _complete binary tree_ i.e. that all levels
	 * of the tree must be filled.  The tree can be conveniently
	 * represented as an array.
	 */
	i = h->nitems++;
	h->items[i] = item;

	/*
	 * The min-heap property: child node must have greater or equal
	 * value than the parent node.
	 *
	 * "Heapify-up" by walking up the tree, comparing the node with
	 * its parent and swapping them if needed until the property is
	 * satisfied.
	 */

	while (i) {
		const size_t parent_idx = HEAP_PARENT(i);
		void *parent = h->items[parent_idx];

		if (h->cmpfunc(item, parent) >= 0) {
			/* Satisfied: the child is greater than the parent. */
			break;
		}

		/* Swap the parent and the child. */
		h->items[parent_idx] = item;
		h->items[i] = parent;

		/* Ascend the tree.. */
		i = parent_idx;
	}

	return true;
}

/*
 * heap_remove_min: remove the smallest (min-value) item from the heap.
 *
 * => Returns the item pointer or NULL if the heap is empty.
 * => This function may no longer be used after invoking heap_sort().
 */
void *
heap_remove_min(heap_t *h)
{
	size_t i, max, left_idx;
	void *item;

	if (h->nitems == 0) {
		return NULL;
	}

	/*
	 * Get the root.
	 */
	i = 0;
	item = h->items[i];
	if ((max = --h->nitems) == 0) {
		/* It was the last item. */
		h->items[i] = NULL;
		return item;
	}
	ASSERT(max < h->cap);

	/* Replace the root with the last item. */
	h->items[i] = h->items[max];
	h->items[max] = NULL;

	/*
	 * "Heapify-down": descend the tree enforcing the min-heap property.
	 */
	while ((left_idx = HEAP_LEFT_NODE(i)) < max) {
		void *parent = h->items[i];
		const size_t right_idx = HEAP_RIGHT_NODE(i);
		size_t smallest_idx = i;

		/*
		 * Find the child with the smallest value.
		 */
		if (h->cmpfunc(h->items[left_idx], parent) < 0) {
			smallest_idx = left_idx;
		}
		if (right_idx < max) {
			const void *smallest = h->items[smallest_idx];

			if (h->cmpfunc(h->items[right_idx], smallest) < 0) {
				smallest_idx = right_idx;
			}
		}

		/* If the parent is the smallest, then we are done. */
		if (smallest_idx == i) {
			break;
		}

		/*
		 * Swap with the smallest value child with the parent
		 * and descend the tree.
		 */
		h->items[i] = h->items[smallest_idx];
		h->items[smallest_idx] = parent;
		i = smallest_idx;
	}

	return item;
}

/*
 * heap_sort: sort the items in descending order (from highest to lowest).
 *
 * => Returns the pointer to the array of items and the item count.
 * => heap_add() and other functions may no longer be invoked after sorting.
 */
void *
heap_sort(heap_t *h, size_t *nitems)
{
	/*
	 * Sort the heap.
	 * - Remove min-item from the heap.
	 * - Place it where the last item was in the array.
	 *
	 * Such in-place sorting will give us reverse order.
	 */

	*nitems = h->nitems;

	while (h->nitems) {
		const size_t last_idx = h->nitems - 1;
		void *min_item = heap_remove_min(h);

		ASSERT(min_item != NULL);
		ASSERT(h->items[last_idx] == NULL);

		h->items[last_idx] = min_item;
	}

	/* Return the objects. */
	return h->items;
}
