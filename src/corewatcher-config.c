#include <stdio.h>
#include <glib.h>

#define DEFAULT_FILE	"/etc/corewatcher.conf"
#define DEFAULT_GROUP	"corewatcher"

int parse_arg(gchar *arg, gchar **group, gchar **key, gchar **value)
{
	gchar **group_key_value = NULL;
	gchar **group_key = NULL;

	if (!arg)
		goto err;
	group_key_value = g_strsplit(arg, "=", -1);
	if (!group_key_value)
		goto err;
	switch (g_strv_length(group_key_value)) {
	case 2:
		*value = g_strdup(group_key_value[1]);
		/* intentional fall through */
	case 1:
		group_key = g_strsplit(group_key_value[0], ".", -1);
		if (!group_key)
			goto err;
		switch (g_strv_length(group_key)) {
		case 1:
			*key = g_strdup(group_key[0]);
			break;
		case 2:
			*group = g_strdup(group_key[0]);
			*key = g_strdup(group_key[1]);
			break;
		default:
			goto err;
		}
		break;
	default:
		goto err;
	}
	g_strfreev(group_key);
	g_strfreev(group_key_value);
	return 0;
err:
	g_free(*group); *group = NULL;
	g_free(*key); *key = NULL;
	g_free(*value); *value = NULL;
	g_strfreev(group_key);
	g_strfreev(group_key_value);
	return -1;
}

int do_set(GKeyFile *keyfile, gchar *arg)
{
	gchar *group = NULL;
	gchar *key = NULL;
	gchar *value = NULL;
	int ret = 0;

	parse_arg(arg, &group, &key, &value);
	if (!key) {
		ret = -1;
		goto out;
	}
	if (!group)
		group = g_strdup(DEFAULT_GROUP);
	if (!value) {
		fprintf(stderr, "missing value in set command\n");
		ret = -1;
		goto out;
	}
	g_key_file_set_value(keyfile, group, key, value);
out:
	g_free(group);
	g_free(key);
	g_free(value);
	return ret;
}

int do_get(GKeyFile *keyfile, gchar *arg)
{
	GError *error = NULL;
	gchar *group = NULL;
	gchar *key = NULL;
	gchar *value = NULL;
	int ret = 0;

	parse_arg(arg, &group, &key, &value);
	if (!key) {
		ret = -1;
		goto out;
	}
	if (!group)
		group = g_strdup(DEFAULT_GROUP);
	if (value) {
		fprintf(stderr, "ingoring value (%s) in get command\n", value);
		g_free(value);
	}
	value = g_key_file_get_value(keyfile, group, key, &error);
	if (error) {
		fprintf(stderr, "%s\n", error->message);
		g_clear_error(&error);
		goto out;
	}
	printf("%s\n", value);
out:
	g_free(group);
	g_free(key);
	g_free(value);
	return ret;
}

int do_get_all(GKeyFile *keyfile)
{
	gchar *group = NULL;
	gchar *key = NULL;
	gchar *value = NULL;
	gchar **groups;
	gchar **keys;
	gsize group_count;
	gsize key_count;
	gsize i, j;

	groups = g_key_file_get_groups(keyfile, &group_count);
	for (i = 0; i < group_count; i++) {
		group = groups[i];
		keys = g_key_file_get_keys(keyfile, group, &key_count, NULL);
		for (j = 0; j < key_count; j++) {
			key = keys[j];
			value = g_key_file_get_value(keyfile, group, key, NULL);
			printf("%s.%s=%s\n", group, key, value);
			g_free(value);
		}
		g_strfreev(keys);
	}
	g_strfreev(groups);
	return 0;
}

#define CMD_SET		(1<<0)
#define CMD_GET		(1<<1)
#define CMD_GET_ALL	(1<<2)

#define HAS_MULTIPLE_BITS(i) ((i) & ((i) -1))

/* global settings from command line argument parsing */
struct {
	gint cmd;
	gchar *file;
	gchar *arg;
} settings = { 0, DEFAULT_FILE, NULL };

#define __unused  __attribute__ ((__unused__))

gboolean set_cmd_option(const gchar *option_name, const gchar *value, gpointer __unused data, GError __unused **Error)
{
	if (!g_strcmp0(option_name, "--set")) {
		settings.cmd |= CMD_SET;
		settings.arg = g_strdup(value);
	}
	else if (!g_strcmp0(option_name, "--get")) {
		settings.cmd |= CMD_GET;
		settings.arg = g_strdup(value);
	}
	else if (!g_strcmp0(option_name, "--get-all")) {
		settings.cmd |= CMD_GET_ALL;
	}
	return TRUE;
}

static GOptionEntry entries[] = {
	{ "file", 'f', 0, G_OPTION_ARG_FILENAME, &settings.file, "file", NULL },
	{ "set", '\0', 0, G_OPTION_ARG_CALLBACK, &set_cmd_option, "set", NULL },
	{ "get", '\0', 0, G_OPTION_ARG_CALLBACK, &set_cmd_option, "get", NULL },
	{ "get-all", '\0', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, &set_cmd_option, "get-all", NULL },
	{ NULL, 0, 0, 0, NULL, NULL, NULL }
};

int main(int argc, char **argv)
{
	GError *error = NULL;
	GOptionContext *context;
	GKeyFile *keyfile;
	GKeyFileFlags flags;
	int ret = 0;

	context = g_option_context_new("");
	g_option_context_add_main_entries(context, entries, NULL);
	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		fprintf(stderr, "%s\n", error->message);
		return -1;
	}
	if (!settings.cmd) {
		fprintf(stderr, "%s", g_option_context_get_help(context, TRUE, NULL));
		return -1;
	}
	if (HAS_MULTIPLE_BITS(settings.cmd)) {
		fprintf(stderr, "only one command allowed at a time\n");
		fprintf(stderr, "%s", g_option_context_get_help(context, TRUE, NULL));
		return -1;
	}

	keyfile = g_key_file_new();
	flags = G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS;
	if (!g_key_file_load_from_file (keyfile, settings.file, flags, &error)) {
		fprintf(stderr, "%s\n", error->message);
		return -1;
	}
	switch(settings.cmd) {
	case CMD_SET:
		ret = do_set(keyfile, settings.arg);
		g_file_set_contents(settings.file, g_key_file_to_data(keyfile, NULL, NULL), -1, NULL);
		break;
	case CMD_GET:
		ret = do_get(keyfile, settings.arg);
		break;
	case CMD_GET_ALL:
		ret = do_get_all(keyfile);
		break;
	}
	g_key_file_free(keyfile);

	return ret;
}

