/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

/*
 * Index file mapping: implements the file opening and mapping with the
 * synchronization around the initialization steps.
 *
 * Synchronization rules:
 *
 * - Files are created using the (O_CREAT | O_EXCL) flags, therefore only
 * one concurrent call will succeed.  The process which succeeds will
 * immediately attempt to acquire the exclusive lock and will only then
 * set the file size.  The lock is released only when the header is setup.
 *
 * - Any other processes opening the file immediately acquire the shared
 * lock and test the file size.  It might acquire it before the creator
 * process acquires the exclusive lock.  In such case, the file size is
 * still zero and the caller will retry.  Otherwise, it will acquire the
 * lock after the creator already initialized the header as described
 * above.  This ensures that only consistent state of the header will be
 * globally visible.
 *
 * - The file may be grow only with the exclusive lock held.  To minimize
 * the lock contention and frequent remaps, the file is extended larger
 * chunks defined by the IDX_SIZE_STEP constant.
 *
 * - Synchronization of the data readers and writers (e.g. data appending)
 * is implemented on top of this by the index structure module ("terms" or
 * "tdmap").
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#define	__NXSLIB_PRIVATE
#include "nxs_impl.h"
#include "index.h"
#include "utils.h"

/*
 * idx_db_open: opens the index file.
 *
 * => Creates the index file if it does not exist.
 * => Returns the file descriptor with the file locked.
 */
int
idx_db_open(idxmap_t *idxmap, const char *path, bool *created)
{
	struct stat st;
	int fd, retry = 10;
again:
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (__predict_false(fd == -1 && errno == ENOENT)) {
		/*
		 * Attempt to create the file and acquire the exclusive
		 * lock to prevent the readers from loading the, initially,
		 * inconsistent state.
		 */
		fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
		if (fd == -1 && errno == EEXIST) {
			/* Concurrent creation: try again. */
			goto again;
		}
		if (f_lock_enter(fd, LOCK_EX) == -1) {
			goto err;
		}
		if (ftruncate(fd, IDX_SIZE_STEP) == -1) {
			goto err;
		}
		*created = true;
	} else {
		/* Acquire shared lock. */
		if (f_lock_enter(fd, LOCK_SH) == -1) {
			goto err;
		}
		*created = false;
	}
	if (fstat(fd, &st) == -1) {
		goto err;
	}
	if (st.st_size == 0) {
		/*
		 * Race condition: if the file size is zero, then we
		 * opened this file while it is still being created.
		 * Drop the shared lock and just try again.
		 */
		f_lock_exit(fd);
		if (--retry == 0) {
			/* If it's a stray zero-length file, then bail out. */
			errno = EIO;
			goto err;
		}
		close(fd);
		goto again;
	}
	idxmap->fd = fd;
	return fd;
err:
	close(fd);
	return -1;
}

/*
 * idx_db_map: map or remap the index based on the new target length.
 *
 * => Must be called with the exclusive lock held if extending.
 */
void *
idx_db_map(idxmap_t *idxmap, size_t target_len, bool extend)
{
	const size_t file_len = roundup2(target_len, IDX_SIZE_STEP);
	void *addr, *current_baseptr = idxmap->baseptr;
	struct stat st;

	ASSERT(!extend || f_lock_owned(idxmap->fd));

	/*
	 * If the current mapping is sufficient, then nothing to do.
	 */
	if (file_len <= idxmap->mapped_len) {
		return current_baseptr;
	}

	/*
	 * Check the file length and perform a basic length verification.
	 */
	if (fstat(idxmap->fd, &st) == -1) {
		return NULL;
	}
	if (file_len > (size_t)st.st_size) {
		/*
		 * If not extending, then this is an error.
		 */
		if (!extend) {
			errno = EINVAL;
			return NULL;
		}
		app_dbgx("extending from %zu to %zu",
		    (size_t)st.st_size, file_len);

		/*
		 * Extending.  WARNING: Must be called with the
		 * exclusive lock held.
		 */
		if (ftruncate(idxmap->fd, file_len) == -1) {
			return NULL;
		}
	}

	app_dbgx("fd %u length %zu", idxmap->fd, file_len);
	addr = mmap(NULL, file_len, PROT_READ | PROT_WRITE,
	    MAP_SHARED | MAP_FILE, idxmap->fd, 0);
	if (addr == MAP_FAILED) {
		return NULL;
	}

	/* Remove the current mapping, if present. */
	if (current_baseptr) {
		munmap(current_baseptr, idxmap->mapped_len);
	}

	/* Update the mapping details. */
	idxmap->baseptr = addr;
	idxmap->mapped_len = file_len;
	return addr;
}

void
idx_db_release(idxmap_t *idxmap)
{
	if (idxmap->baseptr) {
		munmap(idxmap->baseptr, idxmap->mapped_len);
		idxmap->baseptr = NULL;
		idxmap->mapped_len = 0;
	}
	if (idxmap->fd > 0) {
		close(idxmap->fd);
		idxmap->fd = 0;
	}
}
