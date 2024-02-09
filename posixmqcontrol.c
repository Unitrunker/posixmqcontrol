/*-
 * Copyright (c) 2024 The FreeBSD Foundation
 *
 * This software was written by Rick Parrish <unitrunker@unitrunker.net>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <mqueue.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>
#include <sys/queue.h>
#include <limits.h>

typedef enum {false, true} bool;
#define nullptr NULL

struct Creation {
	// true if the queue exists.
	bool exists;
	// set_mode: true if a mode value was specified.
	bool set_mode;
	// mode: access mode with rwx permission bits.
	mode_t mode;
	// depth: maximum queue depth. default to an invalid depth.
	long depth;
	// size: maximum message size. default to an invalid size.
	long size;
	// block: true for blocking I/O and false for non-blocking I/O.
	bool block;
	// set_group: true if a group ID was specified.
	bool set_group;
	// group: group ID.
	unsigned group;
	// set_user: true if a user ID was specified.
	bool set_user;
	// user: user ID.
	unsigned user;
};

// linked list element - used by queues and contents below.
struct element {
    STAILQ_ENTRY(element) links;
    const char *text;
};
// linked list head(s) for queues and contents.
static STAILQ_HEAD(tqh, element)
	queues = STAILQ_HEAD_INITIALIZER(queues),
	contents = STAILQ_HEAD_INITIALIZER(contents);
// send defaults to medium priority.
static long priority = MQ_PRIO_MAX / 2;
static struct Creation creation = {
	.exists = false,
	.set_mode = false,
	.mode = 0755,
	.depth = -1,
	.size = -1,
	.block = true,
	.set_group = false,
	.group = 0,
	.set_user = false,
	.user = 0
};
static const mqd_t fail = (mqd_t)-1;

// OPTIONS parsing utilitarian

static void parse_long(const char *text, long *capture, const char *knob, const char *name) {
	char *cursor = nullptr;
	long value = strtol(text, &cursor, 10);
	if (cursor > text) {
		*capture = value;
	}
	else {
		fprintf(stderr, "error: %s %s invalid format [%s].\n", knob, name, text);
	}
}

static void parse_unsigned(const char *text, bool *set, unsigned *capture, const char *knob, const char *name) {
	char *cursor = nullptr;
	unsigned value = strtoul(text, &cursor, 8);
	if (cursor > text) {
		*set = true;
		*capture = value;
	}
	else {
		fprintf(stderr, "warning: %s %s format [%s] ignored.\n", knob, name, text);
	}
}

static bool sane_queue(const char *text) {
	int size = 0;
	const char * queue = text;
	if (*queue != '/') {
		fprintf(stderr, "error: queue name [%-*.0s] must start with '/'.\n", PATH_MAX, text);
		return false;
	}
	queue++;
	size++;
	while (*queue && size < PATH_MAX) {
		if (*queue == '/') {
			fprintf(stderr, "error: queue name [%-*.0s] - only one '/' permitted.\n", PATH_MAX, text);
			return false;
		}
		queue++;
		size++;
	}
	if (size == PATH_MAX && *queue) {
		fprintf(stderr, "error: queue name [%-*.0s...] may not be longer than %d.\n", PATH_MAX, text, PATH_MAX);
		return false;
	}
	return true;
}

// OPTIONS parsers

static void parse_block(const char *text) {
	if (strcmp(text, "true") == 0 || strcmp(text, "yes") == 0) {
		creation.block = true;
	}
	else if (strcmp(text, "false") == 0 || strcmp(text, "no") == 0) {
		creation.block = false;
	}
	else {
		char *cursor = nullptr;
		long value = strtol(text, &cursor, 10);
		if (cursor > text) {
			creation.block = value != 0;
		}
		else {
			fprintf(stderr, "warning: bad -b block format [%s] ignored.\n", text);
		}
	}
}

static void parse_content(const char *content) {
   	struct element *n1 = (struct element *)malloc(sizeof(struct element));
    n1->text = content;
    STAILQ_INSERT_TAIL(&contents, n1, links);
}

static void parse_depth(const char *text) {
	parse_long(text, &creation.depth, "-d", "depth");
}

static void parse_group(const char *text) {
	struct group* entry = getgrnam(text);
	if (entry == nullptr) {
		parse_unsigned(text, &creation.set_group, &creation.group, "-g", "group");
	}
	else {
		creation.set_group = true;
		creation.group = entry->gr_gid;
	}
}

static void parse_mode(const char *text)
{
	char *cursor = nullptr;
	long value = strtol(text, &cursor, 8);
	if (cursor > text && value > 0 && value < 010000) {
		creation.set_mode = true;
		creation.mode = (mode_t)value;
	}
	else {
		fprintf(stderr, "warning: impossible -m mode value [%s] ignored.\n", text);
	}
}

static void parse_priority(const char *text) {
	char *cursor = nullptr;
	long value = strtol(text, &cursor, 10);
	if (cursor > text) {
		if (value >= 0 && value < MQ_PRIO_MAX) {
			priority = value;
		}
		else {
			fprintf(stderr, "warning: bad -p priority range [%s] ignored.\n", text);			
		}
	}
	else {
		fprintf(stderr, "warning: bad -p priority format [%s] ignored.\n", text);
	}
}

static void parse_queue(const char *queue) {
	if (sane_queue(queue)) {
    	struct element *n1 = (struct element *)malloc(sizeof(struct element));
	    n1->text = queue;
	    STAILQ_INSERT_TAIL(&queues, n1, links);
    }
}

static void parse_single_queue(const char *queue) {
	if (sane_queue(queue)) {
		if (STAILQ_FIRST(&queues) == nullptr) {
	    	struct element *n1 = (struct element *)malloc(sizeof(struct element));
		    n1->text = queue;
		    STAILQ_INSERT_TAIL(&queues, n1, links);
	    }
		else fprintf(stderr, "warning: ignoring extra -q queue [%s].\n", queue);
	}
}

static void parse_size(const char *text) {
	parse_long(text, &creation.size, "-s", "size");
}

static void parse_user(const char *text) {
	struct passwd* entry = getpwnam(text);
	if (entry == nullptr) {
		parse_unsigned(text, &creation.set_user, &creation.user, "-u", "user");
	}
	else {
		creation.set_user = true;
		creation.user = entry->pw_uid;
	}
}

// OPTIONS validators
 
static bool validate_always_true(void) { return true; }

static bool validate_content(void) {
	bool valid = STAILQ_FIRST(&contents) != nullptr;
	if (!valid) fprintf(stderr, "error: no content to send.\n");
	return valid;
}

static bool validate_depth(void) {
	bool valid = creation.exists || creation.depth > 0;
	if (!valid) fprintf(stderr, "error: -d maximum queue depth not provided.\n");
	return valid;
}

static bool validate_mode(void) { return creation.mode > 0; }

static bool validate_queue(void) {
	bool valid = STAILQ_FIRST(&queues) != nullptr;
	if (!valid) fprintf(stderr, "error: missing -q, or no sane queue name given.\n");
	return valid;
}

static bool validate_single_queue(void) {
	bool valid = STAILQ_FIRST(&queues) != nullptr && STAILQ_NEXT(STAILQ_FIRST(&queues), links) == nullptr;
	if (!valid) fprintf(stderr, "error: expected one queue.\n");
	return valid;
}

static bool validate_size(void) {
	bool valid = creation.exists || creation.size > 0;
	if (!valid) fprintf(stderr, "error: -s maximum message size not provided.\n");
	return valid;
}

// OPTIONS table handling.

struct Option {
	// points to array of string pointers terminated by a null pointer.
	const char **pattern;
	// parse argument.
	void (*parse)(const char *);
	// displays an error and returns false if this parameter is not valid.
	// returns true otherwise.
	bool (*validate)(void);
};

// parse options by table.
// index - current index into argv list.
// argc, argv - command line parameters.
// options - null terminated list of pointers to options.
static void parse_options(int index, int argc, const char *argv[], const struct Option **options) {
	while ( (index + 1) < argc ) {
		const struct Option **cursor = options;
		bool match = false;
		while (*cursor != nullptr && !match) {
			const struct Option * option = cursor[0];
			const char **pattern = option->pattern;
			while (*pattern != nullptr && !match) {
				const char *knob = *pattern;
				match = strcmp(knob, argv[index]) == 0;
				if (!match) pattern++;
			}
			if (match) {
				option->parse(argv[index + 1]);
				index += 2;
				break;
			}
			cursor++;
		}
		if (!match && index < argc) {
			fprintf(stderr, "warning: skipping [%s].\n", argv[index]);
			index++;
		}
	}
	if (index < argc) {
		fprintf(stderr, "warning: skipping [%s].\n", argv[index]);
	}
}

// options - null terminated list of pointers to options.
static bool validate_options(const struct Option **options) {
	bool valid = true;
	while (*options != nullptr) {
		const struct Option *option = options[0];
		if (!option->validate())
			valid = false;
		options++;
	}
	return valid;
}

// SUBCOMMANDS

// queue: name of queue to be created.
// q_creation: creation parameters (copied by value).
static int create(const char *queue, struct Creation q_creation) {
	int flags = O_RDWR;
	struct mq_attr stuff = {0, q_creation.depth, q_creation.size, 0, {0}};
	if (!q_creation.block) {
		flags |= O_NONBLOCK;
		stuff.mq_flags |= O_NONBLOCK;
	}
	mqd_t handle = mq_open(queue, flags);
	q_creation.exists = handle != fail;
	if (!q_creation.exists) {
		// apply size and depth checks here.
		// if queue exists, we can default to existing depth and size.
		// but for a new queue, we require that input.
		if (validate_size() && validate_depth()) {
			// no need to re-apply mode.
			q_creation.set_mode = false;
			flags |= O_CREAT;
			handle = mq_open(queue, flags, q_creation.mode, &stuff);
		}
	}
	if (handle == fail) {
		errno_t what = errno;
		perror("mq_open(create)");
		return what;
	}
	
#if __BSD_VISIBLE
	// undocumented. See https://bugs.freebsd.org/bugzilla//show_bug.cgi?id=273230
	int fd = mq_getfd_np(handle);
	if (fd < 0) {
		errno_t what = errno;
		perror("mq_getfd_np(create)");
		mq_close(handle);
		return what;
	}
	struct stat status = {0};
	int result = fstat(fd, &status);
	if (result != 0) {
		errno_t what = errno;
		perror("fstat(create)");
		mq_close(handle);
		return what;
	}
	// do this only if group and / or user given.
	if (q_creation.set_group || q_creation.set_user) {
		q_creation.user = q_creation.set_user ? q_creation.user : status.st_uid;
		q_creation.group = q_creation.set_group ? q_creation.group : status.st_gid;
		result = fchown(fd, q_creation.user, q_creation.group);
		if (result != 0) {
			errno_t what = errno;
			perror("fchown(create)");
			mq_close(handle);
			return what;
		}
	}
	// do this only if altering mode of an existing queue.
	if (q_creation.exists && q_creation.set_mode && q_creation.mode != (status.st_mode & 0x06777)) {
		result = fchmod(fd, q_creation.mode);
		if (result != 0) {
			errno_t what = errno;
			perror("fchmod(create)");
			mq_close(handle);
			return what;
		}
	}
#endif
	
	return mq_close(handle);
}

// queue: name of queue to be removed.
static int rm(const char *queue) {
	int result = mq_unlink(queue);
	if (result != 0) {
		errno_t what = errno;
		perror("mq_unlink");
		return what;
	}
	return result;
}

// queue: name of queue to be inspected.
static int info(const char *queue) {
	mqd_t handle = mq_open(queue, O_RDONLY);
	if (handle == fail) {
		errno_t what = errno;
		perror("mq_open(info)");
		return what;
	}
	struct mq_attr actual = {0, 0, 0, 0, {0}};
	int result = mq_getattr(handle, &actual);
	if (result != 0) {
		errno_t what = errno;
		perror("mq_getattr(info)");
		return what;
	}
	fprintf(stdout, "queue: '%s'\nQSIZE: %lu\nMSGSIZE: %ld\nMAXMSG: %ld\nCURMSG: %ld\nflags: %03ld\n",
		queue, actual.mq_msgsize * actual.mq_curmsgs, actual.mq_msgsize, actual.mq_maxmsg, actual.mq_curmsgs, actual.mq_flags);
#if __BSD_VISIBLE
	int fd = mq_getfd_np(handle);
	struct stat status = {0};
	result = fstat(fd, &status);
	if (result != 0) {
		perror("fstat(info)");
	}
	else {
		fprintf(stdout, "UID: %u\nGID: %u\nMODE: %03o\n", status.st_uid, status.st_gid, status.st_mode);
	}
#endif
	return mq_close(handle);
}

// queue: name of queue to drain one message.
static int recv(const char *queue) {
	mqd_t handle = mq_open(queue, O_RDONLY);
	if (handle == fail) {
		errno_t what = errno;
		perror("mq_open(recv)");
		fprintf(stdout, "error %d\n", what);
		return what;
	}
	struct mq_attr actual = {0, 0, 0, 0, {0}};
	int result = mq_getattr(handle, &actual);
	if (result != 0) {
		errno_t what = errno;
		perror("mq_attr(recv)");
		mq_close(handle);
		return what;
	}
	char *text = (char *)malloc(actual.mq_msgsize + 1);
	memset(text, 0, actual.mq_msgsize + 1);
	unsigned priority = 0;
	result = mq_receive(handle, text, actual.mq_msgsize, &priority);
	if (result < 0) {
		errno_t what = errno;
		perror("mq_receive");
		mq_close(handle);
		return what;
	}

	fprintf(stdout, "[%u]: %-*.*s\n", priority, result, result, text);
	
	return mq_close(handle);
}

// queue: name of queue to send one message.
// text: message text.
// q_priority: message priority in range of 0 to 63.
static int send(const char *queue, const char *text, unsigned q_priority) {
	mqd_t handle = mq_open(queue, O_WRONLY);
	if (handle == fail) {
		errno_t what = errno;
		perror("mq_open(send)");
		return what;
	}
	struct mq_attr actual = {0, 0, 0, 0, {0}};
	int result = mq_getattr(handle, &actual);
	if (result != 0) {
		errno_t what = errno;
		perror("mq_attr(send)");
		mq_close(handle);
		return what;
	}
	int size = strlen(text);
	if (size > actual.mq_msgsize) {
		fprintf(stderr, "warning: truncating message to %ld characters.\n", actual.mq_msgsize);
		size = actual.mq_msgsize;
	}
	
	result = mq_send(handle, text, size, q_priority);
	if (result != 0) {
		errno_t what = errno;
		perror("mq_send");
		mq_close(handle);
		return what;
	}
	return mq_close(handle);
}

static void usage(FILE *file) {
	fprintf(file,
	    "usage:\n\tposixmqcontrol [rm|info|recv] -q <queue>\n"
	    "\tposixmqcontrol create -q <queue> -s <maxsize> -d <maxdepth> [ -m <mode> ] [ -b <block> ] [-u <uid> ] [ -g <gid> ]\n"
	    "\tposixmqcontrol send -q <queue> -c <content> [-p <priority> ]\n");
}

// end of SUBCOMMANDS

// OPTIONS tables

// careful: these 'names' arrays must be terminated by a null pointer.
static const char *names_queue[] = {"-q", "--queue", "-t", "--topic", nullptr};
static const struct Option option_queue = {names_queue, parse_queue, validate_queue};
static const struct Option option_single_queue = {names_queue, parse_single_queue, validate_single_queue};
static const char *names_depth[] = {"-d", "--depth", "--maxmsg", nullptr};
static const struct Option option_depth = {names_depth, parse_depth, validate_always_true};
static const char *names_size[] = {"-s", "--size", "--msgsize", nullptr};
static const struct Option option_size = {names_size, parse_size, validate_always_true};
static const char *names_block[] = {"-b", "--block", nullptr};
static const struct Option option_block = {names_block, parse_block, validate_always_true};
static const char *names_content[] = {"-c", "--content", "--data", "--message", nullptr};
static const struct Option option_content = {names_content, parse_content, validate_content};
static const char *names_priority[] = {"-p", "--priority", nullptr};
static const struct Option option_priority = {names_priority, parse_priority, validate_always_true};
static const char *names_mode[] = {"-m", "--mode", nullptr};
static const struct Option option_mode = {names_mode, parse_mode, validate_mode};
static const char *names_group[] = {"-g", "--gid", nullptr};
static const struct Option option_group = {names_group, parse_group, validate_always_true};
static const char *names_user[] = {"-u", "--uid", nullptr};
static const struct Option option_user = {names_user, parse_user, validate_always_true};

// careful: these arrays must be terminated by a null pointer.
#if __BSD_VISIBLE
static const struct Option *create_options[] = {&option_queue, &option_depth, &option_size, &option_block, &option_mode, &option_group, &option_user, nullptr};
#else
static const struct Option *create_options[] = {&option_queue, &option_depth, &option_size, &option_block, &option_mode, nullptr};
#endif
static const struct Option *info_options[] = {&option_queue, nullptr};
static const struct Option *unlink_options[] = {&option_queue, nullptr};
static const struct Option *recv_options[] = {&option_single_queue, nullptr};
static const struct Option *send_options[] = {&option_queue, &option_content, &option_priority, nullptr};

// 12 mode bits ugsrwxrwxrwx
// g = GID
// u = UID
// s = sticky bit (ignored)
// three rwx fields for owner, group, and world.
// r = read
// w = write
// x = execute

int main(int argc, const char *argv[]) {
    STAILQ_INIT(&queues);
    STAILQ_INIT(&contents);

	if (argc > 1) {
		const char *verb = argv[1];
		int index = 2;

		if ( strcmp("create", verb) == 0 || strcmp("attr", verb) == 0 ) {
			parse_options(index, argc, argv, create_options);
			if ( validate_options(create_options) ) {
				int worst = 0;
				struct element *it;
				STAILQ_FOREACH(it, &queues, links) {
					const char *queue = it->text;
					int result = create(queue, creation);
					if (result != 0)
						worst = result;
				}
				return worst;
			}
			return EINVAL;
		}
		else if ( strcmp("info", verb) == 0 || strcmp("cat", verb) == 0 ) {
			parse_options(index, argc, argv, info_options);
			if ( validate_options(info_options) ) {
				int worst = 0;
				struct element *it;
				STAILQ_FOREACH(it, &queues, links) {
					const char *queue = it->text;
					int result = info(queue);
					if (result != 0)
						worst = result;
				}
				return worst;
			}
			return EINVAL;
		}
		if ( strcmp("send", verb) == 0 ) {
			parse_options(index, argc, argv, send_options);
			if ( validate_options(send_options) ) {
				int worst = 0;
				struct element *itq;
				STAILQ_FOREACH(itq, &queues, links) {
					const char *queue = itq->text;
					struct element *itc;
					STAILQ_FOREACH(itc, &contents, links) {
						const char *content = itc->text;
						int result = send(queue, content, priority);
						if (result != 0)
							worst = result;
					}
				}
				return worst;
			}
			return EINVAL;
		}
		else if ( strcmp("recv", verb) == 0 || strcmp("receive", verb) == 0 ) {
			parse_options(index, argc, argv, recv_options);
			if ( validate_options(recv_options) ) {
				const char *queue = STAILQ_FIRST(&queues)->text;				
				return recv(queue);
			}
			return EINVAL;
		}
		else if ( strcmp("unlink", verb) == 0 || strcmp("rm", verb) == 0 ) {
			parse_options(index, argc, argv, unlink_options);
			if ( validate_options(unlink_options) ) {
				int worst = 0;
				struct element *it;
				STAILQ_FOREACH(it, &queues, links)
				{
					const char *queue = it->text;
					int result = rm(queue);
					if (result != 0)
						worst = result;
				}
				return worst;
			}
			return EINVAL;
		}
		else if (strcmp("help", verb) == 0 ) {
			usage(stdout);
			return 0;
		}
		else {
			fprintf(stderr, "error: Unknown verb [%s]\n", verb);
			return EINVAL;
		}
	}

	usage(stdout);
	return 0;
}
