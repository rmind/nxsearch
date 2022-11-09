/*
 * Copyright (c) 2022 Mindaugas Rasiukevicius <rmind at noxt eu>
 * All rights reserved.
 *
 * Use is subject to license terms, as specified in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <getopt.h>
#include <time.h>
#include <err.h>

#include "nxs.h"
#include "utils.h"

#define	APP_NAME	"nxsearch_test"

static struct timespec	ts;

static void
usage(void)
{
	fprintf(stderr,
	    "Usage:\t" APP_NAME " -i INDEX [ -a | -r ]\n"
	    "      \t" APP_NAME " -i INDEX -d ID -p FILE_PATH\n"
	    "      \t" APP_NAME " -i INDEX -p DIRECTORY_PATH\n"
	    "      \t" APP_NAME " -i INDEX -s QUERY\n"
	    "\n"
	    "Options:\n"
	    "  -a, --add              Add the specified index\n"
	    "  -d, --doc-id           Specify the document ID\n"
	    "  -p, --path PATH        Index the given file or directory\n"
	    "  -i, --index INDEX      Specify the index\n"
	    "  -r, --remove           Drop the specified index\n"
	    "  -s, --search QUERY     Search\n"
	    "\n"
	);
	exit(EXIT_FAILURE);
}

static void
benchmark_start(void)
{
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
		err(EXIT_FAILURE, "clock_gettime");
	}
}

static void
benchmark_end(const char *operation)
{
	struct timespec end;
	int64_t sec, usec, elapsed;

	if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
		err(EXIT_FAILURE, "clock_gettime");
	}

	/* Convert to milliseconds. */
	sec = end.tv_sec - ts.tv_sec;
	if ((usec = (end.tv_nsec - ts.tv_nsec) / 1000) < 0) {
		sec--, usec += (1000000000L / 1000);
	}
	elapsed = sec * 1000 + (usec / 1000);

	printf("%s: %"PRIi64" ms\n", operation, elapsed);
}

static void
index_file(nxs_t *nxs, nxs_index_t *idx, nxs_doc_id_t doc_id, const char *path)
{
	size_t len;
	char *text;

	if ((text = fs_read_file(path, &len)) == NULL) {
		err(EXIT_FAILURE, "fs_read_file() failed");
	}
	if (nxs_index_add(idx, doc_id, text, len) == -1) {
		const char *e = NULL;
		nxs_get_error(nxs, &e);
		errx(EXIT_FAILURE, "could not index %s: %s", path, e);
	}
	free(text);
}

static void
index_dir(nxs_t *nxs, nxs_index_t *idx, const char *path)
{
	char fpath[PATH_MAX];
	struct dirent *dt;
	uint64_t id = 1;
	DIR *dirp;

	if ((dirp = opendir(path)) == NULL) {
		err(EXIT_FAILURE, "opendir");
	}
	while ((dt = readdir(dirp)) != NULL) {
		if (dt->d_type != DT_REG) {
			continue;
		}
		snprintf(fpath, sizeof(fpath) - 1, "%s/%s", path, dt->d_name);
		printf("Indexing %"PRIu64 " -- %s\n", id, dt->d_name);
		index_file(nxs, idx, id++, fpath);
	}
	closedir(dirp);
}

int
main(int argc, char **argv)
{
	static const char *opts_s = "ad:i:p:rs:h?";
	static struct option opts_l[] = {
		{ "add",	no_argument,		0,	'a'	},
		{ "doc-id",	required_argument,	0,	'd'	},
		{ "path",	required_argument,	0,	'p'	},
		{ "index",	required_argument,	0,	'i'	},
		{ "search",	required_argument,	0,	's'	},
		{ "remove",	no_argument,		0,	'r'	},
		{ "help",	no_argument,		0,	'h'	},
		{ NULL,		0,			NULL,	0	}
	};
	nxs_t *nxs;
	nxs_index_t *idx;
	const char *index = NULL, *query = NULL, *path = NULL, *e = NULL;
	bool add = false, drop = false;
	nxs_doc_id_t doc_id = 0;
	int ch;

	while ((ch = getopt_long(argc, argv, opts_s, opts_l, NULL)) != -1) {
		switch (ch) {
		case 'a':
			add = true;
			break;
		case 'd':
			doc_id = atol(optarg);
			break;
		case 'p':
			path = optarg;
			break;
		case 'i':
			index = optarg;
			break;
		case 'r':
			drop = true;
			break;
		case 's':
			query = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (!index)
		usage();

	if ((nxs = nxs_open(NULL)) == NULL) {
		err(EXIT_FAILURE, "could not initialize nxsearch");
	}

	if (add) {
		benchmark_start();
		idx = nxs_index_create(nxs, index, NULL);
		if (idx == NULL) {
			nxs_get_error(nxs, &e);
			errx(EXIT_FAILURE, "could not create the index: %s", e);
		}
		benchmark_end("creating index");
	} else {
		benchmark_start();
		idx = nxs_index_open(nxs, index);
		if (idx == NULL) {
			nxs_get_error(nxs, &e);
			errx(EXIT_FAILURE, "could not open the index: %s", e);
		}
		benchmark_end("loading index");
	}

	if (path) {
		benchmark_start();
		if (!fs_is_dir(path)) {
			if (!doc_id) {
				usage();
			}
			index_file(nxs, idx, doc_id, path);
		} else {
			index_dir(nxs, idx, path);
		}
		benchmark_end("indexing");
	}

	if (query) {
		nxs_resp_t *resp;
		char *json;

		benchmark_start();
		resp = nxs_index_search(idx, NULL, query, strlen(query));
		if (resp == NULL) {
			nxs_get_error(nxs, &e);
			errx(EXIT_FAILURE, "search error: %s", e);
		}
		benchmark_end("search");

		json = nxs_resp_tojson(resp, NULL);
		nxs_resp_release(resp);
		puts(json);
		free(json);
	}

	if (drop) {
		errx(EXIT_FAILURE, "not yet implemented yet");
	}

	benchmark_start();
	nxs_close(nxs);
	benchmark_end("closing index");

	return 0;
}
