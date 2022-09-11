/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Memory fetch/store interface which helps to check for potential
 * buffer overruns as well as handle fetching/storing of integers.
 */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "mmrw.h"
#include "utils.h"

void
mmrw_init(mmrw_t *mm, void *baseptr, size_t length)
{
	memset(mm, 0, sizeof(mmrw_t));

	mm->baseptr = baseptr;
	mm->length = length;

	mm->curptr = mm->baseptr;
	mm->remaining = length;
}

ssize_t
mmrw_advance(mmrw_t *mm, size_t len)
{
	if (mm->remaining < len) {
		return -1;
	}
	mm->curptr += len;
	mm->remaining -= len;
	return len;
}

ssize_t
mmrw_fetch(mmrw_t *mm, void *buf, size_t len)
{
	const void *p = mm->curptr;
	ssize_t ret;

	ret = mmrw_advance(mm, len);
	if (ret > 0) {
		ASSERT((size_t)ret == len);
		memcpy(buf, p, len);
	}
	return ret;
}

ssize_t
mmrw_store(mmrw_t *mm, const void *buf, size_t len)
{
	void *p = mm->curptr;
	ssize_t ret;

	ret = mmrw_advance(mm, len);
	if (ret > 0) {
		ASSERT((size_t)ret == len);
		memcpy(p, buf, len);
	}
	return ret;
}

/*
 * Wrappers for fetching the 16-bit, 32-bit and 64-bit integers
 * which convert the values to the host byte order.
 */

ssize_t
mmrw_fetch16(mmrw_t *mm, uint16_t *u16)
{
	uint16_t buf;

	if (mmrw_fetch(mm, &buf, sizeof(uint16_t)) == -1) {
		return -1;
	}
	*u16 = be16toh(buf);
	return sizeof(uint16_t);
}

ssize_t
mmrw_fetch32(mmrw_t *mm, uint32_t *u32)
{
	uint32_t buf;

	if (mmrw_fetch(mm, &buf, sizeof(uint32_t)) == -1) {
		return -1;
	}
	*u32 = be32toh(buf);
	return sizeof(uint32_t);
}

ssize_t
mmrw_fetch64(mmrw_t *mm, uint64_t *u64)
{
	uint64_t buf;

	if (mmrw_fetch(mm, &buf, sizeof(uint64_t)) == -1) {
		return -1;
	}
	*u64 = be64toh(buf);
	return sizeof(uint64_t);
}

/*
 * Wrappers for storing.  Convert to big-endian.
 */

ssize_t
mmrw_store16(mmrw_t *mm, uint16_t u16)
{
	const uint16_t val = htobe16(u16);
	return mmrw_store(mm, &val, sizeof(val));
}

ssize_t
mmrw_store32(mmrw_t *mm, uint32_t u32)
{
	const uint32_t val = htobe32(u32);
	return mmrw_store(mm, &val, sizeof(val));
}

ssize_t
mmrw_store64(mmrw_t *mm, uint64_t u64)
{
	const uint64_t val = htobe64(u64);
	return mmrw_store(mm, &val, sizeof(val));
}
