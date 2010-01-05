/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 *  Copyright (C) 2008-2009  Kouhei Sutou <kou@clear-code.com>
 *
 *  This library is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "../../config.h"
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "milter-manager-module.h"
#include "milter-manager-configuration.h"
#include "milter-manager-children.h"
#include <milter/core/milter-marshalers.h>

#define DEFAULT_MAINTENANCE_INTERVAL 100

#define MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                                 \
                                 MILTER_TYPE_MANAGER_CONFIGURATION,     \
                                 MilterManagerConfigurationPrivate))

typedef struct _MilterManagerConfigurationPrivate MilterManagerConfigurationPrivate;
struct _MilterManagerConfigurationPrivate
{
    GList *load_paths;
    GList *eggs;
    GList *applicable_conditions;
    gboolean privilege_mode;
    gchar *controller_connection_spec;
    gchar *manager_connection_spec;
    MilterStatus fallback_status;
    gchar *package_platform;
    gchar *package_options;
    gchar *effective_user;
    gchar *effective_group;
    guint manager_unix_socket_mode;
    guint controller_unix_socket_mode;
    gchar *manager_unix_socket_group;
    gchar *controller_unix_socket_group;
    gboolean remove_manager_unix_socket_on_close;
    gboolean remove_controller_unix_socket_on_close;
    gboolean remove_manager_unix_socket_on_create;
    gboolean remove_controller_unix_socket_on_create;
    gboolean daemon;
    gchar *pid_file;
    guint maintenance_interval;
    guint suspend_time_on_unacceptable;
    guint max_connections;
    guint max_file_descriptors;
    guint processed_sessions;
    gchar *custom_configuration_directory;
    GHashTable *locations;
};

enum
{
    PROP_0,
    PROP_PRIVILEGE_MODE,
    PROP_CONTROLLER_CONNECTION_SPEC,
    PROP_MANAGER_CONNECTION_SPEC,
    PROP_FALLBACK_STATUS,
    PROP_PACKAGE_PLATFORM,
    PROP_PACKAGE_OPTIONS,
    PROP_EFFECTIVE_USER,
    PROP_EFFECTIVE_GROUP,
    PROP_MANAGER_UNIX_SOCKET_MODE,
    PROP_CONTROLLER_UNIX_SOCKET_MODE,
    PROP_MANAGER_UNIX_SOCKET_GROUP,
    PROP_CONTROLLER_UNIX_SOCKET_GROUP,
    PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CLOSE,
    PROP_REMOVE_CONTROLLER_UNIX_SOCKET_ON_CLOSE,
    PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CREATE,
    PROP_REMOVE_CONTROLLER_UNIX_SOCKET_ON_CREATE,
    PROP_DAEMON,
    PROP_PID_FILE,
    PROP_MAINTENANCE_INTERVAL,
    PROP_SUSPEND_TIME_ON_UNACCEPTABLE,
    PROP_MAX_CONNECTIONS,
    PROP_MAX_FILE_DESCRIPTORS,
    PROP_PROCESSED_SESSIONS,
    PROP_CUSTOM_CONFIGURATION_DIRECTORY
};

enum
{
    TO_XML,
    LAST_SIGNAL,
};

static gint signals[LAST_SIGNAL] = {0};

static GList *configurations = NULL;
static gchar *configuration_module_dir = NULL;

G_DEFINE_TYPE(MilterManagerConfiguration, milter_manager_configuration,
              G_TYPE_OBJECT);

static void dispose        (GObject         *object);
static void set_property   (GObject         *object,
                            guint            prop_id,
                            const GValue    *value,
                            GParamSpec      *pspec);
static void get_property   (GObject         *object,
                            guint            prop_id,
                            GValue          *value,
                            GParamSpec      *pspec);

static void
milter_manager_configuration_class_init (MilterManagerConfigurationClass *klass)
{
    GObjectClass *gobject_class;
    GParamSpec *spec;

    gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose      = dispose;
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    spec = g_param_spec_boolean("privilege-mode",
                                "Privilege mode",
                                "Whether MilterManager runs on privilege mode",
                                FALSE,
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_PRIVILEGE_MODE, spec);

    spec = g_param_spec_string("controller-connection-spec",
                               "Controller connection spec",
                               "The controller connection spec "
                               "of the milter-manager",
                               NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_CONTROLLER_CONNECTION_SPEC,
                                    spec);

    spec = g_param_spec_string("manager-connection-spec",
                               "Manager connection spec",
                               "The manager connection spec "
                               "of the milter-manager",
                               MILTER_MANAGER_DEFAULT_CONNECTION_SPEC,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_MANAGER_CONNECTION_SPEC,
                                    spec);

    spec = g_param_spec_enum("fallback-status",
                             "Fallback status",
                             "The fallback status of the milter-manager",
                             MILTER_TYPE_STATUS,
                             MILTER_STATUS_ACCEPT,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_FALLBACK_STATUS, spec);

    spec = g_param_spec_string("package-platform",
                               "Package platform",
                               "The package platform of the milter-manager",
                               MILTER_MANAGER_PACKAGE_PLATFORM,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_PACKAGE_PLATFORM,
                                    spec);

    spec = g_param_spec_string("package-options",
                               "Package options",
                               "The package options of the milter-manager",
                               MILTER_MANAGER_PACKAGE_OPTIONS,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_PACKAGE_OPTIONS,
                                    spec);

    spec = g_param_spec_string("effective-user",
                               "Effective user name",
                               "The effective user name of the milter-manager",
                               MILTER_MANAGER_DEFAULT_EFFECTIVE_USER,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_EFFECTIVE_USER,
                                    spec);

    spec = g_param_spec_string("effective-group",
                               "Effective group name",
                               "The effective group name of the milter-manager",
                               MILTER_MANAGER_DEFAULT_EFFECTIVE_GROUP,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_EFFECTIVE_GROUP,
                                    spec);

    spec = g_param_spec_uint("manager-unix-socket-mode",
                             "milter-manager's UNIX socket mode",
                             "The milter-manager's UNIX socket mode",
                             0000,
                             0777,
                             0660,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_MANAGER_UNIX_SOCKET_MODE,
                                    spec);

    spec = g_param_spec_uint("controller-unix-socket-mode",
                             "milter-manager controller's UNIX socket mode",
                             "The milter-manager controller's UNIX socket mode",
                             0000,
                             0777,
                             0660,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_CONTROLLER_UNIX_SOCKET_MODE,
                                    spec);

    spec = g_param_spec_string("manager-unix-socket-group",
                               "milter-manager's UNIX socket group",
                               "The milter-manager's UNIX socket group",
                               MILTER_MANAGER_DEFAULT_SOCKET_GROUP,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_MANAGER_UNIX_SOCKET_GROUP,
                                    spec);

    spec = g_param_spec_string("controller-unix-socket-group",
                               "milter-manager's UNIX socket group",
                               "The milter-manager controller's UNIX socket group",
                               NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_CONTROLLER_UNIX_SOCKET_GROUP,
                                    spec);

    spec = g_param_spec_boolean("remove-manager-unix-socket-on-close",
                                "Remove unix socket for manager on close",
                                "Whether milter-manager removes "
                                "UNIX socket used for manager on close",
                                TRUE,
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CLOSE,
                                    spec);

    spec = g_param_spec_boolean("remove-controller-unix-socket-on-close",
                                "Remove unix socket for controller on close",
                                "Whether milter-manager removes "
                                "UNIX socket used for controller on close",
                                TRUE,
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CLOSE,
                                    spec);

    spec = g_param_spec_boolean("remove-manager-unix-socket-on-create",
                                "Remove unix socket for manager on create",
                                "Whether milter-manager removes "
                                "UNIX socket used for manager on create",
                                TRUE,
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CREATE,
                                    spec);

    spec = g_param_spec_boolean("remove-controller-unix-socket-on-create",
                                "Remove unix socket for controller on create",
                                "Whether milter-manager removes "
                                "UNIX socket used for controller on create",
                                TRUE,
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CREATE,
                                    spec);

    spec = g_param_spec_boolean("daemon",
                                "Run as daemon process",
                                "Whether milter-manager runs as daemon process",
                                FALSE,
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_DAEMON, spec);

    spec = g_param_spec_string("pid-file",
                               "File name to be saved process ID",
                               "The file name to be saved process ID",
                               MILTER_MANAGER_DEFAULT_PID_FILE,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class, PROP_PID_FILE,
                                    spec);

    spec = g_param_spec_uint("maintenance-interval",
                             "Maintenance interval in sessions",
                             "Run maintenance process in after each N sessions. "
                             "0 means that periodical maintenance "
                             "process isn't run.",
                             0,
                             G_MAXUINT,
                             DEFAULT_MAINTENANCE_INTERVAL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_MAINTENANCE_INTERVAL,
                                    spec);

    spec = g_param_spec_uint("suspend-time-on-unacceptable",
                             "Suspend time in sessions",
                             "Suspend time on unacceptable.",
                             0,
                             G_MAXUINT,
                             MILTER_CLIENT_DEFAULT_SUSPEND_TIME_ON_UNACCEPTABLE,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_SUSPEND_TIME_ON_UNACCEPTABLE,
                                    spec);

    spec = g_param_spec_uint("max-connections",
                             "Max connections",
                             "Max number of concurrent connections.",
                             0,
                             G_MAXUINT,
                             MILTER_CLIENT_DEFAULT_MAX_CONNECTIONS,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_MAX_CONNECTIONS,
                                    spec);

    spec = g_param_spec_uint("max-file-descriptors",
                             "Max file descriptors",
                             "Max number of concurrent file descriptors.",
                             0,
                             G_MAXUINT,
                             0,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_MAX_FILE_DESCRIPTORS,
                                    spec);

    spec = g_param_spec_string("custom-configuration-directory",
                               "Directory for custom configuration",
                               "The directory for custom configuration",
                               NULL,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
    g_object_class_install_property(gobject_class,
                                    PROP_CUSTOM_CONFIGURATION_DIRECTORY,
                                    spec);


    signals[TO_XML] =
        g_signal_new("to-xml",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(MilterManagerConfigurationClass, to_xml),
                     NULL, NULL,
                     _milter_marshal_VOID__POINTER_UINT,
                     G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);

    g_type_class_add_private(gobject_class,
                             sizeof(MilterManagerConfigurationPrivate));
}

static void
milter_manager_configuration_init (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;
    const gchar *config_dir_env;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->load_paths = NULL;
    priv->eggs = NULL;
    priv->applicable_conditions = NULL;
    priv->controller_connection_spec = NULL;
    priv->manager_connection_spec = NULL;
    priv->effective_user = NULL;
    priv->effective_group = NULL;
    priv->pid_file = NULL;
    priv->maintenance_interval = DEFAULT_MAINTENANCE_INTERVAL;
    priv->suspend_time_on_unacceptable =
        MILTER_CLIENT_DEFAULT_SUSPEND_TIME_ON_UNACCEPTABLE;
    priv->max_connections =
        MILTER_CLIENT_DEFAULT_MAX_CONNECTIONS;
    priv->max_file_descriptors = 0;
    priv->processed_sessions = 0;
    priv->custom_configuration_directory = NULL;
    priv->locations = g_hash_table_new_full(g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            (GDestroyNotify)g_dataset_destroy);

    config_dir_env = g_getenv("MILTER_MANAGER_CONFIG_DIR");
    if (config_dir_env)
        priv->load_paths = g_list_append(priv->load_paths,
                                         g_strdup(config_dir_env));
    priv->load_paths = g_list_append(priv->load_paths, g_strdup(CONFIG_DIR));


    milter_manager_configuration_clear(configuration);
}

static void
dispose (GObject *object)
{
    MilterManagerConfiguration *configuration;
    MilterManagerConfigurationPrivate *priv;

    configuration = MILTER_MANAGER_CONFIGURATION(object);
    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    milter_manager_configuration_clear_load_paths(configuration);
    milter_manager_configuration_clear(configuration);

    if (priv->manager_connection_spec) {
        g_free(priv->manager_connection_spec);
        priv->manager_connection_spec = NULL;
    }

    if (priv->locations) {
        g_hash_table_unref(priv->locations);
        priv->locations = NULL;
    }

    G_OBJECT_CLASS(milter_manager_configuration_parent_class)->dispose(object);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MilterManagerConfiguration *config;

    config = MILTER_MANAGER_CONFIGURATION(object);
    switch (prop_id) {
    case PROP_PRIVILEGE_MODE:
        milter_manager_configuration_set_privilege_mode(
            config, g_value_get_boolean(value));
        break;
    case PROP_CONTROLLER_CONNECTION_SPEC:
        milter_manager_configuration_set_controller_connection_spec(
            config, g_value_get_string(value));
        break;
    case PROP_MANAGER_CONNECTION_SPEC:
        milter_manager_configuration_set_manager_connection_spec(
            config, g_value_get_string(value));
        break;
    case PROP_FALLBACK_STATUS:
        milter_manager_configuration_set_fallback_status(
            config, g_value_get_enum(value));
        break;
    case PROP_PACKAGE_PLATFORM:
        milter_manager_configuration_set_package_platform(
            config, g_value_get_string(value));
        break;
    case PROP_PACKAGE_OPTIONS:
        milter_manager_configuration_set_package_options(
            config, g_value_get_string(value));
        break;
    case PROP_EFFECTIVE_USER:
        milter_manager_configuration_set_effective_user(
            config, g_value_get_string(value));
        break;
    case PROP_EFFECTIVE_GROUP:
        milter_manager_configuration_set_effective_group(
            config, g_value_get_string(value));
        break;
    case PROP_MANAGER_UNIX_SOCKET_MODE:
        milter_manager_configuration_set_manager_unix_socket_mode(
            config, g_value_get_uint(value));
        break;
    case PROP_CONTROLLER_UNIX_SOCKET_MODE:
        milter_manager_configuration_set_controller_unix_socket_mode(
            config, g_value_get_uint(value));
        break;
    case PROP_MANAGER_UNIX_SOCKET_GROUP:
        milter_manager_configuration_set_manager_unix_socket_group(
            config, g_value_get_string(value));
        break;
    case PROP_CONTROLLER_UNIX_SOCKET_GROUP:
        milter_manager_configuration_set_controller_unix_socket_group(
            config, g_value_get_string(value));
        break;
    case PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CLOSE:
        milter_manager_configuration_set_remove_manager_unix_socket_on_close(
            config, g_value_get_boolean(value));
        break;
    case PROP_REMOVE_CONTROLLER_UNIX_SOCKET_ON_CLOSE:
        milter_manager_configuration_set_remove_controller_unix_socket_on_close(
            config, g_value_get_boolean(value));
        break;
    case PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CREATE:
        milter_manager_configuration_set_remove_manager_unix_socket_on_create(
            config, g_value_get_boolean(value));
        break;
    case PROP_REMOVE_CONTROLLER_UNIX_SOCKET_ON_CREATE:
        milter_manager_configuration_set_remove_controller_unix_socket_on_create(
            config, g_value_get_boolean(value));
        break;
    case PROP_DAEMON:
        milter_manager_configuration_set_daemon(
            config, g_value_get_boolean(value));
        break;
    case PROP_PID_FILE:
        milter_manager_configuration_set_pid_file(
            config, g_value_get_string(value));
        break;
    case PROP_MAINTENANCE_INTERVAL:
        milter_manager_configuration_set_maintenance_interval(
            config, g_value_get_uint(value));
        break;
    case PROP_SUSPEND_TIME_ON_UNACCEPTABLE:
        milter_manager_configuration_set_suspend_time_on_unacceptable(
            config, g_value_get_uint(value));
        break;
    case PROP_MAX_CONNECTIONS:
        milter_manager_configuration_set_max_connections(
            config, g_value_get_uint(value));
        break;
    case PROP_MAX_FILE_DESCRIPTORS:
        milter_manager_configuration_set_max_file_descriptors(
            config, g_value_get_uint(value));
        break;
    case PROP_CUSTOM_CONFIGURATION_DIRECTORY:
        milter_manager_configuration_set_custom_configuration_directory(
            config, g_value_get_string(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(object);
    switch (prop_id) {
    case PROP_PRIVILEGE_MODE:
        g_value_set_boolean(value, priv->privilege_mode);
        break;
    case PROP_CONTROLLER_CONNECTION_SPEC:
        g_value_set_string(value, priv->controller_connection_spec);
        break;
    case PROP_MANAGER_CONNECTION_SPEC:
        g_value_set_string(value, priv->manager_connection_spec);
        break;
    case PROP_FALLBACK_STATUS:
        g_value_set_enum(value, priv->fallback_status);
        break;
    case PROP_PACKAGE_PLATFORM:
        g_value_set_string(value, priv->package_platform);
        break;
    case PROP_PACKAGE_OPTIONS:
        g_value_set_string(value, priv->package_options);
        break;
    case PROP_EFFECTIVE_USER:
        g_value_set_string(value, priv->effective_user);
        break;
    case PROP_EFFECTIVE_GROUP:
        g_value_set_string(value, priv->effective_group);
        break;
    case PROP_MANAGER_UNIX_SOCKET_MODE:
        g_value_set_uint(value, priv->manager_unix_socket_mode);
        break;
    case PROP_CONTROLLER_UNIX_SOCKET_MODE:
        g_value_set_uint(value, priv->controller_unix_socket_mode);
        break;
    case PROP_MANAGER_UNIX_SOCKET_GROUP:
        g_value_set_string(value, priv->manager_unix_socket_group);
        break;
    case PROP_CONTROLLER_UNIX_SOCKET_GROUP:
        g_value_set_string(value, priv->controller_unix_socket_group);
        break;
    case PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CLOSE:
        g_value_set_boolean(value, priv->remove_manager_unix_socket_on_close);
        break;
    case PROP_REMOVE_CONTROLLER_UNIX_SOCKET_ON_CLOSE:
        g_value_set_boolean(value, priv->remove_controller_unix_socket_on_close);
        break;
    case PROP_REMOVE_MANAGER_UNIX_SOCKET_ON_CREATE:
        g_value_set_boolean(value, priv->remove_manager_unix_socket_on_create);
        break;
    case PROP_REMOVE_CONTROLLER_UNIX_SOCKET_ON_CREATE:
        g_value_set_boolean(value, priv->remove_controller_unix_socket_on_create);
        break;
    case PROP_DAEMON:
        g_value_set_boolean(value, priv->daemon);
        break;
    case PROP_PID_FILE:
        g_value_set_string(value, priv->pid_file);
        break;
    case PROP_MAINTENANCE_INTERVAL:
        g_value_set_uint(value, priv->maintenance_interval);
        break;
    case PROP_SUSPEND_TIME_ON_UNACCEPTABLE:
        g_value_set_uint(value, priv->suspend_time_on_unacceptable);
        break;
    case PROP_MAX_CONNECTIONS:
        g_value_set_uint(value, priv->max_connections);
        break;
    case PROP_MAX_FILE_DESCRIPTORS:
        g_value_set_uint(value, priv->max_file_descriptors);
        break;
    case PROP_CUSTOM_CONFIGURATION_DIRECTORY:
        g_value_set_string(value, priv->custom_configuration_directory);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static const gchar *
_milter_manager_configuration_module_dir (void)
{
    const gchar *dir;

    if (configuration_module_dir)
        return configuration_module_dir;

    dir = g_getenv("MILTER_MANAGER_CONFIGURATION_MODULE_DIR");
    if (dir)
        return dir;

    return CONFIGURATION_MODULE_DIR;
}

static MilterManagerModule *
milter_manager_configuration_load_module (const gchar *name)
{
    gchar *module_name;
    MilterManagerModule *module;

    module_name = g_strdup_printf("milter-manager-%s-configuration", name);
    module = milter_manager_module_find(configurations, module_name);
    if (!module) {
        const gchar *module_dir;

        module_dir = _milter_manager_configuration_module_dir();
        module = milter_manager_module_load_module(module_dir, module_name);
        if (module) {
            if (g_type_module_use(G_TYPE_MODULE(module))) {
                configurations = g_list_prepend(configurations, module);
            }
        }
    }

    g_free(module_name);
    return module;
}

void
_milter_manager_configuration_init (void)
{
}

static void
milter_manager_configuration_set_default_module_dir (const gchar *dir)
{
    if (configuration_module_dir)
        g_free(configuration_module_dir);
    configuration_module_dir = NULL;

    if (dir)
        configuration_module_dir = g_strdup(dir);
}

static void
milter_manager_configuration_unload (void)
{
    g_list_foreach(configurations, (GFunc)milter_manager_module_unload, NULL);
    g_list_free(configurations);
    configurations = NULL;
}

void
_milter_manager_configuration_quit (void)
{
    milter_manager_configuration_unload();
    milter_manager_configuration_set_default_module_dir(NULL);
}

GQuark
milter_manager_configuration_error_quark (void)
{
    return g_quark_from_static_string("milter-manager-configuration-error-quark");
}

MilterManagerConfiguration *
milter_manager_configuration_new (const gchar *first_property,
                                  ...)
{
    MilterManagerModule *module;
    GObject *configuration;
    va_list var_args;

    module = milter_manager_configuration_load_module("ruby");
    g_return_val_if_fail(module != NULL, NULL);

    va_start(var_args, first_property);
    configuration = milter_manager_module_instantiate(module,
                                                      first_property, var_args);
    va_end(var_args);

    return MILTER_MANAGER_CONFIGURATION(configuration);
}

void
milter_manager_configuration_append_load_path (MilterManagerConfiguration *configuration,
                                               const gchar *path)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->load_paths = g_list_append(priv->load_paths, g_strdup(path));
}

void
milter_manager_configuration_prepend_load_path (MilterManagerConfiguration *configuration,
                                                const gchar *path)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->load_paths = g_list_prepend(priv->load_paths, g_strdup(path));
}

void
milter_manager_configuration_clear_load_paths (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->load_paths) {
        g_list_foreach(priv->load_paths, (GFunc)g_free, NULL);
        g_list_free(priv->load_paths);
        priv->load_paths = NULL;
    }
}

const GList *
milter_manager_configuration_get_load_paths (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->load_paths;
}

static gchar *
find_file (MilterManagerConfiguration *configuration,
           const gchar *file_name, GError **error)
{
    MilterManagerConfigurationPrivate *priv;
    gchar *full_path = NULL;
    GList *node;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (g_path_is_absolute(file_name)) {
        full_path = g_strdup(file_name);
    } else {
        for (node = priv->load_paths; node; node = g_list_next(node)) {
            const gchar *dir = node->data;

            full_path = g_build_filename(dir, file_name, NULL);
            if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR)) {
                break;
            } else {
                g_free(full_path);
                full_path = NULL;
            }
        }
    }

    if (!full_path) {
        GString *inspected_load_paths;

        inspected_load_paths = g_string_new("[");
        for (node = priv->load_paths; node; node = g_list_next(node)) {
            const gchar *dir = node->data;

            g_string_append_printf(inspected_load_paths, "\"%s\"", dir);
            if (g_list_next(node))
                g_string_append(inspected_load_paths, ", ");
        }
        g_string_append(inspected_load_paths, "]");
        g_set_error(error,
                    MILTER_MANAGER_CONFIGURATION_ERROR,
                    MILTER_MANAGER_CONFIGURATION_ERROR_NOT_EXIST,
                    "path %s doesn't exist in load path (%s)",
                    file_name, inspected_load_paths->str);
        g_string_free(inspected_load_paths, TRUE);
    }

    return full_path;
}

gboolean
milter_manager_configuration_load (MilterManagerConfiguration *configuration,
                                   const gchar *file_name, GError **error)
{
    MilterManagerConfigurationClass *configuration_class;

    configuration_class = MILTER_MANAGER_CONFIGURATION_GET_CLASS(configuration);
    if (configuration_class->load) {
        gboolean success;
        gchar *full_path;

        full_path = find_file(configuration, file_name, error);
        if (!full_path)
            return FALSE;

        success = configuration_class->load(configuration, full_path, error);
        g_free(full_path);
        return success;
    } else {
        g_set_error(error,
                    MILTER_MANAGER_CONFIGURATION_ERROR,
                    MILTER_MANAGER_CONFIGURATION_ERROR_NOT_IMPLEMENTED,
                    "load isn't implemented");
        return FALSE;
    }
}

gboolean
milter_manager_configuration_load_custom (MilterManagerConfiguration *configuration,
                                          const gchar *file_name, GError **error)
{
    MilterManagerConfigurationClass *configuration_class;

    configuration_class = MILTER_MANAGER_CONFIGURATION_GET_CLASS(configuration);
    if (configuration_class->load_custom) {
        gboolean success;
        gchar *full_path;

        full_path = find_file(configuration, file_name, error);
        if (!full_path)
            return FALSE;

        success = configuration_class->load_custom(configuration,
                                                   full_path,
                                                   error);
        g_free(full_path);
        return success;
    } else {
        g_set_error(error,
                    MILTER_MANAGER_CONFIGURATION_ERROR,
                    MILTER_MANAGER_CONFIGURATION_ERROR_NOT_IMPLEMENTED,
                    "load_custom isn't implemented");
        return FALSE;
    }
}

gboolean
milter_manager_configuration_load_if_exist (MilterManagerConfiguration *configuration,
                                            const gchar *file_name,
                                            GError **error)
{
    GError *local_error = NULL;

    if (milter_manager_configuration_load(configuration, file_name,
                                          &local_error))
        return TRUE;

    if (local_error->code == MILTER_MANAGER_CONFIGURATION_ERROR_NOT_EXIST) {
        g_error_free(local_error);
        return TRUE;
    }

    g_propagate_error(error, local_error);
    return FALSE;
}

gboolean
milter_manager_configuration_load_custom_if_exist (MilterManagerConfiguration *configuration,
                                                   const gchar *file_name,
                                                   GError **error)
{
    GError *local_error = NULL;

    if (milter_manager_configuration_load_custom(configuration, file_name,
                                                 &local_error))
        return TRUE;

    if (local_error->code == MILTER_MANAGER_CONFIGURATION_ERROR_NOT_EXIST) {
        g_error_free(local_error);
        return TRUE;
    }

    g_propagate_error(error, local_error);
    return FALSE;
}

void
milter_manager_configuration_reload (MilterManagerConfiguration *configuration)
{
    GError *error = NULL;

    milter_manager_configuration_clear(configuration);
    if (!milter_manager_configuration_load(configuration, CONFIG_FILE_NAME,
                                           &error)) {
        milter_error("[configuration][error][load] <%s>: %s",
                     CONFIG_FILE_NAME, error->message);
        g_error_free(error);
        return;
    }

    if (!milter_manager_configuration_load_custom_if_exist(
            configuration, CUSTOM_CONFIG_FILE_NAME, &error)) {
        milter_error("[configuration][error][load][custom] <%s>: %s",
                     CUSTOM_CONFIG_FILE_NAME, error->message);
        g_error_free(error);
    }
}

gboolean
milter_manager_configuration_save_custom (MilterManagerConfiguration *configuration,
                                          const gchar                *content,
                                          gssize                      size,
                                          GError                    **error)
{
    MilterManagerConfigurationPrivate *priv;
    GError *local_error = NULL;
    GList *node;
    gboolean success = FALSE;
    GString *inspected_tried_paths;

    inspected_tried_paths = g_string_new("[");
    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    for (node = priv->load_paths; node; node = g_list_next(node)) {
        const gchar *dir = node->data;
        gchar *full_path;

        full_path = g_build_filename(dir, CUSTOM_CONFIG_FILE_NAME, NULL);
        g_string_append_printf(inspected_tried_paths, "\"%s\"", full_path);
        if (g_file_set_contents(full_path, content, size, &local_error)) {
            success = TRUE;
        } else {
            g_string_append_printf(inspected_tried_paths, "(%s)",
                                   local_error->message);
            g_error_free(local_error);
            local_error = NULL;
            if (g_list_next(node))
                g_string_append(inspected_tried_paths, ", ");
        }
        g_free(full_path);
        if (success)
            break;
    }
    g_string_append(inspected_tried_paths, "]");

    if (!success) {
        g_set_error(error,
                    MILTER_MANAGER_CONFIGURATION_ERROR,
                    MILTER_MANAGER_CONFIGURATION_ERROR_SAVE,
                    "can't find writable custom configuration file in %s",
                    inspected_tried_paths->str);
        g_string_free(inspected_tried_paths, TRUE);
    }

    return success;
}

gboolean
milter_manager_configuration_is_privilege_mode (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->privilege_mode;
}

void
milter_manager_configuration_set_privilege_mode (MilterManagerConfiguration *configuration,
                                                 gboolean mode)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->privilege_mode = mode;
}

const gchar *
milter_manager_configuration_get_controller_connection_spec (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->controller_connection_spec;
}

void
milter_manager_configuration_set_controller_connection_spec (MilterManagerConfiguration *configuration,
                                                             const gchar *spec)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->controller_connection_spec)
        g_free(priv->controller_connection_spec);
    priv->controller_connection_spec = g_strdup(spec);
}

const gchar *
milter_manager_configuration_get_manager_connection_spec (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->manager_connection_spec;
}

void
milter_manager_configuration_set_manager_connection_spec (MilterManagerConfiguration *configuration,
                                                          const gchar *spec)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->manager_connection_spec)
        g_free(priv->manager_connection_spec);
    priv->manager_connection_spec = g_strdup(spec);
}

const gchar *
milter_manager_configuration_get_effective_user (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->effective_user;
}

void
milter_manager_configuration_set_effective_user (MilterManagerConfiguration *configuration,
                                                 const gchar *effective_user)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->effective_user)
        g_free(priv->effective_user);
    priv->effective_user = g_strdup(effective_user);
}

const gchar *
milter_manager_configuration_get_effective_group (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->effective_group;
}

void
milter_manager_configuration_set_effective_group (MilterManagerConfiguration *configuration,
                                                  const gchar *effective_group)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->effective_group)
        g_free(priv->effective_group);
    priv->effective_group = g_strdup(effective_group);
}

guint
milter_manager_configuration_get_manager_unix_socket_mode (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->manager_unix_socket_mode;
}

void
milter_manager_configuration_set_manager_unix_socket_mode (MilterManagerConfiguration *configuration,
                                                           guint mode)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->manager_unix_socket_mode = mode;
}

guint
milter_manager_configuration_get_controller_unix_socket_mode (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->controller_unix_socket_mode;
}

void
milter_manager_configuration_set_controller_unix_socket_mode (MilterManagerConfiguration *configuration,
                                                              guint mode)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->controller_unix_socket_mode = mode;
}

const gchar *
milter_manager_configuration_get_manager_unix_socket_group (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->manager_unix_socket_group;
}

void
milter_manager_configuration_set_manager_unix_socket_group (MilterManagerConfiguration *configuration,
                                                            const gchar *group)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->manager_unix_socket_group)
        g_free(priv->manager_unix_socket_group);
    priv->manager_unix_socket_group = g_strdup(group);
}

const gchar *
milter_manager_configuration_get_controller_unix_socket_group (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->controller_unix_socket_group;
}

void
milter_manager_configuration_set_controller_unix_socket_group (MilterManagerConfiguration *configuration,
                                                               const gchar *group)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->controller_unix_socket_group)
        g_free(priv->controller_unix_socket_group);
    priv->controller_unix_socket_group = g_strdup(group);
}

gboolean
milter_manager_configuration_is_remove_manager_unix_socket_on_close (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->remove_manager_unix_socket_on_close;
}

void
milter_manager_configuration_set_remove_manager_unix_socket_on_close (MilterManagerConfiguration *configuration,
                                                                      gboolean remove)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->remove_manager_unix_socket_on_close = remove;
}

gboolean
milter_manager_configuration_is_remove_controller_unix_socket_on_close (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->remove_controller_unix_socket_on_close;
}

void
milter_manager_configuration_set_remove_controller_unix_socket_on_close (MilterManagerConfiguration *configuration,
                                                                         gboolean remove)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->remove_controller_unix_socket_on_close = remove;
}

gboolean
milter_manager_configuration_is_remove_manager_unix_socket_on_create (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->remove_manager_unix_socket_on_create;
}

void
milter_manager_configuration_set_remove_manager_unix_socket_on_create (MilterManagerConfiguration *configuration,
                                                                      gboolean remove)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->remove_manager_unix_socket_on_create = remove;
}

gboolean
milter_manager_configuration_is_remove_controller_unix_socket_on_create (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->remove_controller_unix_socket_on_create;
}

void
milter_manager_configuration_set_remove_controller_unix_socket_on_create (MilterManagerConfiguration *configuration,
                                                                         gboolean remove)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->remove_controller_unix_socket_on_create = remove;
}

gboolean
milter_manager_configuration_is_daemon (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->daemon;
}

void
milter_manager_configuration_set_daemon (MilterManagerConfiguration *configuration,
                                         gboolean daemon)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->daemon = daemon;
}

const gchar *
milter_manager_configuration_get_pid_file (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->pid_file;
}

void
milter_manager_configuration_set_pid_file (MilterManagerConfiguration *configuration,
                                           const gchar *pid_file)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->pid_file)
        g_free(priv->pid_file);
    priv->pid_file = g_strdup(pid_file);
}

guint
milter_manager_configuration_get_maintenance_interval (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->maintenance_interval;
}

void
milter_manager_configuration_set_maintenance_interval (MilterManagerConfiguration *configuration,
                                                       guint n_sessions)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->maintenance_interval = n_sessions;
}

guint
milter_manager_configuration_get_suspend_time_on_unacceptable (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->suspend_time_on_unacceptable;
}

void
milter_manager_configuration_set_suspend_time_on_unacceptable (MilterManagerConfiguration *configuration,
                                                               guint suspend_time)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->suspend_time_on_unacceptable = suspend_time;
}

guint
milter_manager_configuration_get_max_connections (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->max_connections;
}

void
milter_manager_configuration_set_max_connections (MilterManagerConfiguration *configuration,
                                                               guint suspend_time)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->max_connections = suspend_time;
}

guint
milter_manager_configuration_get_max_file_descriptors (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->max_file_descriptors;
}

void
milter_manager_configuration_set_max_file_descriptors (MilterManagerConfiguration *configuration,
                                                               guint suspend_time)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->max_file_descriptors = suspend_time;
}

const gchar *
milter_manager_configuration_get_custom_configuration_directory (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->custom_configuration_directory;
}

void
milter_manager_configuration_set_custom_configuration_directory (MilterManagerConfiguration *configuration,
                                                                 const gchar *custom_configuration_directory)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->custom_configuration_directory)
        g_free(priv->custom_configuration_directory);
    priv->custom_configuration_directory = g_strdup(custom_configuration_directory);
}

void
milter_manager_configuration_add_egg (MilterManagerConfiguration *configuration,
                                      MilterManagerEgg         *egg)
{
    MilterManagerConfigurationPrivate *priv;

    g_return_if_fail(egg != NULL);

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    g_object_ref(egg);
    priv->eggs = g_list_append(priv->eggs, egg);
}

MilterManagerEgg *
milter_manager_configuration_find_egg (MilterManagerConfiguration *configuration,
                                       const gchar *name)
{
    MilterManagerConfigurationPrivate *priv;
    GList *node;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    for (node = priv->eggs; node; node = g_list_next(node)) {
        MilterManagerEgg *egg = node->data;

        if (g_str_equal(name, milter_manager_egg_get_name(egg)))
            return egg;
    }

    return NULL;
}

const GList *
milter_manager_configuration_get_eggs (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->eggs;
}

void
milter_manager_configuration_remove_egg (MilterManagerConfiguration *configuration,
                                         MilterManagerEgg *egg)
{
    const gchar *name;

    name = milter_manager_egg_get_name(egg);
    milter_manager_configuration_remove_egg_by_name(configuration, name);
}

void
milter_manager_configuration_remove_egg_by_name (MilterManagerConfiguration *configuration,
                                                 const gchar *name)
{
    MilterManagerConfigurationPrivate *priv;
    GList *node;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    for (node = priv->eggs; node; node = g_list_next(node)) {
        MilterManagerEgg *egg = node->data;

        if (g_str_equal(name, milter_manager_egg_get_name(egg))) {
            priv->eggs = g_list_delete_link(priv->eggs, node);
            g_object_unref(egg);
            break;
        }
    }
}

void
milter_manager_configuration_clear_eggs (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    if (priv->eggs) {
        g_list_foreach(priv->eggs, (GFunc)g_object_unref, NULL);
        g_list_free(priv->eggs);
        priv->eggs = NULL;
    }
}

MilterManagerChildren *
milter_manager_configuration_create_children (MilterManagerConfiguration *configuration)
{
    GList *node;
    MilterManagerChildren *children;
    MilterManagerConfigurationPrivate *priv;

    children = milter_manager_children_new(configuration);
    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    for (node = priv->eggs; node; node = g_list_next(node)) {
        MilterManagerChild *child;
        MilterManagerEgg *egg = node->data;

        if (!milter_manager_egg_is_enabled(egg))
            continue;

        child = milter_manager_egg_hatch(egg);
        if (child) {
            milter_manager_children_add_child(children, child);
            milter_manager_egg_attach_applicable_conditions(egg, child, children);
            g_object_unref(child);
        }
    }

    return children;
}

MilterStatus
milter_manager_configuration_get_fallback_status
                                     (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->fallback_status;
}

void
milter_manager_configuration_set_fallback_status
                                     (MilterManagerConfiguration *configuration,
                                      MilterStatus status)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->fallback_status = status;
}

const gchar *
milter_manager_configuration_get_package_platform
                                     (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->package_platform;
}

void
milter_manager_configuration_set_package_platform
                                     (MilterManagerConfiguration *configuration,
                                      const gchar *platform)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->package_platform)
        g_free(priv->package_platform);
    priv->package_platform = g_strdup(platform);
}

const gchar *
milter_manager_configuration_get_package_options
                                     (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->package_options;
}

void
milter_manager_configuration_set_package_options
                                     (MilterManagerConfiguration *configuration,
                                      const gchar *options)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (priv->package_options)
        g_free(priv->package_options);
    priv->package_options = g_strdup(options);
}

void
milter_manager_configuration_add_applicable_condition (MilterManagerConfiguration *configuration,
                                                       MilterManagerApplicableCondition *condition)
{
    MilterManagerConfigurationPrivate *priv;

    g_return_if_fail(condition != NULL);

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    g_object_ref(condition);
    priv->applicable_conditions =
        g_list_append(priv->applicable_conditions, condition);
}

MilterManagerApplicableCondition *
milter_manager_configuration_find_applicable_condition (MilterManagerConfiguration *configuration,
                                                        const gchar *name)
{
    MilterManagerConfigurationPrivate *priv;
    GList *node;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    for (node = priv->applicable_conditions; node; node = g_list_next(node)) {
        MilterManagerApplicableCondition *condition = node->data;
        const gchar *condition_name;

        condition_name = milter_manager_applicable_condition_get_name(condition);
        if (g_str_equal(name, condition_name))
            return condition;
    }

    return NULL;
}

const GList *
milter_manager_configuration_get_applicable_conditions (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    return priv->applicable_conditions;
}

void
milter_manager_configuration_remove_applicable_condition (MilterManagerConfiguration *configuration,
                                                          MilterManagerApplicableCondition *condition)
{
    const gchar *name;

    name = milter_manager_applicable_condition_get_name(condition);
    milter_manager_configuration_remove_applicable_condition_by_name(configuration,
                                                                     name);
}

void
milter_manager_configuration_remove_applicable_condition_by_name (MilterManagerConfiguration *configuration,
                                                                  const gchar *name)
{
    MilterManagerConfigurationPrivate *priv;
    GList *node;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    for (node = priv->applicable_conditions; node; node = g_list_next(node)) {
        MilterManagerApplicableCondition *condition = node->data;
        const gchar *condition_name;

        condition_name = milter_manager_applicable_condition_get_name(condition);
        if (g_str_equal(name, condition_name)) {
            priv->applicable_conditions =
                g_list_delete_link(priv->applicable_conditions, node);
            g_object_unref(condition);
            break;
        }
    }
}

void
milter_manager_configuration_clear_applicable_conditions (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    if (priv->applicable_conditions) {
        g_list_foreach(priv->applicable_conditions, (GFunc)g_object_unref, NULL);
        g_list_free(priv->applicable_conditions);
        priv->applicable_conditions = NULL;
    }
}

void
milter_manager_configuration_clear (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    milter_manager_configuration_clear_eggs(configuration);
    milter_manager_configuration_clear_applicable_conditions(configuration);

    if (priv->controller_connection_spec) {
        g_free(priv->controller_connection_spec);
        priv->controller_connection_spec = NULL;
    }

    if (priv->manager_connection_spec) {
        g_free(priv->manager_connection_spec);
        priv->manager_connection_spec = NULL;
    }
    priv->manager_connection_spec =
        g_strdup(MILTER_MANAGER_DEFAULT_CONNECTION_SPEC);

    if (priv->package_platform) {
        g_free(priv->package_platform);
        priv->package_platform = g_strdup(MILTER_MANAGER_PACKAGE_PLATFORM);
    }

    if (priv->package_options) {
        g_free(priv->package_options);
        priv->package_options = g_strdup(MILTER_MANAGER_PACKAGE_OPTIONS);
    }

    if (priv->effective_user) {
        g_free(priv->effective_user);
        priv->effective_user = NULL;
    }
    priv->effective_user = g_strdup(MILTER_MANAGER_DEFAULT_EFFECTIVE_USER);

    if (priv->effective_group) {
        g_free(priv->effective_group);
        priv->effective_group = NULL;
    }
    priv->effective_group = g_strdup(MILTER_MANAGER_DEFAULT_EFFECTIVE_GROUP);

    if (priv->manager_unix_socket_group) {
        g_free(priv->manager_unix_socket_group);
        priv->manager_unix_socket_group = NULL;
    }
    priv->manager_unix_socket_group =
        g_strdup(MILTER_MANAGER_DEFAULT_SOCKET_GROUP);

    if (priv->controller_unix_socket_group) {
        g_free(priv->controller_unix_socket_group);
        priv->controller_unix_socket_group = NULL;
    }

    if (priv->pid_file) {
        g_free(priv->pid_file);
        priv->pid_file = NULL;
    }
    priv->pid_file = g_strdup(MILTER_MANAGER_DEFAULT_PID_FILE);

    if (priv->custom_configuration_directory) {
        g_free(priv->custom_configuration_directory);
        priv->custom_configuration_directory = NULL;
    }

    priv->privilege_mode = FALSE;
    priv->manager_unix_socket_mode = 0660;
    priv->controller_unix_socket_mode = 0660;
    priv->remove_manager_unix_socket_on_close = TRUE;
    priv->remove_controller_unix_socket_on_close = TRUE;
    priv->remove_manager_unix_socket_on_create = TRUE;
    priv->remove_controller_unix_socket_on_create = TRUE;
    priv->daemon = FALSE;
    priv->fallback_status = MILTER_STATUS_ACCEPT;
    priv->maintenance_interval = DEFAULT_MAINTENANCE_INTERVAL;
    priv->suspend_time_on_unacceptable =
        MILTER_CLIENT_DEFAULT_SUSPEND_TIME_ON_UNACCEPTABLE;
    priv->max_connections = 0;
    priv->max_file_descriptors = 0;

    if (priv->locations)
        g_hash_table_remove_all(priv->locations);
}

void
milter_manager_configuration_session_finished (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationPrivate *priv;
    MilterManagerConfigurationClass *configuration_class;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    priv->processed_sessions++;

    if (priv->maintenance_interval == 0)
        return;

    if ((priv->processed_sessions % priv->maintenance_interval) != 0)
        return;

    configuration_class = MILTER_MANAGER_CONFIGURATION_GET_CLASS(configuration);
    if (configuration_class->maintain)
        configuration_class->maintain(configuration);
}

gchar *
milter_manager_configuration_to_xml (MilterManagerConfiguration *configuration)
{
    GString *string;

    string = g_string_new(NULL);
    milter_manager_configuration_to_xml_string(configuration, string, 0);
    return g_string_free(string, FALSE);
}

void
milter_manager_configuration_to_xml_string (MilterManagerConfiguration *configuration,
                                            GString *string, guint indent)
{
    MilterManagerConfigurationPrivate *priv;
    gchar *full_path;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);

    milter_utils_append_indent(string, indent);
    g_string_append(string, "<configuration>\n");

    full_path = find_file(configuration, CUSTOM_CONFIG_FILE_NAME, NULL);
    if (full_path) {
        struct stat status;
        if (g_stat(full_path, &status) == 0) {
            gchar *content;

            content = g_strdup_printf("%ld", status.st_mtime);
            milter_utils_xml_append_text_element(string,
                                                 "last-modified", content,
                                                 indent + 2);
        }
        g_free(full_path);
    }

    if (priv->applicable_conditions) {
        GList *node = priv->applicable_conditions;

        milter_utils_append_indent(string, indent + 2);
        g_string_append(string, "<applicable-conditions>\n");
        for (; node; node = g_list_next(node)) {
            MilterManagerApplicableCondition *condition = node->data;
            const gchar *name, *description;

            milter_utils_append_indent(string, indent + 4);
            g_string_append(string, "<applicable-condition>\n");

            name = milter_manager_applicable_condition_get_name(condition);
            milter_utils_xml_append_text_element(string,
                                                 "name", name,
                                                 indent + 6);
            description =
                milter_manager_applicable_condition_get_description(condition);
            if (description)
                milter_utils_xml_append_text_element(string,
                                                     "description", description,
                                                     indent + 6);

            milter_utils_append_indent(string, indent + 4);
            g_string_append(string, "</applicable-condition>\n");
        }
        milter_utils_append_indent(string, indent + 2);
        g_string_append(string, "</applicable-conditions>\n");
    }

    if (priv->eggs) {
        GList *node;

        milter_utils_append_indent(string, indent + 2);
        g_string_append(string, "<milters>\n");
        for (node = priv->eggs; node; node = g_list_next(node)) {
            MilterManagerEgg *egg = node->data;
            milter_manager_egg_to_xml_string(egg, string, indent + 2 + 2);
        }
        milter_utils_append_indent(string, indent + 2);
        g_string_append(string, "</milters>\n");
    }

    g_signal_emit(configuration, signals[TO_XML], 0, string, indent + 2);

    milter_utils_append_indent(string, indent);
    g_string_append(string, "</configuration>\n");
}

gchar *
milter_manager_configuration_dump (MilterManagerConfiguration *configuration)
{
    MilterManagerConfigurationClass *configuration_class;

    configuration_class = MILTER_MANAGER_CONFIGURATION_GET_CLASS(configuration);
    if (configuration_class->dump) {
        return configuration_class->dump(configuration);
    } else {
        return milter_manager_configuration_to_xml(configuration);
    }
}

void
milter_manager_configuration_set_location (MilterManagerConfiguration *configuration,
                                           const gchar *key,
                                           const gchar *file,
                                           gint line)
{
    MilterManagerConfigurationPrivate *priv;
    gconstpointer location;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (!priv->locations)
        return;

    if (!file) {
        g_hash_table_remove(priv->locations, key);
        return;
    }

    location = g_hash_table_lookup(priv->locations, key);
    if (!location) {
        gchar *duped_key;
        duped_key = g_strdup(key);
        location = duped_key;
        g_hash_table_insert(priv->locations, duped_key, (gpointer)location);
    }
    g_dataset_set_data_full(location, "file", g_strdup(file), g_free);
    g_dataset_set_data(location, "line", GINT_TO_POINTER(line));
}

void
milter_manager_configuration_reset_location (MilterManagerConfiguration *configuration,
                                             const gchar *key)
{
    milter_manager_configuration_set_location(configuration, key, NULL, -1);
}

gconstpointer
milter_manager_configuration_get_location (MilterManagerConfiguration *configuration,
                                           const gchar *key)
{
    MilterManagerConfigurationPrivate *priv;

    priv = MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration);
    if (!priv->locations)
        return NULL;

    return g_hash_table_lookup(priv->locations, key);
}

GHashTable *
milter_manager_configuration_get_locations (MilterManagerConfiguration *configuration)
{
    return MILTER_MANAGER_CONFIGURATION_GET_PRIVATE(configuration)->locations;
}

/*
vi:ts=4:nowrap:ai:expandtab:sw=4
*/
