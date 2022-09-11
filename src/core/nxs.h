/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _NXSLIB_H_
#define _NXSLIB_H_

struct nxs;
typedef struct nxs nxs_t;

nxs_t *		nxs_create(const char *);
void		nxs_destroy(nxs_t *);

#endif
