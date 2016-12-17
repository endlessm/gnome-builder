/* gbp-flatpak-source-archive.c
 *
 * Copyright (C) 2016 Endless Mobile, Inc.
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libsoup/soup.h>

#include "config.h"
#include "gbp-flatpak-source-archive.h"

/* This file includes modified code from
 * flatpak/builder/builder-source-archive.c
 * Written by Alexander Larsson, originally licensed under GPL 2.1.
 * Copyright Red Hat, Inc. 2015
 */

typedef enum {
  UNKNOWN,
  RPM,
  TAR,
  TAR_GZIP,
  TAR_COMPRESS,
  TAR_BZIP2,
  TAR_LZIP,
  TAR_LZMA,
  TAR_LZOP,
  TAR_XZ,
  ZIP
} ArchiveType;

gboolean
is_tar (ArchiveType type)
{
  return (type >= TAR) && (type <= TAR_XZ);
}

const char *
tar_decompress_flag (ArchiveType type)
{
  if (type == TAR_GZIP)
    return "-z";
  else if (type == TAR_COMPRESS)
    return "-Z";
  else if (type == TAR_BZIP2)
    return "-j";
  else if (type == TAR_LZIP)
    return "--lzip";
  else if (type == TAR_LZMA)
    return "--lzma";
  else if (type == TAR_LZOP)
    return "--lzop";
  else if (type == TAR_XZ)
    return "-J";
  else
    return NULL;
}

ArchiveType
get_type (GFile *archivefile)
{
  g_autofree gchar *base_name = NULL;
  g_autofree gchar *lower = NULL;

  base_name = g_file_get_basename (archivefile);
  lower = g_ascii_strdown (base_name, -1);

  if (g_str_has_suffix (lower, ".tar"))
    return TAR;

  if (g_str_has_suffix (lower, ".tar.gz") ||
      g_str_has_suffix (lower, ".tgz") ||
      g_str_has_suffix (lower, ".taz"))
    return TAR_GZIP;

  if (g_str_has_suffix (lower, ".tar.Z") ||
      g_str_has_suffix (lower, ".taZ"))
    return TAR_COMPRESS;

  if (g_str_has_suffix (lower, ".tar.bz2") ||
      g_str_has_suffix (lower, ".tz2") ||
      g_str_has_suffix (lower, ".tbz2") ||
      g_str_has_suffix (lower, ".tbz"))
    return TAR_BZIP2;

  if (g_str_has_suffix (lower, ".tar.lz"))
    return TAR_LZIP;

  if (g_str_has_suffix (lower, ".tar.lzma") ||
      g_str_has_suffix (lower, ".tlz"))
    return TAR_LZMA;

  if (g_str_has_suffix (lower, ".tar.lzo"))
    return TAR_LZOP;

  if (g_str_has_suffix (lower, ".tar.xz"))
    return TAR_XZ;

  if (g_str_has_suffix (lower, ".zip"))
    return ZIP;

  if (g_str_has_suffix (lower, ".rpm"))
    return RPM;

  return UNKNOWN;
}

typedef struct
{
  GError    *error;
  GError    *splice_error;
  GMainLoop *loop;
  int        refs;
} SpawnData;

static void
spawn_data_exit (SpawnData *data)
{
  data->refs--;
  if (data->refs == 0)
    g_main_loop_quit (data->loop);
}

static void
spawn_output_spliced_cb (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  SpawnData *data = user_data;

  g_output_stream_splice_finish (G_OUTPUT_STREAM (obj), result, &data->splice_error);
  spawn_data_exit (data);
}

static void
spawn_exit_cb (GObject      *obj,
               GAsyncResult *result,
               gpointer      user_data)
{
  SpawnData *data = user_data;

  g_subprocess_wait_check_finish (G_SUBPROCESS (obj), result, &data->error);
  spawn_data_exit (data);
}

static gboolean
archive_spawnv (GFile                *dir,
                char                **output,
                GError              **error,
                const gchar * const  *argv)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subp = NULL;
  GInputStream *in;
  g_autoptr(GOutputStream) out = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  SpawnData data = {0};
  g_autofree gchar *commandline = NULL;

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);

  if (output)
    g_subprocess_launcher_set_flags (launcher, G_SUBPROCESS_FLAGS_STDOUT_PIPE);

  if (dir)
    {
      g_autofree char *path = g_file_get_path (dir);
      g_subprocess_launcher_set_cwd (launcher, path);
    }

  commandline = g_strjoinv (" ", (gchar **) argv);
  g_debug ("Running '%s'", commandline);

  subp = g_subprocess_launcher_spawnv (launcher, argv, error);

  if (subp == NULL)
    return FALSE;

  loop = g_main_loop_new (NULL, FALSE);

  data.loop = loop;
  data.refs = 1;

  if (output)
    {
      data.refs++;
      in = g_subprocess_get_stdout_pipe (subp);
      out = g_memory_output_stream_new_resizable ();
      g_output_stream_splice_async (out,
                                    in,
                                    G_OUTPUT_STREAM_SPLICE_NONE,
                                    0,
                                    NULL,
                                    spawn_output_spliced_cb,
                                    &data);
    }

  g_subprocess_wait_async (subp, NULL, spawn_exit_cb, &data);

  g_main_loop_run (loop);

  if (data.error)
    {
      g_propagate_error (error, data.error);
      g_clear_error (&data.splice_error);
      return FALSE;
    }

  if (out)
    {
      if (data.splice_error)
        {
          g_propagate_error (error, data.splice_error);
          return FALSE;
        }

      /* Null terminate */
      g_output_stream_write (out, "\0", 1, NULL, NULL);
      g_output_stream_close (out, NULL, NULL);
      *output = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (out));
    }

  return TRUE;
}

static gboolean
archive_spawn (GFile       *dir,
               char       **output,
               GError     **error,
               const gchar *argv0,
               va_list      ap)
{
  g_autoptr(GPtrArray) args = NULL;
  const gchar *arg;

  args = g_ptr_array_new ();
  g_ptr_array_add (args, (gchar *) argv0);
  while ((arg = va_arg (ap, const gchar *)))
    g_ptr_array_add (args, (gchar *) arg);
  g_ptr_array_add (args, NULL);

  return archive_spawnv (dir, output, error, (const gchar * const *) args->pdata);
}

static gboolean
tar (GFile   *dir,
     GError **error,
     ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = archive_spawn (dir, NULL, error, "tar", ap);
  va_end (ap);

  return res;
}

static gboolean
unzip (GFile   *dir,
       GError **error,
       ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = archive_spawn (dir, NULL, error, "unzip", ap);
  va_end (ap);

  return res;
}

static gboolean
unrpm (GFile   *dir,
       const char *rpm_path,
       GError **error)
{
  const gchar *argv[] = {
    "sh", "-c", "rpm2cpio \"$1\" | cpio -i -d",
    "sh", /* shell's $0 */
    rpm_path, /* shell's $1 */
    NULL
  };

  return archive_spawnv (dir, NULL, error, argv);
}

static gboolean
strip_components_into (GFile   *dest,
                       GFile   *src,
                       int      level,
                       GError **error)
{
  g_autoptr(GFileEnumerator) dir_enum = NULL;
  g_autoptr(GFileInfo) child_info = NULL;
  GError *temp_error = NULL;

  dir_enum = g_file_enumerate_children (src, "standard::name,standard::type",
                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                        NULL, error);
  if (!dir_enum)
    return FALSE;

  while ((child_info = g_file_enumerator_next_file (dir_enum, NULL, &temp_error)))
    {
      g_autoptr(GFile) child = NULL;
      g_autoptr(GFile) dest_child = NULL;

      child = g_file_enumerator_get_child (dir_enum, child_info);

      if (g_file_info_get_file_type (child_info) == G_FILE_TYPE_DIRECTORY &&
          level > 0)
        {
          if (!strip_components_into (dest, child, level - 1, error))
            return FALSE;

          g_clear_object (&child_info);
          continue;
        }

      dest_child = g_file_get_child (dest, g_file_info_get_name (child_info));
      if (!g_file_move (child, dest_child, G_FILE_COPY_NONE, NULL, NULL, NULL, error))
        return FALSE;

      g_clear_object (&child_info);
    }

  if (temp_error != NULL)
    {
      g_propagate_error (error, temp_error);
      return FALSE;
    }

  return g_file_delete (src, NULL, error);
}

static GFile *
create_uncompress_directory (GFile   *dest,
                             int      strip_components,
                             GError **error)
{
  GFile *uncompress_dest = NULL;

  if (strip_components > 0)
    {
      g_autoptr(GFile) tmp_dir_template = g_file_get_child (dest, ".uncompressXXXXXX");
      g_autofree char *tmp_dir_path = g_file_get_path (tmp_dir_template);

      if (g_mkdtemp (tmp_dir_path) == NULL)
        {
          int saved_errno = errno;
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Can't create uncompress directory: %s",
                       g_strerror (saved_errno));
          return NULL;
        }

      uncompress_dest = g_file_new_for_path (tmp_dir_path);
    }
  else
    {
      uncompress_dest = g_object_ref (dest);
    }

  return uncompress_dest;
}

SoupSession *
get_soup_session (void)
{
  return soup_session_new_with_options (SOUP_SESSION_USER_AGENT, PACKAGE_NAME,
                                        NULL);
}

static GBytes *
download_uri (SoupURI *uri,
              GError **error)
{
  g_autoptr(SoupSession) session;
  g_autoptr(SoupRequest) req = NULL;
  g_autoptr(GInputStream) input = NULL;
  g_autoptr(GOutputStream) out = NULL;

  session = get_soup_session ();

  req = soup_session_request_uri (session, uri, error);
  if (req == NULL)
    return NULL;

  input = soup_request_send (req, NULL, error);
  if (input == NULL)
    return NULL;

  out = g_memory_output_stream_new_resizable ();
  if (!g_output_stream_splice (out,
                               input,
                               G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET | G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
                               NULL,
                               error))
    return NULL;

  return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (out));
}

static gboolean
download_archive (SoupURI      *uri,
                  const gchar  *sha,
                  GFile        *archive_file,
                  GError      **error)
{
  g_autoptr(GBytes) content = NULL;
  g_autofree gchar *sha256 = NULL;

  content = download_uri (uri, error);
  if (content == NULL)
    return FALSE;

  sha256 = g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, content);
  if (g_strcmp0 (sha256, sha) != 0)
    {
      g_autofree gchar *path = g_file_get_path (archive_file);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Wrong sha256 for %s, expected %s, was %s",
                   path, sha, sha256);
      return FALSE;
    }

  return g_file_replace_contents (archive_file,
                                  g_bytes_get_data (content, NULL),
                                  g_bytes_get_size (content),
                                  NULL, FALSE, G_FILE_CREATE_NONE, NULL,
                                  NULL, error);
}

static gboolean
extract_archive (GFile   *destination,
                 GFile   *archive_file,
                 int      strip_components,
                 GError **error)
{
  ArchiveType type;
  g_autofree char *archive_path = NULL;

  archive_path = g_file_get_path (archive_file);

  g_debug ("Uncompress %s\n", archive_path);

  type = get_type (archive_file);

  if (is_tar (type))
    {
      g_autofree char *strip_components_str = g_strdup_printf ("--strip-components=%u", strip_components);

      /* tar_decompress_flag can return NULL, so put it last */
      if (!tar (destination, error, "xf", archive_path, "--no-same-owner",
                strip_components_str, tar_decompress_flag (type), NULL))
        return FALSE;
    }
  else if (type == ZIP)
    {
      g_autoptr(GFile) zip_dest = NULL;

      zip_dest = create_uncompress_directory (destination, strip_components, error);
      if (zip_dest == NULL)
        return FALSE;

      if (!unzip (zip_dest, error, archive_path, NULL))
        return FALSE;

      if (strip_components > 0 &&
          !strip_components_into (destination, zip_dest, strip_components, error))
        return FALSE;
    }
  else if (type == RPM)
    {
      g_autoptr(GFile) rpm_dest = NULL;

      rpm_dest = create_uncompress_directory (destination, strip_components, error);
      if (rpm_dest == NULL)
        return FALSE;

      if (!unrpm (rpm_dest, archive_path, error))
        return FALSE;

      if (strip_components > 0 &&
          !strip_components_into (destination, rpm_dest, strip_components, error))
        return FALSE;
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unknown archive format of '%s'", archive_path);
      return FALSE;
    }
  return TRUE;
}

GFile *
fetch_archive (const gchar  *url,
               const gchar  *sha,
               const gchar  *module_name,
               GFile        *destination,
               GError      **error)
{
  g_autoptr(GFile) archive_file = NULL;
  g_autoptr(GFile) source_dir = NULL;
  g_autoptr(SoupURI) uri = NULL;
  g_autofree char *archive_name = NULL;
  GError *local_error = NULL;

  source_dir = g_file_get_child (destination, module_name);
  if (!g_file_make_directory_with_parents (source_dir, NULL, &local_error))
    {
      if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        {
          g_propagate_error (error, local_error);
          return NULL;
        }

      g_error_free (local_error);
    }

  uri = soup_uri_new (url);
  archive_name = g_path_get_basename (soup_uri_get_path (uri));
  archive_file = g_file_get_child (source_dir, archive_name);

  if (!download_archive (uri, sha, archive_file, error))
    return NULL;

  if (!extract_archive (source_dir, archive_file, 1, error))
    return NULL;

  return g_steal_pointer (&source_dir);
}
