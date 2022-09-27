/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"
#include "utils.h"

void
strbuf_init(strbuf_t *sb)
{
	sb->buffer[0] = '\0';
	sb->value = sb->buffer;
	sb->length = 0;
	sb->bufsize = STRBUF_DEF_SIZE;
}

ssize_t
strbuf_prealloc(strbuf_t *sb, size_t len)
{
	void *buffer;

	ASSERT(sb->length < sb->bufsize);
	ASSERT(sb->bufsize == STRBUF_DEF_SIZE || sb->value != sb->buffer);

	if (len <= sb->bufsize) {
		return sb->bufsize;
	}
	if ((buffer = malloc(len)) == NULL) {
		return -1;
	}
	if (sb->value != sb->buffer) {
		/* Release the custom buffer. */
		ASSERT(sb->bufsize > STRBUF_DEF_SIZE);
		free(sb->value);
	}
	sb->value = buffer;
	sb->bufsize = len;
	return len;
}

/*
 * strbuf_acquire: acquire the string by copying it over to the buffer.
 *
 * => Returns the buffer size (as opposed to string length) or -1 on failure.
 * => The caller must eventually free the buffer with strbuf_release().
 * => Consecutive strbuf_acquire() calls are allowed.
 */
ssize_t
strbuf_acquire(strbuf_t *sb, const char *value, size_t len)
{
	ASSERT(sb->length < sb->bufsize);
	ASSERT(sb->bufsize == STRBUF_DEF_SIZE || sb->value != sb->buffer);

	if (__predict_false(len >= sb->bufsize)) {
		/*
		 * Double the size, plus NIL terminator.
		 */
		if (strbuf_prealloc(sb, len * 2 + 1) == -1) {
			return -1;
		}
	}
	strncpy(sb->value, value, len);
	sb->value[len] = '\0';
	sb->length = len;
	return sb->bufsize;
}

void
strbuf_release(strbuf_t *sb)
{
	if (sb->value != sb->buffer) {
		/* Release the custom buffer. */
		ASSERT(sb->bufsize > STRBUF_DEF_SIZE);
		free(sb->value);
	}
	strbuf_init(sb);
}
