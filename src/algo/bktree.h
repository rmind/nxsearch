/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _BKTREE_H_
#define	_BKTREE_H_

#define	BKT_DIST_LIMIT		(CHAR_BIT * sizeof(uint64_t))

typedef struct bktree bktree_t;

typedef int (*bktree_distfunc_t)(void *, const void *, const void *);

bktree_t *	bktree_create(bktree_distfunc_t, void *);
void		bktree_destroy(bktree_t *);

int		bktree_insert(bktree_t *, const void *);
int		bktree_search(bktree_t *, unsigned, const void *, deque_t *);

#endif
