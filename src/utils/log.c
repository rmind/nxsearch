/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "utils.h"

int	app_log_level = LOG_NOTICE;

int
app_set_loglevel(const char *level)
{
	static const struct {
		const char *	name;
		int		logopt;
	} log_levels[] = {
		{ "CRITICAL",	LOG_CRIT	},
		{ "ERR",	LOG_ERR		},
		{ "WARNING",	LOG_WARNING	},
		{ "NOTICE",	LOG_NOTICE	},
		{ "INFO",	LOG_INFO	},
		{ "DEBUG",	LOG_DEBUG	}
	};

	for (unsigned i = 0; i < __arraycount(log_levels); i++) {
		if (strcasecmp(log_levels[i].name, level) == 0) {
			app_log_level = log_levels[i].logopt;
			return 0;
		}
	}
	return -1;
}

void
_app_log(int level, const char *file, int line,
    const char *func, const char *fmt, ...)
{
	char fileline[128], buf[2048], *p = buf;
	size_t size = sizeof(buf);
	int err = 0, ret;
	va_list ap;

	if (level & LOG_EMSG) {
	        err = errno;
	}
	snprintf(fileline, sizeof(fileline), "%s:%d", file, line);
	ret = snprintf(p, size, "%-25s :: %s: ", fileline, func);
	p += ret, size -= ret;

	va_start(ap, fmt);
	(void)vsnprintf(p, size, fmt, ap);
	va_end(ap);

	if (err) {
#ifdef __linux__
		errno = err;
		printf("%s (%m)\n", buf);
#else
		char errbuf[128];
		strerror_r(err, errbuf, sizeof(errbuf));
		printf("%s (%s)\n", buf, errbuf);
#endif
	} else {
		puts(buf);
	}
	fflush(stdout);
}
