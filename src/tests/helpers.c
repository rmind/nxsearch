/*
 * Unit test helpers.
 * This code is in the public domain.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "helpers.h"
#include "utils.h"

static char *		tmpdir_list[8];
static unsigned		tmpdir_count = 0;

static char *		tmpfile_list[8];
static unsigned		tmpfile_count = 0;

static void
cleanup(void)
{
	for (unsigned i = 0; i < tmpfile_count; i++) {
		char *file = tmpfile_list[i];
		unlink(file);
		free(file);
	}
	for (unsigned i = 0; i < tmpdir_count; i++) {
		char *dir = tmpdir_list[i];
		rmdir(dir);
		free(dir);
	}
}

char *
get_tmpdir(void)
{
	char *path, tpl[64];

	assert(tmpdir_count < __arraycount(tmpfile_list));
	strncpy(tpl, "/tmp/t_nxsearch_base.XXXXXX", sizeof(tpl));
	tpl[sizeof(tpl) - 1] = '\0';

	path = strdup(mkdtemp(tpl));
	tmpdir_list[tmpdir_count++] = path;

	atexit(cleanup);
	return path;
}

char *
get_tmpfile(const char *dir)
{
	char *path;
	int ret;

	if (!dir) {
		const unsigned c = tmpdir_count;
		dir = c ? tmpdir_list[c - 1] : get_tmpdir();
	}

	ret = asprintf(&path, "%s/%u.db", dir, tmpfile_count);
	assert(ret > 0 && path != NULL);
	tmpfile_list[tmpfile_count++] = path;
	return path;
}

int
mmap_cmp_file(const char *path, const unsigned char *buf, size_t len)
{
	void *addr;
	struct stat st;
	int fd, ret;

	fd = open(path, O_RDONLY);
	assert(fd > 0);

	ret = fstat(fd, &st);
	assert(ret == 0);
	assert(len <= (size_t)st.st_size);

	addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE | MAP_FILE, fd, 0);
	assert(addr != MAP_FAILED);
	ret = memcmp(buf, addr, len);
	munmap(addr, st.st_size);
	return ret;
}

tokenset_t *
get_test_tokenset(const char *tokens[], size_t count)
{
	tokenset_t *tset;

	tset = tokenset_create();
	assert(tset != NULL);

	for (unsigned i = 0; i < count; i++) {
		const char *token = tokens[i];
		const size_t len = strlen(token);
		token_t *t = token_create(token, len);
		tokenset_add(tset, t);
	}
	return tset;
}
