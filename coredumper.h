/*
 * Core dump watcher & collector
 *
 * (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License.
 */

#ifndef __INCLUDE_GUARD_COREDUMPER_H__
#define __INCLUDE_GUARD_COREDUMPER_H__

#include <glib.h>

#define __unused  __attribute__ ((__unused__))


extern char *find_executable(char *fragment);
extern char *find_coredump(char *corefile);
extern void dbus_say_thanks(void);
extern GList *coredumps;
extern void submit_queue(void);
extern void clear_queue(void);
extern void read_config_file(char *filename);


#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

extern DBusConnection *bus; 
extern DBusHandlerResult got_message(
                DBusConnection __unused *conn,
                DBusMessage *message,
                void __unused *user_data);

#endif
