/* ide-langserv-symbol-tree-private.h
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

#ifndef IDE_LANGSERV_SYMBOL_TREE_PRIVATE_H
#define IDE_LANGSERV_SYMBOL_TREE_PRIVATE_H

#include "ide-langserv-symbol-tree.h"

G_BEGIN_DECLS

IdeLangservSymbolTree *ide_langserv_symbol_tree_new (GPtrArray *symbols);

G_END_DECLS

#endif /* IDE_LANGSERV_SYMBOL_TREE_PRIVATE_H */
