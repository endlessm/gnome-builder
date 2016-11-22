/* ide-shortcut-context.c
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

#define G_LOG_DOMAIN "ide-shortcut-context"

#include <gobject/gvaluecollector.h>
#include <string.h>

#include "ide-shortcut-context.h"
#include "ide-shortcut-controller.h"

typedef enum
{
  SHORTCUT_ACTION = 1,
  SHORTCUT_SIGNAL,
} ShortcutType;

typedef struct _Shortcut
{
  ShortcutType    type;
  GdkModifierType modifier;
  guint           keyval;
  union {
    struct {
      const gchar *prefix;
      const gchar *name;
      GVariant    *param;
    } action;
    struct {
      const gchar *name;
      GQuark       detail;
      GArray      *params;
    } signal;
  };
  struct _Shortcut *next;
} Shortcut;

typedef struct
{
  gchar      *name;
  GHashTable *keymap;
} IdeShortcutContextPrivate;

enum {
  PROP_0,
  PROP_NAME,
  N_PROPS
};

struct _IdeShortcutContext { GObject object; };
G_DEFINE_TYPE_WITH_PRIVATE (IdeShortcutContext, ide_shortcut_context, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];

static void
translate_keyval (guint             *keyval,
                  GdkModifierType   *modifier)
{
  *modifier = *modifier & gtk_accelerator_get_default_mod_mask () & ~GDK_RELEASE_MASK;

  if (*modifier & GDK_SHIFT_MASK)
    {
      if (*keyval == GDK_KEY_Tab)
        *keyval = GDK_KEY_ISO_Left_Tab;
      else
        *keyval = gdk_keyval_to_upper (*keyval);
    }
}

static void
shortcut_free (gpointer data)
{
  Shortcut *shortcut = data;

  if (shortcut != NULL)
    {
      g_clear_pointer (&shortcut->next, shortcut_free);

      switch (shortcut->type)
        {
        case SHORTCUT_ACTION:
          g_clear_pointer (&shortcut->action.param, g_variant_unref);
          break;

        case SHORTCUT_SIGNAL:
          g_array_unref (shortcut->signal.params);
          break;

        default:
          g_assert_not_reached ();
        }

      g_slice_free (Shortcut, shortcut);
    }
}

static guint
shortcut_hash (gconstpointer data)
{
  const Shortcut *shortcut = data;

  return shortcut->keyval ^ shortcut->modifier;
}

static gboolean
shortcut_equal (gconstpointer a,
                gconstpointer b)
{
  const Shortcut *as = a;
  const Shortcut *bs = b;

  return as->keyval == bs->keyval && as->modifier == bs->modifier;
}

static gboolean
widget_action (GtkWidget   *widget,
               const gchar *prefix,
               const gchar *action_name,
               GVariant    *parameter)
{
  GtkWidget *toplevel;
  GApplication *app;
  GActionGroup *group = NULL;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (prefix != NULL);
  g_assert (action_name != NULL);

  app = g_application_get_default ();
  toplevel = gtk_widget_get_toplevel (widget);

  while ((group == NULL) && (widget != NULL))
    {
      group = gtk_widget_get_action_group (widget, prefix);

      if G_UNLIKELY (GTK_IS_POPOVER (widget))
        {
          GtkWidget *relative_to;

          relative_to = gtk_popover_get_relative_to (GTK_POPOVER (widget));

          if (relative_to != NULL)
            widget = relative_to;
          else
            widget = gtk_widget_get_parent (widget);
        }
      else
        {
          widget = gtk_widget_get_parent (widget);
        }
    }

  if (!group && g_str_equal (prefix, "win") && G_IS_ACTION_GROUP (toplevel))
    group = G_ACTION_GROUP (toplevel);

  if (!group && g_str_equal (prefix, "app") && G_IS_ACTION_GROUP (app))
    group = G_ACTION_GROUP (app);

  if (group && g_action_group_has_action (group, action_name))
    {
      g_action_group_activate_action (group, action_name, parameter);
      return TRUE;
    }

  g_warning ("Failed to locate action %s.%s", prefix, action_name);

  return FALSE;
}

static gboolean
shortcut_action_activate (Shortcut          *shortcut,
                          GtkWidget         *widget,
                          const GdkEventKey *event)
{
  g_assert (shortcut != NULL);
  g_assert (GTK_IS_WIDGET (widget));
  g_assert (event != NULL);

  return widget_action (widget,
                        shortcut->action.prefix,
                        shortcut->action.name,
                        shortcut->action.param);
}

static gboolean
find_instance_and_signal (GtkWidget          *widget,
                          const gchar        *signal_name,
                          gpointer           *instance,
                          GSignalQuery       *query)
{
  IdeShortcutController *controller;

  g_assert (GTK_IS_WIDGET (widget));
  g_assert (signal_name != NULL);
  g_assert (instance != NULL);
  g_assert (query != NULL);

  *instance = NULL;

  /*
   * First we want to see if we can resolve the signal on the widgets
   * controller (if there is one). This allows us to change contexts
   * from signals without installing signals on the actual widgets.
   */

  controller = ide_shortcut_controller_find (widget);

  if (controller != NULL)
    {
      guint signal_id;

      signal_id = g_signal_lookup (signal_name, G_OBJECT_TYPE (controller));

      if (signal_id != 0)
        {
          g_signal_query (signal_id, query);
          *instance = controller;
          return TRUE;
        }
    }

  /*
   * This diverts from Gtk signal keybindings a bit in that we
   * allow you to activate a signal on any widget in the focus
   * hierarchy starting from the provided widget up.
   */

  while (widget != NULL)
    {
      guint signal_id;

      signal_id = g_signal_lookup (signal_name, G_OBJECT_TYPE (widget));

      if (signal_id != 0)
        {
          g_signal_query (signal_id, query);
          *instance = widget;
          return TRUE;
        }

      widget = gtk_widget_get_parent (widget);
    }

  return FALSE;
}

static gboolean
shortcut_signal_activate (Shortcut          *shortcut,
                          GtkWidget         *widget,
                          const GdkEventKey *event)
{
  GValue *params;
  GValue return_value = { 0 };
  GSignalQuery query;
  gpointer instance = NULL;

  g_assert (shortcut != NULL);
  g_assert (GTK_IS_WIDGET (widget));
  g_assert (event != NULL);

  if (!find_instance_and_signal (widget, shortcut->signal.name, &instance, &query))
    {
      g_warning ("Failed to locate signal %s in hierarchy of %s",
                 shortcut->signal.name, G_OBJECT_TYPE_NAME (widget));
      return TRUE;
    }

  if (query.n_params != shortcut->signal.params->len)
    goto parameter_mismatch;

  for (guint i = 0; i < query.n_params; i++)
    {
      if (!G_VALUE_HOLDS (&g_array_index (shortcut->signal.params, GValue, i), query.param_types[i]))
        goto parameter_mismatch;
    }

  params = g_new0 (GValue, 1 + query.n_params);
  g_value_init_from_instance (&params[0], instance);
  for (guint i = 0; i < query.n_params; i++)
    {
      GValue *src_value = &g_array_index (shortcut->signal.params, GValue, i);

      g_value_init (&params[1+i], G_VALUE_TYPE (src_value));
      g_value_copy (src_value, &params[1+i]);
    }

  if (query.return_type != G_TYPE_NONE)
    g_value_init (&return_value, query.return_type);

  g_signal_emitv (params, query.signal_id, shortcut->signal.detail, &return_value);

  for (guint i = 0; i < query.n_params + 1; i++)
    g_value_unset (&params[i]);
  g_free (params);

  return GDK_EVENT_STOP;

parameter_mismatch:
  g_warning ("The parameters are not correct for signal %s",
             shortcut->signal.name);

  /*
   * If there was a bug with the signal descriptor, we still want
   * to swallow the event to keep it from propagating further.
   */

  return GDK_EVENT_STOP;
}

static gboolean
shortcut_activate (Shortcut          *shortcut,
                   GtkWidget         *widget,
                   const GdkEventKey *event)
{
  gboolean handled = FALSE;

  g_assert (shortcut != NULL);
  g_assert (GTK_IS_WIDGET (widget));
  g_assert (event != NULL);

  for (; shortcut != NULL; shortcut = shortcut->next)
    {
      switch (shortcut->type)
        {
        case SHORTCUT_ACTION:
          handled |= shortcut_action_activate (shortcut, widget, event);
          break;

        case SHORTCUT_SIGNAL:
          handled |= shortcut_signal_activate (shortcut, widget, event);
          break;

        default:
          g_assert_not_reached ();
          return FALSE;
        }
    }

  return handled;
}

static void
ide_shortcut_context_finalize (GObject *object)
{
  IdeShortcutContext *self = (IdeShortcutContext *)object;
  IdeShortcutContextPrivate *priv = ide_shortcut_context_get_instance_private (self);

  g_clear_pointer (&priv->name, g_free);
  g_clear_pointer (&priv->keymap, g_hash_table_unref);

  G_OBJECT_CLASS (ide_shortcut_context_parent_class)->finalize (object);
}

static void
ide_shortcut_context_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  IdeShortcutContext *self = (IdeShortcutContext *)object;
  IdeShortcutContextPrivate *priv = ide_shortcut_context_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ide_shortcut_context_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  IdeShortcutContext *self = (IdeShortcutContext *)object;
  IdeShortcutContextPrivate *priv = ide_shortcut_context_get_instance_private (self);

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
ide_shortcut_context_class_init (IdeShortcutContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ide_shortcut_context_finalize;
  object_class->get_property = ide_shortcut_context_get_property;
  object_class->set_property = ide_shortcut_context_set_property;

  properties [PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "Name",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
ide_shortcut_context_init (IdeShortcutContext *self)
{
}

IdeShortcutContext *
ide_shortcut_context_new (const gchar *name)
{
  return g_object_new (IDE_TYPE_SHORTCUT_CONTEXT,
                       "name", name,
                       NULL);
}

const gchar *
ide_shortcut_context_get_name (IdeShortcutContext *self)
{
  IdeShortcutContextPrivate *priv = ide_shortcut_context_get_instance_private (self);

  g_return_val_if_fail (IDE_IS_SHORTCUT_CONTEXT (self), NULL);

  return priv->name;
}

gboolean
ide_shortcut_context_activate (IdeShortcutContext *self,
                               GtkWidget          *widget,
                               const GdkEventKey  *event)
{
  IdeShortcutContextPrivate *priv = ide_shortcut_context_get_instance_private (self);

  g_return_val_if_fail (IDE_IS_SHORTCUT_CONTEXT (self), FALSE);
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  if (priv->keymap != NULL)
    {
      Shortcut lookup = { 0 };
      Shortcut *shortcut;

      lookup.keyval = event->keyval;
      lookup.modifier = event->state;

      translate_keyval (&lookup.keyval, &lookup.modifier);

      shortcut = g_hash_table_lookup (priv->keymap, &lookup);

      if (shortcut != NULL)
        return shortcut_activate (shortcut, widget, event);
    }

  return GDK_EVENT_PROPAGATE;
}

static void
ide_shortcut_context_add (IdeShortcutContext *self,
                          Shortcut           *shortcut)
{
  IdeShortcutContextPrivate *priv = ide_shortcut_context_get_instance_private (self);
  Shortcut *head;

  g_assert (IDE_IS_SHORTCUT_CONTEXT (self));
  g_assert (shortcut != NULL);

  if (priv->keymap == NULL)
    priv->keymap = g_hash_table_new_full (shortcut_hash, shortcut_equal, NULL, shortcut_free);

  translate_keyval (&shortcut->keyval, &shortcut->modifier);

  /*
   * If we find that there is another entry for this shortcut, we chain onto
   * the end of that item. This allows us to call multiple signals, or
   * interleave signals and actions.
   */

  if (g_hash_table_lookup_extended (priv->keymap, shortcut, (gpointer *)&head, NULL))
    {
      while (head->next != NULL)
        head = head->next;
      head->next = shortcut;
    }
  else
    {
      g_hash_table_insert (priv->keymap, shortcut, shortcut);
    }
}

void
ide_shortcut_context_add_action (IdeShortcutContext *self,
                                 const gchar        *accel,
                                 const gchar        *detailed_action_name)
{
  Shortcut *shortcut;
  g_autofree gchar *action_name = NULL;
  g_autofree gchar *prefix = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) action_target = NULL;
  const gchar *dot;
  const gchar *name;
  GdkModifierType modifier = 0;
  guint keyval = 0;

  g_return_if_fail (IDE_IS_SHORTCUT_CONTEXT (self));
  g_return_if_fail (accel != NULL);
  g_return_if_fail (detailed_action_name != NULL);

  gtk_accelerator_parse (accel, &keyval, &modifier);

  if (!g_action_parse_detailed_name (detailed_action_name, &action_name, &action_target, &error))
    {
      g_warning ("%s", error->message);
      return;
    }

  if (NULL != (dot = strchr (action_name, '.')))
    {
      name = &dot[1];
      prefix = g_strndup (action_name, dot - action_name);
    }
  else
    {
      name = action_name;
      prefix = NULL;
    }

  shortcut = g_slice_new0 (Shortcut);
  shortcut->type = SHORTCUT_ACTION;
  shortcut->keyval = keyval;
  shortcut->modifier = modifier;
  shortcut->action.prefix = prefix ? g_intern_string (prefix) : NULL;
  shortcut->action.name = g_intern_string (name);
  shortcut->action.param = g_steal_pointer (&action_target);

  ide_shortcut_context_add (self, shortcut);
}

void
ide_shortcut_context_add_signal_va_list (IdeShortcutContext *self,
                                         const gchar        *accel,
                                         const gchar        *signal_name,
                                         guint               n_args,
                                         va_list             args)
{
  g_autoptr(GArray) params = NULL;
  g_autofree gchar *truncated_name = NULL;
  const gchar *detail_str;
  Shortcut *shortcut;
  GdkModifierType modifier = 0;
  guint keyval = 0;
  GQuark detail = 0;

  g_return_if_fail (IDE_IS_SHORTCUT_CONTEXT (self));
  g_return_if_fail (accel != NULL);
  g_return_if_fail (signal_name != NULL);

  gtk_accelerator_parse (accel, &keyval, &modifier);

  if (NULL != (detail_str = strstr (signal_name, "::")))
    {
      truncated_name = g_strndup (signal_name, detail_str - signal_name);
      signal_name = truncated_name;
      detail_str = &detail_str[2];
      detail = g_quark_try_string (detail_str);
    }

  params = g_array_new (FALSE, FALSE, sizeof (GValue));
  g_array_set_clear_func (params, (GDestroyNotify)g_value_unset);

  for (; n_args > 0; n_args--)
    {
      g_autofree gchar *errstr = NULL;
      GValue value = { 0 };
      GType type;

      type = va_arg (args, GType);

      G_VALUE_COLLECT_INIT (&value, type, args, 0, &errstr);

      if (errstr != NULL)
        {
          g_warning ("%s", errstr);
          break;
        }

      g_array_append_val (params, value);
    }

  shortcut = g_slice_new0 (Shortcut);
  shortcut->type = SHORTCUT_SIGNAL;
  shortcut->keyval = keyval;
  shortcut->modifier = modifier;
  shortcut->signal.name = g_intern_string (signal_name);
  shortcut->signal.detail = detail;
  shortcut->signal.params = g_steal_pointer (&params);

  ide_shortcut_context_add (self, shortcut);
}

void
ide_shortcut_context_add_signal (IdeShortcutContext *self,
                                 const gchar        *accel,
                                 const gchar        *signal_name,
                                 guint               n_args,
                                 ...)
{
  va_list args;

  va_start (args, n_args);
  ide_shortcut_context_add_signal_va_list (self, accel, signal_name, n_args, args);
  va_end (args);
}

gboolean
ide_shortcut_context_remove (IdeShortcutContext *self,
                             const gchar        *accel)
{
  IdeShortcutContextPrivate *priv = ide_shortcut_context_get_instance_private (self);
  Shortcut lookup = { 0 };

  g_return_val_if_fail (IDE_IS_SHORTCUT_CONTEXT (self), FALSE);
  g_return_val_if_fail (accel != NULL, FALSE);

  gtk_accelerator_parse (accel, &lookup.keyval, &lookup.modifier);

  translate_keyval (&lookup.keyval, &lookup.modifier);

  return g_hash_table_remove (priv->keymap, &lookup);
}
