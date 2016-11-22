/* ide-shortcut-context.h
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

#ifndef IDE_SHORTCUT_CONTEXT_H
#define IDE_SHORTCUT_CONTEXT_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define IDE_TYPE_SHORTCUT_CONTEXT (ide_shortcut_context_get_type())

G_DECLARE_FINAL_TYPE (IdeShortcutContext, ide_shortcut_context, IDE, SHORTCUT_CONTEXT, GObject)

IdeShortcutContext *ide_shortcut_context_new                (const gchar        *name);
const gchar        *ide_shortcut_context_get_name           (IdeShortcutContext *self);
gboolean            ide_shortcut_context_activate           (IdeShortcutContext *self,
                                                             GtkWidget          *widget,
                                                             const GdkEventKey  *event);
void                ide_shortcut_context_add_action         (IdeShortcutContext *self,
                                                             const gchar        *accel,
                                                             const gchar        *detailed_action_name);
void                ide_shortcut_context_add_signal         (IdeShortcutContext *self,
                                                             const gchar        *accel,
                                                             const gchar        *signal_name,
                                                             guint               n_args,
                                                             ...);
void                ide_shortcut_context_add_signal_va_list (IdeShortcutContext *self,
                                                             const gchar        *accel,
                                                             const gchar        *signal_name,
                                                             guint               n_args,
                                                             va_list             args);
gboolean            ide_shortcut_context_remove             (IdeShortcutContext *self,
                                                             const gchar        *accel);

G_END_DECLS

#endif /* IDE_SHORTCUT_CONTEXT_H */
