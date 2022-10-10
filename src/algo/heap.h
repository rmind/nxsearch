/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _HEAP_H_
#define	_HEAP_H_

typedef int (*heap_cmpfunc_t)(const void *, const void *);

typedef struct heap heap_t;

heap_t *	heap_create(size_t, heap_cmpfunc_t);
void		heap_destroy(heap_t *);

bool		heap_add(heap_t *, void *);
void *		heap_remove_min(heap_t *);
void *		heap_sort(heap_t *, size_t *);

#endif
