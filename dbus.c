/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * Authors:
 * 	Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License.
 */

#define _BSD_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <glib.h>
#include <asm/unistd.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "coredumper.h"

DBusConnection *bus;

int pinged;
extern int opted_in;

DBusHandlerResult got_message(
		DBusConnection __unused *conn,
		DBusMessage *message,
		void __unused *user_data)
{
	if (dbus_message_is_signal(message,
		"org.moblin.coredump.ping", "ping")) {
		pinged = 1;
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_is_signal(message,
		"org.moblin.coredump.permission", "yes")) {
		submit_queue();
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.moblin.coredump.permission", "always")) {
		submit_queue();
		opted_in = 2;
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.moblin.coredump.permission", "never")) {
		clear_queue();
		opted_in = 0;
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.moblin.coredump.permission", "no")) {
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
	message = dbus_message_new_signal("/org/moblin/coredump/permission",
			"org.moblin.coredump.permission", "ask");
	if (detail_file_name) {
		dbus_message_append_args(message,
			DBUS_TYPE_STRING, &detail_file_name,
			DBUS_TYPE_INVALID);
	}
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}

void dbus_say_thanks(void)
{
	DBusMessage *message;
	if (!bus)
		return;

	message = dbus_message_new_signal("/org/moblin/coredump/sent",
			"org.moblin.coredump.sent", "sent");
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}

