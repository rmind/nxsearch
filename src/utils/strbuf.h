/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _STRBUF_UTILS_H_
#define _STRBUF_UTILS_H_

/*
 * Default string buffer size.  An average length of the word should
 * be well less than 32 characters.  Double the buffer, however, for
 * the possibility of temporary conversions to UTF-16.
 */
#define	STRBUF_DEF_SIZE		(2U * 32)

typedef struct {
	/*
	 * The string and its current length (excluding NIL terminator).
	 */
	char *		value;
	unsigned	length;

	/*
	 * Pre-allocated buffer to store the string.  If new string
	 * does not fit into the fixed size buffer, then a larger buffer
	 * will be allocated.  In such case, value != &buffer.
	 *
	 * WARNING: The string must always be NIL terminated.
	 */
	unsigned	bufsize;
	char		buffer[STRBUF_DEF_SIZE];
} strbuf_t;

void		strbuf_init(strbuf_t *);
ssize_t		strbuf_prealloc(strbuf_t *, size_t);
ssize_t		strbuf_acquire(strbuf_t *, const char *, size_t);
void		strbuf_release(strbuf_t *);

#endif
