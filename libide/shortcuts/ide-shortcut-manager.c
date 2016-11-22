/* ide-shortcut-manager.c
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

#define G_LOG_DOMAIN "ide-shortcut-manager.h"

#include "ide-shortcut-controller.h"
#include "ide-shortcut-manager.h"

typedef struct
{
  IdeShortcutTheme *theme;
} IdeShortcutManagerPrivate;

enum {
  PROP_0,
  PROP_THEME,
  N_PROPS
};

struct _IdeShortcutManager { GObject object; };
G_DEFINE_TYPE_WITH_PRIVATE (IdeShortcutManager, ide_shortcut_manager, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
ide_shortcut_manager_finalize (GObject *object)
{
  IdeShortcutManager *self = (IdeShortcutManager *)object;
  IdeShortcutManagerPrivate *priv = ide_shortcut_manager_get_instance_private (self);

  g_clear_object (&priv->theme);

  G_OBJECT_CLASS (ide_shortcut_manager_parent_class)->finalize (object);
}

static void
ide_shortcut_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  IdeShortcutManager *self = (IdeShortcutManager *)object;

  switch (prop_id)
    {
    case PROP_THEME:
      g_value_set_object (value, ide_shortcut_manager_get_theme (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_shortcut_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  IdeShortcutManager *self = (IdeShortcutManager *)object;

  switch (prop_id)
    {
    case PROP_THEME:
      ide_shortcut_manager_set_theme (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_shortcut_manager_class_init (IdeShortcutManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ide_shortcut_manager_finalize;
  object_class->get_property = ide_shortcut_manager_get_property;
  object_class->set_property = ide_shortcut_manager_set_property;

  properties [PROP_THEME] =
    g_param_spec_object ("theme",
                         "Theme",
                         "The current key theme.",
                         IDE_TYPE_SHORTCUT_THEME,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
ide_shortcut_manager_init (IdeShortcutManager *self)
{
}

/**
 * ide_shortcut_manager_get_default:
 *
 * Gets the singleton #IdeShortcutManager for the process.
 *
 * Returns: (transfer none) (not nullable): An #IdeShortcutManager.
 */
IdeShortcutManager *
ide_shortcut_manager_get_default (void)
{
  static IdeShortcutManager *instance;

  if (instance == NULL)
    {
      instance = g_object_new (IDE_TYPE_SHORTCUT_MANAGER, NULL);
      g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
    }

  return instance;
}

/**
 * ide_shortcut_manager_get_theme:
 *
 * Gets the "theme" property.
 *
 * Returns: (transfer none) (not nullable): An #IdeShortcutTheme.
 */
IdeShortcutTheme *
ide_shortcut_manager_get_theme (IdeShortcutManager *self)
{
  IdeShortcutManagerPrivate *priv = ide_shortcut_manager_get_instance_private (self);

  g_return_val_if_fail (IDE_IS_SHORTCUT_MANAGER (self), NULL);

  if (priv->theme == NULL)
    priv->theme = g_object_new (IDE_TYPE_SHORTCUT_THEME,
                                "name", "default",
                                NULL);

  return priv->theme;
}

/**
 * ide_shortcut_manager_set_theme:
 * @self: An #IdeShortcutManager
 * @theme: (not nullable): An #IdeShortcutTheme
 *
 * Sets the theme for the shortcut manager.
 */
void
ide_shortcut_manager_set_theme (IdeShortcutManager *self,
                                IdeShortcutTheme   *theme)
{
  IdeShortcutManagerPrivate *priv = ide_shortcut_manager_get_instance_private (self);

  g_return_if_fail (IDE_IS_SHORTCUT_MANAGER (self));
  g_return_if_fail (IDE_IS_SHORTCUT_THEME (theme));

  /*
   * It is important that IdeShortcutController instances watch for
   * notify::theme so that they can reset their state. Otherwise, we
   * could be transitioning between incorrect contexts.
   */

  if (g_set_object (&priv->theme, theme))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_THEME]);
}

/**
 * ide_shortcut_manager_handle_event:
 * @self: (nullable): An #IdeShortcutManager
 * @toplevel: A #GtkWidget or %NULL.
 * @event: A #GdkEventKey event to handle.
 *
 * This function will try to dispatch @event to the proper widget and
 * #IdeShortcutContext. If the event is handled, then %TRUE is returned.
 *
 * You should call this from #GtkWidget::key-press-event handler in your
 * #GtkWindow toplevel.
 *
 * Returns: %TRUE if the event was handled.
 */
gboolean
ide_shortcut_manager_handle_event (IdeShortcutManager *self,
                                   const GdkEventKey  *event,
                                   GtkWidget          *toplevel)
{
  GtkWidget *widget;
  GtkWidget *focus;
  GdkModifierType modifier;

  if (self == NULL)
    self = ide_shortcut_manager_get_default ();

  g_return_val_if_fail (IDE_IS_SHORTCUT_MANAGER (self), FALSE);
  g_return_val_if_fail (GTK_IS_WINDOW (toplevel), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  if (event->type != GDK_KEY_PRESS)
    return GDK_EVENT_PROPAGATE;

  modifier = event->state & gtk_accelerator_get_default_mod_mask ();
  widget = focus = gtk_window_get_focus (GTK_WINDOW (toplevel));

  while (widget != NULL)
    {
      g_autoptr(GPtrArray) sets = NULL;
      IdeShortcutController *controller;
      GtkStyleContext *style_context;

      if (NULL != (controller = ide_shortcut_controller_find (widget)))
        {
          if (ide_shortcut_controller_handle_event (controller, event))
            return GDK_EVENT_STOP;
        }

      style_context = gtk_widget_get_style_context (widget);
      gtk_style_context_get (style_context,
                             gtk_style_context_get_state (style_context),
                             "-gtk-key-bindings", &sets,
                             NULL);

      if (sets != NULL)
        {
          for (guint i = 0; i < sets->len; i++)
            {
              GtkBindingSet *set = g_ptr_array_index (sets, i);

              if (gtk_binding_set_activate (set, event->keyval, modifier, G_OBJECT (widget)))
                return GDK_EVENT_STOP;
            }
        }

      /*
       * If this widget is also our focus, then try to activate the
       * default keybindings for the widget.
       *
       * TODO: We should probably allow contexts to override this.
       */
      if (widget == focus)
        {
          GtkBindingSet *set = gtk_binding_set_by_class (G_OBJECT_GET_CLASS (widget));

          if (gtk_binding_set_activate (set, event->keyval, modifier, G_OBJECT (widget)))
            return GDK_EVENT_STOP;
        }

      widget = gtk_widget_get_parent (widget);
    }

  return GDK_EVENT_PROPAGATE;
}
