/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gsm-inhibitor.h"
#include "org.gnome.SessionManager.Inhibitor.h"

#include "gsm-util.h"

static guint32 inhibitor_serial = 1;

struct GsmInhibitorPrivate
{
        char *id;
        char *bus_name;
        char *app_id;
        char *client_id;
        char *reason;
        guint flags;
        guint cookie;
        GDBusConnection *connection;
        GsmExportedInhibitor *skeleton;

        guint                 watch_id;
};

enum {
        PROP_0,
        PROP_BUS_NAME,
        PROP_REASON,
        PROP_APP_ID,
        PROP_CLIENT_ID,
        PROP_FLAGS,
        PROP_COOKIE
};

enum {
        VANISHED,
        LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_CODE (GsmInhibitor, gsm_inhibitor, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (GsmInhibitor))

#define GSM_INHIBITOR_DBUS_IFACE "org.gnome.SessionManager.Inhibitor"

static const GDBusErrorEntry gsm_inhibitor_error_entries[] = {
        { GSM_INHIBITOR_ERROR_GENERAL, GSM_INHIBITOR_DBUS_IFACE ".GeneralError" },
        { GSM_INHIBITOR_ERROR_NOT_SET, GSM_INHIBITOR_DBUS_IFACE ".NotSet" }
};

GQuark
gsm_inhibitor_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("gsm_inhibitor_error",
                                            &quark_volatile,
                                            gsm_inhibitor_error_entries,
                                            G_N_ELEMENTS (gsm_inhibitor_error_entries));
        return quark_volatile;
}

static gboolean
gsm_inhibitor_get_app_id (GsmExportedInhibitor  *skeleton,
                          GDBusMethodInvocation *invocation,
                          GsmInhibitor          *inhibitor)
{
        const gchar *id;

        if (inhibitor->priv->app_id != NULL) {
                id = inhibitor->priv->app_id;
        } else {
                id = "";
        }

        gsm_exported_inhibitor_complete_get_app_id (skeleton, invocation, id);

        return TRUE;
}

static gboolean
gsm_inhibitor_get_client_id (GsmExportedInhibitor  *skeleton,
                             GDBusMethodInvocation *invocation,
                             GsmInhibitor          *inhibitor)
{
        /* object paths are not allowed to be NULL or blank */
        if (IS_STRING_EMPTY (inhibitor->priv->client_id)) {
                g_dbus_method_invocation_return_error (invocation,
                                                       GSM_INHIBITOR_ERROR,
                                                       GSM_INHIBITOR_ERROR_NOT_SET,
                                                       "Value is not set");
                return TRUE;
        }

        gsm_exported_inhibitor_complete_get_client_id (skeleton, invocation, inhibitor->priv->client_id);
        g_debug ("GsmInhibitor: getting client-id = '%s'", inhibitor->priv->client_id);

        return TRUE;
}

static gboolean
gsm_inhibitor_get_reason (GsmExportedInhibitor  *skeleton,
                          GDBusMethodInvocation *invocation,
                          GsmInhibitor          *inhibitor)
{
        const gchar *reason;

        if (inhibitor->priv->reason != NULL) {
                reason = inhibitor->priv->reason;
        } else {
                reason = "";
        }

        gsm_exported_inhibitor_complete_get_reason (skeleton, invocation, reason);
        return TRUE;
}

static gboolean
gsm_inhibitor_get_flags (GsmExportedInhibitor  *skeleton,
                         GDBusMethodInvocation *invocation,
                         GsmInhibitor          *inhibitor)
{
        gsm_exported_inhibitor_complete_get_flags (skeleton, invocation, inhibitor->priv->flags);
        return TRUE;
}

static guint32
get_next_inhibitor_serial (void)
{
        guint32 serial;

        serial = inhibitor_serial++;

        if ((gint32)inhibitor_serial < 0) {
                inhibitor_serial = 1;
        }

        return serial;
}

static gboolean
register_inhibitor (GsmInhibitor *inhibitor)
{
        GError *error;
        GsmExportedInhibitor *skeleton;

        error = NULL;
        inhibitor->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

        if (error != NULL) {
                g_critical ("error getting session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        skeleton = gsm_exported_inhibitor_skeleton_new ();
        inhibitor->priv->skeleton = skeleton;
        g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                          inhibitor->priv->connection,
                                          inhibitor->priv->id, &error);

        if (error != NULL) {
                g_critical ("error exporting inhibitor on session bus: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_signal_connect (skeleton, "handle-get-app-id",
                          G_CALLBACK (gsm_inhibitor_get_app_id), inhibitor);
        g_signal_connect (skeleton, "handle-get-client-id",
                          G_CALLBACK (gsm_inhibitor_get_client_id), inhibitor);
        g_signal_connect (skeleton, "handle-get-flags",
                          G_CALLBACK (gsm_inhibitor_get_flags), inhibitor);
        g_signal_connect (skeleton, "handle-get-reason",
                          G_CALLBACK (gsm_inhibitor_get_reason), inhibitor);

        return TRUE;
}

static GObject *
gsm_inhibitor_constructor (GType                  type,
                           guint                  n_construct_properties,
                           GObjectConstructParam *construct_properties)
{
        GsmInhibitor *inhibitor;
        gboolean      res;

        inhibitor = GSM_INHIBITOR (G_OBJECT_CLASS (gsm_inhibitor_parent_class)->constructor (type,
                                                                                             n_construct_properties,
                                                                                             construct_properties));

        g_free (inhibitor->priv->id);
        inhibitor->priv->id = g_strdup_printf ("/org/gnome/SessionManager/Inhibitor%u", get_next_inhibitor_serial ());
        res = register_inhibitor (inhibitor);
        if (! res) {
                g_warning ("Unable to register inhibitor with session bus");
        }

        return G_OBJECT (inhibitor);
}

static void
gsm_inhibitor_init (GsmInhibitor *inhibitor)
{
        inhibitor->priv = gsm_inhibitor_get_instance_private (inhibitor);
}

static void
on_inhibitor_vanished (GDBusConnection *connection,
                       const char      *name,
                       gpointer         user_data)
{
        GsmInhibitor  *inhibitor = user_data;

        g_bus_unwatch_name (inhibitor->priv->watch_id);
        inhibitor->priv->watch_id = 0;

        g_signal_emit (inhibitor, signals[VANISHED], 0);
}

static void
gsm_inhibitor_set_bus_name (GsmInhibitor  *inhibitor,
                            const char    *bus_name)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->bus_name);

        if (bus_name != NULL) {
                inhibitor->priv->bus_name = g_strdup (bus_name);

                inhibitor->priv->watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                              bus_name,
                                                              G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                              NULL,
                                                              on_inhibitor_vanished,
                                                              inhibitor,
                                                              NULL);
        } else {
                inhibitor->priv->bus_name = g_strdup ("");
        }
        g_object_notify (G_OBJECT (inhibitor), "bus-name");
}

static void
gsm_inhibitor_set_app_id (GsmInhibitor  *inhibitor,
                          const char    *app_id)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->app_id);

        inhibitor->priv->app_id = g_strdup (app_id);
        g_object_notify (G_OBJECT (inhibitor), "app-id");
}

static void
gsm_inhibitor_set_client_id (GsmInhibitor  *inhibitor,
                             const char    *client_id)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->client_id);

        g_debug ("GsmInhibitor: setting client-id = %s", client_id);

        if (client_id != NULL) {
                inhibitor->priv->client_id = g_strdup (client_id);
        } else {
                inhibitor->priv->client_id = g_strdup ("");
        }
        g_object_notify (G_OBJECT (inhibitor), "client-id");
}

static void
gsm_inhibitor_set_reason (GsmInhibitor  *inhibitor,
                          const char    *reason)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        g_free (inhibitor->priv->reason);

        if (reason != NULL) {
                inhibitor->priv->reason = g_strdup (reason);
        } else {
                inhibitor->priv->reason = g_strdup ("");
        }
        g_object_notify (G_OBJECT (inhibitor), "reason");
}

static void
gsm_inhibitor_set_cookie (GsmInhibitor  *inhibitor,
                          guint          cookie)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        if (inhibitor->priv->cookie != cookie) {
                inhibitor->priv->cookie = cookie;
                g_object_notify (G_OBJECT (inhibitor), "cookie");
        }
}

static void
gsm_inhibitor_set_flags (GsmInhibitor  *inhibitor,
                         guint          flags)
{
        g_return_if_fail (GSM_IS_INHIBITOR (inhibitor));

        if (inhibitor->priv->flags != flags) {
                inhibitor->priv->flags = flags;
                g_object_notify (G_OBJECT (inhibitor), "flags");
        }
}

const char *
gsm_inhibitor_peek_bus_name (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->bus_name;
}

const char *
gsm_inhibitor_peek_id (GsmInhibitor *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->id;
}

const char *
gsm_inhibitor_peek_app_id (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->app_id;
}

const char *
gsm_inhibitor_peek_client_id (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->client_id;
}

const char *
gsm_inhibitor_peek_reason (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), NULL);

        return inhibitor->priv->reason;
}

guint
gsm_inhibitor_peek_flags (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), 0);

        return inhibitor->priv->flags;
}

guint
gsm_inhibitor_peek_cookie (GsmInhibitor  *inhibitor)
{
        g_return_val_if_fail (GSM_IS_INHIBITOR (inhibitor), 0);

        return inhibitor->priv->cookie;
}

static void
gsm_inhibitor_set_property (GObject       *object,
                            guint          prop_id,
                            const GValue  *value,
                            GParamSpec    *pspec)
{
        GsmInhibitor *self;

        self = GSM_INHIBITOR (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                gsm_inhibitor_set_bus_name (self, g_value_get_string (value));
                break;
        case PROP_APP_ID:
                gsm_inhibitor_set_app_id (self, g_value_get_string (value));
                break;
        case PROP_CLIENT_ID:
                gsm_inhibitor_set_client_id (self, g_value_get_string (value));
                break;
        case PROP_REASON:
                gsm_inhibitor_set_reason (self, g_value_get_string (value));
                break;
        case PROP_FLAGS:
                gsm_inhibitor_set_flags (self, g_value_get_uint (value));
                break;
        case PROP_COOKIE:
                gsm_inhibitor_set_cookie (self, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_inhibitor_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
        GsmInhibitor *self;

        self = GSM_INHIBITOR (object);

        switch (prop_id) {
        case PROP_BUS_NAME:
                g_value_set_string (value, self->priv->bus_name);
                break;
        case PROP_APP_ID:
                g_value_set_string (value, self->priv->app_id);
                break;
        case PROP_CLIENT_ID:
                g_value_set_string (value, self->priv->client_id);
                break;
        case PROP_REASON:
                g_value_set_string (value, self->priv->reason);
                break;
        case PROP_FLAGS:
                g_value_set_uint (value, self->priv->flags);
                break;
        case PROP_COOKIE:
                g_value_set_uint (value, self->priv->cookie);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsm_inhibitor_finalize (GObject *object)
{
        GsmInhibitor *inhibitor = (GsmInhibitor *) object;

        g_free (inhibitor->priv->id);
        g_free (inhibitor->priv->bus_name);
        g_free (inhibitor->priv->app_id);
        g_free (inhibitor->priv->client_id);
        g_free (inhibitor->priv->reason);

        if (inhibitor->priv->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport_from_connection (G_DBUS_INTERFACE_SKELETON (inhibitor->priv->skeleton),
                                                                    inhibitor->priv->connection);
                g_clear_object (&inhibitor->priv->skeleton);
        }

        if (inhibitor->priv->watch_id != 0) {
                g_bus_unwatch_name (inhibitor->priv->watch_id);
        }

        g_clear_object (&inhibitor->priv->connection);

        G_OBJECT_CLASS (gsm_inhibitor_parent_class)->finalize (object);
}

static void
gsm_inhibitor_class_init (GsmInhibitorClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize             = gsm_inhibitor_finalize;
        object_class->constructor          = gsm_inhibitor_constructor;
        object_class->get_property         = gsm_inhibitor_get_property;
        object_class->set_property         = gsm_inhibitor_set_property;

        signals[VANISHED] =
                g_signal_new ("vanished",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              0, NULL, NULL, NULL,
                              G_TYPE_NONE,
                              0);
        g_object_class_install_property (object_class,
                                         PROP_BUS_NAME,
                                         g_param_spec_string ("bus-name",
                                                              "bus-name",
                                                              "bus-name",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_APP_ID,
                                         g_param_spec_string ("app-id",
                                                              "app-id",
                                                              "app-id",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_CLIENT_ID,
                                         g_param_spec_string ("client-id",
                                                              "client-id",
                                                              "client-id",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_REASON,
                                         g_param_spec_string ("reason",
                                                              "reason",
                                                              "reason",
                                                              "",
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_FLAGS,
                                         g_param_spec_uint ("flags",
                                                            "flags",
                                                            "flags",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_COOKIE,
                                         g_param_spec_uint ("cookie",
                                                            "cookie",
                                                            "cookie",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

GsmInhibitor *
gsm_inhibitor_new (const char    *app_id,
                   guint          flags,
                   const char    *reason,
                   const char    *bus_name,
                   guint          cookie)
{
        GsmInhibitor *inhibitor;

        inhibitor = g_object_new (GSM_TYPE_INHIBITOR,
                                  "app-id", app_id,
                                  "reason", reason,
                                  "bus-name", bus_name,
                                  "flags", flags,
                                  "cookie", cookie,
                                  NULL);

        return inhibitor;
}

GsmInhibitor *
gsm_inhibitor_new_for_client (const char    *client_id,
                              const char    *app_id,
                              guint          flags,
                              const char    *reason,
                              const char    *bus_name,
                              guint          cookie)
{
        GsmInhibitor *inhibitor;

        inhibitor = g_object_new (GSM_TYPE_INHIBITOR,
                                  "client-id", client_id,
                                  "app-id", app_id,
                                  "reason", reason,
                                  "bus-name", bus_name,
                                  "flags", flags,
                                  "cookie", cookie,
                                  NULL);

        return inhibitor;
}
