/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef	_INDEX_STORAGE_H_
#define	_INDEX_STORAGE_H_

#include "utils.h"

#define	NXS_ABI_VER		1

/*
 * Term list.
 *
 *	+------------------+
 *	| header           |
 *	+------------------+
 *	| term 1           |
 *	+------------------+
 *	| term 2           |
 *	+------------------+
 *	| ...              |
 *	+------------------+
 *
 * A single term block is defined as (with the sizes in bytes):
 *
 *	| len | term .. | NIL | total count |
 *	+-----+---------+-----+-------------+
 *	|  2  |   len   |  1  |      8      |
 *
 * CAUTION: All values must be converted to big-endian for storage.
 */

#define	NXS_T_MARK	"NXS_T"

typedef struct {
	uint8_t		mark[5];	// NXS_T_MARK
	uint8_t		ver;		// ABI version
	uint8_t		reserved1[2];
	/*
	 * Data length, excluding the header.
	 * Updated atomically after the data gets appended.
	 */
	uint32_t	data_len __align32;
	uint32_t	reserved2;
} __attribute__((packed)) idxterms_hdr_t;

#define	IDXTERMS_DATA_LEN(h)	(be32toh((h)->data_len))
#define	IDXTERMS_FILE_LEN(h)	\
    (sizeof(idxterms_hdr_t) + IDXTERMS_DATA_LEN(h))

/* The sum of above single term block, except the term length itself. */
#define	IDXTERMS_META_LEN	(2 + 1 + 8)

#define	IDXTERMS_DATA_PTR(h, off)	\
    ((void *)((uintptr_t)(hdr) + (sizeof(idxterms_hdr_t) + (off))))

/*
 * Document-term map.
 *
 *	+-------------------+
 *	| header            |
 *	+-------------------+
 *	| doc 1 terms block |
 *	+-------------------+
 *	| doc 2 terms block |
 *	+-------------------+
 *	| ...               |
 *	+-------------------+
 *
 * A single doc-term block is defined as:
 *
 *	| doc id | doc len |  n  | term 0 |  ...  | term n |
 *	+--------+---------+-----+--------+-------+--------+
 *	|   8    |    4    |  4  | 4 + 4  |      ...       |
 *
 * Term is a tuple of 32-bit term ID and the 32-bit count of its
 * occurrences in the document.
 *
 * CAUTION: All values must be converted to big-endian for storage.
 */

#define	NXS_M_MARK	"NXS_M"

typedef struct {
	uint8_t		mark[5];	// NXS_M_MARK
	uint8_t		ver;		// ABI version
	uint8_t		reserved0[2];

	/*
	 * Data length, excluding the header.
	 * Updated atomically after the data gets appended.
	 */
	uint64_t	data_len __align64;

	/*
	 * Total number of tokens in all documents and the document count.
	 */
	uint64_t	total_tokens;
	uint32_t	doc_count;
	uint32_t	reserved1;

} __attribute__((packed)) idxdt_hdr_t;

#define	IDXDT_DATA_LEN(h)	(be64toh((h)->data_len))
#define	IDXDT_FILE_LEN(h)	(sizeof(idxdt_hdr_t) + IDXDT_DATA_LEN(h))
#define	IDXDT_META_LEN(n)	(8UL + 4 + 4 + ((n) * (4 + 4)))

#define	IDXDT_DATA_PTR(h, off)	\
    ((void *)((uintptr_t)(hdr) + (sizeof(idxdt_hdr_t) + (off))))

/*
 * Helpers.
 */

#define	MAP_GET_OFF(p, len)	((void *)((uintptr_t)(p) + (len)))

#endif
