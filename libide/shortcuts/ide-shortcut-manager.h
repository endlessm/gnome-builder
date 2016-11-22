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

#ifndef IDE_SHORTCUT_MANAGER_H
#define IDE_SHORTCUT_MANAGER_H

#include <gtk/gtk.h>

#include "ide-shortcut-theme.h"

G_BEGIN_DECLS

#define IDE_TYPE_SHORTCUT_MANAGER (ide_shortcut_manager_get_type())

G_DECLARE_FINAL_TYPE (IdeShortcutManager, ide_shortcut_manager, IDE, SHORTCUT_MANAGER, GObject)

IdeShortcutManager *ide_shortcut_manager_get_default  (void);
IdeShortcutTheme   *ide_shortcut_manager_get_theme    (IdeShortcutManager *self);
void                ide_shortcut_manager_set_theme    (IdeShortcutManager *self,
                                                       IdeShortcutTheme   *theme);
gboolean            ide_shortcut_manager_handle_event (IdeShortcutManager *self,
                                                       const GdkEventKey  *event,
                                                       GtkWidget          *toplevel);

G_END_DECLS

#endif /* IDE_SHORTCUT_MANAGER_H */
