/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#define DEFAULT_RTSP_ADDRESS "127.0.0.1"
#define DEFAULT_RTSP_PORT    "8900"
#define DEFAULT_RTSP_POINT   "/live"

/// Command line option variables.
static gchar *address  = DEFAULT_RTSP_ADDRESS;
static gchar *port     = DEFAULT_RTSP_PORT;
static gchar *mpoint   = DEFAULT_RTSP_POINT;
static gboolean record = FALSE;

static const GOptionEntry entries[] = {
    { "address", 'a', 0, G_OPTION_ARG_STRING, &address,
      "Server IP address (default: " DEFAULT_RTSP_ADDRESS ")",
      "ADDRESS"
    },
    { "port", 'p', 0, G_OPTION_ARG_STRING, &port,
      "Port on which to stream the payload (default: " DEFAULT_RTSP_PORT ")",
      "PORT"
    },
    { "mount", 'm', 0, G_OPTION_ARG_STRING, &mpoint,
      "Mount point for the payload (default: " DEFAULT_RTSP_POINT ")",
      "POINT"
    },
    { "record", 'r', 0, G_OPTION_ARG_NONE, &record,
      "Use RECORD transport mode instead of PLAY", NULL
    },
    { NULL }
};

// This timeout is periodically run to clean up the expired sessions from the
// pool. This needs to be run explicitly currently but might be done
// automatically as part of the mainloop.
static gboolean
timeout (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\n\nReceived an interrupt signal, quit main loop ...\n");
  g_main_loop_quit (mloop);

  return FALSE;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx = NULL;
  GstRTSPServer *server = NULL;
  GstRTSPMediaFactory *factory = NULL;
  GstRTSPMountPoints *mounts = NULL;
  GMainLoop *mloop = NULL;

  g_set_prgname ("gst-rtsp-server");

  // Parse command line entries.
  if ((ctx = g_option_context_new ("DESCRIPTION")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -EFAULT;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Initialize main loop.
  mloop = g_main_loop_new (NULL, FALSE);
  g_return_val_if_fail (mloop != NULL, -ENODEV);

  // Initialize RTSP server.
  server = gst_rtsp_server_new ();
  g_return_val_if_fail (server != NULL, -ENODEV);

  // Set the server IP address.
  gst_rtsp_server_set_address (server, address);

  // Set the server port.
  gst_rtsp_server_set_service (server, port);

  // Get the mount points for this server.
  mounts = gst_rtsp_server_get_mount_points (server);
  g_return_val_if_fail (mounts != NULL, -ENODEV);

  // Create a media factory.
  factory = gst_rtsp_media_factory_new ();
  g_return_val_if_fail (factory != NULL, -ENODEV);

  gst_rtsp_media_factory_set_shared (factory, TRUE);

  // Add the factory with given path to the mount points.
  gst_rtsp_mount_points_add_factory (mounts, mpoint, factory);

  // No need to keep reference for below objects.
  g_object_unref (mounts);

  gst_rtsp_media_factory_set_transport_mode (factory,
      record ? GST_RTSP_TRANSPORT_MODE_RECORD : GST_RTSP_TRANSPORT_MODE_PLAY);
  gst_rtsp_media_factory_set_launch (factory, argv[1]);

  // Add a timeout for the session cleanup.
  g_timeout_add_seconds (5, (GSourceFunc) timeout, server);

  // Attach the RTSP server to the main context.
  if (0 == gst_rtsp_server_attach (server, NULL)) {
    g_printerr ("Failed to attach RTSP server to main loop context!\n");

    g_object_unref (factory);
    g_object_unref (server);
    g_main_loop_unref (mloop);

    return -ENODEV;
  }

  // Register function for handling interrupt signals with the main loop.
  g_unix_signal_add (SIGINT, handle_interrupt_signal, mloop);

  g_print ("Stream ready at rtsp://%s:%s%s\n", address, port, mpoint);

  // Run main loop.
  g_main_loop_run (mloop);

  g_object_unref (factory);
  g_object_unref (server);
  g_main_loop_unref (mloop);

  gst_deinit ();

  return 0;
}
