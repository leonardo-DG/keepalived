/*
 * Soft:        Perform a GET query to a remote HTTP/HTTPS server.
 *              Set a timer to compute global remote server response
 *              time.
 *
 * Part:        Main entry point.
 *
 * Version:     $Id: main.c,v 1.1.16 2009/02/14 03:25:07 acassen Exp $
 *
 * Authors:     Alexandre Cassen, <acassen@linux-vs.org>
 *
 *              This program is distributed in the hope that it will be useful,
 *              but WITHOUT ANY WARRANTY; without even the implied warranty of
 *              MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *              See the GNU General Public License for more details.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Copyright (C) 2001-2012 Alexandre Cassen, <acassen@gmail.com>
 */

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "main.h"
#include "utils.h"
#include "signals.h"

/* global var */
REQ *req = NULL;

/* Terminate handler */
void
sigend(void *v, int sig)
{
	/* register the terminate thread */
	thread_add_terminate_event(master);
}

/* Initialize signal handler */
void
signal_init(void)
{
	signal_handler_init();
	signal_set(SIGHUP, sigend, NULL);
	signal_set(SIGINT, sigend, NULL);
	signal_set(SIGTERM, sigend, NULL);
	signal_ignore(SIGPIPE);
}

/* Usage function */
static void
usage(const char *prog)
{
	enum feat_hashes i;

	fprintf(stderr, VERSION_STRING);
	fprintf(stderr,
		"Usage:\n"
		"  %s -s server-address -p port -u url\n"
		"  %s -S -s server-address -p port -u url\n"
		"  %s -h\n" "  %s -r\n\n", prog, prog, prog, prog);
	fprintf(stderr,
		"Commands:\n"
		"Either long or short options are allowed.\n"
		"  %s --use-ssl         -S       Use SSL connection to remote server.\n"
		"  %s --server          -s       Use the specified remote server address.\n"
		"  %s --port            -p       Use the specified remote server port.\n"
		"  %s --url             -u       Use the specified remote server url.\n"
		"  %s --use-virtualhost -V       Use the specified virtualhost in GET query.\n"
		"  %s --hash            -H       Use the specified hash algorithm.\n"
		"  %s --verbose         -v       Use verbose mode output.\n"
		"  %s --help            -h       Display this short inlined help screen.\n"
		"  %s --release         -r       Display the release number\n",
		prog, prog, prog, prog, prog, prog, prog, prog, prog);
	fprintf(stderr, "\nSupported hash algorithms:\n");
	for (i = hash_first; i < hash_guard; i++)
		fprintf(stderr, "  %s%s\n",
			hashes[i].id, i == hash_default ? " (default)": "");
}

/* Command line parser */
static int
parse_cmdline(int argc, char **argv, REQ * req_obj)
{
	int c;
	enum feat_hashes i;

	struct option long_options[] = {
		{"release",         no_argument,       0, 'r'},
		{"help",            no_argument,       0, 'h'},
		{"verbose",         no_argument,       0, 'v'},
		{"use-ssl",         no_argument,       0, 'S'},
		{"server",          optional_argument, 0, 's'},
		{"hash",            optional_argument, 0, 'H'},
		{"use-virtualhost", optional_argument, 0, 'V'},
		{"port",            optional_argument, 0, 'p'},
		{"url",             optional_argument, 0, 'u'},
		{0, 0, 0, 0}
	};

	/* Parse the command line arguments */
	while ((c = getopt_long (argc, argv, "rhvSs:H:V:p:u:", long_options, NULL)) != EOF) {
		switch (c) {
		case 'r':
			fprintf(stderr, VERSION_STRING);
			break;
		case 'h':
			usage(argv[0]);
			break;
		case 'v':
			req_obj->verbose = 1;
			break;
		case 'S':
			req_obj->ssl = 1;
			break;
		case 's':
			if (!inet_ston(optarg, &req_obj->addr_ip)) {
				fprintf(stderr, "server should be an IP, not %s\n", optarg);
				return CMD_LINE_ERROR;
			}
			break;
		case 'H':
			for (i = hash_first; i < hash_guard; i++)
				if (!strcasecmp(optarg, hashes[i].id)) {
					req_obj->hash = i;
					break;
				}
			if (i == hash_guard) {
				fprintf(stderr, "unknown hash algoritm: %s\n", optarg);
				return CMD_LINE_ERROR;
			}
			break;
		case 'V':
			req_obj->vhost = optarg;
			break;
		case 'p':
			req_obj->addr_port = htons(atoi(optarg));
			break;
		case 'u':
			req_obj->url = optarg;
			break;
		default:
			usage(argv[0]);
			return CMD_LINE_ERROR;
		}
	}

	/* check unexpected arguments */
	if (optind < argc) {
		fprintf(stderr, "Unexpected argument(s): ");
		while (optind < argc)
			printf("%s ", argv[optind++]);
		printf("\n");
		return CMD_LINE_ERROR;
	}

	return CMD_LINE_SUCCESS;
}

int
main(int argc, char **argv)
{
	thread_t thread;

	/* Allocate the room */
	req = (REQ *) MALLOC(sizeof (REQ));

	/* Preset (potentially) non-zero defaults */
	req->hash = hash_default;

	/* Command line parser */
	if (!parse_cmdline(argc, argv, req)) {
		FREE(req);
		exit(0);
	}

	/* Check minimum configuration need */
	if (!req->addr_ip && !req->addr_port && !req->url) {
		FREE(req);
		exit(0);
	}

	/* Init the reference timer */
	req->ref_time = timer_tol(timer_now());
	DBG("Reference timer = %lu\n", req->ref_time);

	/* Init SSL context */
	init_ssl();

	/* Signal handling initialization  */
	signal_init();

	/* Create the master thread */
	master = thread_make_master();

	/* Register the GET request */
	init_sock();

	/*
	 * Processing the master thread queues,
	 * return and execute one ready thread.
	 * Run until error, used for debuging only.
	 * Note that not calling launch_scheduler() does
	 * not activate SIGCHLD handling, however, this
	 * is no issue here.
	 */
	while (thread_fetch(master, &thread))
		thread_call(&thread);

	/* Finalize output informations */
	if (req->verbose)
		printf("Global response time for [%s] =%lu\n",
		       req->url, req->response_time - req->ref_time);

	/* exit cleanly */
	SSL_CTX_free(req->ctx);
	free_sock(sock);
	FREE(req);
	exit(0);
}
