/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _DEQUE_H_
#define _DEQUE_H_

typedef struct deque deque_t;

deque_t *	deque_create(unsigned, unsigned);
void		deque_destroy(deque_t *);

int		deque_push(deque_t *, void *);
void *		deque_pop_front(deque_t *);
void *		deque_pop_back(deque_t *);

void **		deque_get_array(deque_t *);
void *		deque_get(deque_t *, unsigned);
size_t		deque_count(const deque_t *);

#endif
