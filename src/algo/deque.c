/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Double-ended queue (deque) using a circular array.
 */

#include <stdlib.h>

#include "deque.h"
#include "utils.h"

struct deque {
	unsigned	start;		// offset to the first element
	unsigned	count;		// the number of elements
	unsigned	size;		// the size of the deque
	unsigned	grow_step;
	void **		elements;
};

deque_t *
deque_create(unsigned step)
{
	deque_t *dq;

	if ((dq = calloc(1, sizeof(deque_t))) == NULL) {
		return NULL;
	}
	dq->grow_step = MIN(step, 64);
	dq->size = dq->grow_step;

	dq->elements = calloc(dq->grow_step, sizeof(void *));
	if (!dq->elements) {
		free(dq);
		return NULL;
	}
	return dq;
}

void
deque_destroy(deque_t *dq)
{
	free(dq->elements);
	free(dq);
}

void *
deque_pop_front(deque_t *dq)
{
	void *elm;

	if (!dq->count) {
		return NULL;
	}

	elm = dq->elements[dq->start];
	dq->elements[dq->start] = NULL;

	if (++dq->start == dq->size) {
		dq->start = 0;
	}
	dq->count--;
	return elm;
}

void *
deque_pop_back(deque_t *dq)
{
	unsigned last;
	void *elm;

	if (!dq->count) {
		return NULL;
	}
	last = dq->start + --dq->count;
	if (last >= dq->size) {
		last -= dq->size;
	}
	elm = dq->elements[last];
	dq->elements[last] = NULL;
	return elm;
}

int
deque_push(deque_t *dq, void *elm)
{
	unsigned next;

	/*
	 * If the queue is full, then extend it.
	 */
	if (dq->count == dq->size) {
		void **elements, *it_elm;
		unsigned i, count = dq->count;

		/*
		 * Grow by a step.
		 */
		elements = calloc(dq->size + dq->grow_step, sizeof(void *));
		if (!elements) {
			return -1;
		}

		/*
		 * Copy over all elements.
		 */
		i = 0;
		while ((it_elm = deque_pop_front(dq)) != NULL) {
			elements[i++] = it_elm;
		}
		free(dq->elements);
		dq->elements = elements;

		/* Starting over. */
		dq->start = 0;
		dq->count = count;
		dq->size += dq->grow_step;
	}

	next = dq->start + dq->count;
	if (next >= dq->size) {
		next -= dq->size;
	}
	dq->elements[next] = elm;
	dq->count++;

	return 0;
}
