/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _MMRW_UTILS_H_
#define _MMRW_UTILS_H_

typedef struct {
	void *		baseptr;
	size_t		length;
	uint8_t *	curptr;
	size_t		remaining;
} mmrw_t;

#define	MMRW_GET_OFFSET(mf) \
    ((uintptr_t)(mf)->curptr - (uintptr_t)(mf)->baseptr)

void		mmrw_init(mmrw_t *, void *, size_t);

ssize_t		mmrw_advance(mmrw_t *, size_t);
ssize_t		mmrw_seek(mmrw_t *, size_t);

ssize_t		mmrw_fetch(mmrw_t *, void *, size_t);
ssize_t		mmrw_store(mmrw_t *, const void *, size_t);

ssize_t		mmrw_fetch16(mmrw_t *, uint16_t *);
ssize_t		mmrw_fetch32(mmrw_t *, uint32_t *);
ssize_t		mmrw_fetch64(mmrw_t *, uint64_t *);

ssize_t		mmrw_store16(mmrw_t *, uint16_t);
ssize_t		mmrw_store32(mmrw_t *, uint32_t);
ssize_t		mmrw_store64(mmrw_t *, uint64_t);

#endif
