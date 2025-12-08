/*	$NetBSD: devpubd.c,v 1.7 2021/06/21 03:14:12 christos Exp $	*/

/*-
 * Copyright (c) 2011 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2011-2015\
Jared D. McNeill <jmcneill@invisible.ca>. All rights reserved.");
__RCSID("$NetBSD: devpubd.c,v 1.7 2021/06/21 03:14:12 christos Exp $");

#include <sys/queue.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/drvctlio.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>

#include "ndevd.h"

#define LOG_BUFFER_SIZE 300
#define LOG_MSG_MAX     512

static int log_count = 0;
static int syslog_connected = 0;

static int drvctl_fd = -1;
static const char devpubd_script[] = DEVPUBD_RUN_HOOKS;
static int socket_fd = -1;
static unsigned int max_clients = 50;
static unsigned int num_clients = 0;
static unsigned int ndevd_stop = 0;

struct log_msg {
	int priority;
	char logmsg[LOG_MSG_MAX];
	TAILQ_ENTRY(log_msg) entries;
};

static TAILQ_HEAD(, log_msg) log_msgs;

struct devpubd_probe_event {
	char *device;
	TAILQ_ENTRY(devpubd_probe_event) entries;
};

static TAILQ_HEAD(, devpubd_probe_event) devpubd_probe_events;

struct client {
	int fd;
	TAILQ_ENTRY(client) entries;
};

static TAILQ_HEAD(, client) clients;

static void
close_ndevd(int a)
{
	ndevd_stop = 1;
}

// syslog wrapper
static void
syslog_w(int priority, const char *fmt, ...)
{
	va_list ap;
	struct log_msg *log;
	char msg[LOG_MSG_MAX];

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	if (!syslog_connected && access(_PATH_LOG, F_OK) == 0) {
		syslog_connected = 1;
		TAILQ_FOREACH(log, &log_msgs, entries) {
			syslog(log->priority, "%s", log->logmsg);
		}
	}
	
	if (syslog_connected) {
		syslog(priority, "%s", msg);
    } else {
		fprintf(stdin, "ndevd: %s\n", msg);
		if (log_count < LOG_BUFFER_SIZE) {
			struct log_msg *newlog = (struct log_msg *)calloc(1, sizeof(*newlog));
			strncpy(newlog->logmsg, msg, LOG_MSG_MAX - 1);
			newlog->logmsg[LOG_MSG_MAX - 1] = '\0';
			newlog->priority = priority;
			TAILQ_INSERT_TAIL(&log_msgs, newlog, entries);
			log_count++;
		}
	}
}

static int
create_socket(const char *name)
{
	int fd, slen;
	struct sockaddr_un sun;

	if ((fd = socket(PF_LOCAL, SOCK_SEQPACKET, 0)) < 0) {
		syslog_w(LOG_ERR, "socket error: '%s'", name);
		return -1;
	}
	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, name, sizeof(sun.sun_path));
	slen = SUN_LEN(&sun);
	unlink(name);
	if (bind(fd, (struct sockaddr *) & sun, slen) < 0) {
		syslog_w(LOG_ERR, "bind error: '%s'", name);
		return -1;
	}
	listen(fd, max_clients);
	if (chown(name, 0, 0)) {
		syslog_w(LOG_ERR, "chown error: '%s'", name);
		return -1;
	}
	if (chmod(name, 0666)) {
		syslog_w(LOG_ERR, "chmod error: '%s'", name);
		return -1;
	}
	syslog_w(LOG_INFO, "socket created");
	return (fd);
}

static void
notify_clients(const char *event, const char *device, const char *parent)
{
	struct client *cli, *tmp;
	struct ndevd_msg msg;
	int event_len, device_len, parent_len;
	int maxlen = sizeof(msg.event);
	int msglen = sizeof(msg);

	event_len = snprintf(msg.event, maxlen, "%s", event);
	device_len = snprintf(msg.device, maxlen, "%s", device);
	parent_len = snprintf(msg.parent, maxlen, "%s", parent);

	if (event_len < 0 || device_len < 0 || parent_len < 0 ||
		event_len >= maxlen || device_len >= maxlen || parent_len >= maxlen) {
		syslog_w(LOG_ERR, "notify_clients: message too long or encoding error");
		return;
	}

	TAILQ_FOREACH_SAFE(cli, &clients, entries, tmp) {
		if (send(cli->fd, &msg, msglen, MSG_EOR | MSG_NOSIGNAL) != msglen) {
			syslog_w(LOG_WARNING, "notification of (%d) failed (%s), dropped from the clients", cli->fd, strerror(errno));
			close(cli->fd);
			TAILQ_REMOVE(&clients, cli, entries);
			num_clients--;
		}
	}
}

static void
handle_clients(int reject)
{
	int client_fd = accept(socket_fd, NULL, NULL );

	if (client_fd == -1 ) {
		syslog_w(LOG_ERR, "accept failed (%s)", strerror(errno));
		return;
	}

	if (reject) {
		close(client_fd);
		return;
	}

	struct client *newcli = (struct client *)calloc(1, sizeof(*newcli));
	if (!newcli) {
		syslog_w(LOG_ERR, "calloc failed for new client");
		close(client_fd);
		return;
	}

	newcli->fd = client_fd;
	TAILQ_INSERT_TAIL(&clients, newcli, entries);
	num_clients++;
}

__dead static void
devpubd_exec(const char *path, char * const *argv)
{
	int error;

	error = execv(path, argv);
	if (error) {
		syslog_w(LOG_ERR, "couldn't exec '%s': %m", path);
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}

static void
devpubd_eventhandler(const char *event, const char **device)
{
	char **argv;
	pid_t pid;
	int status;
	size_t i, ndevs;

	for (ndevs = 0, i = 0; device[i] != NULL; i++) {
		++ndevs;
		syslog_w(LOG_DEBUG, "event = '%s', device = '%s'", event,
			device[i]);
	}

	argv = (char **)calloc(3 + ndevs, sizeof(*argv));
	argv[0] = (char *)__UNCONST(devpubd_script);
	argv[1] = (char *)__UNCONST(event);
	for (i = 0; i < ndevs; i++) {
		argv[2 + i] = (char *)__UNCONST(device[i]);
	}
	argv[2 + i] = NULL;

	pid = fork();
	switch (pid) {
	case -1:
		syslog_w(LOG_ERR, "fork failed: %m");
		break;
	case 0:
		devpubd_exec(devpubd_script, argv);
		/* NOTREACHED */
	default:
		if (waitpid(pid, &status, 0) == -1) {
			syslog_w(LOG_ERR, "waitpid(%d) failed: %m", pid);
			break;
		}
		if (WIFEXITED(status) && WEXITSTATUS(status)) {
			syslog_w(LOG_WARNING, "%s exited with status %d",
				devpubd_script, WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			syslog_w(LOG_WARNING, "%s received signal %d",
				devpubd_script, WTERMSIG(status));
		}
		break;
	}

	free(argv);
}

static void
devpubd_eventloop(void)
{
	const char *event, *device[2], *parent;
	prop_dictionary_t ev;
	int res, rv, a = 0;
	struct timeval tv;
	fd_set fds;
	int max_fd = 0;
	int reported = 0;
	int reject = 0;

	assert(drvctl_fd != -1);

	device[1] = NULL;

	while (!ndevd_stop) {
		if (socket_fd < 0 || (a < 1 && access(NDEVD_SOCKET, F_OK) < 0)) {
			if (socket_fd >= 0) {
				close(socket_fd);
				a++;
				syslog_w(LOG_WARNING, "%s recreate", NDEVD_SOCKET);
			}
			socket_fd = create_socket(NDEVD_SOCKET);
			max_fd = (drvctl_fd > socket_fd ? drvctl_fd : socket_fd) + 1;
		}
		tv.tv_sec = 60;
		tv.tv_usec = 0;
		FD_ZERO(&fds);
		FD_SET(drvctl_fd, &fds);
		FD_SET(socket_fd, &fds);
		rv = select(max_fd, &fds, NULL, NULL, &tv);
		if (rv == -1) {
			if (errno == EINTR)
				continue;
			err(EXIT_FAILURE, "select() failed");
		}
		if (FD_ISSET(drvctl_fd, &fds)) {
			res = prop_dictionary_recv_ioctl(drvctl_fd, DRVGETEVENT, &ev);
			if (res)
				err(EXIT_FAILURE, "DRVGETEVENT failed");
			prop_dictionary_get_string(ev, "event", &event);
			prop_dictionary_get_string(ev, "device", &device[0]);
			prop_dictionary_get_string(ev, "parent", &parent);

			syslog_w(LOG_INFO,"event='%s', device='%s', parent='%s'", event, device[0], parent);

			devpubd_eventhandler(event, device);

			notify_clients(event, device[0], parent);

			prop_object_release(ev);
		}
		if (FD_ISSET(socket_fd, &fds)) {
			if (num_clients >= max_clients) {
				if (!reported) {
					syslog_w(LOG_WARNING, "stop accepting, client/limit: %d/%d", num_clients, max_clients);
					reported = 1;
				}
				reject = 1;
			} else {
				if (reported) {
					syslog_w(LOG_DEBUG, "start accepting, client/limit: %d/%d", num_clients, max_clients);
					reported = 0;
				}
				reject = 0;
			}
			handle_clients(reject);
		}
	}

	struct client *cli, *tmp;
	TAILQ_FOREACH_SAFE(cli, &clients, entries, tmp) {
		close(cli->fd);
		TAILQ_REMOVE(&clients, cli, entries);
		free(cli);
	}
	close(socket_fd);
	unlink(NDEVD_SOCKET);
}

static void
devpubd_probe(const char *device)
{
	struct devlistargs laa;
	size_t len, children, n;
	void *p;
	int error;

	assert(drvctl_fd != -1);

	memset(&laa, 0, sizeof(laa));
	if (device)
		strlcpy(laa.l_devname, device, sizeof(laa.l_devname));

	/* Get the child device count for this device */
	error = ioctl(drvctl_fd, DRVLISTDEV, &laa);
	if (error) {
		syslog_w(LOG_ERR, "DRVLISTDEV failed: %m");
		return;
	}

child_count_changed:
	/* If this device has no children, return */
	if (laa.l_children == 0)
		return;

	/* Allocate a buffer large enough to hold the child device names */
	p = laa.l_childname;
	children = laa.l_children;

	len = children * sizeof(laa.l_childname[0]);
	laa.l_childname = (char (*)[16])realloc(laa.l_childname, len);
	if (laa.l_childname == NULL) {
		syslog_w(LOG_ERR, "couldn't allocate %zu bytes", len);
		laa.l_childname = (char (*)[16])p;
		goto done;
	}

	/* Get a list of child devices */
	error = ioctl(drvctl_fd, DRVLISTDEV, &laa);
	if (error) {
		syslog_w(LOG_ERR, "DRVLISTDEV failed: %m");
		goto done;
	}

	/* If the child count changed between DRVLISTDEV calls, retry */
	if (children != laa.l_children)
		goto child_count_changed;

	/*
	 * For each child device, queue an attach event and
	 * then scan each one for additional devices.
	 */
	for (n = 0; n < laa.l_children; n++) {
		struct devpubd_probe_event *ev = (struct devpubd_probe_event *)calloc(1, sizeof(*ev));
		ev->device = strdup(laa.l_childname[n]);
		TAILQ_INSERT_TAIL(&devpubd_probe_events, ev, entries);
	}
	for (n = 0; n < laa.l_children; n++)
		devpubd_probe(laa.l_childname[n]);

done:
	free(laa.l_childname);
	return;
}

static void
devpubd_init(void)
{
	struct devpubd_probe_event *ev;
	const char **devs;
	size_t ndevs, i;

	TAILQ_INIT(&log_msgs);

	TAILQ_INIT(&devpubd_probe_events);
	devpubd_probe(NULL);
	ndevs = 0;
	TAILQ_FOREACH(ev, &devpubd_probe_events, entries) {
		++ndevs;
	}
	devs = (const char **)calloc(ndevs + 1, sizeof(*devs));
	i = 0;
	TAILQ_FOREACH(ev, &devpubd_probe_events, entries) {
		devs[i++] = ev->device;
	}
	devpubd_eventhandler(NDEVD_ATTACH_EVENT, devs);
	free(devs);
	while ((ev = TAILQ_FIRST(&devpubd_probe_events)) != NULL) {
		TAILQ_REMOVE(&devpubd_probe_events, ev, entries);
		free(ev->device);
		free(ev);
	}

	TAILQ_INIT(&clients);
}

__dead static void
usage(void)
{
	fprintf(stderr, "usage: %s [-1f]\n", getprogname());
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	bool fflag = false;
	bool once = false;
	int ch;

	setprogname(argv[0]);

	while ((ch = getopt(argc, argv, "1fh")) != -1) {
		switch (ch) {
		case '1':
			fflag = true;
			once = true;
			break;
		case 'f':
			fflag = true;
			break;
		case 'h':
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc)
		usage();
		/* NOTREACHED */

	drvctl_fd = open(DRVCTLDEV, O_RDWR);
	if (drvctl_fd == -1)
		err(EXIT_FAILURE, "couldn't open " DRVCTLDEV);

	/* Look for devices that are already present */
	devpubd_init();

	if (!fflag) {
		if (daemon(0, 0) == -1) {
			err(EXIT_FAILURE, "couldn't fork");
		}
	}

	if (!once) {
		signal(SIGPIPE, SIG_IGN);
		signal(SIGHUP, close_ndevd);
		signal(SIGINT, close_ndevd);
		signal(SIGTERM, close_ndevd);
		devpubd_eventloop();
	}

	struct log_msg *msg, *mtmp;
	TAILQ_FOREACH_SAFE(msg, &log_msgs, entries, mtmp) {
		TAILQ_REMOVE(&log_msgs, msg, entries);
		free(msg);
	}

	close(drvctl_fd);

	return EXIT_SUCCESS;
}
