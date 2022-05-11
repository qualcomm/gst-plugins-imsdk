/*
* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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
*
* Changes from Qualcomm Innovation Center are provided under the following license:
* Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*
*    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <sys/poll.h>
#include <signal.h>
#include <time.h>

#define HASH_LINE  "##################################################"
#define EQUAL_LINE "=================================================="
#define DASH_LINE  "--------------------------------------------------"

#define APPEND_SECTION_SEPARATOR(string) \
  g_string_append_printf (string, " %.*s%.*s\n", 39, DASH_LINE, 40, DASH_LINE);

#define APPEND_MENU_HEADER(string) \
  g_string_append_printf (string, "\n\n%.*s MENU %.*s\n\n", \
      37, HASH_LINE, 37, HASH_LINE);

#define APPEND_PIPELINE_CONTROLS_SECTION(string) \
  g_string_append_printf (string, " %.*s Pipeline Controls %.*s\n", \
      30, EQUAL_LINE, 30, EQUAL_LINE);

#define APPEND_ELEMENT_PROPERTIES_SECTION(string) \
  g_string_append_printf (string, " %.*s Plugin Properties %.*s\n", \
      30, EQUAL_LINE, 30, EQUAL_LINE);

#define APPEND_PAD_PROPERTIES_SECTION(name, string) \
  g_string_append_printf (string, " %.*s %s Pad %.*s\n", \
      36 - strlen(name) / 2, DASH_LINE, name, \
      37 - (strlen(name) / 2) - (strlen(name) % 2), DASH_LINE);

#define APPEND_ELEMENT_SIGNALS_SECTION(string) \
  g_string_append_printf (string, " %.*s Plugin Signals %.*s\n", \
      31, EQUAL_LINE, 32, EQUAL_LINE);

#define APPEND_OTHER_OPTS_SECTION(string) \
  g_string_append_printf (string, " %.*s Other %.*s\n", \
      36, EQUAL_LINE, 36, EQUAL_LINE);

#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))



#define GPIOCHIPFILE                              "/dev/gpiochip0"
#define GPIOLINEOFFSET                            42
#define STDIN_MESSAGE                             "APP_STDIN_MSG"
#define TERMINATE_MESSAGE                         "APP_TERMINATE_MSG"
#define PIPELINE_STATE_MESSAGE                    "APP_PIPELINE_STATE_MSG"
#define PIPELINE_EOS_MESSAGE                      "APP_PIPELINE_EOS_MSG"
#define PIPELINE_CNTRL_MESSAGE                    "APP_PIPELINE_CNTRL_MSG"

#define NULL_STATE_OPTION                         "0"
#define READY_STATE_OPTION                        "1"
#define PAUSED_STATE_OPTION                       "2"
#define PLAYING_STATE_OPTION                      "3"

//main message queue options
#define GPIO_EVENT                                "0"
#define PLUGIN_MODE_OPTION                        "p"
#define MENU_BACK_OPTION                          "b"
#define MENU_RETURN_OPTION                        "r"
#define QUIT_OPTION                               "q"
#define TURN_OFF_BWC                              "b0"
#define TURN_OFF_POV                              "p0"
#define TURN_ON_BWC                               "b1"
#define TURN_ON_POV                               "p1"
#define SWITCH_TO_POV                             "sp"
#define SWITCH_TO_BWC                             "sb"
#define USER_PREF_POV_AT                          "u"   /* Decides which state to switch when POV is attached runtime */
#define EDIT_BWC_PIPELINE                         "be"
#define EDIT_POV_PIPELINE                         "pe"
#define CAPTURE_IMAGE_BWC                         "ib"
#define CAPTURE_IMAGE_POV                         "ip"

#define BWC                                       1 << 0
#define POV                                       1 << 1

#define APP_STATE_BWC_AND_POV                     ( BWC | POV )
#define APP_STATE_BWC_ONLY                        ( BWC )
#define APP_STATE_POV_ONLY                        ( POV )
#define PIPELINE_BWC_NAME                         "BWC Pipeline"
#define PIPELINE_POV_NAME                         "POV Pipeline"

#define GST_APP_CONTEXT_CAST(obj)                 ((GstAppContext*)(obj))

typedef struct _GstAppContext GstAppContext;

struct _GstAppContext
{
  // Main application event loop.
  GMainLoop *mloop;

  // GStreamer pipeline instance.
  GstElement *pipeline1, *pipeline2;

  //Gst bus for pipelines
  GstBus *bus1, *bus2;

  // Asynchronous queue thread communication.
  GAsyncQueue *messages_main, *messages1, *messages2;

  //pointers threads  for the pipeline control
  GThread *p1thread, *p2thread;
};

/// Command line option variables.
static gboolean eos_on_shutdown = FALSE;

/// To handle the bus events on two pipelines
static gint bus1_watch_id = 0, bus2_watch_id = 0;

/// Which state to switch when POV is attached runtime
static guint user_pref = APP_STATE_POV_ONLY;

/// IOCTL handle to poll the rising/falling edge events on the GPIO
static gint gpio_req_fd;

/// Will be set/cleared based on GPIO falling/rising edge respectively
static gboolean pov_plugged_in = FALSE;

static const GOptionEntry entries[] = {
  {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
      "Send EOS event before transition from PLAYING to NULL state", NULL},
  {NULL}
};

static GMutex mutex;
static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = g_new0 (GstAppContext, 1);
  ctx->messages_main =
      g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->messages1 = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->messages2 = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->pipeline1 = NULL;
  ctx->pipeline2 = NULL;
  ctx->bus1 = NULL;
  ctx->bus2 = NULL;
  ctx->mloop = NULL;
  ctx->p1thread = NULL;
  ctx->p2thread = NULL;

  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->pipeline1 != NULL)
    gst_object_unref (ctx->pipeline1);
  if (ctx->pipeline2 != NULL)
    gst_object_unref (ctx->pipeline2);
  if (ctx->bus1)
    gst_object_unref (ctx->bus1);
  if (ctx->bus2)
    gst_object_unref (ctx->bus2);
  g_async_queue_unref (ctx->messages_main);
  g_async_queue_unref (ctx->messages1);
  g_async_queue_unref (ctx->messages2);
  g_free (ctx);

  return;
}

/**
 * wait_main_message ()
 *
 * Used by the Menu thread to wait for user input
 * and gpio event messages
*/
static gboolean
wait_main_message (GAsyncQueue * messages, gchar ** input)
{
  GstStructure *message = NULL;

  // Cleanup input variable from previous uses.
  g_free (*input);
  *input = NULL;

  // Wait for either a STDIN or TERMINATE message.
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_STATE_MESSAGE)) {
      *input = g_strdup (PIPELINE_STATE_MESSAGE);
      gst_structure_free (message);
      return FALSE;
    }
    // todo currently GPIO_EVENT is sent as STDIN_MESSAGE
    // add another message type for GPIO_EVENT?

    if (gst_structure_has_name (message, STDIN_MESSAGE)) {
      *input = g_strdup (gst_structure_get_string (message, "input"));
      break;
    }
    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

/**
 * wait_pipeline_control_message()
 *
 * Used by the pipeline threads to await control
 * messages to change the pipelines' states
 */
static gboolean
wait_pipeline_control_message (GAsyncQueue * messages, GstState * state)
{
  GstStructure *message = NULL;
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }
    if (gst_structure_has_name (message, PIPELINE_CNTRL_MESSAGE)) {
      GstState set_state = GST_STATE_VOID_PENDING;
      gst_structure_get_uint (message, "state", (guint *) & set_state);
      *state = set_state;
      break;
    }
  }
  gst_structure_free (message);
  return TRUE;
}

static gboolean
wait_pipeline_state_message (GAsyncQueue * messages, GstState state)
{
  GstStructure *message = NULL;

  // Pipeline does not notify us when changing to NULL state, skip wait.
  if (state == GST_STATE_NULL)
    return TRUE;

  // Wait for either a PIPELINE_STATE or TERMINATE message.
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_STATE_MESSAGE)) {
      GstState new = GST_STATE_VOID_PENDING;
      gst_structure_get_uint (message, "new", (guint *) & new);

      if (new == state)
        break;
    }

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
wait_pipeline_eos_message (GAsyncQueue * messages)
{
  GstStructure *message = NULL;

  // Wait for either a PIPELINE_EOS or TERMINATE message.
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_EOS_MESSAGE))
      break;

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

/**
 * handle_usr_signal ()
 *
 * Temporary signal handler to toggle
 * the POV attached status upon SIGUSR1(#10).
 * Used to "soft" simulate the GPIO events.
 * This can be removed once there is "soft" way
 * to set the actual GPIO 42
 */

static gboolean
handle_usr_signal (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  pov_plugged_in ^= 1;
  g_print ("\n\n\n********** %s pov_plugged_in is %d\n\n\n", __func__,
      pov_plugged_in);
  // Push stdin string into the inputs queue.
  g_async_queue_push (appctx->messages_main, gst_structure_new (STDIN_MESSAGE,
          "input", G_TYPE_STRING, GPIO_EVENT, NULL));
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstState state_p1 = GST_STATE_VOID_PENDING, state_p2 = GST_STATE_VOID_PENDING;
  static gboolean waiting_eos = FALSE;

  g_async_queue_push (appctx->messages1,
      gst_structure_new_empty (TERMINATE_MESSAGE));
  g_async_queue_push (appctx->messages2,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  // Get the current state of the pipeline.
  gst_element_get_state (appctx->pipeline1, &state_p1, NULL, 0);
  gst_element_get_state (appctx->pipeline2, &state_p2, NULL, 0);

  if (eos_on_shutdown && !waiting_eos) {
    g_print ("\nEOS enabled -- Sending EOS on the pipeline\n");
    if (state_p1 == GST_STATE_PLAYING) {
      gst_element_post_message (GST_ELEMENT (appctx->pipeline1),
          gst_message_new_custom (GST_MESSAGE_EOS,
              GST_OBJECT (appctx->pipeline1),
              gst_structure_new_empty ("GST_PIPELINE_INTERRUPT")));
    }
    if (state_p2 == GST_STATE_PLAYING) {
      gst_element_post_message (GST_ELEMENT (appctx->pipeline2),
          gst_message_new_custom (GST_MESSAGE_EOS,
              GST_OBJECT (appctx->pipeline2),
              gst_structure_new_empty ("GST_PIPELINE_INTERRUPT")));
    }

    g_print ("\nWaiting for EOS ...\n");
    waiting_eos = TRUE;
  } else if (eos_on_shutdown && waiting_eos) {
    g_print ("\nInterrupt while waiting for EOS - quit main loop...\n");
    g_main_loop_quit (appctx->mloop);
    waiting_eos = FALSE;
  } else {
    g_print ("\n\nReceived an interrupt signal, stopping pipeline ...\n");
  }
  // Signal menu thread to quit.
  g_async_queue_push (appctx->messages_main,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  return TRUE;
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition condition,
    gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GIOStatus status = G_IO_STATUS_NORMAL;
  gchar *input = NULL;

  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((G_IO_STATUS_ERROR == status) && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    } else if ((G_IO_STATUS_ERROR == status) && (NULL == error)) {
      g_printerr ("Unknown error!\n");
      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  // Clear trailing whitespace and newline.
  input = g_strchomp (input);

  // Push stdin string into the inputs queue.
  g_async_queue_push (appctx->messages_main, gst_structure_new (STDIN_MESSAGE,
          "input", G_TYPE_STRING, input, NULL));
  g_free (input);

  return TRUE;
}

/**
 * handle_bus_message ()
 *
 * Common bus handler for all pipelines
 * The bus is identified based on the pipeline
 *
 */

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstElement *this_pipeline = NULL;
  GAsyncQueue *this_msg = NULL;
  gchar *pipeline_name = NULL;
  static GstState target_state = GST_STATE_VOID_PENDING;
  static gboolean in_progress = FALSE, buffering = FALSE;
  if (bus == appctx->bus1) {
    this_pipeline = appctx->pipeline1;
    this_msg = appctx->messages1;
    pipeline_name = gst_element_get_name (appctx->pipeline1);
  } else if (bus == appctx->bus2) {
    this_pipeline = appctx->pipeline2;
    this_msg = appctx->messages2;
    pipeline_name = gst_element_get_name (appctx->pipeline2);
  }

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_error (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      g_print ("\n\nSetting \"%s\" to NULL ...\n", pipeline_name);
      gst_element_set_state (this_pipeline, GST_STATE_NULL);

      g_async_queue_push (this_msg,
          gst_structure_new_empty (TERMINATE_MESSAGE));
    }
      break;
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_warning (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);
    }
      break;
    case GST_MESSAGE_EOS:
      g_print ("\nReceived End-of-Stream from '%s' ...\n",
          GST_MESSAGE_SRC_NAME (message));

      g_async_queue_push (this_msg,
          gst_structure_new_empty (PIPELINE_EOS_MESSAGE));

      // Stop pipelinein case user interrupt has been sent.
      //gst_element_set_state (this_pipeline, GST_STATE_NULL);
      //g_print ("%s set to NULL\n", pipeline_name);
      break;
    case GST_MESSAGE_REQUEST_STATE:
    {
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      GstState state;

      gst_message_parse_request_state (message, &state);
      g_print ("\nSetting \"%s\" state to %s as requested by %s...\n",
          pipeline_name, gst_element_state_get_name (state), name);

      gst_element_set_state (this_pipeline, state);
      target_state = state;

      g_free (name);
    }
      break;
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new, pending;

      // Handle state changes only for the pipeline.
      if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (this_pipeline))
        break;

      gst_message_parse_state_changed (message, &old, &new, &pending);
      g_print ("\n\"%s\" state changed from %s to %s, pending: %s\n",
          pipeline_name, gst_element_state_get_name (old),
          gst_element_state_get_name (new),
          gst_element_state_get_name (pending));

      g_async_queue_push (this_msg, gst_structure_new (PIPELINE_STATE_MESSAGE,
              "new", G_TYPE_UINT, new, "pending", G_TYPE_UINT, pending, NULL));
    }
      break;
    case GST_MESSAGE_BUFFERING:
    {
      gint percent = 0;

      gst_message_parse_buffering (message, &percent);
      g_print ("\nBuffering... %d%%  \r", percent);

      if (percent == 100) {
        // Clear the BUFFERING status.
        buffering = FALSE;

        // Done buffering, if the pending state is playing, go back.
        if (target_state == GST_STATE_PLAYING) {
          g_print ("\nFinished buffering, setting \"%s\" state to PLAYING.\n",
              pipeline_name);
          gst_element_set_state (this_pipeline, GST_STATE_PLAYING);
        }
      } else {
        // Busy buffering...
        gst_element_get_state (this_pipeline, NULL, &target_state, 0);

        if (!buffering && target_state == GST_STATE_PLAYING) {
          g_print ("\nBuffering, setting \"%s\" to PAUSED state.\n",
              pipeline_name);
          gst_element_set_state (this_pipeline, GST_STATE_PAUSED);
          target_state = GST_STATE_PAUSED;
        }

        buffering = TRUE;
      }
    }
      break;
    case GST_MESSAGE_PROGRESS:
    {
      GstProgressType type;
      gchar *code = NULL, *text = NULL;

      gst_message_parse_progress (message, &type, &code, &text);
      g_print ("\nProgress: (%s) %s\n", code, text);

      switch (type) {
        case GST_PROGRESS_TYPE_START:
        case GST_PROGRESS_TYPE_CONTINUE:
          in_progress = TRUE;
          break;
        case GST_PROGRESS_TYPE_COMPLETE:
        case GST_PROGRESS_TYPE_CANCELED:
        case GST_PROGRESS_TYPE_ERROR:
          in_progress = FALSE;
          break;
      }

      g_free (code);
      g_free (text);
    }
      break;
    default:
      break;
  }
  g_free (pipeline_name);
  return TRUE;
}

static gboolean
update_pipeline_state (GstElement * pipeline, GAsyncQueue * messages,
    GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;
  gchar pipeline_name[100];
  gchar *ptr_pipeline_name = NULL;

  // First check current and pending states of the pipeline.
  ret = gst_element_get_state (pipeline, &current, &pending, 0);
  ptr_pipeline_name = gst_element_get_name (pipeline);
  if (ptr_pipeline_name)
    g_strlcpy (pipeline_name, ptr_pipeline_name, 100);
  g_free (ptr_pipeline_name);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to retrieve \"%s\" state!\n", pipeline_name);
    return TRUE;
  }

  if (state == current) {
    g_print ("\"%s\" Already in %s state\n", pipeline_name,
        gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("\"%s\" Pending %s state\n", pipeline_name,
        gst_element_state_get_name (state));
    return TRUE;
  }
  // Check whether to send an EOS event on the pipeline.
  if (eos_on_shutdown &&
      (current == GST_STATE_PLAYING) && (state == GST_STATE_NULL)) {
    g_print ("%s EOS enabled -- Sending EOS on the %s\n", __func__,
        pipeline_name);

    if (!gst_element_send_event (pipeline, gst_event_new_eos ())) {
      g_printerr ("Failed to send EOS event to \"%s\"!", pipeline_name);
      return TRUE;
    }

    if (!wait_pipeline_eos_message (messages))
      return FALSE;
  }

  if (state == GST_STATE_PLAYING) {
    g_print ("waiting for the camera to stabilize \n");
    usleep (500000);
  }
  //TODO:
  //the StopSession (to NULL state) is taking approx 3sec in case of POV unplug
  //so hold Body unit to start until the StopSession is complete so that it wont be blocked
  //Need to be checked in qmmf-sdk whether the 3sec delay can be reduced
  //Also change the lock to cond timed wait
  g_mutex_lock (&mutex);
  g_print ("\n\nSetting \"%s\" to %s\n", pipeline_name,
      gst_element_state_get_name (state));
  ret = gst_element_set_state (pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: %s Failed to transition to %s state!\n",
          pipeline_name, gst_element_state_get_name (state));
      g_mutex_unlock (&mutex);
      return TRUE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\"%s\" is live and does not need PREROLL.\n", pipeline_name);
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("\"%s\" is PREROLLING ...\n", pipeline_name);

      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("\"%s\" failed to PREROLL!\n", pipeline_name);
        g_mutex_unlock (&mutex);
        return TRUE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("\"%s\" state change was successful\n", pipeline_name);
      break;
  }
  g_print ("Wait for the state msg \n");
  if (!wait_pipeline_state_message (messages, state)) {
    g_mutex_unlock (&mutex);
    return FALSE;
  }
  g_mutex_unlock (&mutex);
  return TRUE;
}

static void
get_object_properties (GObject * object, GstState state, guint * index,
    GstStructure * props, GString * options)
{
  GParamSpec **propspecs;
  guint i = 0, nprops = 0;

  propspecs =
      g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &nprops);

  for (i = 0; i < nprops; i++) {
    GParamSpec *param = propspecs[i];
    gchar *field = NULL, *property = NULL;
    const gchar *name = NULL;

    // List only the properties that are mutable in current state.
    if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (param, state))
      continue;

    name = g_param_spec_get_name (param);

    field = g_strdup_printf ("%u", (*index));
    property = !GST_IS_PAD (object) ? g_strdup (name) :
        g_strdup_printf ("%s::%s", GST_PAD_NAME (object), name);

    gst_structure_set (props, field, G_TYPE_STRING, property, NULL);

    g_string_append_printf (options, "   (%2u) %-25s: %s\n", (*index),
        name, g_param_spec_get_blurb (param));

    g_free (property);
    g_free (field);

    // Increment the index for the next option.
    (*index)++;
  }

  return;
}

static void
get_object_signals (GObject * object, guint * index, GstStructure * signals,
    GString * options)
{
  GSignalQuery *query = NULL;
  GType type;

  for (type = G_OBJECT_TYPE (object); type; type = g_type_parent (type)) {
    guint i = 0, *signal_ids = NULL, nsignals = 0;
    gchar *field = NULL;

    if (type == GST_TYPE_ELEMENT || type == GST_TYPE_OBJECT)
      break;

    // Ignore GstBin elements.
    if (type == GST_TYPE_BIN && G_OBJECT_TYPE (object) != GST_TYPE_BIN)
      continue;

    // Lists the signals that this element type has.
    signal_ids = g_signal_list_ids (type, &nsignals);

    // Go over each signal and query additional information.
    for (i = 0; i < nsignals; i++) {
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (signal_ids[i], query);

      if (query->signal_flags & G_SIGNAL_ACTION) {
        field = g_strdup_printf ("%u", (*index));
        gst_structure_set (signals, field, G_TYPE_STRING,
            query->signal_name, NULL);

        g_string_append_printf (options, "   (%2u) %-25s\n", (*index),
            query->signal_name);

        g_free (field);
        field = NULL;

        // Increment the index for the next option.
        (*index)++;
      }

      g_free (query);
      query = NULL;
    }

    // Free the allocated resources for the next iteration.
    g_free (signal_ids);
    signal_ids = NULL;
  }

  return;
}

static void
print_pipeline_options (GstElement * pipeline)
{
  GString *options = g_string_new (NULL);

  APPEND_MENU_HEADER (options);

  APPEND_PIPELINE_CONTROLS_SECTION (options);

  g_string_append_printf (options, "   (%s) %-25s: %s\n", PLUGIN_MODE_OPTION,
      "Plugin Mode", "Choose a plugin which to control");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", MENU_BACK_OPTION,
      "Back", "Return to the previous menu");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);
}

static void
print_pipeline_elements (GstElement * pipeline, GstStructure * plugins)
{
  GString *graph = g_string_new (NULL);
  guint index = 1;

  GstIterator *it = NULL;
  GValue item = G_VALUE_INIT;
  gboolean done = FALSE;

  APPEND_SECTION_SEPARATOR (graph);

  it = gst_bin_iterate_sorted (GST_BIN (pipeline));

  while (!done) {
    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_OK:
      {
        GstElement *element = g_value_get_object (&item);
        gchar *name = gst_element_get_name (element);
        gchar *field = g_strdup_printf ("%u", index);

        gst_structure_set (plugins, field, G_TYPE_STRING, name, NULL);
        g_string_append_printf (graph, "   (%2u) %-25s\n", index, name);

        g_free (field);
        g_free (name);

        g_value_reset (&item);
        index++;
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }

  APPEND_SECTION_SEPARATOR (graph);

  g_value_unset (&item);
  gst_iterator_free (it);

  g_print ("%s", graph->str);
  g_string_free (graph, TRUE);
}

static void
print_element_options (GstElement * element, GstStructure * props,
    GstStructure * signals)
{
  GString *options = g_string_new (NULL);
  GstState state = GST_STATE_VOID_PENDING;
  guint index = 0;

  APPEND_MENU_HEADER (options);

  // Get the current state of the element.
  gst_element_get_state (element, &state, NULL, 0);

  // Get the plugin element properties.
  APPEND_ELEMENT_PROPERTIES_SECTION (options);
  get_object_properties (G_OBJECT (element), state, &index, props, options);

  {
    GstIterator *it = NULL;
    gboolean done = FALSE;

    // Iterate over the element pads and check their properties.
    it = gst_element_iterate_pads (element);

    while (!done) {
      GValue item = G_VALUE_INIT;
      GObject *object = NULL;

      switch (gst_iterator_next (it, &item)) {
        case GST_ITERATOR_OK:
          object = g_value_get_object (&item);

          APPEND_PAD_PROPERTIES_SECTION (GST_PAD_NAME (object), options);
          get_object_properties (object, state, &index, props, options);

          g_value_reset (&item);
          break;
        case GST_ITERATOR_RESYNC:
          gst_iterator_resync (it);
          break;
        case GST_ITERATOR_ERROR:
        case GST_ITERATOR_DONE:
          done = TRUE;
          break;
      }
    }

    gst_iterator_free (it);
  }

  // Get the plugin element signals.
  APPEND_ELEMENT_SIGNALS_SECTION (options);
  get_object_signals (G_OBJECT (element), &index, signals, options);

  APPEND_OTHER_OPTS_SECTION (options);
  g_string_append_printf (options, "   (%s) %-25s: %s\n", MENU_BACK_OPTION,
      "Back", "Return to the previous menu");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);
}

static void
print_property_info (GObject * object, GParamSpec * propspecs)
{
  GString *info = g_string_new (NULL);

  APPEND_SECTION_SEPARATOR (info);

  switch (G_PARAM_SPEC_VALUE_TYPE (propspecs)) {
    case G_TYPE_UINT:
    {
      guint value;
      GParamSpecUInt *range = G_PARAM_SPEC_UINT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %u, Range: %u - %u\n",
          value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_INT:
    {
      gint value;
      GParamSpecInt *range = G_PARAM_SPEC_INT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %d, Range: %d - %d\n",
          value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_ULONG:
    {
      gulong value;
      GParamSpecULong *range = G_PARAM_SPEC_ULONG (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %lu, Range: %lu - %lu\n",
          value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_LONG:
    {
      glong value;
      GParamSpecLong *range = G_PARAM_SPEC_LONG (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %ld, Range: %ld - %ld\n",
          value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_UINT64:
    {
      guint64 value;
      GParamSpecUInt64 *range = G_PARAM_SPEC_UINT64 (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %" G_GUINT64_FORMAT ", "
          "Range: %" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT "\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_INT64:
    {
      gint64 value;
      GParamSpecInt64 *range = G_PARAM_SPEC_INT64 (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %" G_GINT64_FORMAT ", "
          "Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT "\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_FLOAT:
    {
      gfloat value;
      GParamSpecFloat *range = G_PARAM_SPEC_FLOAT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %15.7g, "
          "Range: %15.7g - %15.7g\n", value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_DOUBLE:
    {
      gdouble value;
      GParamSpecDouble *range = G_PARAM_SPEC_DOUBLE (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %15.7g, "
          "Range: %15.7g - %15.7g\n", value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_BOOLEAN:
    {
      gboolean value;
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %s, Possible values: "
          "0(false), 1(true)\n", value ? "true" : "false");
      break;
    }
    case G_TYPE_STRING:
    {
      gchar *value;
      g_object_get (object, propspecs->name, &value, NULL);
      g_string_append_printf (info, " Current value: %s\n", value);
      break;
    }
    default:
      if (G_IS_PARAM_SPEC_ENUM (propspecs)) {
        GEnumValue *enumvalues;
        gint value;
        const gchar *nick = "";
        guint idx = 0;

        g_object_get (object, propspecs->name, &value, NULL);
        enumvalues =
            G_ENUM_CLASS (g_type_class_ref (propspecs->value_type))->values;

        while (enumvalues[idx].value_name) {
          g_string_append_printf (info, "   (%d): %-16s - %s\n",
              enumvalues[idx].value, enumvalues[idx].value_nick,
              enumvalues[idx].value_name);

          if (enumvalues[idx].value == value)
            nick = enumvalues[idx].value_nick;

          idx++;
        }

        g_string_append_printf (info, "\n Current value: %d, \"%s\"\n",
            value, nick);
      } else if (propspecs->value_type == GST_TYPE_ARRAY) {
        GValue value = G_VALUE_INIT;
        gchar *string = NULL;

        g_value_init (&value, GST_TYPE_ARRAY);
        g_object_get_property (object, propspecs->name, &value);

        string = gst_value_serialize (&value);
        g_string_append_printf (info, "\n Current value: %s\n", string);

        g_value_unset (&value);
        g_free (string);
      } else if (propspecs->value_type == GST_TYPE_STRUCTURE) {
        GValue value = G_VALUE_INIT;
        GstStructure *structure = NULL;
        gchar *string = NULL;

        g_value_init (&value, GST_TYPE_STRUCTURE);
        g_object_get_property (object, propspecs->name, &value);

        structure = g_value_dup_boxed (&value);
        g_value_unset (&value);

        string = gst_structure_to_string (structure);
        gst_structure_free (structure);

        g_string_append_printf (info, "\n Current value: %s\n", string);
        g_free (string);
      } else {
        g_string_append_printf (info, "Unknown type %ld \"%s\"\n",
            (glong) propspecs->value_type, g_type_name (propspecs->value_type));
      }
      break;
  }

  APPEND_SECTION_SEPARATOR (info);

  g_print ("%s", info->str);
  g_string_free (info, TRUE);
}

static gboolean
gst_pipeline_menu (GstElement * pipeline, GAsyncQueue * messages,
    GstElement ** element)
{
  gchar *input = NULL;

  print_pipeline_options (pipeline);

  g_print ("\n\nChoose an option: ");

  // If FALSE is returned termination signal has been issued.
  if (!wait_main_message (messages, &input))
    return FALSE;
  else if (g_str_equal (input, GPIO_EVENT)) {
    //if the input in from queue is a GPIO event, return
    //push the GPIO EVENT msg again to preserve it for the main menu thread
    g_async_queue_push (messages, gst_structure_new (STDIN_MESSAGE,
            "input", G_TYPE_STRING, GPIO_EVENT, NULL));
    g_free (input);
    return FALSE;
  }

  if (g_str_equal (input, PLUGIN_MODE_OPTION)) {
    GstStructure *plugins = gst_structure_new_empty ("plugins");

    // Print a graph with all plugins in the pipeline.
    print_pipeline_elements (pipeline, plugins);

    // Choose a plugin to control.
    g_print ("\nEnter plugin name or its index (or press Enter to return): ");

    // If FALSE is returned termination signal has been issued.
    if (!wait_main_message (messages, &input)) {
      gst_structure_free (plugins);
      return FALSE;
    } else if (g_str_equal (input, GPIO_EVENT)) {
      //if the input in from queue is a GPIO event, return
      //push the GPIO EVENT msg again to preserve it for the main menu thread
      g_async_queue_push (messages, gst_structure_new (STDIN_MESSAGE,
              "input", G_TYPE_STRING, GPIO_EVENT, NULL));
      gst_structure_free (plugins);
      g_free (input);
      return FALSE;
    }

    if (gst_structure_has_field (plugins, input)) {
      const gchar *name = gst_structure_get_string (plugins, input);

      if ((*element = gst_bin_get_by_name (GST_BIN (pipeline), name)) == NULL)
        g_printerr ("Invalid plugin index!\n");

    } else if (!g_str_equal (input, "")) {
      if ((*element = gst_bin_get_by_name (GST_BIN (pipeline), input)) == NULL)
        g_printerr ("Invalid plugin name!\n");
    }

    gst_structure_free (plugins);
  } else if (g_str_equal (input, MENU_BACK_OPTION)) {
    g_print ("\nBack..!!\n");
    g_free (input);
    return FALSE;
  }

  g_free (input);
  return TRUE;
}

static gboolean
gst_element_menu (GstElement ** element, GAsyncQueue * messages)
{
  GstStructure *props = NULL, *signals = NULL;
  gchar *input = NULL;

  props = gst_structure_new_empty ("properties");
  signals = gst_structure_new_empty ("signals");

  print_element_options (*element, props, signals);

  g_print ("\n\nChoose an option: ");

  // If FALSE is returned termination signal has been issued.
  if (!wait_main_message (messages, &input)) {
    gst_structure_free (props);
    gst_structure_free (signals);

    return FALSE;
  } else if (g_str_equal (input, GPIO_EVENT)) {
    //if the input in from queue is a GPIO event, return
    //push the GPIO EVENT msg again to preserve it for the main menu thread
    g_async_queue_push (messages, gst_structure_new (STDIN_MESSAGE,
            "input", G_TYPE_STRING, GPIO_EVENT, NULL));
    gst_structure_free (props);
    gst_structure_free (signals);
    g_free (input);
    return FALSE;
  }

  if (gst_structure_has_field (props, input)) {
    GObject *object = NULL;
    GParamSpec *propspecs = NULL;
    gchar **strings = NULL;

    // Get the property string from the structure.
    const gchar *propname = gst_structure_get_string (props, input);

    // Split the string in order to check whether it is pad property.
    strings = g_strsplit (propname, "::", 2);

    // In case property belongs to a pad get reference to that pad by name.
    object = (g_strv_length (strings) != 2) ? G_OBJECT (*element) :
        G_OBJECT (gst_element_get_static_pad (*element, strings[0]));

    // In case property belongs to a pad get pad property name.
    propname = (g_strv_length (strings) != 2) ? propname : strings[1];

    // Get the property specs structure.
    propspecs =
        g_object_class_find_property (G_OBJECT_GET_CLASS (object), propname);

    print_property_info (object, propspecs);

    if (propspecs->flags & G_PARAM_WRITABLE) {
      g_print ("\nEnter value (or press Enter to keep current one): ");

      // If FALSE is returned termination signal has been issued.
      if (!wait_main_message (messages, &input)) {
        gst_structure_free (props);
        gst_structure_free (signals);

        return FALSE;
      } else if (g_str_equal (input, GPIO_EVENT)) {
        //if the input in from queue is a GPIO event, return
        //push the GPIO EVENT msg again to preserve it for the main menu thread
        g_async_queue_push (messages, gst_structure_new (STDIN_MESSAGE,
                "input", G_TYPE_STRING, GPIO_EVENT, NULL));
        gst_structure_free (props);
        gst_structure_free (signals);
        g_free (input);
        return FALSE;
      }
      // If it's not an empty string deserialize the string to a GValue.
      if (!g_str_equal (input, "")) {
        GValue value = G_VALUE_INIT;
        g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));

        if (gst_value_deserialize (&value, input))
          g_object_set_property (object, propname, &value);
      }
    } else if (propspecs->flags & G_PARAM_READABLE) {
      g_print ("\nRead-Only property. Press Enter to continue...");
    }
    // Unreference in case the object was a pad.
    if (GST_IS_PAD (object))
      gst_object_unref (object);

    g_strfreev (strings);
  } else if (gst_structure_has_field (signals, input)) {
    const gchar *signalname = gst_structure_get_string (signals, input);
    g_signal_emit_by_name (*element, signalname);
  } else if (g_str_equal (input, MENU_BACK_OPTION)) {
    gst_object_unref (*element);
    *element = NULL;
  } else {
    g_print ("Invalid option: '%s'\n", input);
  }

  g_free (input);

  gst_structure_free (props);
  gst_structure_free (signals);

  return TRUE;
}

/**
 * get_gpio_handle ()
 *
 * Updates the handle of the gpoio events
 * to the global var gpio_req_fd
 *
 * returns -1 if error
 */
static gint
get_gpio_handle ()
{
  struct gpioevent_request req;
  struct gpiohandle_data data;

  int fd;
  int ret;
  fd = open (GPIOCHIPFILE, O_RDONLY);
  if (fd == -1) {
    g_printerr ("failed to open %s : %s\n", GPIOCHIPFILE, strerror (errno));
    return -1;
  }

  req.lineoffset = GPIOLINEOFFSET;
  req.handleflags = GPIOHANDLE_REQUEST_INPUT;
  req.eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
  ret = ioctl (fd, GPIO_GET_LINEEVENT_IOCTL, &req);
  if (ret == -1) {
    g_printerr ("Unable to get line event from ioctl : %s", strerror (errno));
    close (fd);
    return -1;
  }

  /* Read initial states */
  ret = ioctl (req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data);
  if (ret == -1) {
    g_printerr ("Failed to issue GPIOHANDLE GET LINE "
        "VALUES IOCTL (%s)\n", strerror (errno));
    return -1;
  }
  g_print ("Monitoring line %d on %s\n", GPIOLINEOFFSET, GPIOCHIPFILE);
  g_print ("Initial line value: %d\n", data.values[0]);
  pov_plugged_in = data.values[0] ? FALSE : TRUE;
  gpio_req_fd = req.fd;
  return 0;
}

/**
 * timer_handler()
 *
 * Called when the expiry of the
 * timer fired upon every gpio event
 **/
static gpointer
timer_handler (union sigval sv)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (sv.sival_ptr);
    /* plug event */
  GstStructure *event_struct =
      gst_structure_new_empty ("camera-plug");
  GstEvent *cam_plug_event =
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, event_struct);
  //Send 'camera-plug' event to POV pipeline
  gst_element_send_event (appctx->pipeline2, cam_plug_event);
  g_async_queue_push (appctx->messages_main, gst_structure_new (STDIN_MESSAGE,
          "input", G_TYPE_STRING, GPIO_EVENT, NULL));

  return NULL;
}

/**
 * gpio_event_thread ()
 *
 * Thread to monitor the gpio rising and falling edge events
 * Performs a "timed" poll () call on the gpio_req_fd
 * The poll () will be unblocked whenever there is one of the below
 *  - gpio event
 *  - timeout
 * Based on the poll function's return value it can be determined
 * if there was gpio event or a timeout.
 * If there is a gpio event, then a timer is fired to be expire within
 * timer_expiry_ms, if there is a consequent gpio event before timer_expiry_ms,
 * then timer gets reset again to timer_expiry_ms. So the last occured gpio event
 * before timer_expiry_ms is considered for the final gpio value. This is to wait
 * for the gpio line to stabilize while plug/un-plug of the POV camera
 *
 * If the poll () returns because of poll_timeout_ms and main loop is running, then
 * it goes to next iteration of poll. Else if the main loop is not running, the thread exits
 */
static gpointer
gpio_event_thread (gpointer userdata)
{
  struct pollfd pfd[1];
  gint poll_timeout_ms = 1000;
  int fd = gpio_req_fd;
  int ret;
  pfd[0].fd = fd;
  pfd[0].events = POLLIN;
  long long timer_expiry_ns = 10 * 1000000LL;

  /* timer */
  timer_t timerid;
  struct sigevent sev;
  struct itimerspec its;
  memset(&sev, 0, sizeof(struct sigevent));
  memset(&its, 0, sizeof(struct itimerspec));

  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_value.sival_ptr = appctx;
  sev.sigev_notify_function = timer_handler;
  if (timer_create (CLOCK_REALTIME, &sev, &timerid) == -1) {
    g_printerr ("Failed to create timer");
  }
  its.it_value.tv_sec = 0;
  its.it_value.tv_nsec = timer_expiry_ns;
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;
  while (1) {
    ret = poll (pfd, 1, poll_timeout_ms);
    if (ret == -1) {
      g_printerr ("Error while polling event from GPIO: %s", strerror (errno));
    } else if (ret == 0) {
      if (!g_main_loop_is_running (appctx->mloop)) {
        g_print ("exiting %s\n", __func__);
        break;
      }
    } else if (pfd[0].revents & POLLIN) {
      struct gpioevent_data event;
      g_print ("Reading the gpio event\n");
      ret = read (fd, &event, sizeof (event));
      if (ret == -1) {
        if (errno == -EAGAIN) {
          g_print ("nothing available\n");
          continue;
        } else {
          ret = -errno;
          g_printerr ("Failed to read event (%s)\n", strerror (errno));
          continue;
        }
      }
      if (ret != sizeof (event)) {
        g_printerr ("Reading event failed\n");
        continue;
      }
      g_print ("GPIO EVENT @ %" G_GUINT64_FORMAT " - ", event.timestamp);
      switch (event.id) {
        case GPIOEVENT_EVENT_RISING_EDGE:
          g_print ("  rising edge\n");
          if (timer_settime (timerid, 0, &its, NULL) == -1) {
            g_printerr ("Failed to set timer\n");
          }
          if (pov_plugged_in != FALSE) {
            pov_plugged_in = FALSE;
          }
          break;
        case GPIOEVENT_EVENT_FALLING_EDGE:
          g_print ("  falling edge\n");
          if (timer_settime (timerid, 0, &its, NULL) == -1) {
            g_printerr ("Failed to set timer\n");
          }
          if (pov_plugged_in != TRUE) {
            pov_plugged_in = TRUE;
          }
          break;
        default:
          g_print ("unknown event\n");
      }
    }
  }
  timer_delete (timerid);
  close (fd);
  return NULL;
}

/**
 * pipeline2_thread ()
 *
 * Independant thread to wait for
 *  - pipeline state change control msgs
 *  - terminate msgs
 */
static gpointer
pipeline2_thread (gpointer userdata)
{

  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  do {
    //wait for message
    GstState new_state;
    if (wait_pipeline_control_message (appctx->messages2, &new_state)) {
      update_pipeline_state (appctx->pipeline2, appctx->messages2, new_state);
    } else {
      //exit
      update_pipeline_state (appctx->pipeline2, appctx->messages2,
          GST_STATE_NULL);
      break;
    }
    // Push a notification to main msg queue to update the App state accordingly
    g_async_queue_push (appctx->messages_main,
        gst_structure_new_empty (PIPELINE_STATE_MESSAGE));
  } while (1);
  g_source_remove (bus2_watch_id);
  return NULL;
}

/**
 * pipeline1_thread ()
 *
 * Independant thread to wait for
 *  - pipeline state change control msgs
 *  - terminate msgs
 */
static gpointer
pipeline1_thread (gpointer userdata)
{

  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  do {
    //wait for message
    GstState new_state;
    if (wait_pipeline_control_message (appctx->messages1, &new_state)) {
      update_pipeline_state (appctx->pipeline1, appctx->messages1, new_state);
    } else {
      //exit
      update_pipeline_state (appctx->pipeline1, appctx->messages1,
          GST_STATE_NULL);
      break;
    }
    // Push a notification to main msg queue to update the App state accordingly
    g_async_queue_push (appctx->messages_main,
        gst_structure_new_empty (PIPELINE_STATE_MESSAGE));
  } while (1);
  g_source_remove (bus1_watch_id);
  return NULL;
}

/**
 * create_pipeline ()
 *
 * Wrapper function to do gst_parse_launch and add bus watch
 * for the pipeline elements passed as string 'input'
 *
 */

static gint
create_pipeline (GstElement ** pipeline, gpointer userdata, gchar * input)
{
  GError *error = NULL;
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  // Create a GST pipeline element.
  *pipeline = gst_parse_launch ((const gchar *) input, &error);

  // Check for errors on pipe creation.
  if ((NULL == *pipeline) && (error != NULL)) {
    g_printerr ("\n\nFailed to create pipeline, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    return -1;
  } else if ((NULL == *pipeline) && (NULL == error)) {
    g_printerr ("\n\nFailed to create pipeline, unknown error!\n");
    return -1;
  } else if ((*pipeline != NULL) && (error != NULL)) {
    g_printerr ("\n\nErroneous pipeline, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);
    return -1;
  } else {
    g_print ("\n%s Successfully created pipeline %p\n", __func__, *pipeline);
    GstBus *bus = NULL;
    guint bus_watch_id = 0;
    // Retrieve reference to the pipeline's bus.
    if ((bus = gst_pipeline_get_bus (GST_PIPELINE (*pipeline))) == NULL) {
      g_printerr ("\n\nERROR: Failed to retrieve pipeline bus!\n");
      return -1;
    }
    // Watch for messages on the pipeline's bus.
    bus_watch_id = gst_bus_add_watch (bus, handle_bus_message, appctx);
    gst_object_unref (bus);
    return bus_watch_id;
  }
}

/**
 * configure_pipeline ()
 *
 * Sub menu function to accept the pipeline elements from
 * STDIN and create pipeline
 */
static gint
configure_pipeline (guint pipeline_idx, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gchar *input = NULL;
  do {
    g_print ("\n\nPlease enter the pipeline %d elements: \n" EQUAL_LINE "\n",
        pipeline_idx);
    if (wait_main_message (appctx->messages_main, &input)) {
      switch (pipeline_idx) {
        case 1:
          bus1_watch_id = create_pipeline (&appctx->pipeline1, appctx, input);
          if (bus1_watch_id < 0)
            continue;
          gst_element_set_name (appctx->pipeline1, PIPELINE_BWC_NAME);
          appctx->bus1 =
              gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline1));
          break;
        case 2:
          bus2_watch_id = create_pipeline (&appctx->pipeline2, appctx, input);
          if (bus2_watch_id < 0)
            continue;
          gst_element_set_name (appctx->pipeline2, PIPELINE_POV_NAME);
          appctx->bus2 =
              gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline2));
          break;
        default:
          g_printerr ("%s: Invalid pipeline index %d\n", __func__,
              pipeline_idx);
          return -1;
      }
      return 0;
      //fixme: observing SIGSEGV upon multiple gpio events when waitin for pipeline from user
    }
  } while (1);
  return -1;
}

/**
 * edit_pipeline ()
 *
 * Sub menu function to enable user to edit
 * pipeline and element options by accepting input via
 * STDIN
 */

static void
edit_pipeline (GstElement * pipeline, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstElement *element = NULL;
  gboolean iter = TRUE;
  while (iter) {
    if (element == NULL) {
      iter = gst_pipeline_menu (pipeline, appctx->messages_main, &element);
    } else {
      iter = gst_element_menu (&element, appctx->messages_main);
    }
  }
  if (element != NULL)
    gst_object_unref (element);
}

/**
 * update_pipelines ()
 *
 * Helper function to set the pipeline control msgs into its
 * respective async msg queue based on the current state
 */

static void
update_pipelines (guint state, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  if (state & BWC) {
    g_async_queue_push (appctx->messages1,
        gst_structure_new (PIPELINE_CNTRL_MESSAGE, "state", G_TYPE_UINT,
            GST_STATE_PLAYING, NULL));
  } else {
    g_async_queue_push (appctx->messages1,
        gst_structure_new (PIPELINE_CNTRL_MESSAGE, "state", G_TYPE_UINT,
            GST_STATE_NULL, NULL));
  }

  if (state & POV) {
    g_async_queue_push (appctx->messages2,
        gst_structure_new (PIPELINE_CNTRL_MESSAGE, "state", G_TYPE_UINT,
            GST_STATE_PLAYING, NULL));
  } else {
    g_async_queue_push (appctx->messages2,
        gst_structure_new (PIPELINE_CNTRL_MESSAGE, "state", G_TYPE_UINT,
            GST_STATE_NULL, NULL));
  }
}

/**
 * configure_pref_on_pov_attach ()
 *
 * Sub menu function to seek user input
 * on what is the User Preferred behaviour
 * when the POV is attached to the main unit
 *  - Run both BWC and POV pipeline
 *  - Run only POV pipeline
 *  - Run only BWC pipeline
 */
static void
configure_pref_on_pov_attach (gpointer userdata)
{
  gchar *input = NULL;
  gchar user_pref_both = 'a', user_pref_pov_only = 'b', user_pref_bwc_only = 'c';
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  g_print
      ("\n\nPlease enter the preferred behaviour when POV is attached runtime: \n"
      EQUAL_LINE "\n");
  g_print ("  (%c)   Both BWC and POV running\n", user_pref_both);
  g_print ("  (%c)   Only POV running\n", user_pref_pov_only);
  g_print ("  (%c)   Only BWC running\n", user_pref_bwc_only);
  do {
    if (wait_main_message (appctx->messages_main, &input)) {
      if (input[0] == user_pref_both) {
        user_pref = APP_STATE_BWC_AND_POV;
        break;
      } else if (input[0] == user_pref_pov_only) {
        user_pref = APP_STATE_POV_ONLY;
        break;
      } else if (input[0] == user_pref_bwc_only) {
        user_pref = APP_STATE_BWC_ONLY;
        break;
      }
    }
  } while (1);

  g_print ("Setting user_pref to %d \n", user_pref);
}

static void
capture_image (GstElement * pipeline)
{
  GValue item = G_VALUE_INIT;
  GstIterator *it = gst_bin_iterate_sources (GST_BIN (pipeline));
  while (GST_ITERATOR_OK == gst_iterator_next (it, &item)) {
    GstElement *element = g_value_get_object (&item);
    if (element) {
      GstElementFactory *factory = gst_element_get_factory (element);
      g_print ("%s checking %s\n", __func__, GST_OBJECT_NAME (factory));
      if (g_str_equal (GST_OBJECT_NAME (factory), "qtiqmmfsrc")) {
        g_signal_emit_by_name (element, "capture-image");
        g_print ("Emitted signal 'capture-image' \n");
        break;
      }
    } else {
      g_printerr ("%s - Unable to get element\n", __func__);
    }
  }
  g_value_unset (&item);
  gst_iterator_free (it);
}

/**
 * menu_thread ()
 *
 * Main Menu thread to listen on message queue for menu options and
 * gpio events
 *
 * Handles the state transitions based w.r.t. the respective conditions
 */

static gpointer
menu_thread (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gboolean active = TRUE;
  gchar *input = NULL;
  gboolean running = FALSE;
  guint current_state = 0, pending_state = 0, new_state = 0;
  gchar menu_text_b0[] = "Turn off BWC";
  gchar menu_text_p0[] = "Turn off POV";
  gchar menu_text_b1[] = "Turn on BWC";
  gchar menu_text_p1[] = "Turn on POV";
  gchar menu_text_sb[] = "Switch to BWC";
  gchar menu_text_sp[] = "Switch to POV";
  gchar menu_text_be[] = "BWC pipeline elements menu";
  gchar menu_text_pe[] = "POV pipeline elements menu";
  gchar menu_text_u[] =
      "Change preferred behaviour when POV is attached runtime";
  gchar menu_text_quit[] = "Quit application";
  gchar menu_text_ib[] = "Capture Image from BWC";
  gchar menu_text_ip[] = "Capture Image from POV";

  if ((0 != configure_pipeline (1, appctx)) ||
      (0 != configure_pipeline (2, appctx))) {
    g_async_queue_push (appctx->messages1,
        gst_structure_new_empty (TERMINATE_MESSAGE));
    g_async_queue_push (appctx->messages2,
        gst_structure_new_empty (TERMINATE_MESSAGE));
    g_main_loop_quit (appctx->mloop);
    return NULL;
  }

  configure_pref_on_pov_attach (appctx);
  new_state = pov_plugged_in ? user_pref : APP_STATE_BWC_ONLY;

  while (active) {
    if (current_state != new_state && pending_state != new_state) {
      update_pipelines (new_state, appctx);
      pending_state = new_state;
    }
    // print the menu as per the current state
    GString *menu = g_string_new (NULL);
    APPEND_MENU_HEADER (menu);
    g_string_append_printf (menu,
        " *************Current state: BWC: %sPLAYING, POV: %sPLAYING*************\n",
        current_state & BWC ? "" : "NOT-", current_state & POV ? "" : "NOT-");
    if (current_state == APP_STATE_BWC_AND_POV) {
      g_string_append_printf (menu, " (%2s)%-8s%-25s\n", TURN_OFF_BWC, " ",
          menu_text_b0);
      g_string_append_printf (menu, " (%2s)%-8s%-25s\n", TURN_OFF_POV, " ",
          menu_text_p0);
      g_string_append_printf (menu, " (%2s)%-8s%-25s\n", CAPTURE_IMAGE_BWC, " ",
          menu_text_ib);
      g_string_append_printf (menu, " (%2s)%-8s%-25s\n", CAPTURE_IMAGE_POV, " ",
          menu_text_ip);
    } else if (current_state == APP_STATE_BWC_ONLY) {
      if (pov_plugged_in) {
        g_string_append_printf (menu, " (%2s)%-8s%-25s\n", TURN_ON_POV, " ",
            menu_text_p1);
        g_string_append_printf (menu, " (%2s)%-8s%-25s\n", SWITCH_TO_POV, " ",
            menu_text_sp);
      }
      g_string_append_printf (menu, " (%2s)%-8s%-25s\n", CAPTURE_IMAGE_BWC, " ",
          menu_text_ib);
    } else if (current_state == APP_STATE_POV_ONLY) {
      g_string_append_printf (menu, " (%2s)%-8s%-25s\n", TURN_ON_BWC, " ",
          menu_text_b1);
      g_string_append_printf (menu, " (%2s)%-8s%-25s\n", SWITCH_TO_BWC, " ",
          menu_text_sb);
      g_string_append_printf (menu, " (%2s)%-8s%-25s\n", CAPTURE_IMAGE_POV, " ",
          menu_text_ip);
    }
    g_string_append_printf (menu, " (%2s)%-8s%-25s\n", EDIT_BWC_PIPELINE, " ",
        menu_text_be);
    g_string_append_printf (menu, " (%2s)%-8s%-25s\n", EDIT_POV_PIPELINE, " ",
        menu_text_pe);
    g_string_append_printf (menu, " (%2s)%-8s%-25s\n", USER_PREF_POV_AT, " ",
        menu_text_u);
    g_string_append_printf (menu, " (%2s)%-8s%-25s\n", QUIT_OPTION, " ",
        menu_text_quit);
    g_string_append_printf (menu, " *************POV %sPLUGGED*************\n",
        pov_plugged_in ? "" : "UN");
    g_print ("%s", menu->str);
    g_string_free (menu, TRUE);
    if (!wait_main_message (appctx->messages_main, &input)) {
      if (input && g_str_equal (input, PIPELINE_STATE_MESSAGE)) {
        GstState state_p1 = GST_STATE_VOID_PENDING, state_p2 =
            GST_STATE_VOID_PENDING;
        // Get the current state of the pipelines.
        gst_element_get_state (appctx->pipeline1, &state_p1, NULL, 0);
        gst_element_get_state (appctx->pipeline2, &state_p2, NULL, 0);
        if (state_p1 == GST_STATE_PLAYING) {
          current_state |= BWC;
        } else {
          current_state &= ~(BWC);
        }
        if (state_p2 == GST_STATE_PLAYING) {
          current_state |= POV;
        } else {
          current_state &= ~(POV);
        }
        continue;
      } else {
        //terminate
        active = FALSE;
        break;
      }
    }
    switch (current_state) {
      case APP_STATE_BWC_AND_POV:
        if (g_str_equal (input, GPIO_EVENT) && !pov_plugged_in) {
          new_state = APP_STATE_BWC_ONLY;
        } else if (g_str_equal (input, TURN_OFF_BWC)) {
          new_state = APP_STATE_POV_ONLY;
        } else if (g_str_equal (input, TURN_OFF_POV)) {
          new_state = APP_STATE_BWC_ONLY;
        } else if (g_str_equal (input, EDIT_BWC_PIPELINE)) {
          edit_pipeline (appctx->pipeline1, appctx);
        } else if (g_str_equal (input, EDIT_POV_PIPELINE)) {
          edit_pipeline (appctx->pipeline2, appctx);
        } else if (g_str_equal (input, USER_PREF_POV_AT)) {
          configure_pref_on_pov_attach (appctx);
          new_state = pov_plugged_in ? user_pref : APP_STATE_BWC_ONLY;
        } else if (g_str_equal (input, CAPTURE_IMAGE_BWC)) {
          capture_image (appctx->pipeline1);
        } else if (g_str_equal (input, CAPTURE_IMAGE_POV)) {
          capture_image (appctx->pipeline2);
        } else if (g_str_equal (input, QUIT_OPTION)) {
          active = FALSE;
        }
        break;
      case APP_STATE_BWC_ONLY:
        if (g_str_equal (input, GPIO_EVENT) && pov_plugged_in) {
          if (user_pref == APP_STATE_POV_ONLY) {
            new_state = APP_STATE_POV_ONLY;
          } else if (user_pref == APP_STATE_BWC_AND_POV) {
            new_state = APP_STATE_BWC_AND_POV;
          }
        } else if (g_str_equal (input, TURN_ON_POV) && pov_plugged_in) {
          new_state = APP_STATE_BWC_AND_POV;
        } else if (g_str_equal (input, SWITCH_TO_POV) && pov_plugged_in) {
          new_state = APP_STATE_POV_ONLY;
        } else if (g_str_equal (input, EDIT_BWC_PIPELINE)) {
          edit_pipeline (appctx->pipeline1, appctx);
        } else if (g_str_equal (input, EDIT_POV_PIPELINE)) {
          edit_pipeline (appctx->pipeline2, appctx);
        } else if (g_str_equal (input, USER_PREF_POV_AT)) {
          configure_pref_on_pov_attach (appctx);
          new_state = pov_plugged_in ? user_pref : APP_STATE_BWC_ONLY;
        } else if (g_str_equal (input, CAPTURE_IMAGE_BWC)) {
          capture_image (appctx->pipeline1);
        } else if (g_str_equal (input, QUIT_OPTION)) {
          active = FALSE;
        }
        break;
      case APP_STATE_POV_ONLY:
        if (g_str_equal (input, GPIO_EVENT) && !pov_plugged_in) {
          new_state = APP_STATE_BWC_ONLY;
        } else if (g_str_equal (input, TURN_ON_BWC)) {
          new_state = APP_STATE_BWC_AND_POV;
        } else if (g_str_equal (input, SWITCH_TO_BWC)) {
          new_state = APP_STATE_BWC_ONLY;
        } else if (g_str_equal (input, EDIT_BWC_PIPELINE)) {
          edit_pipeline (appctx->pipeline1, appctx);
        } else if (g_str_equal (input, EDIT_POV_PIPELINE)) {
          edit_pipeline (appctx->pipeline2, appctx);
        } else if (g_str_equal (input, USER_PREF_POV_AT)) {
          configure_pref_on_pov_attach (appctx);
          new_state = pov_plugged_in ? user_pref : APP_STATE_BWC_ONLY;
        } else if (g_str_equal (input, CAPTURE_IMAGE_POV)) {
          capture_image (appctx->pipeline1);
        } else if (g_str_equal (input, QUIT_OPTION)) {
          active = FALSE;
        }
        break;
      default:
        new_state = pov_plugged_in ? user_pref : APP_STATE_BWC_ONLY;
        if (g_str_equal (input, QUIT_OPTION)) {
          active = FALSE;
        }
        break;
    }
  }
  g_async_queue_push (appctx->messages1,
      gst_structure_new_empty (TERMINATE_MESSAGE));
  g_print ("waiting for p1thread to join\n");
  g_thread_join (appctx->p1thread);
  g_async_queue_push (appctx->messages2,
      gst_structure_new_empty (TERMINATE_MESSAGE));
  g_print ("waiting for p2thread to join\n");
  g_thread_join (appctx->p2thread);
  g_main_loop_quit (appctx->mloop);
  return NULL;
}


/**
 * init_camera ()
 *
 * A helper function to quickly run and stop a dummy pipeline
 * using qtiqmmfsrc. This is a workaround to avoid issues while
 * starting both cameras at the same instance.
 */
static void
init_camera ()
{
  GstElement *pipeline;
  g_print (EQUAL_LINE " Intialising.. please wait.. " EQUAL_LINE "\n");
  pipeline = gst_parse_launch ("qtiqmmfsrc camera=1 ! fakesink", NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

gint
main (gint argc, gchar * argv[])
{
  GstAppContext *appctx = gst_app_context_new ();
  GOptionContext *optsctx = NULL;
  GstBus *bus = NULL;
  GIOChannel *iostdin = NULL, *iogpio42 = NULL;
  GThread *mthread = NULL, *gpiothread = NULL;
  guint intrpt_watch_id = 0, stdin_watch_id = 0, usrsig_watch_id =
      0, event_watch_id = 0;
  gint fd = -1;
  gchar *str_pipeline1 = NULL, *str_pipeline2 = NULL;

  g_set_prgname ("gst-hotplug-pov-camera-app");

  // Parse command line entries.
  if ((optsctx = g_option_context_new ("DESCRIPTION")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (optsctx, entries, NULL);
    g_option_context_add_group (optsctx, gst_init_get_option_group ());

    success = g_option_context_parse (optsctx, &argc, &argv, &error);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);

      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      return -1;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  init_camera ();
  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Register function for handling user signals with the main loop.
  usrsig_watch_id = g_unix_signal_add (SIGUSR1, handle_usr_signal, appctx);

  // Create IO channel from the stdin stream.
  if ((iostdin = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("ERROR: Failed to initialize Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  // Register handling function with the main loop for stdin channel data.
  stdin_watch_id =
      g_io_add_watch (iostdin, G_IO_IN | G_IO_PRI, handle_stdin_source, appctx);
  g_io_channel_unref (iostdin);


  if (-1 == get_gpio_handle ()) {
    g_printerr ("ERROR: Failed to get the ioctl handle for the GPIO!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  //Initiate the gpio poll thread
  if ((gpiothread =
          g_thread_new ("gpiopoll", gpio_event_thread, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create gpio poll thread thread!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  // Initiate the pipeline 1 thread.
  if ((appctx->p1thread =
          g_thread_new ("Pipeline1", pipeline1_thread, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create Pipeline1 thread!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  // Initiate the pipeline 2 thread.
  if ((appctx->p2thread =
          g_thread_new ("Pipeline2", pipeline2_thread, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create Pipeline2 thread!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  // Initiate the main menu thread.
  if ((mthread = g_thread_new ("MenuThread", menu_thread, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create menu thread!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Run main loop.
  g_main_loop_run (appctx->mloop);

  // Waits until main menu thread finishes.
  g_print ("waiting for gpiothread to join\n");
  g_thread_join (gpiothread);
  g_print ("waiting for mthread to join\n");
  g_thread_join (mthread);

  g_source_remove (stdin_watch_id);
  g_source_remove (intrpt_watch_id);
  g_source_remove (usrsig_watch_id);

  gst_app_context_free (appctx);
  gst_deinit ();

  return 0;
}
