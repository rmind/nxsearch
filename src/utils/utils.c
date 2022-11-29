/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
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

void *
fs_read_file(const char *path, size_t *len)
{
	char *text = NULL;
	struct stat st;
	ssize_t ret;
	int fd;

	if ((fd = open(path, O_RDONLY)) == -1) {
		return NULL;
	}
	if (fstat(fd, &st) == -1) {
		goto out;
	}
	if ((text = malloc(st.st_size + 1)) == NULL) {
		goto out;
	}
	if ((ret = fs_read(fd, text, st.st_size)) == -1) {
		free(text);
		goto out;
	}
	text[ret] = '\0';
	if (len) {
		*len = (size_t)st.st_size;
	}
out:
	close(fd);
	return text;
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

/*
 * flock_owned: check that the file-lock is owned.
 *
 * WARNING: May only be used to check whether the current process owns the
 * lock for diagnostic purposes and must not be used for locking decisions.
 */
bool
f_lock_owned(int fd __unused)
{
	return true;  // XXX: TODO
}

int
f_lock_enter(int fd, int operation)
{
	int ret;

	while ((ret = flock(fd, operation)) == -1 && errno == EINTR) {
		// retry
	}
	return ret;
}

void
f_lock_exit(int fd)
{
	ASSERT(f_lock_owned(fd));

	while (flock(fd, LOCK_UN) == -1 && errno == EINTR) {
		// retry
	}
}
