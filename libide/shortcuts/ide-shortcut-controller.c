/* ide-shortcut-controller.c
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

#define G_LOG_DOMAIN "ide-shortcut-controller"

#include "ide-shortcut-context.h"
#include "ide-shortcut-controller.h"
#include "ide-shortcut-manager.h"

typedef struct
{
  /*
   * This is the widget for which we are the shortcut controller. There are
   * zero or one shortcut controller for a given widget. These are persistent
   * and dispatch events to the current IdeShortcutContext (which can be
   * changed upon theme changes or shortcuts emitting the ::set-context signal.
   */
  GtkWidget *widget;

  /*
   * This is the current context for the controller. These are collections of
   * shortcuts to signals, actions, etc. The context can be changed in reaction
   * to different events.
   */
  IdeShortcutContext *context;

  /*
   * This is a pointer to the root controller for the window. We register with
   * the root controller so that keybindings can be activated even when the
   * focus widget is somewhere else.
   */
  IdeShortcutController *root;

  /*
   * The root controller keeps track of the children controllers in the window.
   * Instead of allocating GList entries, we use an inline GList for the Queue
   * link nodes.
   */
  GQueue descendants;
  GList  descendants_link;

  /*
   * Signal handlers to react to various changes in the system.
   */
  gulong hierarchy_changed_handler;
  gulong widget_destroy_handler;
} IdeShortcutControllerPrivate;

enum {
  PROP_0,
  PROP_WIDGET,
  N_PROPS
};

enum {
  SET_CONTEXT,
  N_SIGNALS
};

struct _IdeShortcutController { GObject object; };
G_DEFINE_TYPE_WITH_PRIVATE (IdeShortcutController, ide_shortcut_controller, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];
static guint       signals [N_SIGNALS];
static GQuark      root_quark;
static GQuark      controller_quark;

static void ide_shortcut_controller_connect    (IdeShortcutController *self);
static void ide_shortcut_controller_disconnect (IdeShortcutController *self);

static gboolean
ide_shortcut_controller_is_mapped (IdeShortcutController *self)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);
  return priv->widget != NULL && gtk_widget_get_mapped (priv->widget);
}

static void
ide_shortcut_controller_add (IdeShortcutController *self,
                             IdeShortcutController *descendant)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);
  IdeShortcutControllerPrivate *dpriv = ide_shortcut_controller_get_instance_private (descendant);

  g_assert (IDE_IS_SHORTCUT_CONTROLLER (self));
  g_assert (IDE_IS_SHORTCUT_CONTROLLER (descendant));

  g_object_ref (descendant);

  if (ide_shortcut_controller_is_mapped (descendant))
    g_queue_push_head_link (&priv->descendants, &dpriv->descendants_link);
  else
    g_queue_push_tail_link (&priv->descendants, &dpriv->descendants_link);
}

static void
ide_shortcut_controller_remove (IdeShortcutController *self,
                                IdeShortcutController *descendant)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);
  IdeShortcutControllerPrivate *dpriv = ide_shortcut_controller_get_instance_private (descendant);

  g_assert (IDE_IS_SHORTCUT_CONTROLLER (self));
  g_assert (IDE_IS_SHORTCUT_CONTROLLER (descendant));

  g_queue_unlink (&priv->descendants, &dpriv->descendants_link);
}

static void
ide_shortcut_controller_widget_destroy (IdeShortcutController *self,
                                        GtkWidget             *widget)
{
  g_assert (IDE_IS_SHORTCUT_CONTROLLER (self));
  g_assert (GTK_IS_WIDGET (widget));

  ide_shortcut_controller_disconnect (self);
}

static void
ide_shortcut_controller_widget_hierarchy_changed (IdeShortcutController *self,
                                                  GtkWidget             *widget)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);
  GtkWidget *toplevel;

  g_assert (IDE_IS_SHORTCUT_CONTROLLER (self));
  g_assert (GTK_IS_WIDGET (widget));

  g_object_ref (self);

  /*
   * Here we register our controller with the toplevel controller. If that
   * widget doesn't yet have a placeholder toplevel controller, then we
   * create that and attach to it.
   *
   * The toplevel controller is used to dispatch events from the window
   * to any controller that could be activating for the window.
   */

  if (priv->root != NULL)
    {
      ide_shortcut_controller_remove (priv->root, self);
      g_clear_object (&priv->root);
    }

  toplevel = gtk_widget_get_toplevel (widget);

  if (toplevel != widget)
    {
      priv->root = g_object_get_qdata (G_OBJECT (toplevel), root_quark);
      if (priv->root == NULL)
        priv->root = ide_shortcut_controller_new (toplevel);
      ide_shortcut_controller_add (priv->root, self);
    }

  g_object_unref (self);
}

static void
ide_shortcut_controller_disconnect (IdeShortcutController *self)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);

  g_assert (IDE_IS_SHORTCUT_CONTROLLER (self));
  g_assert (GTK_IS_WIDGET (priv->widget));

  g_signal_handler_disconnect (priv->widget, priv->widget_destroy_handler);
  priv->widget_destroy_handler = 0;

  g_signal_handler_disconnect (priv->widget, priv->hierarchy_changed_handler);
  priv->hierarchy_changed_handler = 0;
}

static void
ide_shortcut_controller_connect (IdeShortcutController *self)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);

  g_assert (IDE_IS_SHORTCUT_CONTROLLER (self));
  g_assert (GTK_IS_WIDGET (priv->widget));

  priv->widget_destroy_handler =
    g_signal_connect_swapped (priv->widget,
                              "destroy",
                              G_CALLBACK (ide_shortcut_controller_widget_destroy),
                              self);

  priv->hierarchy_changed_handler =
    g_signal_connect_swapped (priv->widget,
                              "hierarchy-changed",
                              G_CALLBACK (ide_shortcut_controller_widget_hierarchy_changed),
                              self);

  ide_shortcut_controller_widget_hierarchy_changed (self, priv->widget);
}

static void
ide_shortcut_controller_set_widget (IdeShortcutController *self,
                                    GtkWidget             *widget)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);

  g_assert (IDE_IS_SHORTCUT_CONTROLLER (self));
  g_assert (GTK_IS_WIDGET (widget));

  if (widget != priv->widget)
    {
      if (priv->widget != NULL)
        {
          ide_shortcut_controller_disconnect (self);
          g_object_remove_weak_pointer (G_OBJECT (priv->widget), (gpointer *)&priv->widget);
          priv->widget = NULL;
        }

      if (widget != NULL && widget != priv->widget)
        {
          priv->widget = widget;
          g_object_add_weak_pointer (G_OBJECT (priv->widget), (gpointer *)&priv->widget);
          ide_shortcut_controller_connect (self);
        }

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_WIDGET]);
    }
}

static void
ide_shortcut_controller_real_set_context (IdeShortcutController *self,
                                          const gchar           *name)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);
  g_autoptr(IdeShortcutContext) context = NULL;
  IdeShortcutManager *manager;
  IdeShortcutTheme *theme;

  g_return_if_fail (IDE_IS_SHORTCUT_CONTROLLER (self));
  g_return_if_fail (name != NULL);

  manager = ide_shortcut_manager_get_default ();
  theme = ide_shortcut_manager_get_theme (manager);
  context = ide_shortcut_theme_find_context_by_name (theme, name);

  g_set_object (&priv->context, context);
}

static void
ide_shortcut_controller_finalize (GObject *object)
{
  IdeShortcutController *self = (IdeShortcutController *)object;
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);

  if (priv->widget != NULL)
    {
      g_object_remove_weak_pointer (G_OBJECT (priv->widget), (gpointer *)&priv->widget);
      priv->widget = NULL;
    }

  g_clear_object (&priv->context);
  g_clear_object (&priv->root);

  while (priv->descendants.length > 0)
    g_queue_unlink (&priv->descendants, priv->descendants.head);

  G_OBJECT_CLASS (ide_shortcut_controller_parent_class)->finalize (object);
}

static void
ide_shortcut_controller_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  IdeShortcutController *self = (IdeShortcutController *)object;
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_WIDGET:
      g_value_set_object (value, priv->widget);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_shortcut_controller_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  IdeShortcutController *self = (IdeShortcutController *)object;

  switch (prop_id)
    {
    case PROP_WIDGET:
      ide_shortcut_controller_set_widget (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_shortcut_controller_class_init (IdeShortcutControllerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ide_shortcut_controller_finalize;
  object_class->get_property = ide_shortcut_controller_get_property;
  object_class->set_property = ide_shortcut_controller_set_property;

  properties [PROP_WIDGET] =
    g_param_spec_object ("widget",
                         "Widget",
                         "The widget for the controller",
                         GTK_TYPE_WIDGET,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * IdeShortcutController:set-context:
   * @self: An #IdeShortcutController
   * @name: The name of the context
   *
   * This changes the current context on the #IdeShortcutController to be the
   * context matching @name. This is found by looking up the context by name
   * in the active #IdeShortcutTheme.
   */
  signals [SET_CONTEXT] =
    g_signal_new_class_handler ("set-context",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_CALLBACK (ide_shortcut_controller_real_set_context),
                                NULL, NULL, NULL,
                                G_TYPE_NONE, 1, G_TYPE_STRING);

  controller_quark = g_quark_from_static_string ("IDE_SHORTCUT_CONTROLLER");
  root_quark = g_quark_from_static_string ("IDE_SHORTCUT_CONTROLLER_ROOT");
}

static void
ide_shortcut_controller_init (IdeShortcutController *self)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);

  g_queue_init (&priv->descendants);

  priv->descendants_link.data = self;
}

IdeShortcutController *
ide_shortcut_controller_new (GtkWidget *widget)
{
  IdeShortcutController *ret;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

  if (NULL != (ret = g_object_get_qdata (G_OBJECT (widget), controller_quark)))
    return g_object_ref (ret);

  ret = g_object_new (IDE_TYPE_SHORTCUT_CONTROLLER,
                      "widget", widget,
                      NULL);

  g_object_set_qdata_full (G_OBJECT (widget),
                           controller_quark,
                           g_object_ref (ret),
                           g_object_unref);

  return ret;
}

/**
 * ide_shortcut_controller_find:
 *
 * Finds the registered #IdeShortcutController for a widget.
 *
 * Returns: (nullable) (transfer none): An #IdeShortcutController or %NULL.
 */
IdeShortcutController *
ide_shortcut_controller_find (GtkWidget *widget)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

  return g_object_get_qdata (G_OBJECT (widget), controller_quark);
}

static IdeShortcutContext *
ide_shortcut_controller_get_context (IdeShortcutController *self)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);

  g_return_val_if_fail (IDE_IS_SHORTCUT_CONTROLLER (self), NULL);

  if (priv->widget == NULL)
    return NULL;

  if (priv->context == NULL)
    {
      IdeShortcutManager *manager;
      IdeShortcutTheme *theme;

      manager = ide_shortcut_manager_get_default ();
      theme = ide_shortcut_manager_get_theme (manager);

      priv->context = ide_shortcut_theme_find_default_context (theme, priv->widget);
    }

  return priv->context;
}

/**
 * ide_shortcut_controller_handle_event:
 * @self: An #IdeShortcutController
 * @event: A #GdkEventKey
 *
 * This function uses @event to determine if the current context has a shortcut
 * registered matching the event. If so, the shortcut will be dispatched and
 * %TRUE is returned.
 *
 * Otherwise, %FALSE is returned.
 *
 * Returns: %TRUE if @event has been handled, otherwise %FALSE.
 */
gboolean
ide_shortcut_controller_handle_event (IdeShortcutController *self,
                                      const GdkEventKey     *event)
{
  IdeShortcutControllerPrivate *priv = ide_shortcut_controller_get_instance_private (self);
  IdeShortcutContext *context;

  g_return_val_if_fail (IDE_IS_SHORTCUT_CONTROLLER (self), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  if (priv->widget == NULL ||
      !gtk_widget_get_visible (priv->widget) ||
      !gtk_widget_is_sensitive (priv->widget))
    return FALSE;

  context = ide_shortcut_controller_get_context (self);

  if (context != NULL)
    {
      if (ide_shortcut_context_activate (context, priv->widget, event))
        return GDK_EVENT_STOP;
    }

  for (GList *iter = priv->descendants.head; iter != NULL; iter = iter->next)
    {
      IdeShortcutController *descendant = iter->data;

      if (ide_shortcut_controller_handle_event (descendant, event))
        return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}
