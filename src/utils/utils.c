/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <ctype.h>
#include <errno.h>

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
	return 0;
}

ssize_t
fs_read(int fd, void *buf, size_t target)
{
	ssize_t toread = target;
	uint8_t *bufp = buf;

	while (toread) {
		ssize_t ret;

		if ((ret = read(fd, bufp, toread)) <= 0) {
			if (ret == -1 && errno == EINTR) {
				continue;
			}
			if (ret == 0) {
				break;
			}
			return -1;
		}
		bufp += ret;
		toread -= ret;
	}
	return target - toread;
}

bool
fs_is_dir(const char *path)
{
	struct stat sb;

	if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
		return true;
	}
	return false;
}
