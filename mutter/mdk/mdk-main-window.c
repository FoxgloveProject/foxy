/*
 * Copyright (C) 2025 Red Hat Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "mdk-main-window.h"

#include "mdk-window.h"

struct _MdkMainWindow
{
  MdkWindow parent;
};

G_DEFINE_TYPE (MdkMainWindow, mdk_main_window, MDK_TYPE_WINDOW)

static void
mdk_main_window_dispose (GObject *object)
{
  gtk_widget_dispose_template (GTK_WIDGET (object), MDK_TYPE_MAIN_WINDOW);

  G_OBJECT_CLASS (mdk_main_window_parent_class)->dispose (object);
}

static void
mdk_main_window_class_init (MdkMainWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = mdk_main_window_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/ui/mdk-main-window.ui");
}

static void
mdk_main_window_init (MdkMainWindow *main_window)
{
  gtk_widget_init_template (GTK_WIDGET (main_window));
}
