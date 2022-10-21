/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef _UTF8_UTILS_H_
#define _UTF8_UTILS_H_

typedef struct utf8_ctx utf8_ctx_t;

utf8_ctx_t *	utf8_ctx_create(const char *);
void		utf8_ctx_destroy(utf8_ctx_t *);

ssize_t		utf8_to_utf16(utf8_ctx_t *, const char *, uint16_t *, size_t);
ssize_t		utf8_from_utf16(utf8_ctx_t *, const uint16_t *, char *, size_t);
ssize_t		utf8_from_utf16_new(utf8_ctx_t *, const uint16_t *, 
			    size_t, strbuf_t *);

ssize_t		utf8_tolower(utf8_ctx_t *, const char *, char *, size_t);
ssize_t		utf8_toupper(utf8_ctx_t *, const char *, char *, size_t);
ssize_t		utf8_subs_diacritics(utf8_ctx_t *, strbuf_t *);
ssize_t		utf8_normalize(utf8_ctx_t *, strbuf_t *);

#endif
