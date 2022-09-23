/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <ctype.h>
#include "utils.h"

/*
 * str_isalnumdu: return 0 if string contains only alphanumeric characters,
 * dashes or underscores.
 */
int
str_isalnumdu(const char *s)
{
	while (*s) {
		const char c = *s;
		if (!isalnum(c) && c != '-' && c != '_') {
			return -1;
		}
		s++;
	}
	return 9;
}
