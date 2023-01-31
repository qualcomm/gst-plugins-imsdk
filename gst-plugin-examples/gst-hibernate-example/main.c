/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <linux/power_state.h>
#include <cutils/uevent.h>
#include <dbus/dbus.h>
#include <glib-unix.h>
#include <gst/gst.h>

#include "wifi.h"

#define TAG "\ngst-hibernate-example: "

#define HIBERNATE_EXIT_EVENT "HIBERNATE_EXIT_EVENT"
#define SUBSYSTEM_RESTORE_EVENT "SUBSYSTEM_RESTORE_EVENT"

#define UEVENT_MSG_LEN 256
#define PS_EVENT "POWER_STATE_EVENT = "
#define PS_EVENT_STRING_LEN 20  /*Length of PS_EVENT */

static gboolean
wait_hibernate_exit_uevent ()
{
  enum ps_event_type ps_event;
  gint device_fd;
  gchar msg[UEVENT_MSG_LEN + 2];
  guint n;
  guint i;

  g_print ("%suevent_open_socket\n", TAG);
  device_fd = uevent_open_socket (64 * 1024, true);
  if (device_fd < 0) {
    g_printerr ("%sPS Event Lisenter: Open Socket Failed\n", TAG);
  }

  while ((n =
          uevent_kernel_multicast_recv (device_fd, msg, UEVENT_MSG_LEN)) > 0) {
    if (n < 0 || n > UEVENT_MSG_LEN) {
      g_printerr ("%sIncorrect Uevent Message Length\n", TAG);
      continue;
    }

    g_print ("%sReceived uevent %s\n", TAG, msg);

    msg[n] = '\0';
    msg[n + 1] = '\0';
    i = 0;
    char *msg_ptr = (char *) msg;
    if (strstr (msg, "power_state")) {
      while (*msg_ptr) {
        if (!strncmp (msg_ptr, PS_EVENT, PS_EVENT_STRING_LEN)) {
          msg_ptr += PS_EVENT_STRING_LEN;
          i += PS_EVENT_STRING_LEN;
          ps_event = atoi (msg_ptr);

          switch (ps_event) {

            case EXIT_HIBERNATE:
              g_print ("%sWakeup from Hibernate\n", TAG);
              close (device_fd);
              return TRUE;

            case MDSP_BEFORE_POWERDOWN:
              g_print ("%sMODEM_BEFORE_POWER_DOWN\n", TAG);
              break;

            case MDSP_AFTER_POWERUP:
              g_print ("%sMODEM_AFTER_POWER_UP\n", TAG);
              break;

            case ADSP_BEFORE_POWERDOWN:
              g_print ("%sADSP_BEFORE_POWER_DOWN\n", TAG);
              break;

            case CDSP_BEFORE_POWERDOWN:
              g_print ("%sCDSP_BEFORE_POWER_DOWN\n", TAG);
              break;

            case ADSP_AFTER_POWERUP:
              g_print ("%sADSP_AFTER_POWER_UP\n", TAG);
              break;

            case CDSP_AFTER_POWERUP:
              g_print ("%sCDSP_AFTER_POWER_UP\n", TAG);
              break;

            case PREPARE_FOR_HIBERNATION:
              g_print ("%sPrepare Swap Partition\n", TAG);
              break;

            default:
              g_printerr ("%sGarbage Uevent Error\n", TAG);
              break;
          }
        }
        while (*msg_ptr++ && i++ && i < n + 2);
        i++;
      }
    }
  }

  g_printerr ("%sDid not receive required uevent\n", TAG);
  close (device_fd);

  return FALSE;
}

static gboolean
wait_subsystem_restore_dbus ()
{
  DBusConnection *connection;
  DBusMessage *msg;
  DBusMessageIter args;
  DBusPendingCall *pending;
  DBusError dbus_error;
  gboolean *arg_true = TRUE;
  gchar *response;
  gboolean result = FALSE;

  dbus_error_init (&dbus_error);

  connection = dbus_bus_get (DBUS_BUS_SYSTEM, &dbus_error);
  if (!connection) {
    g_printerr ("%sdbus connection failed.\n", TAG);
    return FALSE;
  }

  msg = dbus_message_new_method_call ("org.Qti.HibernateService",       // target for the method call
      "/org/Qti/HibernateService/HibernateManager",     // object to call on
      "org.Qti.HibernateService.HibernateManager",      // interface to call on
      "AreAllSubsystemsUp");    // method name
  if (NULL == msg) {
    g_printerr ("%sdbus message Null.\n", TAG);
    dbus_connection_flush (connection);
    return FALSE;
  }
  // append arguments
  dbus_message_iter_init_append (msg, &args);
  if (!dbus_message_iter_append_basic (&args, DBUS_TYPE_BOOLEAN, &arg_true)) {
    g_printerr ("%sdbus_message_iter_init_append: Out Of Memory!\n", TAG);
    dbus_message_unref (msg);
    dbus_connection_flush (connection);
    return FALSE;
  }
  // send message and get a handle for a reply
  if (!dbus_connection_send_with_reply (connection, msg, &pending, -1)) {       // -1 is default timeout
    g_printerr ("%sdbus_connection_send_with_reply: Out Of Memory!\n", TAG);
    dbus_message_unref (msg);
    dbus_connection_flush (connection);
    return FALSE;
  }
  if (NULL == pending) {
    g_printerr ("%sdbus pending Call NULL.\n", TAG);
    dbus_message_unref (msg);
    dbus_connection_flush (connection);
    return FALSE;
  }
  // block until we receive a reply
  dbus_pending_call_block (pending);

  // free message
  dbus_message_unref (msg);

  // get the reply message
  msg = dbus_pending_call_steal_reply (pending);
  if (NULL == msg) {
    g_printerr ("%sdbus reply NULL.\n", TAG);
    dbus_pending_call_unref (pending);
    dbus_message_unref (msg);
    dbus_connection_flush (connection);
    return FALSE;
  }
  // read the parameters
  if (!dbus_message_iter_init (msg, &args))
    g_printerr ("%sdbus message has no arguments!\n", TAG);
  else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type (&args))
    g_printerr ("%sdbus argument is not string!\n", TAG);
  else
    dbus_message_iter_get_basic (&args, &response);

  g_print ("%sdbus got reply: %s\n", TAG, response);
  if (strncmp (response, "1", 1) == 0)
    result = TRUE;
  else
    result = FALSE;

  // free the pending message handle
  dbus_pending_call_unref (pending);
  // free reply and close connection
  dbus_message_unref (msg);
  dbus_connection_flush (connection);

  return result;
}

static gboolean
wait_hibernate_exit (GAsyncQueue * event_queue)
{
  GstStructure *event = NULL;

  // Wait for HIBERNATE_EXIT_EVENT event.
  while ((event = g_async_queue_pop (event_queue)) != NULL) {
    if (gst_structure_has_name (event, HIBERNATE_EXIT_EVENT))
      break;
    gst_structure_free (event);
  }

  gst_structure_free (event);
  return TRUE;
}

static gboolean
wait_subsystem_restore (GAsyncQueue * event_queue)
{
  GstStructure *event = NULL;

  // Wait for SUBSYSTEM_RESTORE_EVENT event.
  while ((event = g_async_queue_pop (event_queue)) != NULL) {
    if (gst_structure_has_name (event, SUBSYSTEM_RESTORE_EVENT))
      break;
    gst_structure_free (event);
  }

  gst_structure_free (event);
  return TRUE;
}

static gpointer
event_handler (gpointer userdata)
{
  GAsyncQueue *event_queue = (GAsyncQueue *) (userdata);
  gboolean hibernate_exit_done = FALSE;
  gboolean subsystem_restore_done = FALSE;

  while (!hibernate_exit_done) {
    hibernate_exit_done = wait_hibernate_exit_uevent ();
  }
  g_print ("%sHibernate exit done.\n", TAG);
  g_async_queue_push (event_queue,
      gst_structure_new_empty (HIBERNATE_EXIT_EVENT));

  while (!subsystem_restore_done) {
    subsystem_restore_done = wait_subsystem_restore_dbus ();
  }
  g_print ("%sSubsystem restore done.\n", TAG);
  g_async_queue_push (event_queue,
      gst_structure_new_empty (SUBSYSTEM_RESTORE_EVENT));

  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  g_print ("%sStarted gst-hibernate-example program.\n", TAG);

  GThread *event_thread = NULL;
  GAsyncQueue *event_queue;
  gboolean success = TRUE;
  gboolean restore_wifi;

  // Initiate the GAsyncQueue for hibernate exit.
  event_queue = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);

  // Initiate the event_handler thread.
  g_print ("%sCreating event_thread\n", TAG);
  if ((event_thread = g_thread_new
          ("event_thread", event_handler, event_queue)) == NULL) {
    g_printerr ("%sERROR: Failed to create event_thread!\n", TAG);
    return -1;
  }

  restore_wifi = is_wifi_on ();
  g_print ("%sWiFi status is %d.\n", TAG, restore_wifi);
  if (restore_wifi) {
    g_print ("%sStarted disable_wifi.\n", TAG);
    success = disable_wifi ();
    g_print ("%sEnded disable_wifi. Result is %d.\n", TAG, success);
  }
  // Trigger hibernate
  g_print ("%sStarted triggering hibernate.\n", TAG);
  system
      ("dbus-send --system --dest=org.Qti.HibernateService --print-reply "
      "--type=method_call '/org/Qti/HibernateService/HibernateManager' "
      "org.Qti.HibernateService.HibernateManager.Hibernate boolean:true");
  g_print ("%sEnded Triggering Hibernate.\n", TAG);

  // Wait for hibernate exit uevent
  g_print ("%sStarted wait_hibernate_exit.\n", TAG);
  success = wait_hibernate_exit (event_queue);
  g_print ("%sEnded wait_hibernate_exit. Result is %d.\n", TAG, success);

  ///////////////////////CAMERA LAUNCH/////////////////////////////////////
  g_print ("%sStarted camera launch.\n", TAG);
  GstElement *pipeline;

  // Initialize GStreamer
  gst_init (&argc, &argv);

  // Build the pipeline
  pipeline = gst_parse_launch ("gst-launch-1.0 -e qtiqmmfsrc ! \
  video/x-raw\(memory:GBM\),format=NV12,width=1920,height=1080,framerate=30/1 ! \
  multifilesink max-files=1 location=\"/data/frame%d.yuv\"", NULL);

  // Start playing
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  sleep (10);

  // Free resources
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  g_print ("%sEnded camera launch.\n", TAG);
  /////////////////////////////////////////////////////////////////////

  // Wait for subsystem restore
  g_print ("%sStarted wait_subsystem_restore.\n", TAG);
  success = wait_subsystem_restore (event_queue);
  g_print ("%sEnded wait_subsystem_restore. Result is %d.\n", TAG, success);

  // Restore WiFi state
  if (restore_wifi) {
    g_print ("%sStarted enable_wifi.\n", TAG);
    success = enable_wifi ();
    g_print ("%sEnded enable_wifi. Result is %d.\n", TAG, success);
  }
  // Wait until event thread finishes.
  g_thread_join (event_thread);
  g_async_queue_unref (event_queue);
  g_print ("%sEnded gst-hibernate-example program.\n", TAG);

  return 0;
}
