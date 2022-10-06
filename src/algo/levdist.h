/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _LEVDIST_H_
#define _LEVDIST_H_

struct levdist;
typedef struct levdist levdist_t;

levdist_t *	levdist_create(void);
void		levdist_destroy(levdist_t *);
int		levdist(levdist_t *, const char *, size_t, const char *, size_t);

#endif
