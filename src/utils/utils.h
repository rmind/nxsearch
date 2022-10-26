/*
 * Copyright (c) 2020-2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#ifndef	_UTILS_H_
#define	_UTILS_H_

#include <sys/cdefs.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <syslog.h>
#include <limits.h>
#include <assert.h>

/*
 * A regular assert (debug/diagnostic only).
 */

#if defined(DEBUG)
#define	ASSERT		assert
#else
#define	ASSERT(x)
#endif

/*
 * Branch prediction macros.
 */

#ifndef __predict_true
#define	__predict_true(x)	__builtin_expect((x) != 0, 1)
#define	__predict_false(x)	__builtin_expect((x) != 0, 0)
#endif

/*
 * Various C helpers and attribute macros.
 */

#ifndef __packed
#define	__packed		__attribute__((__packed__))
#endif

#ifndef __aligned
#define	__aligned(x)		__attribute__((__aligned__(x)))
#endif

#ifndef __unused
#define	__unused		__attribute__((__unused__))
#endif

#ifndef __arraycount
#define	__arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

#ifndef __UNCONST
#define	__UNCONST(a)		((void *)(unsigned long)(const void *)(a))
#endif

#define	__align32		__attribute__((aligned(__alignof__(uint32_t))))
#define	__align64		__attribute__((aligned(__alignof__(uint64_t))))

#ifndef ALIGNED_POINTER
#define	ALIGNED_POINTER(p,t)	((((uintptr_t)(p)) & (sizeof(t) - 1)) == 0)
#endif

#ifndef ffs64
#define	ffs64(x)		((uint64_t)ffsll(x))
#endif

#ifndef popcount64
#define	popcount64(x)		__builtin_popcountll(x)
#endif

/*
 * Minimum, maximum and rounding macros.
 */

#ifndef MIN
#define	MIN(x, y)	((x) < (y) ? (x) : (y))
#endif

#ifndef MAX
#define	MAX(x, y)	((x) > (y) ? (x) : (y))
#endif

#ifndef roundup2
#define	roundup2(x,m)	((((x) - 1) | ((m) - 1)) + 1)
#endif

/*
 * Atomics.
 */

#define	atomic_load_relaxed(p)		\
    __atomic_load_n((p), __ATOMIC_RELAXED)

#define	atomic_store_relaxed(p, v)	\
    __atomic_store_n((p), (v), __ATOMIC_RELAXED)

#define	atomic_load_acquire(p)		\
    __atomic_load_n((p), __ATOMIC_ACQUIRE)

#define	atomic_store_release(p, v)	\
    __atomic_store_n((p), (v), __ATOMIC_RELEASE)

#define	atomic_cas_relaxed(p, e, d)	\
    __atomic_compare_exchange_n((p), (e), (d), \
    true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)

/*
 * Byte-order conversions.
 */
#if defined(__linux__) || defined(sun)
#include <endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define	be16toh(x)	ntohs(x)
#define	htobe16(x)	htons(x)
#define	be32toh(x)	ntohl(x)
#define	htobe32(x)	htonl(x)
#define	be64toh(x)	OSSwapBigToHostInt64(x)
#define	htobe64(x)	OSSwapHostToBigInt64(x)
#else
#include <sys/endian.h>
#endif

/*
 * DSO visibility.
 */

#ifndef __GNUC_PREREQ__
#ifdef __GNUC__
#define	__GNUC_PREREQ__(x, y)						\
    ((__GNUC__ == (x) && __GNUC_MINOR__ >= (y)) || (__GNUC__ > (x)))
#else
#define	__GNUC_PREREQ__(x, y)	0
#endif
#endif

#if !defined(__dso_public)
#if __GNUC_PREREQ__(4, 0)
#define	__dso_public	__attribute__((__visibility__("default")))
#else
#define	__dso_public
#endif
#endif

/*
 * Log interface.
 */

extern int app_log_level;

int	app_set_loglevel(const char *);

void	_app_log(int, const char *, int, const char *, const char *, ...);

#define	LOG_EMSG	(1U << 31)

/* Optimize-out the debug logging. */
#ifdef DEBUG
#define	app_dbgx(m, ...)	if (__predict_false(app_log_level >= LOG_DEBUG)) \
    _app_log(LOG_DEBUG, __FILE__, __LINE__, __func__, m, __VA_ARGS__)
#define	app_dbg(m, ...)		if (__predict_false(app_log_level >= LOG_DEBUG)) \
    _app_log(LOG_DEBUG|LOG_EMSG, __FILE__, __LINE__, __func__, m, __VA_ARGS__)
#else
#define	app_dbgx(m, ...)
#define	app_dbg(m, ...)
#endif

/*
 * Misc.
 */

int	str_isalnumdu(const char *);
ssize_t	fs_read(int, void *, size_t);
bool	fs_is_dir(const char *);

#endif
