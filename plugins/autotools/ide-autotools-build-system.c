/* ide-autotools-build-system.c
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "ide-autotools-build-system"

#include "config.h"

#include <gio/gio.h>
#include <gtksourceview/gtksource.h>
#include <ide.h>

#include "ide-autotools-build-system.h"
#include "ide-autotools-builder.h"
#include "ide-makecache.h"

struct _IdeAutotoolsBuildSystem
{
  IdeObject  parent_instance;

  GFile     *project_file;
  gchar     *tarball_name;
};

static void async_initable_iface_init (GAsyncInitableIface *iface);
static void build_system_iface_init (IdeBuildSystemInterface *iface);
static void tags_builder_iface_init (IdeTagsBuilderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (IdeAutotoolsBuildSystem,
                         ide_autotools_build_system,
                         IDE_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (IDE_TYPE_TAGS_BUILDER, tags_builder_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init)
                         G_IMPLEMENT_INTERFACE (IDE_TYPE_BUILD_SYSTEM, build_system_iface_init))

enum {
  PROP_0,
  PROP_PROJECT_FILE,
  PROP_TARBALL_NAME,
  LAST_PROP
};

static GParamSpec *properties [LAST_PROP];

const gchar *
ide_autotools_build_system_get_tarball_name (IdeAutotoolsBuildSystem *self)
{
  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (self), NULL);

  return self->tarball_name;
}

static IdeBuilder *
ide_autotools_build_system_get_builder (IdeBuildSystem    *build_system,
                                        IdeConfiguration  *configuration,
                                        GError           **error)
{
  IdeBuilder *ret;
  IdeContext *context;

  IDE_ENTRY;

  g_assert (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (build_system));
  g_assert (IDE_IS_CONFIGURATION (configuration));

  context = ide_object_get_context (IDE_OBJECT (build_system));
  g_assert (IDE_IS_CONTEXT (context));

  ret = g_object_new (IDE_TYPE_AUTOTOOLS_BUILDER,
                      "context", context,
                      "configuration", configuration,
                      NULL);

  IDE_RETURN (ret);
}

static gboolean
is_configure (GFile *file)
{
  gchar *name;
  gboolean ret;

  g_assert (G_IS_FILE (file));

  name = g_file_get_basename (file);
  ret = ((0 == g_strcmp0 (name, "configure.ac")) ||
         (0 == g_strcmp0 (name, "configure.in")));
  g_free (name);

  return ret;
}

static void
ide_autotools_build_system_discover_file_worker (GTask        *task,
                                                 gpointer      source_object,
                                                 gpointer      task_data,
                                                 GCancellable *cancellable)
{
  g_autoptr(GFile) parent = NULL;
  g_autoptr(GFile) configure_ac = NULL;
  g_autoptr(GFile) configure_in = NULL;
  GFile *file = task_data;

  g_assert (G_IS_TASK (task));
  g_assert (G_IS_FILE (file));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  /*
   * Previously, we used to walk up the tree looking for configure.ac. But
   * that causes more problems than it solves as it means that we cannot
   * handle test projects inside the Builder project (and other projects
   * may have the same issue for sub-projects).
   */

  if (is_configure (file) && g_file_query_exists (file, cancellable))
    {
      g_task_return_pointer (task, g_object_ref (file), g_object_unref);
      return;
    }

  if (g_file_query_file_type (file, 0, cancellable) == G_FILE_TYPE_DIRECTORY)
    parent = g_object_ref (file);
  else
    parent = g_file_get_parent (file);

  configure_ac = g_file_get_child (parent, "configure.ac");
  if (g_file_query_exists (configure_ac, cancellable))
    {
      g_task_return_pointer (task, g_steal_pointer (&configure_ac), g_object_unref);
      return;
    }

  configure_in = g_file_get_child (parent, "configure.in");
  if (g_file_query_exists (configure_in, cancellable))
    {
      g_task_return_pointer (task, g_steal_pointer (&configure_in), g_object_unref);
      return;
    }

  g_task_return_new_error (task,
                           G_IO_ERROR,
                           G_IO_ERROR_NOT_FOUND,
                           "Failed to locate configure.ac");
}

static void
ide_autotools_build_system_discover_file_async (IdeAutotoolsBuildSystem *system,
                                                GFile                   *file,
                                                GCancellable            *cancellable,
                                                GAsyncReadyCallback      callback,
                                                gpointer                 user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (system));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (system, cancellable, callback, user_data);
  g_task_set_task_data (task, g_object_ref (file), g_object_unref);
  g_task_run_in_thread (task, ide_autotools_build_system_discover_file_worker);
}

static GFile *
ide_autotools_build_system_discover_file_finish (IdeAutotoolsBuildSystem  *system,
                                                 GAsyncResult             *result,
                                                 GError                  **error)
{
  GTask *task = (GTask *)result;

  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (system), NULL);
  g_return_val_if_fail (G_IS_TASK (task), NULL);

  return g_task_propagate_pointer (task, error);
}

#if 0
static gboolean
looks_like_makefile (IdeBuffer *buffer)
{
  GtkSourceLanguage *language;
  const gchar *path;
  IdeFile *file;

  g_assert (IDE_IS_BUFFER (buffer));

  file = ide_buffer_get_file (buffer);
  path = ide_file_get_path (file);

  if (path != NULL)
    {
      if (g_str_has_suffix (path, "Makefile.am") || g_str_has_suffix (path, ".mk"))
        return TRUE;
    }

  language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (buffer));

  if (language != NULL)
    {
      const gchar *lang_id;

      lang_id = gtk_source_language_get_id (language);

      if (ide_str_equal0 (lang_id, "automake") || ide_str_equal0 (lang_id, "makefile"))
        return TRUE;
    }

  return FALSE;
}
#endif

static void
ide_autotools_build_system__buffer_saved_cb (IdeAutotoolsBuildSystem *self,
                                             IdeBuffer               *buffer,
                                             IdeBufferManager        *buffer_manager)
{
  g_assert (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (self));
  g_assert (IDE_IS_BUFFER (buffer));
  g_assert (IDE_IS_BUFFER_MANAGER (buffer_manager));

#if 0
  if (looks_like_makefile (buffer))
    egg_task_cache_evict (self->task_cache, MAKECACHE_KEY);
#endif
}

static void
ide_autotools_build_system__vcs_changed_cb (IdeAutotoolsBuildSystem *self,
                                            IdeVcs                  *vcs)
{
  IDE_ENTRY;

  g_assert (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (self));
  g_assert (IDE_IS_VCS (vcs));

  IDE_TRACE_MSG ("VCS has changed, evicting cached makecaches");

#if 0
  egg_task_cache_evict_all (self->task_cache);
#endif

  IDE_EXIT;
}

static void
ide_autotools_build_system__context_loaded_cb (IdeAutotoolsBuildSystem *self,
                                               IdeContext              *context)
{
  IdeVcs *vcs;

  IDE_ENTRY;

  g_assert (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (self));
  g_assert (IDE_IS_CONTEXT (context));

  vcs = ide_context_get_vcs (context);

  g_signal_connect_object (vcs,
                           "changed",
                           G_CALLBACK (ide_autotools_build_system__vcs_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  IDE_EXIT;
}

static void
ide_autotools_build_system_constructed (GObject *object)
{
  IdeAutotoolsBuildSystem *self = (IdeAutotoolsBuildSystem *)object;
  IdeBufferManager *buffer_manager;
  IdeContext *context;

  G_OBJECT_CLASS (ide_autotools_build_system_parent_class)->constructed (object);

  context = ide_object_get_context (IDE_OBJECT (self));
  g_assert (IDE_IS_CONTEXT (context));

  buffer_manager = ide_context_get_buffer_manager (context);
  g_assert (IDE_IS_BUFFER_MANAGER (buffer_manager));

  g_signal_connect_object (context,
                           "loaded",
                           G_CALLBACK (ide_autotools_build_system__context_loaded_cb),
                           self,
                           G_CONNECT_SWAPPED);

  /*
   * FIXME:
   *
   * We could setup and try to track all of the makefiles in the system
   * with inotify watches. That would require that 1) we can tell if a file
   * is an automake file (or a dependent included file), and 2) lots of
   * inotify watches.
   *
   * What is cheap, easy, and can be done right now is to just watch for save
   * events on files that look like makefiles, and invalidate the makecache.
   */
  g_signal_connect_object (buffer_manager,
                           "buffer-saved",
                           G_CALLBACK (ide_autotools_build_system__buffer_saved_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

static gint
ide_autotools_build_system_get_priority (IdeBuildSystem *system)
{
  return -100;
}

static void
ide_autotools_build_system_finalize (GObject *object)
{
  IdeAutotoolsBuildSystem *self = (IdeAutotoolsBuildSystem *)object;

  g_clear_pointer (&self->tarball_name, g_free);
  g_clear_object (&self->project_file);

  G_OBJECT_CLASS (ide_autotools_build_system_parent_class)->finalize (object);
}

static void
ide_autotools_build_system_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  IdeAutotoolsBuildSystem *self = IDE_AUTOTOOLS_BUILD_SYSTEM (object);

  switch (prop_id)
    {
    case PROP_PROJECT_FILE:
      g_value_set_object (value, self->project_file);
      break;

    case PROP_TARBALL_NAME:
      g_value_set_string (value,
                          ide_autotools_build_system_get_tarball_name (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_autotools_build_system_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  IdeAutotoolsBuildSystem *self = IDE_AUTOTOOLS_BUILD_SYSTEM (object);

  switch (prop_id)
    {
    case PROP_PROJECT_FILE:
      g_clear_object (&self->project_file);
      self->project_file = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
build_system_iface_init (IdeBuildSystemInterface *iface)
{
  iface->get_priority = ide_autotools_build_system_get_priority;
  iface->get_builder = ide_autotools_build_system_get_builder;
}

static void
ide_autotools_build_system_class_init (IdeAutotoolsBuildSystemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = ide_autotools_build_system_constructed;
  object_class->finalize = ide_autotools_build_system_finalize;
  object_class->get_property = ide_autotools_build_system_get_property;
  object_class->set_property = ide_autotools_build_system_set_property;

  properties [PROP_TARBALL_NAME] =
    g_param_spec_string ("tarball-name",
                         "Tarball Name",
                         "The name of the project tarball.",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_PROJECT_FILE] =
    g_param_spec_object ("project-file",
                         "Project File",
                         "The path of the project file.",
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, LAST_PROP, properties);
}

static void
ide_autotools_build_system_init (IdeAutotoolsBuildSystem *self)
{
}

static void
ide_autotools_build_system_parse_async (IdeAutotoolsBuildSystem *system,
                                        GFile                   *project_file,
                                        GCancellable            *cancellable,
                                        GAsyncReadyCallback      callback,
                                        gpointer                 user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (system));
  g_return_if_fail (G_IS_FILE (project_file));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (system, cancellable, callback, user_data);
  g_task_return_boolean (task, TRUE);
}

static gboolean
ide_autotools_build_system_parse_finish (IdeAutotoolsBuildSystem  *system,
                                         GAsyncResult             *result,
                                         GError                  **error)
{
  GTask *task = (GTask *)result;

  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (system), FALSE);
  g_return_val_if_fail (G_IS_TASK (task), FALSE);

  return g_task_propagate_boolean (task, error);
}

static void
parse_cb (GObject      *object,
          GAsyncResult *result,
          gpointer      user_data)
{
  IdeAutotoolsBuildSystem *self = (IdeAutotoolsBuildSystem *)object;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  g_return_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (self));
  g_return_if_fail (G_IS_TASK (task));

  if (!ide_autotools_build_system_parse_finish (self, result, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
discover_file_cb (GObject      *object,
                  GAsyncResult *result,
                  gpointer      user_data)
{
  IdeAutotoolsBuildSystem *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GFile) file = NULL;
  GError *error = NULL;

  g_return_if_fail (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  file = ide_autotools_build_system_discover_file_finish (self, result, &error);

  if (!file)
    {
      g_task_return_error (task, error);
      return;
    }

  g_object_set (self, "project-file", file, NULL);

  ide_autotools_build_system_parse_async (self,
                                          file,
                                          g_task_get_cancellable (task),
                                          parse_cb,
                                          g_object_ref (task));
}

static void
ide_autotools_build_system_init_async (GAsyncInitable      *initable,
                                       gint                 io_priority,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
  IdeAutotoolsBuildSystem *system = (IdeAutotoolsBuildSystem *)initable;
  g_autoptr(GTask) task = NULL;
  IdeContext *context;
  GFile *project_file;

  g_return_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (system));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (initable, cancellable, callback, user_data);
  context = ide_object_get_context (IDE_OBJECT (system));
  project_file = ide_context_get_project_file (context);

  ide_autotools_build_system_discover_file_async (system,
                                                  project_file,
                                                  cancellable,
                                                  discover_file_cb,
                                                  g_object_ref (task));
}

static gboolean
ide_autotools_build_system_init_finish (GAsyncInitable  *initable,
                                        GAsyncResult    *result,
                                        GError         **error)
{
  GTask *task = (GTask *)result;

  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (initable), FALSE);
  g_return_val_if_fail (G_IS_TASK (task), FALSE);

  return g_task_propagate_boolean (task, error);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = ide_autotools_build_system_init_async;
  iface->init_finish = ide_autotools_build_system_init_finish;
}

static void
simple_make_command_cb (GObject      *object,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  IdeSubprocess *subprocess = (IdeSubprocess *)object;
  g_autoptr(GTask) task = user_data;
  GError *error = NULL;

  if (!ide_subprocess_wait_check_finish (subprocess, result, &error))
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

static void
simple_make_command (GFile            *directory,
                     const gchar      *target,
                     GTask            *task,
                     IdeConfiguration *configuration)
{
  g_autoptr(IdeSubprocessLauncher) launcher = NULL;
  g_autoptr(IdeSubprocess) subprocess = NULL;
  g_autofree gchar *cwd = NULL;
  GCancellable *cancellable;
  IdeRuntime *runtime;
  GError *error = NULL;

  g_assert (G_IS_FILE (directory));
  g_assert (target != NULL);
  g_assert (G_IS_TASK (task));
  g_assert (IDE_IS_CONFIGURATION (configuration));

  cancellable = g_task_get_cancellable (task);

  if (!g_file_is_native (directory))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_REGULAR_FILE,
                               "Cannot use non-local directories.");
      return;
    }

  if (NULL == (runtime = ide_configuration_get_runtime (configuration)))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_FOUND,
                               "Failed to locate runtime");
      return;
    }

  if (NULL == (launcher = ide_runtime_create_launcher (runtime, &error)))
    {
      g_task_return_error (task, error);
      return;
    }

  cwd = g_file_get_path (directory);
  ide_subprocess_launcher_set_cwd (launcher, cwd);

  if (ide_runtime_contains_program_in_path (runtime, "gmake", cancellable))
    ide_subprocess_launcher_push_argv (launcher, "gmake");
  else
    ide_subprocess_launcher_push_argv (launcher, "make");

  ide_subprocess_launcher_push_argv (launcher, target);

  g_task_set_return_on_cancel (task, FALSE);

  if (g_task_return_error_if_cancelled (task))
    return;

  if (NULL == (subprocess = ide_subprocess_launcher_spawn (launcher, cancellable, &error)))
    {
      g_task_return_error (task, error);
      return;
    }

  ide_subprocess_wait_check_async (subprocess,
                                   cancellable,
                                   simple_make_command_cb,
                                   g_object_ref (task));
}

static void
ide_autotools_build_system_tags_build_async (IdeTagsBuilder      *builder,
                                             GFile               *file_or_directory,
                                             gboolean             recursive,
                                             GCancellable        *cancellable,
                                             GAsyncReadyCallback  callback,
                                             gpointer             user_data)
{
  IdeAutotoolsBuildSystem *self = (IdeAutotoolsBuildSystem *)builder;
  IdeConfigurationManager *config_manager;
  IdeConfiguration *configuration;
  IdeContext *context;
  g_autoptr(GTask) task = NULL;

  g_assert (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (self));
  g_assert (G_IS_FILE (file_or_directory));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  context = ide_object_get_context (IDE_OBJECT (self));
  config_manager = ide_context_get_configuration_manager (context);
  configuration = ide_configuration_manager_get_current (config_manager);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, ide_autotools_build_system_tags_build_async);
  simple_make_command (file_or_directory, "ctags", task, configuration);
}

static gboolean
ide_autotools_build_system_tags_build_finish (IdeTagsBuilder  *builder,
                                              GAsyncResult    *result,
                                              GError         **error)
{
  IdeAutotoolsBuildSystem *self = (IdeAutotoolsBuildSystem *)builder;
  GTask *task = (GTask *)result;

  g_return_val_if_fail (IDE_IS_AUTOTOOLS_BUILD_SYSTEM (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

  return g_task_propagate_boolean (task, error);
}

static void
tags_builder_iface_init (IdeTagsBuilderInterface *iface)
{
  iface->build_async = ide_autotools_build_system_tags_build_async;
  iface->build_finish = ide_autotools_build_system_tags_build_finish;
}
