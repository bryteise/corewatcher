/*
 * Copyright 2007, Intel Corporation
 *
 * This file is part of corewatcher.org
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * Authors:
 *	Arjan van de Ven <arjan@linux.intel.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <syslog.h>
#include <getopt.h>
#include <sys/prctl.h>
#include <asm/unistd.h>

#include <curl/curl.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>



/*
 * Debian etch has an ancient glib2 library, work around
 */
#if !GLIB_CHECK_VERSION(2, 14, 0)
#define g_timeout_add_seconds(a, b, c) g_timeout_add((a)*1000, b, c)
#endif

#if defined(__i386__)
#  define __NR_ioprio_set 289
#elif defined(__x86_64__)
#  define __NR_ioprio_set 251
#elif defined(__arm__)
#  define __NR_ioprio_set 314
#elif defined (__powerpc__)
#  define __NR_ioprio_set 273
#else
#  error "Unsupported arch"
#endif
#define IOPRIO_WHO_PROCESS 1
#define IOPRIO_CLASS_RT 1
#define IOPRIO_CLASS_IDLE 3
#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_IDLE_LOWEST (7 |  (IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT))
#define IOPRIO_RT_LOWEST (7 |  (IOPRIO_CLASS_RT << IOPRIO_CLASS_SHIFT))

#include "corewatcher.h"


static struct option opts[] = {
	{ "nodaemon", 0, NULL, 'n' },
	{ "debug",    0, NULL, 'd' },
	{ "always",   0, NULL, 'a' },
	{ "test",     0, NULL, 't' },
	{ "help",     0, NULL, 'h' },
	{ 0, 0, NULL, 0 }
};


static DBusConnection *bus;

int pinged;
int testmode;

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
	fprintf(stderr, "  -n, --nodaemon  Do not daemonize, run in foreground\n");
	fprintf(stderr, "  -d, --debug     Enable debug mode\n");
	fprintf(stderr, "  -a, --always    Always send core dumps\n");
	fprintf(stderr, "  -t, --test      Do not send anything\n");
	fprintf(stderr, "  -h, --help      Display this help message\n");
}

static DBusHandlerResult got_message(
		DBusConnection __unused *conn,
		DBusMessage *message,
		void __unused *user_data)
{
	if (dbus_message_is_signal(message,
		"org.corewatcher.submit.ping", "ping")) {
		pinged = 1;
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_is_signal(message,
		"org.corewatcher.submit.permission", "yes")) {
		submit_queue();
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.corewatcher.submit.permission", "always")) {
		submit_queue();
		opted_in = 2;
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.corewatcher.submit.permission", "never")) {
		clear_queue();
		opted_in = 0;
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.corewatcher.submit.permission", "no")) {
		clear_queue();
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void dbus_ask_permission(char * detail_file_name)
{
	DBusMessage *message;
	if (!bus)
		return;
	message = dbus_message_new_signal("/org/corewatcher/submit/permission",
			"org.corewatcher.submit.permission", "ask");
	if (detail_file_name) {
		dbus_message_append_args(message,
			DBUS_TYPE_STRING, &detail_file_name,
			DBUS_TYPE_INVALID);
	}
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}

void dbus_say_thanks(char *url)
{
	DBusMessage *message;
	if (!bus)
		return;
	if (url && strlen(url)) {
		message = dbus_message_new_signal("/org/corewatcher/submit/url",
			"org.corewatcher.submit.url", "url");
		dbus_message_append_args (message, DBUS_TYPE_STRING, &url, DBUS_TYPE_INVALID);
		dbus_connection_send(bus, message, NULL);
		dbus_message_unref(message);
		syslog(LOG_WARNING, "corewatcher.org: oops is posted as %s", url);
	}

	message = dbus_message_new_signal("/org/corewatcher/submit/sent",
			"org.corewatcher.submit.sent", "sent");
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}

int main(int argc, char**argv)
{
	GMainLoop *loop;
	DBusError error;
	int godaemon = 1;
	int debug = 0;

/*
 * Signal the kernel that we're not timing critical
 */
#ifdef PR_SET_TIMERSLACK
	prctl(PR_SET_TIMERSLACK,1000*1000*1000, 0, 0, 0);
#endif
	/* Be easier on the rest of the system */
	if (syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS, pid,
		    IOPRIO_IDLE_LOWEST) == -1)
		perror("Can not set IO priority to lowest IDLE class");
	nice(15);

	read_config_file("/etc/corewatcher.conf");

	while (1) {
		int c;
		int i;

		c = getopt_long(argc, argv, "adnh", opts, &i);
		if (c == -1)
			break;

		switch(c) {
		case 'n':
			fprintf(stderr, "+ Not running as daemon\n");
			godaemon = 0;
			break;
		case 'd':
			fprintf(stderr, "+ Starting corewatcher in debug mode\n");
			debug = 1;
			break;
		case 'a':
			fprintf(stderr, "+ Sending All reports\n");
			opted_in = 2;
			break;
		case 't':
			fprintf(stderr, "+ Test mode enabled: not sending anything\n");
			break;
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		default:
			break;
		}
	}

	if (!opted_in && !testmode) {
		fprintf(stderr, "+ Inactive by user preference\n");
		return EXIT_SUCCESS;
	}

	/*
	 * the curl docs say that we "should" call curl_global_init early,
	 * even though it'll be called later on via curl_easy_init().
	 * We ignore this advice, since 99.99% of the time this program
	 * will not use http at all, but the curl code does consume
	 * memory.
	 */

/*
	curl_global_init(CURL_GLOBAL_ALL);
*/

	if (godaemon && daemon(0, 0)) {
		fprintf(stderr, "corewatcher failed to daemonize.. exiting \n");
		return EXIT_FAILURE;
	}
	sched_yield();

	loop = g_main_loop_new(NULL, FALSE);
	dbus_error_init(&error);
	bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (bus) {
		dbus_connection_setup_with_g_main(bus, NULL);
		dbus_bus_add_match(bus, "type='signal',interface='org.corewatcher.submit.ping'", &error);
		dbus_bus_add_match(bus, "type='signal',interface='org.corewatcher.submit.permission'", &error);
		dbus_connection_add_filter(bus, got_message, NULL, NULL);
	}

	if (!debug)
		sleep(20);

	/* we scan dmesg before /var/log/messages; dmesg is a more accurate source normally */
	scan_dmesg(NULL);
	/* during boot... don't go too fast and slow the system down */

	if (testmode) {
		g_main_loop_unref(loop);
		dbus_bus_remove_match(bus, "type='signal',interface='org.corewatcher.submit.ping'", &error);
		dbus_bus_remove_match(bus, "type='signal',interface='org.corewatcher.submit.permission'", &error);
		free(submit_url);
		fprintf(stderr, "+ Exiting from testmode\n");
		return EXIT_SUCCESS;
	}

	/* now, start polling for oopses to occur */

	g_timeout_add_seconds(10, scan_dmesg, NULL);

	g_main_loop_run(loop);
	dbus_bus_remove_match(bus, "type='signal',interface='org.corewatcher.submit.ping'", &error);
	dbus_bus_remove_match(bus, "type='signal',interface='org.corewatcher.submit.permission'", &error);

	g_main_loop_unref(loop);
	free(submit_url);

	return EXIT_SUCCESS;
}
