/* notmuch - Not much of an email program, (just index and search)
 *
 * Copyright © 2009 Carl Worth
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/ .
 *
 * Author: Carl Worth <cworth@cworth.org>
 */

#include "notmuch-client.h"

#include <pwd.h>
#include <netdb.h>
#include <assert.h>

static const char toplevel_config_comment[] =
    " .notmuch-config - Configuration file for the notmuch mail system\n"
    "\n"
    " For more information about notmuch, see http://notmuchmail.org";

static const char database_config_comment[] =
    " Database configuration\n"
    "\n"
    " The only value supported here is 'path' which should be the top-level\n"
    " directory where your mail currently exists and to where mail will be\n"
    " delivered in the future. Files should be individual email messages.\n"
    " Notmuch will store its database within a sub-directory of the path\n"
    " configured here named \".notmuch\".\n";

static const char new_config_comment[] =
    " Configuration for \"notmuch new\"\n"
    "\n"
    " The following options are supported here:\n"
    "\n"
    "\ttags	A list (separated by ';') of the tags that will be\n"
    "\t	added to all messages incorporated by \"notmuch new\".\n"
    "\n"
    "\tignore	A list (separated by ';') of file and directory names\n"
    "\t	that will not be searched for messages by \"notmuch new\".\n"
    "\n"
    "\t	NOTE: *Every* file/directory that goes by one of those\n"
    "\t	names will be ignored, independent of its depth/location\n"
    "\t	in the mail store.\n";

static const char user_config_comment[] =
    " User configuration\n"
    "\n"
    " Here is where you can let notmuch know how you would like to be\n"
    " addressed. Valid settings are\n"
    "\n"
    "\tname		Your full name.\n"
    "\tprimary_email	Your primary email address.\n"
    "\tother_email	A list (separated by ';') of other email addresses\n"
    "\t		at which you receive email.\n"
    "\n"
    " Notmuch will use the various email addresses configured here when\n"
    " formatting replies. It will avoid including your own addresses in the\n"
    " recipient list of replies, and will set the From address based on the\n"
    " address to which the original email was addressed.\n";

static const char maildir_config_comment[] =
    " Maildir compatibility configuration\n"
    "\n"
    " The following option is supported here:\n"
    "\n"
    "\tsynchronize_flags      Valid values are true and false.\n"
    "\n"
    "\tIf true, then the following maildir flags (in message filenames)\n"
    "\twill be synchronized with the corresponding notmuch tags:\n"
    "\n"
    "\t\tFlag	Tag\n"
    "\t\t----	-------\n"
    "\t\tD	draft\n"
    "\t\tF	flagged\n"
    "\t\tP	passed\n"
    "\t\tR	replied\n"
    "\t\tS	unread (added when 'S' flag is not present)\n"
    "\n"
    "\tThe \"notmuch new\" command will notice flag changes in filenames\n"
    "\tand update tags, while the \"notmuch tag\" and \"notmuch restore\"\n"
    "\tcommands will notice tag changes and update flags in filenames\n";

static const char search_config_comment[] =
    " Search configuration\n"
    "\n"
    " The following option is supported here:\n"
    "\n"
    "\texclude_tags\n"
    "\t\tA ;-separated list of tags that will be excluded from\n"
    "\t\tsearch results by default.  Using an excluded tag in a\n"
    "\t\tquery will override that exclusion.\n";

struct _notmuch_config {
    char *filename;
    GKeyFile *key_file;
    notmuch_bool_t is_new;

    char *database_path;
    char *user_name;
    char *user_primary_email;
    const char **user_other_email;
    size_t user_other_email_length;
    const char **new_tags;
    size_t new_tags_length;
    const char **new_ignore;
    size_t new_ignore_length;
    notmuch_bool_t maildir_synchronize_flags;
    const char **search_exclude_tags;
    size_t search_exclude_tags_length;
};

static int
notmuch_config_destructor (notmuch_config_t *config)
{
    if (config->key_file)
	g_key_file_free (config->key_file);

    return 0;
}

static char *
get_name_from_passwd_file (void *ctx)
{
    long pw_buf_size;
    char *pw_buf;
    struct passwd passwd, *ignored;
    char *name;
    int e;

    pw_buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (pw_buf_size == -1) pw_buf_size = 64;
    pw_buf = talloc_size (ctx, pw_buf_size);

    while ((e = getpwuid_r (getuid (), &passwd, pw_buf,
                            pw_buf_size, &ignored)) == ERANGE) {
        pw_buf_size = pw_buf_size * 2;
        pw_buf = talloc_zero_size(ctx, pw_buf_size);
    }

    if (e == 0) {
	char *comma = strchr (passwd.pw_gecos, ',');
	if (comma)
	    name = talloc_strndup (ctx, passwd.pw_gecos,
				   comma - passwd.pw_gecos);
	else
	    name = talloc_strdup (ctx, passwd.pw_gecos);
    } else {
	name = talloc_strdup (ctx, "");
    }

    talloc_free (pw_buf);

    return name;
}

static char *
get_username_from_passwd_file (void *ctx)
{
    long pw_buf_size;
    char *pw_buf;
    struct passwd passwd, *ignored;
    char *name;
    int e;

    pw_buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (pw_buf_size == -1) pw_buf_size = 64;
    pw_buf = talloc_zero_size (ctx, pw_buf_size);

    while ((e = getpwuid_r (getuid (), &passwd, pw_buf,
                            pw_buf_size, &ignored)) == ERANGE) {
        pw_buf_size = pw_buf_size * 2;
        pw_buf = talloc_zero_size(ctx, pw_buf_size);
    }

    if (e == 0)
	name = talloc_strdup (ctx, passwd.pw_name);
    else
	name = talloc_strdup (ctx, "");

    talloc_free (pw_buf);

    return name;
}

/* Open the named notmuch configuration file. If the filename is NULL,
 * the value of the environment variable $NOTMUCH_CONFIG will be used.
 * If $NOTMUCH_CONFIG is unset, the default configuration file
 * ($HOME/.notmuch-config) will be used.
 *
 * If any error occurs, (out of memory, or a permission-denied error,
 * etc.), this function will print a message to stderr and return
 * NULL.
 *
 * FILE NOT FOUND: When the specified configuration file (whether from
 * 'filename' or the $NOTMUCH_CONFIG environment variable) does not
 * exist, the behavior of this function depends on the 'is_new_ret'
 * variable.
 *
 *	If is_new_ret is NULL, then a "file not found" message will be
 *	printed to stderr and NULL will be returned.

 *	If is_new_ret is non-NULL then a default configuration will be
 *	returned and *is_new_ret will be set to 1 on return so that
 *	the caller can recognize this case.
 *
 * 	These default configuration settings are determined as
 * 	follows:
 *
 *		database_path:		$HOME/mail
 *
 *		user_name:		From /etc/passwd
 *
 *		user_primary_mail: 	$EMAIL variable if set, otherwise
 *					constructed from the username and
 *					hostname of the current machine.
 *
 *		user_other_email:	Not set.
 *
 *	The default configuration also contains comments to guide the
 *	user in editing the file directly.
 */
notmuch_config_t *
notmuch_config_open (void *ctx,
		     const char *filename,
		     notmuch_bool_t create_new)
{
    GError *error = NULL;
    size_t tmp;
    char *notmuch_config_env = NULL;
    int file_had_database_group;
    int file_had_new_group;
    int file_had_user_group;
    int file_had_maildir_group;
    int file_had_search_group;

    notmuch_config_t *config = talloc (ctx, notmuch_config_t);
    if (config == NULL) {
	fprintf (stderr, "Out of memory.\n");
	return NULL;
    }
    
    talloc_set_destructor (config, notmuch_config_destructor);

    if (filename) {
	config->filename = talloc_strdup (config, filename);
    } else if ((notmuch_config_env = getenv ("NOTMUCH_CONFIG"))) {
	config->filename = talloc_strdup (config, notmuch_config_env);
    } else {
	config->filename = talloc_asprintf (config, "%s/.notmuch-config",
					    getenv ("HOME"));
    }

    config->key_file = g_key_file_new ();

    config->is_new = FALSE;
    config->database_path = NULL;
    config->user_name = NULL;
    config->user_primary_email = NULL;
    config->user_other_email = NULL;
    config->user_other_email_length = 0;
    config->new_tags = NULL;
    config->new_tags_length = 0;
    config->new_ignore = NULL;
    config->new_ignore_length = 0;
    config->maildir_synchronize_flags = TRUE;
    config->search_exclude_tags = NULL;
    config->search_exclude_tags_length = 0;

    if (! g_key_file_load_from_file (config->key_file,
				     config->filename,
				     G_KEY_FILE_KEEP_COMMENTS,
				     &error))
    {
	/* If create_new is true, then the caller is prepared for a
	 * default configuration file in the case of FILE NOT
	 * FOUND. Otherwise, any read failure is an error.
	 */
	if (create_new &&
	    error->domain == G_FILE_ERROR &&
	    error->code == G_FILE_ERROR_NOENT)
	{
	    g_error_free (error);
	    config->is_new = TRUE;
	}
	else
	{
	    fprintf (stderr, "Error reading configuration file %s: %s\n",
		     config->filename, error->message);
	    talloc_free (config);
	    g_error_free (error);
	    return NULL;
	}
    }

    /* Whenever we know of configuration sections that don't appear in
     * the configuration file, we add some comments to help the user
     * understand what can be done.
     *
     * It would be convenient to just add those comments now, but
     * apparently g_key_file will clear any comments when keys are
     * added later that create the groups. So we have to check for the
     * groups now, but add the comments only after setting all of our
     * values.
     */
    file_had_database_group = g_key_file_has_group (config->key_file,
						    "database");
    file_had_new_group = g_key_file_has_group (config->key_file, "new");
    file_had_user_group = g_key_file_has_group (config->key_file, "user");
    file_had_maildir_group = g_key_file_has_group (config->key_file, "maildir");
    file_had_search_group = g_key_file_has_group (config->key_file, "search");


    if (notmuch_config_get_database_path (config) == NULL) {
	char *path = talloc_asprintf (config, "%s/mail",
				      getenv ("HOME"));
	notmuch_config_set_database_path (config, path);
	talloc_free (path);
    }

    if (notmuch_config_get_user_name (config) == NULL) {
	char *name = get_name_from_passwd_file (config);
	notmuch_config_set_user_name (config, name);
	talloc_free (name);
    }

    if (notmuch_config_get_user_primary_email (config) == NULL) {
	char *email = getenv ("EMAIL");
	if (email) {
	    notmuch_config_set_user_primary_email (config, email);
	} else {
	    char hostname[256];
	    struct hostent *hostent;
	    const char *domainname;

	    char *username = get_username_from_passwd_file (config);

	    gethostname (hostname, 256);
	    hostname[255] = '\0';

	    hostent = gethostbyname (hostname);
	    if (hostent && (domainname = strchr (hostent->h_name, '.')))
		domainname += 1;
	    else
		domainname = "(none)";

	    email = talloc_asprintf (config, "%s@%s.%s",
				     username, hostname, domainname);

	    notmuch_config_set_user_primary_email (config, email);

	    talloc_free (username);
	    talloc_free (email);
	}
    }

    if (notmuch_config_get_new_tags (config, &tmp) == NULL) {
        const char *tags[] = { "unread", "inbox" };
	notmuch_config_set_new_tags (config, tags, 2);
    }

    if (notmuch_config_get_new_ignore (config, &tmp) == NULL) {
	notmuch_config_set_new_ignore (config, NULL, 0);
    }

    if (notmuch_config_get_search_exclude_tags (config, &tmp) == NULL) {
	if (config->is_new) {
	    const char *tags[] = { "deleted", "spam" };
	    notmuch_config_set_search_exclude_tags (config, tags, 2);
	} else {
	    notmuch_config_set_search_exclude_tags (config, NULL, 0);
	}
    }

    error = NULL;
    config->maildir_synchronize_flags =
	g_key_file_get_boolean (config->key_file,
				"maildir", "synchronize_flags", &error);
    if (error) {
	notmuch_config_set_maildir_synchronize_flags (config, TRUE);
	g_error_free (error);
    }

    /* Whenever we know of configuration sections that don't appear in
     * the configuration file, we add some comments to help the user
     * understand what can be done. */
    if (config->is_new)
	g_key_file_set_comment (config->key_file, NULL, NULL,
				toplevel_config_comment, NULL);

    if (! file_had_database_group)
	g_key_file_set_comment (config->key_file, "database", NULL,
				database_config_comment, NULL);

    if (! file_had_new_group)
	g_key_file_set_comment (config->key_file, "new", NULL,
				new_config_comment, NULL);

    if (! file_had_user_group)
	g_key_file_set_comment (config->key_file, "user", NULL,
				user_config_comment, NULL);

    if (! file_had_maildir_group)
	g_key_file_set_comment (config->key_file, "maildir", NULL,
				maildir_config_comment, NULL);

    if (! file_had_search_group)
	g_key_file_set_comment (config->key_file, "search", NULL,
				search_config_comment, NULL);

    return config;
}

/* Close the given notmuch_config_t object, freeing all resources.
 * 
 * Note: Any changes made to the configuration are *not* saved by this
 * function. To save changes, call notmuch_config_save before
 * notmuch_config_close.
*/
void
notmuch_config_close (notmuch_config_t *config)
{
    talloc_free (config);
}

/* Save any changes made to the notmuch configuration.
 *
 * Any comments originally in the file will be preserved.
 *
 * Returns 0 if successful, and 1 in case of any error, (after
 * printing a description of the error to stderr).
 */
int
notmuch_config_save (notmuch_config_t *config)
{
    size_t length;
    char *data;
    GError *error = NULL;

    data = g_key_file_to_data (config->key_file, &length, NULL);
    if (data == NULL) {
	fprintf (stderr, "Out of memory.\n");
	return 1;
    }

    if (! g_file_set_contents (config->filename, data, length, &error)) {
	fprintf (stderr, "Error saving configuration to %s: %s\n",
		 config->filename, error->message);
	g_error_free (error);
	g_free (data);
	return 1;
    }

    g_free (data);
    return 0;
}

notmuch_bool_t
notmuch_config_is_new (notmuch_config_t *config)
{
    return config->is_new;
}


static const char **
_config_get_list (notmuch_config_t *config,
		  const char *section, const char *key,
		  const char ***outlist, size_t *list_length, size_t *ret_length)
{
    assert(outlist);

    if (*outlist == NULL) {

	char **inlist = g_key_file_get_string_list (config->key_file,
					     section, key, list_length, NULL);
	if (inlist) {
	    unsigned int i;

	    *outlist = talloc_size (config, sizeof (char *) * (*list_length + 1));

	    for (i = 0; i < *list_length; i++)
		(*outlist)[i] = talloc_strdup (*outlist, inlist[i]);

	    (*outlist)[i] = NULL;

	    g_strfreev (inlist);
	}
    }

    if (ret_length)
	*ret_length = *list_length;

    return *outlist;
}

static void
_config_set_list (notmuch_config_t *config,
		  const char *group, const char *name,
		  const char *list[],
		  size_t length, const char ***config_var )
{
    g_key_file_set_string_list (config->key_file, group, name, list, length);
    talloc_free (*config_var);
    *config_var = NULL;
}

const char *
notmuch_config_get_database_path (notmuch_config_t *config)
{
    char *path;

    if (config->database_path == NULL) {
	path = g_key_file_get_string (config->key_file,
				      "database", "path", NULL);
	if (path) {
	    config->database_path = talloc_strdup (config, path);
	    free (path);
	}
    }

    return config->database_path;
}

void
notmuch_config_set_database_path (notmuch_config_t *config,
				  const char *database_path)
{
    g_key_file_set_string (config->key_file,
			   "database", "path", database_path);

    talloc_free (config->database_path);
    config->database_path = NULL;
}

const char *
notmuch_config_get_user_name (notmuch_config_t *config)
{
    char *name;

    if (config->user_name == NULL) {
	name = g_key_file_get_string (config->key_file,
				      "user", "name", NULL);
	if (name) {
	    config->user_name = talloc_strdup (config, name);
	    free (name);
	}
    }

    return config->user_name;
}

void
notmuch_config_set_user_name (notmuch_config_t *config,
			      const char *user_name)
{
    g_key_file_set_string (config->key_file,
			   "user", "name", user_name);

    talloc_free (config->user_name);
    config->user_name = NULL;
}

const char *
notmuch_config_get_user_primary_email (notmuch_config_t *config)
{
    char *email;

    if (config->user_primary_email == NULL) {
	email = g_key_file_get_string (config->key_file,
				       "user", "primary_email", NULL);
	if (email) {
	    config->user_primary_email = talloc_strdup (config, email);
	    free (email);
	}
    }

    return config->user_primary_email;
}

void
notmuch_config_set_user_primary_email (notmuch_config_t *config,
				       const char *primary_email)
{
    g_key_file_set_string (config->key_file,
			   "user", "primary_email", primary_email);

    talloc_free (config->user_primary_email);
    config->user_primary_email = NULL;
}

const char **
notmuch_config_get_user_other_email (notmuch_config_t *config,   size_t *length)
{
    return _config_get_list (config, "user", "other_email",
			     &(config->user_other_email),
			     &(config->user_other_email_length), length);
}

const char **
notmuch_config_get_new_tags (notmuch_config_t *config,   size_t *length)
{
    return _config_get_list (config, "new", "tags",
			     &(config->new_tags),
			     &(config->new_tags_length), length);
}

const char **
notmuch_config_get_new_ignore (notmuch_config_t *config, size_t *length)
{
    return _config_get_list (config, "new", "ignore",
			     &(config->new_ignore),
			     &(config->new_ignore_length), length);
}

void
notmuch_config_set_user_other_email (notmuch_config_t *config,
				     const char *list[],
				     size_t length)
{
    _config_set_list (config, "user", "other_email", list, length,
		     &(config->user_other_email));
}

void
notmuch_config_set_new_tags (notmuch_config_t *config,
				     const char *list[],
				     size_t length)
{
    _config_set_list (config, "new", "tags", list, length,
		     &(config->new_tags));
}

void
notmuch_config_set_new_ignore (notmuch_config_t *config,
			       const char *list[],
			       size_t length)
{
    _config_set_list (config, "new", "ignore", list, length,
		     &(config->new_ignore));
}

const char **
notmuch_config_get_search_exclude_tags (notmuch_config_t *config, size_t *length)
{
    return _config_get_list (config, "search", "exclude_tags",
			     &(config->search_exclude_tags),
			     &(config->search_exclude_tags_length), length);
}

void
notmuch_config_set_search_exclude_tags (notmuch_config_t *config,
				      const char *list[],
				      size_t length)
{
    _config_set_list (config, "search", "exclude_tags", list, length,
		      &(config->search_exclude_tags));
}

/* Given a configuration item of the form <group>.<key> return the
 * component group and key. If any error occurs, print a message on
 * stderr and return 1. Otherwise, return 0.
 *
 * Note: This function modifies the original 'item' string.
 */
static int
_item_split (char *item, char **group, char **key)
{
    char *period;

    *group = item;

    period = index (item, '.');
    if (period == NULL || *(period+1) == '\0') {
	fprintf (stderr,
		 "Invalid configuration name: %s\n"
		 "(Should be of the form <section>.<item>)\n", item);
	return 1;
    }

    *period = '\0';
    *key = period + 1;

    return 0;
}

static int
notmuch_config_command_get (notmuch_config_t *config, char *item)
{
    if (strcmp(item, "database.path") == 0) {
	printf ("%s\n", notmuch_config_get_database_path (config));
    } else if (strcmp(item, "user.name") == 0) {
	printf ("%s\n", notmuch_config_get_user_name (config));
    } else if (strcmp(item, "user.primary_email") == 0) {
	printf ("%s\n", notmuch_config_get_user_primary_email (config));
    } else if (strcmp(item, "user.other_email") == 0) {
	const char **other_email;
	size_t i, length;
	
	other_email = notmuch_config_get_user_other_email (config, &length);
	for (i = 0; i < length; i++)
	    printf ("%s\n", other_email[i]);
    } else if (strcmp(item, "new.tags") == 0) {
	const char **tags;
	size_t i, length;

	tags = notmuch_config_get_new_tags (config, &length);
	for (i = 0; i < length; i++)
	    printf ("%s\n", tags[i]);
    } else {
	char **value;
	size_t i, length;
	char *group, *key;

	if (_item_split (item, &group, &key))
	    return 1;

	value = g_key_file_get_string_list (config->key_file,
					    group, key,
					    &length, NULL);
	if (value == NULL) {
	    fprintf (stderr, "Unknown configuration item: %s.%s\n",
		     group, key);
	    return 1;
	}

	for (i = 0; i < length; i++)
	    printf ("%s\n", value[i]);

	g_strfreev (value);
    }

    return 0;
}

static int
notmuch_config_command_set (notmuch_config_t *config, char *item, int argc, char *argv[])
{
    char *group, *key;

    if (_item_split (item, &group, &key))
	return 1;

    /* With only the name of an item, we clear it from the
     * configuration file.
     *
     * With a single value, we set it as a string.
     *
     * With multiple values, we set them as a string list.
     */
    switch (argc) {
    case 0:
	g_key_file_remove_key (config->key_file, group, key, NULL);
	break;
    case 1:
	g_key_file_set_string (config->key_file, group, key, argv[0]);
	break;
    default:
	g_key_file_set_string_list (config->key_file, group, key,
				    (const gchar **) argv, argc);
	break;
    }

    return notmuch_config_save (config);
}

static int
notmuch_config_command_list (notmuch_config_t *config)
{
    char **groups;
    size_t g, groups_length;

    groups = g_key_file_get_groups (config->key_file, &groups_length);
    if (groups == NULL)
	return 1;

    for (g = 0; g < groups_length; g++) {
	char **keys;
	size_t k, keys_length;

	keys = g_key_file_get_keys (config->key_file,
				    groups[g], &keys_length, NULL);
	if (keys == NULL)
	    continue;

	for (k = 0; k < keys_length; k++) {
	    char *value;

	    value = g_key_file_get_string (config->key_file,
					   groups[g], keys[k], NULL);
	    if (value != NULL) {
		printf ("%s.%s=%s\n", groups[g], keys[k], value);
		free (value);
	    }
	}

	g_strfreev (keys);
    }

    g_strfreev (groups);

    return 0;
}

int
notmuch_config_command (notmuch_config_t *config, int argc, char *argv[])
{
    argc--; argv++; /* skip subcommand argument */

    if (argc < 1) {
	fprintf (stderr, "Error: notmuch config requires at least one argument.\n");
	return 1;
    }

    if (strcmp (argv[0], "get") == 0) {
	if (argc != 2) {
	    fprintf (stderr, "Error: notmuch config get requires exactly "
		     "one argument.\n");
	    return 1;
	}
	return notmuch_config_command_get (config, argv[1]);
    } else if (strcmp (argv[0], "set") == 0) {
	if (argc < 2) {
	    fprintf (stderr, "Error: notmuch config set requires at least "
		     "one argument.\n");
	    return 1;
	}
	return notmuch_config_command_set (config, argv[1], argc - 2, argv + 2);
    } else if (strcmp (argv[0], "list") == 0) {
	return notmuch_config_command_list (config);
    }

    fprintf (stderr, "Unrecognized argument for notmuch config: %s\n",
	     argv[0]);
    return 1;
}

notmuch_bool_t
notmuch_config_get_maildir_synchronize_flags (notmuch_config_t *config)
{
    return config->maildir_synchronize_flags;
}

void
notmuch_config_set_maildir_synchronize_flags (notmuch_config_t *config,
					      notmuch_bool_t synchronize_flags)
{
    g_key_file_set_boolean (config->key_file,
			    "maildir", "synchronize_flags", synchronize_flags);
    config->maildir_synchronize_flags = synchronize_flags;
}
