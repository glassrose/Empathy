/*
 * Copyright (C) 2011 Collabora Ltd.
 *
 * The code contained in this file is free software; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this code; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Emilio Pozuelo Monfort <emilio.pozuelo@collabora.co.uk>
 */

#include "config.h"

#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include <telepathy-glib/telepathy-glib.h>

#include <telepathy-yell/telepathy-yell.h>

#include "empathy-call-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

static const gchar *
get_error_display_message (GError *error)
{
  if (error->domain != TP_ERROR)
    return _("There was an error starting the call");

  switch (error->code)
    {
      case TP_ERROR_NETWORK_ERROR:
        return _("Network error");
      case TP_ERROR_NOT_CAPABLE:
        return _("The specified contact doesn't support calls");
      case TP_ERROR_OFFLINE:
        return _("The specified contact is offline");
      case TP_ERROR_INVALID_HANDLE:
        return _("The specified contact is not valid");
      case TP_ERROR_EMERGENCY_CALLS_NOT_SUPPORTED:
        return _("Emergency calls are not supported on this protocol");
    }

  return _("There was an error starting the call");
}

static void
show_call_error (GError *error)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL, 0,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
      "%s",
      get_error_display_message (error));

  g_signal_connect_swapped (dialog, "response",
      G_CALLBACK (gtk_widget_destroy),
      dialog);

  gtk_widget_show (dialog);
}

GHashTable *
empathy_call_create_call_request (const gchar *contact,
    gboolean initial_audio,
    gboolean initial_video)
{
  return tp_asv_new (
    TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
      TPY_IFACE_CHANNEL_TYPE_CALL,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
      TP_HANDLE_TYPE_CONTACT,
    TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
      contact,
    TPY_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO, G_TYPE_BOOLEAN,
      initial_audio,
    TPY_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO, G_TYPE_BOOLEAN,
      initial_video,
    NULL);
}

GHashTable *
empathy_call_create_streamed_media_request (const gchar *contact,
    gboolean initial_audio,
    gboolean initial_video)
{
  return tp_asv_new (
    TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
      TP_HANDLE_TYPE_CONTACT,
    TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING,
      contact,
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO, G_TYPE_BOOLEAN,
      initial_audio,
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_VIDEO, G_TYPE_BOOLEAN,
      initial_video,
    NULL);
}

static void
create_call_channel_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;

  if (!tp_account_channel_request_create_channel_finish (
           TP_ACCOUNT_CHANNEL_REQUEST (source),
           result,
           &error))
    {
      DEBUG ("Failed to create Call channel: %s", error->message);
      show_call_error (error);
      g_error_free (error);
    }
}

static void
create_streamed_media_channel_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountChannelRequest *call_req = user_data;
  GError *error = NULL;

  if (tp_account_channel_request_create_channel_finish (
      TP_ACCOUNT_CHANNEL_REQUEST (source), result, &error))
    {
      g_object_unref (call_req);
      return;
    }

  DEBUG ("Failed to create StreamedMedia channel: %s", error->message);

  if (error->code != TP_ERROR_NOT_IMPLEMENTED)
    {
      show_call_error (error);
      return;
    }

  DEBUG ("Let's try with a Call channel");
  g_error_free (error);
  tp_account_channel_request_create_channel_async (call_req, NULL, NULL,
      create_call_channel_cb,
      NULL);
}

void
empathy_call_new_with_streams (const gchar *contact,
    TpAccount *account,
    gboolean initial_audio,
    gboolean initial_video,
    gint64 timestamp)
{
  GHashTable *call_request, *streamed_media_request;
  TpAccountChannelRequest *call_req, *streamed_media_req;

  call_request = empathy_call_create_call_request (contact,
      initial_audio,
      initial_video);

  streamed_media_request = empathy_call_create_streamed_media_request (
      contact, initial_audio, initial_video);

  call_req = tp_account_channel_request_new (account, call_request, timestamp);
  streamed_media_req = tp_account_channel_request_new (account,
      streamed_media_request,
      timestamp);

  tp_account_channel_request_create_channel_async (streamed_media_req, NULL, NULL,
      create_streamed_media_channel_cb,
      call_req);

  g_hash_table_unref (call_request);
  g_hash_table_unref (streamed_media_request);
  g_object_unref (streamed_media_req);
}