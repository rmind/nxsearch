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
 * Term index (list).
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
 *	| len | term .. | NIL | [pad] | total count |
 *	+-----+---------+-----+-------+-------------+
 *	|  2  |   len   |  1  |  ...  |      8      |
 *
 * The total count must 64-bit aligned, therefore padding must be added
 * to enforce the alignment where needed.
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
	uint32_t	data_len;
	uint32_t	reserved2;
} __attribute__((packed)) idxterms_hdr_t;

static_assert(sizeof(idxterms_hdr_t) == 16, "ABI guard");
static_assert(sizeof(idxterms_hdr_t) % 8 == 0, "alignment guard");

#define	IDXTERMS_DATA_PTR(h, off)	\
    ((void *)((uintptr_t)(hdr) + (sizeof(idxterms_hdr_t) + (off))))

/* The sum of above single term block, except the term length itself. */
#define	IDXTERMS_META_LEN	(2UL + 1 + 8)
#define	IDXTERMS_META_MAXLEN	(IDXTERMS_META_LEN + 8)
#define	IDXTERMS_PAD_LEN(len)	(roundup2(2UL + 1 + (len), 8) - (2 + 1 + (len)))
#define	IDXTERMS_BLK_LEN(len)	\
    (IDXTERMS_META_LEN + (len) + IDXTERMS_PAD_LEN(len))

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
 * The document length is counted in tokens (note that this includes
 * all repetitions/duplicates).
 *
 * Term is a tuple of 32-bit term ID and the 32-bit count of its
 * occurrences in the document.
 *
 * CAUTION: All values must be converted to big-endian for storage.
 */

#define	NXS_D_MARK	"NXS_D"

typedef struct {
	uint8_t		mark[5];	// NXS_D_MARK
	uint8_t		ver;		// ABI version
	uint8_t		reserved0[2];

	/*
	 * Data length, excluding the header.
	 * Updated atomically after the data gets appended.
	 */
	uint64_t	data_len;

	/*
	 * Total number of seen tokens (including repetitions/duplicates)
	 * in all documents in the index.  Also, the total document count.
	 */
	uint64_t	token_count;
	uint32_t	doc_count;
	uint32_t	reserved1;

} __attribute__((packed)) idxdt_hdr_t;

static_assert(sizeof(idxdt_hdr_t) == 32, "ABI guard");
static_assert(sizeof(idxdt_hdr_t) % 8 == 0, "alignment guard");

#define	IDXDT_DATA_PTR(h, off)	\
    ((void *)((uintptr_t)(hdr) + (sizeof(idxdt_hdr_t) + (off))))

#define	IDXDT_META_LEN(n)	(8UL + 4 + 4 + ((n) * (4 + 4)))

#define	IDXDT_TOKEN_COUNT(h)	\
    be64toh(atomic_load_relaxed(&(hdr)->token_count))

#define	IDXDT_DOC_COUNT(h)	\
    be32toh(atomic_load_relaxed(&(hdr)->doc_count))

/*
 * Helpers.
 */

#define	MAP_GET_OFF(p, len)	((void *)((uintptr_t)(p) + (len)))

#endif
