/* ide-shortcut-theme.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "ide-shortcut-theme"

#include "ide-shortcut-theme.h"

typedef struct
{
  gchar      *name;
  GHashTable *contexts;
} IdeShortcutThemePrivate;

enum {
  PROP_0,
  PROP_NAME,
  N_PROPS
};

struct _IdeShortcutTheme { GObject object; };
G_DEFINE_TYPE_WITH_PRIVATE (IdeShortcutTheme, ide_shortcut_theme, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
ide_shortcut_theme_finalize (GObject *object)
{
  IdeShortcutTheme *self = (IdeShortcutTheme *)object;
  IdeShortcutThemePrivate *priv = ide_shortcut_theme_get_instance_private (self);

  g_clear_pointer (&priv->name, g_free);
  g_clear_pointer (&priv->contexts, g_hash_table_unref);

  G_OBJECT_CLASS (ide_shortcut_theme_parent_class)->finalize (object);
}

static void
ide_shortcut_theme_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  IdeShortcutTheme *self = (IdeShortcutTheme *)object;

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, ide_shortcut_theme_get_name (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_shortcut_theme_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  IdeShortcutTheme *self = (IdeShortcutTheme *)object;
  IdeShortcutThemePrivate *priv = ide_shortcut_theme_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_shortcut_theme_class_init (IdeShortcutThemeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ide_shortcut_theme_finalize;
  object_class->get_property = ide_shortcut_theme_get_property;
  object_class->set_property = ide_shortcut_theme_set_property;

  properties [PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "The name of the theme",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
ide_shortcut_theme_init (IdeShortcutTheme *self)
{
  IdeShortcutThemePrivate *priv = ide_shortcut_theme_get_instance_private (self);

  priv->contexts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

const gchar *
ide_shortcut_theme_get_name (IdeShortcutTheme *self)
{
  IdeShortcutThemePrivate *priv = ide_shortcut_theme_get_instance_private (self);

  g_return_val_if_fail (IDE_IS_SHORTCUT_THEME (self), NULL);

  return priv->name;
}

/**
 * ide_shortcut_theme_find_context_by_name:
 * @self: An #IdeShortcutContext
 * @name: The name of the context
 *
 * Gets the context named @name. If the context does not exist, it will
 * be created.
 *
 * Returns: (not nullable) (transfer full): An #IdeShortcutContext
 */
IdeShortcutContext *
ide_shortcut_theme_find_context_by_name (IdeShortcutTheme *self,
                                         const gchar      *name)
{
  IdeShortcutThemePrivate *priv = ide_shortcut_theme_get_instance_private (self);
  IdeShortcutContext *ret;

  g_return_val_if_fail (IDE_IS_SHORTCUT_THEME (self), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (NULL == (ret = g_hash_table_lookup (priv->contexts, name)))
    {
      ret = ide_shortcut_context_new (name);
      g_hash_table_insert (priv->contexts, g_strdup (name), ret);
    }

  return g_object_ref (ret);
}

static IdeShortcutContext *
ide_shortcut_theme_find_default_context_by_type (IdeShortcutTheme *self,
                                                 GType             type)
{
  IdeShortcutThemePrivate *priv = ide_shortcut_theme_get_instance_private (self);
  g_autofree gchar *name = NULL;

  g_return_val_if_fail (IDE_IS_SHORTCUT_THEME (self), NULL);
  g_return_val_if_fail (g_type_is_a (type, GTK_TYPE_WIDGET), NULL);

  name = g_strdup_printf ("%s::%s::default", priv->name, g_type_name (type));

  return ide_shortcut_theme_find_context_by_name (self, name);
}

/**
 * ide_shortcut_theme_find_default_context:
 *
 * Finds the default context in the theme for @widget.
 *
 * Returns: (nullable) (transfer full): An #IdeShortcutContext or %NULL.
 */
IdeShortcutContext *
ide_shortcut_theme_find_default_context (IdeShortcutTheme *self,
                                         GtkWidget        *widget)
{
  g_return_val_if_fail (IDE_IS_SHORTCUT_THEME (self), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

  return ide_shortcut_theme_find_default_context_by_type (self, G_OBJECT_TYPE (widget));
}
