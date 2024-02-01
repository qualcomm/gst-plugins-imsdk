/*
* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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

#include <cmath>

#include <glib-unix.h>
#include <gst/gst.h>

#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#define DASH_LINE   "----------------------------------------------------------------------"
#define SPACE       "                                                                      "
#define HASH_LINE  "##################################################"
#define EQUAL_LINE "=================================================="
#define DASH_SLINE "--------------------------------------------------"

#define APPEND_SECTION_SEPARATOR(string) \
  g_string_append_printf (string, " %.*s%.*s\n", 39, DASH_SLINE, 40, DASH_SLINE);

#define APPEND_MENU_HEADER(string) \
  g_string_append_printf (string, "\n\n%.*s MENU %.*s\n\n", \
      37, HASH_LINE, 37, HASH_LINE);

#define APPEND_PIPELINE_CONTROLS_SECTION(string) \
  g_string_append_printf (string, " %.*s Pipeline Controls %.*s\n", \
      30, EQUAL_LINE, 30, EQUAL_LINE);

#define APPEND_ELEMENT_SIGNALS_SECTION(string) \
  g_string_append_printf (string, " %.*s Plugin Signals %.*s\n", \
      31, EQUAL_LINE, 32, EQUAL_LINE);

#define APPEND_OTHER_OPTS_SECTION(string) \
  g_string_append_printf (string, " %.*s Other %.*s\n", \
      36, EQUAL_LINE, 36, EQUAL_LINE);

#define MAX_SIZE                       200
#define NULL_STATE_OPTION              "0"
#define READY_STATE_OPTION             "1"
#define PAUSED_STATE_OPTION            "2"
#define PLAYING_STATE_OPTION           "3"
#define CHECK_METADATA_OPTION          "4"

#define CAPTURE_MODE_OPTION            "c"
#define QUIT_OPTION                    "q"
#define MENU_BACK_OPTION               "b"

#define GST_CAMERA_PIPELINE "qtiqmmfsrc name=camera " \
    "camera.video_0 ! video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1 ! " \
    "queue ! appsink name=sink emit-signals=true async=false enable-last-sample=false"

#define GST_CAMERA_PIPELINE_DISPLAY "qtiqmmfsrc name=camera " \
    "camera.video_0 ! video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1 ! " \
    "queue ! appsink name=sink emit-signals=true async=false enable-last-sample=false " \
    "camera.video_1 ! video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1 ! " \
    "queue ! waylandsink fullscreen=true"

#define TERMINATE_MESSAGE      "APP_TERMINATE_MSG"
#define PIPELINE_STATE_MESSAGE "APP_PIPELINE_STATE_MSG"
#define PIPELINE_EOS_MESSAGE   "APP_PIPELINE_EOS_MSG"
#define STDIN_MESSAGE          "APP_STDIN_MSG"

#define GST_APP_CONTEXT_CAST(obj)           ((GstAppContext*)(obj))

typedef enum {
  VIDEO_METADATA_OPTION = 1,
  IMAGE_METADATA_OPTION,
  STATIC_METADATA_OPTION,
  SESSION_METADATA_OPTION
} GstMainMenuOption;

typedef enum {
  LIST_ALL_TAGS = 1,
  DUMP_ALL_TAGS,
  DUMP_CUSTOM_TAGS,
  GET_TAG,
  SET_TAG
} GstMetadataMenuOption;

typedef enum {
  COLLECT_TAGS = 1,
  APPLY_TAGS
} GstSessMetadataMenuOption;

typedef struct _GstAppContext GstAppContext;

struct _GstAppContext
{
  // Main application event loop.
  GMainLoop   *mloop;

  // GStreamer pipeline instance.
  GstElement  *pipeline;

  // Asynchronous queue thread communication.
  GAsyncQueue *messages;
};

// Command line option variables
static gboolean eos_on_shutdown = TRUE;
static gboolean display = FALSE;

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = NULL;
  g_return_val_if_fail ((ctx = g_new0 (GstAppContext, 1)) != NULL, NULL);

  ctx->mloop = NULL;
  ctx->pipeline = NULL;
  ctx->messages = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);

  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->pipeline != NULL)
    gst_object_unref (ctx->pipeline);

  g_async_queue_unref (ctx->messages);

  g_free (ctx);
  return;
}

static inline gboolean
g_type_is_pointer (GType type)
{
  if (type == G_TYPE_POINTER)
    return FALSE;

  if (G_TYPE_IS_OBJECT (type) || G_TYPE_IS_BOXED (type) ||
      G_TYPE_FUNDAMENTAL (type) == G_TYPE_POINTER)
    return TRUE;

  return FALSE;
}

static void
gst_sample_release (GstSample * sample)
{
  gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
  gst_sample_set_buffer (sample, NULL);
#endif
}

static GstElement*
get_element_from_pipeline (GstElement * pipeline, const gchar * factory_name)
{
  GstElement *element = NULL;
  GstElementFactory *elem_factory = gst_element_factory_find (factory_name);
  GstIterator *it = NULL;
  GValue value = G_VALUE_INIT;

  // Iterate the pipeline and check factory of each element.
  for (it = gst_bin_iterate_elements (GST_BIN (pipeline));
      gst_iterator_next (it, &value) == GST_ITERATOR_OK;
      g_value_reset (&value)) {
    element = GST_ELEMENT (g_value_get_object (&value));

    if (gst_element_get_factory (element) == elem_factory)
      goto free;
  }
  g_value_reset (&value);
  element = NULL;

free:
  gst_iterator_free (it);
  gst_object_unref (elem_factory);

  return element;
}

// Expand handle_interrupt_signal following gst-pipeline-app.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstState state = GST_STATE_VOID_PENDING;
  static gboolean waiting_eos = FALSE;

  // Signal menu thread to quit.
  g_async_queue_push (appctx->messages,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  // Get the current state of the pipeline.
  gst_element_get_state (appctx->pipeline, &state, NULL, 0);

  if (eos_on_shutdown && !waiting_eos && (state == GST_STATE_PLAYING)) {
    g_print ("\nEOS enabled -- Sending EOS on the pipeline\n");

    gst_element_post_message (GST_ELEMENT (appctx->pipeline),
        gst_message_new_custom (GST_MESSAGE_EOS, GST_OBJECT (appctx->pipeline),
            gst_structure_new_empty ("GST_PIPELINE_INTERRUPT")));

    g_print ("\nWaiting for EOS ...\n");
    waiting_eos = TRUE;
  } else if (eos_on_shutdown && waiting_eos) {
    g_print ("\nInterrupt while waiting for EOS - quit main loop...\n");

    gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
    g_main_loop_quit (appctx->mloop);

    waiting_eos = FALSE;
  } else {
    g_print ("\n\nReceived an interrupt signal, stopping pipeline ...\n");
    gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
    g_main_loop_quit (appctx->mloop);
  }

  return TRUE;
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  static GstState target_state = GST_STATE_VOID_PENDING;
  static gboolean in_progress = FALSE, buffering = FALSE;

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

      g_print ("\nSetting pipeline to NULL ...\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (TERMINATE_MESSAGE));
      g_main_loop_quit (appctx->mloop);
      break;
    }
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_warning (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      break;
    }
    case GST_MESSAGE_EOS:
    {
      g_print ("\nReceived End-of-Stream from '%s' ...\n",
          GST_MESSAGE_SRC_NAME (message));

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (PIPELINE_EOS_MESSAGE));

      break;
    }
    case GST_MESSAGE_REQUEST_STATE:
    {
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      GstState state;

      gst_message_parse_request_state (message, &state);
      g_print ("\nSetting pipeline state to %s as requested by %s...\n",
          gst_element_state_get_name (state), name);

      gst_element_set_state (appctx->pipeline, state);
      target_state = state;

      g_free (name);

      break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState oldstate, newstate, pending;

      // Handle state changes only for the pipeline.
      if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (appctx->pipeline))
        break;

      gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
      g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
          gst_element_state_get_name (oldstate),
          gst_element_state_get_name (newstate),
          gst_element_state_get_name (pending));

      g_async_queue_push (appctx->messages, gst_structure_new (
          PIPELINE_STATE_MESSAGE, "new", G_TYPE_UINT, newstate,
          "pending", G_TYPE_UINT, pending, NULL));

      break;
    }
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
          g_print ("\nFinished buffering, setting state to PLAYING.\n");
          gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);
        }
      } else {
        // Busy buffering...
        gst_element_get_state (appctx->pipeline, NULL, &target_state, 0);

        if (!buffering && target_state == GST_STATE_PLAYING) {
          g_print ("\nBuffering, setting pipeline to PAUSED state.\n");
          gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);
          target_state = GST_STATE_PAUSED;
        }

        buffering = TRUE;
      }

      break;
    }
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

      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition condition,
    gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gchar *input = NULL;
  GIOStatus status = G_IO_STATUS_NORMAL;

  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((G_IO_STATUS_ERROR == status) && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);

      return FALSE;
    } else if ((G_IO_STATUS_ERROR == status) && (NULL == error)) {
      g_printerr ("ERROR: Unknown error!\n");

      return FALSE;
    } else if ((G_IO_STATUS_AGAIN != status) && (NULL == input)) {
      g_printerr ("ERROR: Input is NULL!\n");

      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  if (strlen (input) > 1)
    input = g_strchomp (input);

  // Push stdin string into the inputs queue.
  g_async_queue_push (appctx->messages, gst_structure_new (STDIN_MESSAGE,
      "input", G_TYPE_STRING, input, NULL));
  g_free (input);

  return TRUE;
}

static gboolean
wait_stdin_message (GAsyncQueue * queue, gchar ** input)
{
  GstStructure *message = NULL;

  // Clear input from previous use.
  g_clear_pointer (input, g_free);

  // Block the thread until there's no input from the user
  // or eos/error msg occurs.
  while ((message = (GstStructure *)g_async_queue_pop (queue)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      // Returning FALSE will cause menu thread to terminate.
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE))
      *input = g_strdup (gst_structure_get_string (message, "input"));

    if (*input != NULL)
      break;

    // Clear message to terminate the loop after having popped the data.
    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static GstFlowReturn
new_sample (GstElement * element, gpointer userdata)
{
  FILE* ts_file = (FILE*) userdata;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  gchar *output = NULL;
  GstMapInfo info;
  guint64 timestamp = 0;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (element, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  if (ts_file == NULL) {
    gst_sample_release (sample);
    return GST_FLOW_OK;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    g_printerr ("ERROR: Failed to map the pulled buffer!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  // Extract the original camera timestamp and dump to a file.
  timestamp = GST_BUFFER_OFFSET_END (buffer);

  output = g_strdup_printf ("Camera timestamp: %" G_GUINT64_FORMAT "\n",
      timestamp);
  fputs (output, ts_file);
  g_free (output);

  gst_buffer_unmap (buffer, &info);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

static gboolean
wait_pipeline_eos_message (GAsyncQueue * messages)
{
  GstStructure *message = NULL;

  // Wait for either a PIPELINE_EOS or TERMINATE message.
  while ((message = (GstStructure*) g_async_queue_pop (messages)) != NULL) {
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

static gboolean
wait_pipeline_state_message (GAsyncQueue * messages, GstState state)
{
  GstStructure *message = NULL;

  // Pipeline does not notify us when changing to NULL state, skip wait.
  if (state == GST_STATE_NULL)
    return TRUE;

  // Wait for either a PIPELINE_STATE or TERMINATE message.
  while ((message = (GstStructure*) g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_STATE_MESSAGE)) {
      GstState newstate = GST_STATE_VOID_PENDING;
      gst_structure_get_uint (message, "new", (guint*) &newstate);

      if (newstate == state)
        break;
    }
    gst_structure_free (message);
  }
  gst_structure_free (message);
  return TRUE;
}

static gboolean
update_pipeline_state (GstElement * pipeline, GAsyncQueue * messages,
    GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;

  // First check current and pending states of the pipeline.
  ret = gst_element_get_state (pipeline, &current, &pending, 0);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("ERROR: Failed to retrieve pipeline state!\n");
    return TRUE;
  }

  if (state == current) {
    g_print ("Already in %s state\n", gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("Pending %s state\n", gst_element_state_get_name (state));
    return TRUE;
  }

  // Check whether to send an EOS event on the pipeline.
  if (eos_on_shutdown &&
      (current == GST_STATE_PLAYING) && (state == GST_STATE_NULL)) {
    g_print ("EOS enabled -- Sending EOS on the pipeline\n");

    if (!gst_element_send_event (pipeline, gst_event_new_eos ())) {
      g_printerr ("ERROR: Failed to send EOS event!");
      return TRUE;
    }

    if (!wait_pipeline_eos_message (messages))
      return FALSE;
  }

  g_print ("Setting pipeline to %s\n", gst_element_state_get_name (state));
  ret = gst_element_set_state (pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to %s state!\n",
          gst_element_state_get_name (state));

      return TRUE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("ERROR: Pipeline failed to PREROLL!\n");
        return TRUE;
      }

      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  if (!wait_pipeline_state_message (messages, state))
    return FALSE;

  return TRUE;
}

static gboolean
validate_input_tag (gchar * input, gchar ** section, gchar ** tag)
{
  // Extract section name and tag name from user's input.
  gchar **split_input;

  g_strstrip (input);
  split_input = g_strsplit (input, " ", 2);

  if (g_strv_length (split_input) < 2) {
    g_print ("Tag and section name not in correct format.\n");
    g_strfreev (split_input);

    return FALSE;
  } else {
    *section = split_input[0];
    *tag = split_input[1];

    g_strstrip (*section);
    g_strstrip (*tag);
    return TRUE;
  }
}

static gint
find_tag_by_name (const gchar * section_name, const gchar * tag_name,
    ::camera::CameraMetadata * meta, guint32 * tag_id)
{
  gchar *tag = NULL;
  gint tag_type = -1;
  const std::shared_ptr<::camera::VendorTagDescriptor> vtags =
     ::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor();

  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return -1;
  }

  // Retrieve tag ID of the tag.
  tag = g_strconcat (section_name, ".", tag_name, NULL);
  if (meta->getTagFromName (tag, vtags.get(), tag_id) != 0) {
    g_print ("Unable to locate tag %s\n", tag);
    g_free (tag);
    return -1;
  }
  g_free (tag);

  // Determine data type of the tag.
  if (*tag_id < VENDOR_SECTION_START)
    tag_type = get_camera_metadata_tag_type (*tag_id);
  else
    tag_type = vtags->getTagType (*tag_id);

  return tag_type;
}

static gint
get_tag_typechar (const gchar * section_name, const gchar * tag_name,
    ::camera::CameraMetadata * meta, gchar ** type, guint32 * tag_id)
{
  gchar *tag_value = NULL;
  status_t status = 0;
  gint tag_type = -1;

  if ((tag_type =
      find_tag_by_name (section_name, tag_name, meta, tag_id)) == -1)
    return -1;

  switch (tag_type) {
    case TYPE_BYTE:
      *type = g_strdup ("Unsigned Int8");
      break;
    case TYPE_INT32:
      *type = g_strdup ("Int32");
      break;
    case TYPE_FLOAT:
      *type = g_strdup ("Float");
      break;
    case TYPE_INT64:
      *type = g_strdup ("Int64");
      break;
    case TYPE_DOUBLE:
      *type = g_strdup ("Double");
      break;
    case TYPE_RATIONAL:
      *type = g_strdup ("Fraction");
      break;
    default:
      *type = NULL;
      g_print ("Invalid type\n");
      break;
  }

  if (!meta->exists (*tag_id)) {
    g_print ("Warning: Tag doesn't exist in the static-metadata.\n");
  }
  if ((-1 == tag_type) || (-1 == *tag_id)) {
    g_print ("Cannot find tag_type and tag_id.\n");
    *type = g_strdup ("null");
  }

  return tag_type;
}

static gchar*
get_tag (const gchar * section_name, const gchar * tag_name,
    ::camera::CameraMetadata * meta, gchar ** type)
{
  gchar *tag_value = NULL;
  status_t status = 0;
  guint32 tag_id = 0;
  gint tag_type = -1;

  if ((tag_type =
      find_tag_by_name (section_name, tag_name, meta, &tag_id)) == -1)
    return NULL;

  switch (tag_type) {
    case TYPE_BYTE:
      *type = g_strdup ("Unsigned Int8");
      if (meta->exists (tag_id)) {
        guint8 value = meta->find (tag_id).data.u8[0];
        tag_value = g_strdup_printf ("%" G_GUINT16_FORMAT, value);
      }
      break;
    case TYPE_INT32:
      *type = g_strdup ("Int32");
      if (meta->exists (tag_id)) {
        gint32 value = meta->find (tag_id).data.i32[0];
        tag_value = g_strdup_printf ("%" G_GINT32_FORMAT, value);
      }
      break;
    case TYPE_FLOAT:
      *type = g_strdup ("Float");
      if (meta->exists (tag_id)) {
        gfloat value = meta->find (tag_id).data.f[0];
        tag_value = g_strdup_printf ("%f", value);
      }
      break;
    case TYPE_INT64:
      *type = g_strdup ("Int64");
      if (meta->exists (tag_id)) {
        gint64 value = meta->find (tag_id).data.i64[0];
        tag_value = g_strdup_printf ("%" G_GINT64_FORMAT, value);
      }
      break;
    case TYPE_DOUBLE:
      *type = g_strdup ("Double");
      if (meta->exists (tag_id)) {
        gdouble value = meta->find (tag_id).data.d[0];
        tag_value = g_strdup_printf ("%lf", value);
      }
      break;
    case TYPE_RATIONAL:
      *type = g_strdup ("Fraction");
      if (meta->exists (tag_id)) {
        gint32 value_num = meta->find (tag_id).data.r[0].numerator;
        gint32 value_den = meta->find (tag_id).data.r[0].denominator;
        tag_value = g_strdup_printf ("%" G_GINT32_FORMAT "/%" G_GINT32_FORMAT,
            value_num, value_den);
      }
      break;
    default:
      *type = NULL;
      g_print ("Invalid type\n");
      break;
  }

  if (!meta->exists (tag_id)) {
    g_print ("Tag doesn't exist in the meta.\n");
    tag_value = g_strdup ("null");
    *type = g_strdup ("null");
  }

  return tag_value;
}

static gint
set_tag (GstElement * pipeline, const gchar * section_name,
    const gchar * tag_name, gchar * new_value)
{
  GstElement *camsrc = get_element_from_pipeline (pipeline, "qtiqmmfsrc");
  ::camera::CameraMetadata *meta = nullptr;
  status_t status = -1;
  guint32 tag_id = 0;
  gint tag_type = -1;

  g_object_get (G_OBJECT (camsrc), "video-metadata", &meta, NULL);

  if ((tag_type =
      find_tag_by_name (section_name, tag_name, meta, &tag_id)) == -1)
    goto free;

  switch (tag_type) {
    case TYPE_BYTE:
    {
      gchar *endptr;
      const guint8 tag_value = g_ascii_strtoull ((const gchar *) new_value,
          &endptr, 0);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_INT32:
    {
      gchar *endptr;
      const gint32 tag_value = g_ascii_strtoll ((const gchar *) new_value,
          &endptr, 0);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_FLOAT:
    {
      gchar *endptr;
      const gfloat tag_value = g_ascii_strtod ((const gchar *) new_value,
          &endptr);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_INT64:
    {
      gchar *endptr;
      const gint64 tag_value = g_ascii_strtoll ((const gchar *) new_value,
          &endptr, 0);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_DOUBLE:
    {
      gchar *endptr;
      const gdouble tag_value = g_ascii_strtod ((const gchar *) new_value,
          &endptr);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_RATIONAL:
    {
      gchar *endptr1, *endptr2;
      gint32 tag_value_num, tag_value_den;
      gchar **split_input = g_strsplit (new_value, "/", -1);

      if (g_strv_length (split_input) != 2) {
        g_print ("Invalid input. Use the format: 'num/denom' (without quotes)\n");
        g_strfreev (split_input);
        break;
      }

      g_strstrip (split_input[0]);
      g_strstrip (split_input[1]);

      tag_value_num = g_ascii_strtoll ((const gchar *) split_input[0],
          &endptr1, 0);
      tag_value_den = g_ascii_strtoll ((const gchar *) split_input[1],
          &endptr2, 0);

      if (*endptr1 == '\0' && split_input[0] != endptr1
          && *endptr2 == '\0' && split_input[1] != endptr2) {
        camera_metadata_rational_t tag_value;

        tag_value.numerator = tag_value_num;
        tag_value.denominator = tag_value_den;

        status = meta->update (tag_id,
            (const camera_metadata_rational_t*) &tag_value, 1);
      } else {
        g_print ("Invalid input!\n");
        g_strfreev (split_input);

        break;
      }

      g_strfreev (split_input);
      break;
    }
    default:
      g_print ("Invalid type!\n");
      break;
  }

  if (status == 0) {
    g_object_set (G_OBJECT (camsrc), "video-metadata", meta, NULL);
    g_print ("The tag is set successfully.\n");
  } else {
    g_printerr ("ERROR: Couldn't set the value\n");
  }

free:
  if (meta != NULL) {
    delete meta;
    meta = NULL;
  }
  gst_object_unref (camsrc);

  return 0;
}

static gint
collect_tags (GstElement * pipeline, const gchar * section_name,
    const gchar * tag_name, gchar * new_value, ::camera::CameraMetadata * meta,
    gint tag_type, guint32 tag_id)
{
  status_t status = -1;

  switch (tag_type) {
    case TYPE_BYTE:
    {
      gchar *endptr;
      const guint8 tag_value = g_ascii_strtoull ((const gchar *) new_value,
          &endptr, 0);

      g_print ("tag_value = %u\n", tag_value);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_INT32:
    {
      gchar *endptr;
      const gint32 tag_value = g_ascii_strtoll ((const gchar *) new_value,
          &endptr, 0);

      g_print ("tag_value = %d\n", tag_value);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_FLOAT:
    {
      gchar *endptr;
      const gfloat tag_value = g_ascii_strtod ((const gchar *) new_value,
          &endptr);

      g_print ("tag_value = %f\n", tag_value);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_INT64:
    {
      gchar *endptr;
      const gint64 tag_value = g_ascii_strtoll ((const gchar *) new_value,
          &endptr, 0);

      g_print ("tag_value = %ld\n", tag_value);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_DOUBLE:
    {
      gchar *endptr;
      const gdouble tag_value = g_ascii_strtod ((const gchar *) new_value,
          &endptr);

      g_print ("tag_value = %lf\n", tag_value);

      if (*endptr == '\0' && new_value != endptr)
        status = meta->update (tag_id, &tag_value, 1);
      else
        g_print ("Invalid input!\n");

      break;
    }
    case TYPE_RATIONAL:
    {
      gchar *endptr1, *endptr2;
      gint32 tag_value_num, tag_value_den;
      gchar **split_input = g_strsplit (new_value, "/", -1);

      if (g_strv_length (split_input) != 2) {
        g_print ("Invalid input. Use the format: 'num/denom' (without quotes)\n");
        g_strfreev (split_input);
        break;
      }

      g_strstrip (split_input[0]);
      g_strstrip (split_input[1]);

      tag_value_num = g_ascii_strtoll ((const gchar *) split_input[0],
          &endptr1, 0);
      tag_value_den = g_ascii_strtoll ((const gchar *) split_input[1],
          &endptr2, 0);

      g_print ("tag_value_num = %d\n", tag_value_num);
      g_print ("tag_value_den = %d\n", tag_value_den);

      if (*endptr1 == '\0' && split_input[0] != endptr1
          && *endptr2 == '\0' && split_input[1] != endptr2) {
        camera_metadata_rational_t tag_value;

        tag_value.numerator = tag_value_num;
        tag_value.denominator = tag_value_den;

        status = meta->update (tag_id,
            (const camera_metadata_rational_t*) &tag_value, 1);
      } else {
        g_print ("Invalid input!\n");
        g_strfreev (split_input);

        break;
      }

      g_strfreev (split_input);
      break;
    }
    default:
      g_print ("Invalid type!\n");
      break;
  }

  if (status == 0) {
    g_print ("The tag is collected successfully.\n");
  } else {
    g_printerr ("ERROR: Couldn't collect the value\n");
  }

free:

  return 0;
}

static gint
apply_tags (GstElement * pipeline, ::camera::CameraMetadata * meta_collect)
{
  GstElement *camsrc = get_element_from_pipeline (pipeline, "qtiqmmfsrc");
  g_object_set (G_OBJECT (camsrc), "session-metadata", meta_collect, NULL);

  g_print ("Setting session-metadata is done.\n");

  gst_object_unref (camsrc);
  if (meta_collect != NULL) {
    // CameraMetadata::clear() will free_camera_metadata(mBuffer)
    // and mBuffer = NULL.
    meta_collect->clear ();
  }

  return 0;
}

static void
print_vendor_tags (::camera::CameraMetadata * meta, FILE * file)
{
  gchar *header = NULL;
  const std::shared_ptr<::camera::VendorTagDescriptor> vtags =
      ::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor();

  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return;
  }

  header = g_new0 (gchar, MAX_SIZE);

  if (file != NULL) {
    g_snprintf (header, MAX_SIZE, "\n%.58s Vendor tags %.58s\n\n", DASH_LINE,
        DASH_LINE);
    fputs (header, file);

    g_snprintf (header, MAX_SIZE, "%.22s SECTION %.22s %.4s %.18s "
        "TAG %.18s %.4s %.8s VALUE %.8s\n", DASH_LINE, DASH_LINE, SPACE,
        DASH_LINE, DASH_LINE, SPACE, DASH_LINE, DASH_LINE);
    fputs (header, file);
  } else {
    g_print ("\n%.53s Vendor tags %.54s\n\n", DASH_LINE, DASH_LINE);
    g_print ("%.3s TAG ID %.3s %.4s %.22s SECTION %.22s %.4s %.18s TAG %.18s\n",
        DASH_LINE, DASH_LINE, SPACE, DASH_LINE, DASH_LINE, SPACE, DASH_LINE,
        DASH_LINE);
  }

  {
    gchar *type = NULL, *value = NULL;
    gchar line[MAX_SIZE];
    gint padding = 0;

    gint vtagCount = vtags->getTagCount ();
    guint *vtagsId = g_new0 (guint, vtagCount);
    vtags->getTagArray (vtagsId);

    for (size_t i = 0; i < vtagCount; i++) {
      if (!meta->exists (vtagsId[i]))
        continue;

      const gchar *section_name = vtags->getSectionName (vtagsId[i]);
      const gchar *tag_name = vtags->getTagName (vtagsId[i]);
      if (section_name == NULL || tag_name == NULL)
        continue;

      if (file == NULL) {
        // List all tags on console.
        g_print ("%-14u %.4s %-53s %.4s %-41s\n", vtagsId[i], SPACE,
            section_name, SPACE, tag_name);

        continue;
      }

      // Dump all tags in a file.
      value = get_tag (section_name, tag_name, meta, &type);
      padding = 10 - ceil (strlen (value)/2);

      g_snprintf (line, MAX_SIZE, "%-53s %.4s %-41s %.4s %.*s%s\n",
          section_name, SPACE, tag_name, SPACE, padding, SPACE, value);
      fputs (line, file);

      g_free (type);
      g_free (value);
    }

    g_free (vtagsId);
  }

  if (file != NULL) {
    g_snprintf (header, MAX_SIZE, "\n%s%.59s\n", DASH_LINE, DASH_LINE);
    fputs (header, file);
  } else {
    g_print ("\n%s%.50s\n\n", DASH_LINE, DASH_LINE);
  }

  g_free (header);
}

static void
print_android_tags (::camera::CameraMetadata * meta, FILE * file)
{
  gchar *header = g_new0 (gchar, MAX_SIZE);

  if (file != NULL) {
    g_snprintf (header, MAX_SIZE, "\n%.41s Android tags %.40s\n\n",
        DASH_LINE, DASH_LINE);
    fputs (header, file);

    g_snprintf (header, MAX_SIZE, "%.8s SECTION %.8s %.4s %.15s "
        "TAG %.15s %.4s %.8s VALUE %.8s\n", DASH_LINE, DASH_LINE, SPACE,
        DASH_LINE, DASH_LINE, SPACE, DASH_LINE, DASH_LINE);
    fputs (header, file);
  } else {
    g_print ("\n%.36s Android tags %.36s\n\n", DASH_LINE, DASH_LINE);
    g_print ("%.3s TAG ID %.3s %.4s %.8s SECTION %.8s %.4s %.15s TAG %.15s\n",
        DASH_LINE, DASH_LINE, SPACE, DASH_LINE, DASH_LINE, SPACE, DASH_LINE,
        DASH_LINE);
  }

  {
    gchar* value = NULL, *type = NULL;
    gchar line[MAX_SIZE];
    gint padding = 0;

    for (size_t section = 0; section < ANDROID_SECTION_COUNT; section++) {
      guint start = camera_metadata_section_bounds[section][0];
      guint end = camera_metadata_section_bounds[section][1];

      const gchar *section_name = get_camera_metadata_section_name (start);

      for (size_t i = start; i < end; i++) {
        if (!meta->exists (i))
          continue;

        const gchar *tag_name = get_camera_metadata_tag_name (i);
        if (section_name == NULL || tag_name == NULL)
          continue;

        if (file == NULL) {
          // List all tags on console.
          g_print ("%-14ld %.4s %-25s %.4s %-35s\n", i, SPACE, section_name,
              SPACE, tag_name);

          continue;
        }

        // Dump all tags in a file.
        value = get_tag (section_name, tag_name, meta, &type);
        padding = 10 - ceil (strlen (value)/2);

        g_snprintf (line, MAX_SIZE, "%-25s %.4s %-35s %.4s %.*s%s\n",
            section_name, SPACE, tag_name, SPACE, padding, SPACE, value);
        fputs (line, file);

        g_free (type);
        g_free (value);
      }
    }
  }

  if (file != NULL) {
    g_snprintf (header, MAX_SIZE, "\n%s%.25s\n\n\n", DASH_LINE, DASH_LINE);
    fputs (header, file);
  } else {
    g_print ("\n%s%.16s\n\n", DASH_LINE, DASH_LINE);
  }

  g_free (header);
}

static void
result_metadata (GstElement * element, gpointer metadata, gpointer userdata)
{
  ::camera::CameraMetadata *meta =
      static_cast<::camera::CameraMetadata*> (metadata);
  FILE* file = (FILE*) userdata;

  print_android_tags (meta, file);
  print_vendor_tags (meta, file);
  fputs ("\n\n\n\n\n", file);
}

static void
urgent_metadata (GstElement * element, gpointer metadata, gpointer userdata)
{
  ::camera::CameraMetadata *meta =
      static_cast<::camera::CameraMetadata*> (metadata);
  FILE* file = (FILE*) userdata;

  print_android_tags (meta, file);
  print_vendor_tags (meta, file);
  fputs ("\n\n\n\n\n", file);
}

static void
list_all_tags (::camera::CameraMetadata * meta)
{
  g_print ("\nNumber of entries : %ld\n", meta->entryCount());

  print_android_tags (meta, NULL);
  print_vendor_tags (meta, NULL);
}

static void
dump_all_tags (::camera::CameraMetadata * meta, gchar * prop)
{
  FILE *file = NULL;
  gchar *header = NULL;
  static gint sno = 1;

  gchar *filename = g_strdup_printf ("/data/misc/qmmf/all_tags_%d.txt", sno++);

  if ((file = fopen (filename, "w")) == NULL) {
    g_printerr ("ERROR: Failed to open file for writing\n");
    g_free (filename);
    return;
  }

  header = g_strdup_printf ("%.57s %s %.57s\n\n", DASH_LINE, prop,
      DASH_LINE);
  fputs (header, file);
  g_free (header);

  print_android_tags (meta, file);
  print_vendor_tags (meta, file);
  fclose (file);

  g_print ("\nValues of all tags saved to %s successfully.\n", filename);
  g_free (filename);
}

static void
dump_custom_tags (::camera::CameraMetadata * meta,
    gchar * file_path, gchar * prop)
{
  FILE *configfp = NULL, *outputfp = NULL;
  gchar *section = NULL, *tag = NULL, *type = NULL, *value = NULL;
  gchar *configline = NULL, *outputline = NULL, *header = NULL, *filename = NULL;
  static gint sno = 1;
  gint padding = 0;

  if ((configfp = fopen (file_path, "r")) == NULL) {
    g_printerr ("ERROR: Failed to open config file.\n");
    return;
  }

  filename = g_strdup_printf ("/data/misc/qmmf/custom_tags_%d.txt", sno++);
  if ((outputfp = fopen (filename, "w")) == NULL) {
    g_printerr ("ERROR: Failed to open file for writing.\n");
    g_free (filename);
    return;
  }

  header = g_new0 (gchar, MAX_SIZE);
  configline = g_new0 (gchar, MAX_SIZE);
  outputline = g_new0 (gchar, MAX_SIZE);

  g_snprintf (header, MAX_SIZE, "%.57s %s %.57s\n\n", DASH_LINE, prop,
      DASH_LINE);
  fputs (header, outputfp);

  g_snprintf (header, MAX_SIZE, "LINE NO.%.4s %.22s SECTION %.22s %.4s" \
      "%.15s TAG %.15s %.4s %.5s VALUE %.5s\n", SPACE, DASH_LINE, DASH_LINE,
      SPACE, DASH_LINE, DASH_LINE, SPACE, DASH_LINE, DASH_LINE);
  fputs (header, outputfp);
  g_free (header);

  int i = 1;
  while (fgets (configline, MAX_SIZE, configfp) != NULL) {
    g_print ("Line %d : \n   ", i);
    g_snprintf (outputline, MAX_SIZE, "%-8d%.4s %-53s%.4s %-35s %.4s %.7s%s\n",
        i, SPACE, "INVALID", SPACE, "INVALID", SPACE, SPACE, "N/A");

    if (!validate_input_tag (configline, &section, &tag))
      goto put;

    if ((value = get_tag (section, tag, meta, &type)) == NULL)
      goto free_and_put;

    padding = 8 - ceil (strlen (value)/2);
    g_snprintf (outputline, MAX_SIZE, "%-8d%.4s %-53s%.4s %-35s %.4s %.*s%s\n",
        i, SPACE, section, SPACE, tag, SPACE, padding, SPACE, value);

    if (!g_str_equal (value, "null"))
      g_print ("Printed successfully.\n");

    g_free (value);
    g_free (type);

free_and_put:
    g_free (section);
    g_free (tag);
put:
    fputs (outputline, outputfp);
    i++;
  }

  fclose (outputfp);
  fclose (configfp);

  g_print ("\nValues of tags in the config file saved to %s successfully.\n",
      filename);

  g_free (outputline);
  g_free (configline);
  g_free (filename);
}

static void
print_metadata_menu (gchar * prop)
{
  gint spaces = (strlen (prop) > 14 ? 67 : 66);

  g_print ("\n%.25s %s %.25s\n", DASH_LINE, prop, DASH_LINE);

  // session-metadata doesn't have get-tag related function.
  if (!g_str_equal (prop, "session-metadata")) {
    g_print ("   (%d) %-25s\n", LIST_ALL_TAGS, "List all available tags");
    g_print ("   (%d) %-25s\n", DUMP_ALL_TAGS,
        "Dump all tags values in a file");
    g_print ("   (%d) %-25s\n", DUMP_CUSTOM_TAGS,
        "Dump custom tags values in a file");
    g_print ("   (%d) %-25s\n", GET_TAG, "Get a tag");
  }

  if (g_str_equal (prop, "video-metadata"))
    g_print ("   (%d) %-25s\n", SET_TAG, "Set a tag");

  if (g_str_equal (prop, "session-metadata")) {
    g_print ("   (%d) %-25s\n", COLLECT_TAGS, "Collect tags");
    g_print ("   (%d) %-25s\n", APPLY_TAGS, "Apply tags");
  }

  g_print ("%.*s\n", spaces, DASH_LINE);
  g_print ("   (%s) %-25s\n", MENU_BACK_OPTION, "Back");
  g_print ("\nChoose an option: ");
}

static void
print_menu ()
{
  g_print ("\n%.25s MENU %.25s\n", DASH_LINE, DASH_LINE);
  g_print ("   (%d) %-25s\n", VIDEO_METADATA_OPTION, "video-metadata");
  g_print ("   (%d) %-25s\n", IMAGE_METADATA_OPTION, "image-metadata");
  g_print ("   (%d) %-25s\n", STATIC_METADATA_OPTION, "static-metadata");
  g_print ("   (%d) %-25s\n", SESSION_METADATA_OPTION, "session-metadata");

  g_print ("%.56s\n", DASH_LINE);
  g_print ("   (%s) %-25s\n", QUIT_OPTION, "Quit");
  g_print ("\nChoose an option: ");
}

static gboolean
handle_tag_menu (GstAppContext * appctx, gchar * prop,
    GstMetadataMenuOption option)
{
  gchar *str = NULL;
  gchar *section = NULL, *tag = NULL, *type = NULL, *value = NULL;
  gboolean active = TRUE;

  ::camera::CameraMetadata *meta = nullptr;
  GstElement *camsrc = NULL;

  while (TRUE) {
    g_print ("Enter section name and tag name separated by space " \
        "without quotes (e.g. section_name tag_name) : ");

    if (!wait_stdin_message (appctx->messages, &str))
      return FALSE;

    // If Enter is pressed, return to metadata menu.
    if (g_str_equal (str, "\n"))
      goto exit;

    if (!validate_input_tag (str, &section, &tag))
      continue;

    camsrc = get_element_from_pipeline (appctx->pipeline,
        "qtiqmmfsrc");
    g_object_get (G_OBJECT (camsrc), prop, &meta, NULL);
    gst_object_unref (camsrc);

    if (meta == NULL) {
      g_printerr ("ERROR: Meta not found\n");
      goto exit;
    }

    value = get_tag (section, tag, meta, &type);
    g_print ("Current value = %s\n", value);

    // Delete metadata after being used.
    if (meta != NULL) {
      delete meta;
      meta = NULL;
    }

    if (value == NULL) {
      g_free (section);
      g_free (tag);
      continue;
    }

    if (option == SET_TAG) {
      g_print ("Type: %s\n", type);
      g_print ("Enter the new value: ");

      if (!wait_stdin_message (appctx->messages, &str))
        return FALSE;

      if (!g_str_equal (str, "\n"))
        set_tag (appctx->pipeline, section, tag, str);
    }
    g_free (section);
    g_free (tag);
    g_free (value);
    g_free (type);
  }

exit:
  g_free (str);
  return active;
}

static gboolean
collect_tags_menu_sessionmetadata (GstAppContext * appctx, gchar * prop,
    ::camera::CameraMetadata * meta_collect)
{
  gchar *str = NULL;
  gchar *section = NULL, *tag = NULL, *type = NULL, *value = NULL;
  gint tag_type = -1;
  guint32 tag_id = 0;
  gboolean active = TRUE;

  GstElement *camsrc = get_element_from_pipeline (appctx->pipeline,
      "qtiqmmfsrc");
  ::camera::CameraMetadata *meta_static = nullptr;
  g_object_get (G_OBJECT (camsrc), "static-metadata", &meta_static, NULL);

  while (TRUE) {
    g_print ("Enter section name and tag name separated by space " \
        "without quotes (e.g. section_name tag_name) : ");

    if (!wait_stdin_message (appctx->messages, &str))
      return FALSE;

    // If Enter is pressed, return to metadata menu.
    if (g_str_equal (str, "\n"))
      goto exit;

    if (!validate_input_tag (str, &section, &tag))
      continue;

    tag_type = get_tag_typechar (section, tag, meta_static, &type, &tag_id);
    if (-1 == tag_type) {
      g_printerr ("No Target Type in static-metadata.\n");
      goto exit;
    }
    g_print ("Target Type in static-metadata: %s\n", type);
    g_print ("Enter the new value: ");

    if (!wait_stdin_message (appctx->messages, &str))
      return FALSE;

    if (!g_str_equal (str, "\n"))
      collect_tags (appctx->pipeline, section, tag, str, 
          meta_collect, tag_type, tag_id);

    g_free (section);
    g_free (tag);
    g_free (value);
  }

exit:
  if (meta_static != NULL) {
    delete meta_static;
    meta_static = NULL;
  }
  gst_object_unref (camsrc);

  g_free (str);
  return active;
}

static gboolean
handle_metadata_menu (GstAppContext * appctx,
    gchar ** prop, ::camera::CameraMetadata *meta_collect)
{
  ::camera::CameraMetadata *meta = nullptr;
  GstElement *camsrc = NULL;
  gchar *str = NULL, *endptr = NULL;
  gboolean active = TRUE;
  gint input = 0;

  print_metadata_menu (*prop);

  if (!wait_stdin_message (appctx->messages, &str))
    return FALSE;

  if (g_str_equal (str, MENU_BACK_OPTION)) {
    *prop = NULL;
    goto exit;
  }

  if (!g_str_equal (*prop, "session-metadata")) {
    camsrc = get_element_from_pipeline (appctx->pipeline, "qtiqmmfsrc");
    g_object_get (G_OBJECT (camsrc), *prop, &meta, NULL);
    gst_object_unref (camsrc);

    if (meta == NULL) {
      g_printerr ("ERROR: Meta not found\n");
      goto exit;
    }
  }

  input = g_ascii_strtoll ((const gchar *) str, &endptr, 0);

  if (!g_str_equal (*prop, "session-metadata")) {
    switch (input) {
      case LIST_ALL_TAGS:
        list_all_tags (meta);
        break;
      case DUMP_ALL_TAGS:
        dump_all_tags (meta, *prop);
        break;
      case DUMP_CUSTOM_TAGS:
        g_print ("Enter full path of config file (or press Enter to return): ");
        if (!wait_stdin_message (appctx->messages, &str)) {
          active = FALSE;
          goto exit;
        } else if (!g_str_equal (str, "\n")) {
          dump_custom_tags (meta, str, *prop);
        }
        break;
      case GET_TAG:
        active = handle_tag_menu (appctx, *prop, GET_TAG);
        break;
      case SET_TAG:
        if (g_str_equal (*prop, "video-metadata"))
          active = handle_tag_menu (appctx, *prop, SET_TAG);
        break;
      default:
        break;
    }
  } else {
    switch (input) {
      case COLLECT_TAGS:
        active = collect_tags_menu_sessionmetadata (appctx, *prop,
            meta_collect);
        break;
      case APPLY_TAGS:
        if (0 == apply_tags (appctx->pipeline, meta_collect))
          active = TRUE;
        break;
      default:
        break;
    }
  }

exit:
  if (meta != NULL) {
    delete meta;
    meta = NULL;
  }

  g_free (str);
  return active;
}

static gboolean
handle_meta_menu (GstAppContext * appctx, gchar ** prop)
{
  const static gchar *prop_names[] = {"video-metadata", "image-metadata",
      "static-metadata", "session-metadata"};
  gchar *str = NULL;

  print_menu ();

  if (!wait_stdin_message (appctx->messages, &str))
    return FALSE;

  if (g_str_equal (str, QUIT_OPTION)) {
    g_free (str);
    return FALSE;
  }

  {
    gchar *endptr;
    gint input = g_ascii_strtoll ((const gchar *) str, &endptr, 0);

    if (input >= VIDEO_METADATA_OPTION && input <= SESSION_METADATA_OPTION)
      *prop = const_cast <gchar *> (prop_names[input-1]);
  }

  g_free (str);
  return TRUE;
}

static void
print_pipeline_options (GstElement * pipeline)
{
  GString *options = g_string_new (NULL);

  APPEND_MENU_HEADER (options);

  APPEND_PIPELINE_CONTROLS_SECTION (options);
  g_string_append_printf (options, "   (%s) %-25s: %s\n", NULL_STATE_OPTION,
      "NULL", "Set the pipeline into NULL state");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", READY_STATE_OPTION,
      "READY", "Set the pipeline into READY state");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", PAUSED_STATE_OPTION,
      "PAUSED", "Set the pipeline into PAUSED state");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", PLAYING_STATE_OPTION,
      "PLAYING", "Set the pipeline into PLAYING state");

  APPEND_OTHER_OPTS_SECTION (options);
  g_string_append_printf (options, "   (%s) %-25s: %s\n", CHECK_METADATA_OPTION,
      "META", "Check or set metadata in READY/PAUSED/PLAYING state");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", CAPTURE_MODE_OPTION,
      "Capture Options", "Choose a capture option (pipeline should support)");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", QUIT_OPTION,
      "Quit", "Exit the application");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);
}

static gboolean
gst_pipeline_menu (GstAppContext *appctx, GstElement ** element, gchar ** prop)
{
  gchar *input = NULL;
  gboolean active = TRUE;
  static GstState target_state = GST_STATE_VOID_PENDING;

  GstElement * pipeline  = appctx->pipeline;
  GAsyncQueue * messages = appctx->messages;

  print_pipeline_options (pipeline);

  g_print ("\n\nChoose an option: ");

  // If FALSE is returned termination signal has been issued.
  if (!wait_stdin_message (messages, &input))
    return FALSE;

  if (g_str_equal (input, NULL_STATE_OPTION)) {
    if (!update_pipeline_state (pipeline, messages, GST_STATE_NULL)) {
      active = FALSE;
      goto exit;
    }

  } else if (g_str_equal (input, READY_STATE_OPTION)) {
    if (!update_pipeline_state (pipeline, messages, GST_STATE_READY)) {
      active = FALSE;
      goto exit;
    }

  } else if (g_str_equal (input, PAUSED_STATE_OPTION)) {
    if (!update_pipeline_state (pipeline, messages, GST_STATE_PAUSED)) {
      active = FALSE;
      goto exit;
    }

  } else if (g_str_equal (input, PLAYING_STATE_OPTION)) {
    if (!update_pipeline_state (pipeline, messages, GST_STATE_PLAYING)) {
      active = FALSE;
      goto exit;
    }

  } else if (g_str_equal (input, CHECK_METADATA_OPTION)) {
    gst_element_get_state (pipeline, &target_state, NULL, 0);

    if (target_state != GST_STATE_NULL) {
      g_print ("\nCheck metadata now: \n");
      if ((*prop) == NULL)
        active = handle_meta_menu (appctx, prop);
      else {
        g_printerr ("(*prop) == NULL in gst_pipeline_menu()\n");
        active = FALSE;
      }
      goto exit;
    } else {
      g_print ("\nGST State cannot be set or check in NULL state.\n");
      active = FALSE;
      goto exit;
    }

  } else if (g_str_equal (input, CAPTURE_MODE_OPTION)) {
    *element = get_element_from_pipeline (pipeline, "qtiqmmfsrc");
    if (NULL == *element) {
      g_printerr ("No qtiqmmfsrc found in pipeline.\n");
      active = FALSE;
      goto exit;
    }

  } else if (g_str_equal (input, QUIT_OPTION)) {
    g_print ("\nQuit pressed!!\n");

    update_pipeline_state (pipeline, messages, GST_STATE_NULL);
    active = FALSE;
    goto exit;
  }

exit:
  g_free (input);
  return active;
}

static void
get_object_signals (GObject * object, guint * index, GstStructure * signals,
    GString * options)
{
  GType type;

  for (type = G_OBJECT_TYPE (object); type; type = g_type_parent (type)) {
    guint i = 0, n = 0, *signal_ids = NULL, n_signals = 0;
    gchar *field = NULL;

    if (type == GST_TYPE_ELEMENT || type == GST_TYPE_OBJECT)
      break;

    // Ignore GstBin elements.
    if (type == GST_TYPE_BIN && G_OBJECT_TYPE (object) != GST_TYPE_BIN)
      continue;

    // Lists the signals that this element type has.
    signal_ids = g_signal_list_ids (type, &n_signals);

    // Go over each signal and query additional information.
    for (i = 0; i < n_signals; i++) {
      GSignalQuery query = {};

      g_signal_query (signal_ids[i], &query);

      // Remain capture-image and cancel-capture. Index starts from capture.
      if (query.signal_flags & G_SIGNAL_ACTION) {
        field = g_strdup_printf ("%u", (*index));
        gst_structure_set (signals, field, G_TYPE_UINT,
            query.signal_id, NULL);

        g_clear_pointer (&field, g_free);

        g_string_append_printf (options, "   (%2u) %-25s: %s (%s* object",
            (*index), query.signal_name, g_type_name (query.return_type),
            g_type_name (type));

        for (n = 0; n < query.n_params; n++) {
          GType ptype = query.param_types[n];
          gboolean asterisk = g_type_is_pointer (ptype);

          g_string_append_printf (options, ", %s%s arg%d",
              g_type_name (ptype), asterisk ? "*" : "", n);
        }

        g_string_append_printf (options, ")\n");

        // Increment the index for the next option.
        (*index)++;
      }
    }

    // Free the allocated resources for the next iteration.
    g_free (signal_ids);
    signal_ids = NULL;
  }

  return;
}

static void
print_element_options (GstElement * element, GstStructure * signals)
{
  GString *options = g_string_new (NULL);
  guint index = 0;

  APPEND_MENU_HEADER (options);

  // Remove "Plugin Properties" and "Pad" here.
  // Get the plugin element signals.
  APPEND_ELEMENT_SIGNALS_SECTION (options);
  get_object_signals (G_OBJECT (element), &index, signals, options);

  APPEND_OTHER_OPTS_SECTION (options);
  g_string_append_printf (options, "   (%2s) %-25s: %s\n", MENU_BACK_OPTION,
      "Back", "Return to the previous menu");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);
}

static gboolean
gst_signal_menu (GstElement * element, GAsyncQueue * messages,
    const guint signal_id)
{
  GValue *arguments = NULL, value = G_VALUE_INIT;
  GSignalQuery query;
  gchar *input = NULL, *status = NULL;
  guint num = 0;

  // Query the signal parameters.
  g_signal_query (signal_id, &query);

  // Allocate memory for the array if signal arguments.
  arguments = g_new0 (GValue, query.n_params + 1);

  // First signal argument is always the GsElement to which it belongs.
  g_value_init (&arguments[0], GST_TYPE_ELEMENT);
  g_value_set_object (&arguments[0], element);

  for (num = 0; num < query.n_params; num++) {
    GString *info = NULL;
    GType gtype = query.param_types[num];
    gboolean asterisk = g_type_is_pointer (gtype);

    // Initilaize the GValue container for current argument.
    g_value_init (&arguments[num + 1], gtype);

    // TODO Ignore arguments of type GPtrArray, for now.
    if (gtype == G_TYPE_PTR_ARRAY)
      continue;

    info = g_string_new (NULL);

    // Add additional information if the argument is enum.
    if (G_TYPE_IS_ENUM (gtype)) {
      GEnumClass *enumklass = NULL;
      guint idx = 0;

      g_string_append_printf (info, "\nPossible enum values:\n");
      enumklass = G_ENUM_CLASS (g_type_class_ref (gtype));

      for (idx = 0; idx < enumklass->n_values; idx++) {
        GEnumValue *genum = &(enumklass->values[idx]);

        g_string_append_printf (info, "   (%d): %s - %s\n",
            genum->value, genum->value_nick, genum->value_name);
      }

      g_type_class_unref (enumklass);
    }

    g_string_append_printf (info, "Enter '%s%s' value for arg%u: ",
        g_type_name (gtype), asterisk ? "*" : "", num);

    do {
      g_print ("%s", info->str);

      // If FALSE is returned termination signal has been issued.
      if (!wait_stdin_message (messages, &input))
        return FALSE;

      // Empty inputs are not acceptable, deserialization must be successful.
    } while (g_str_equal (input, "") ||
        !gst_value_deserialize (&arguments[num + 1], input));

    g_clear_pointer (&input, g_free);
    g_string_free (info, TRUE);
  }

  g_value_init (&value, query.return_type);
  g_signal_emitv (arguments, signal_id, 0, &value);

  for (num = 0; num < (query.n_params + 1); num++)
    g_value_reset (&arguments[num]);

  g_free (arguments);

  status = gst_value_serialize (&value);
  g_value_reset (&value);

  g_print ("\n Signal return value: '%s'\n", status);
  g_free (status);

  return TRUE;
}

static gboolean
gst_element_menu (GstElement ** element, GAsyncQueue * messages)
{
  GstStructure *signals = NULL;
  gchar *input = NULL;
  gboolean active = TRUE;

  signals = gst_structure_new_empty ("signals");

  print_element_options (*element, signals);

  g_print ("\n\nChoose an option: ");

  // If FALSE is returned termination signal has been issued.
  active = wait_stdin_message (messages, &input);

  // Handle the chosen option if not signalled to quit.
  if (active && gst_structure_has_field (signals, input)) {
    guint signal_id = 0;

    gst_structure_get_uint (signals, input, &signal_id);
    active = gst_signal_menu (*element, messages, signal_id);
  } else if (active && g_str_equal (input, MENU_BACK_OPTION)) {
    gst_object_unref (*element);
    *element = NULL;
  } else if (active) {
    g_print ("Invalid option: '%s', and don't input properties here.\n", input);
  }

  g_free (input);

  gst_structure_free (signals);

  return active;
}

static gpointer
main_menu (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstElement *element = NULL;
  gchar *prop = NULL;
  gboolean active = TRUE;

  ::camera::CameraMetadata *meta_collect = nullptr;
  meta_collect = new ::camera::CameraMetadata(128, 128);

  while (active) {
    // In case no element has been chosen enter in the pipeline menu.
    if (NULL == element && prop == NULL)
      active = gst_pipeline_menu (appctx, &element, &prop);
    else if (NULL != element && prop == NULL)
      active = gst_element_menu (&element, appctx->messages);
    else if (NULL == element && prop != NULL)
      active = handle_metadata_menu (appctx, &prop, meta_collect);
    else
      g_printerr ("Invalid menu state, element != NULL && prop != NULL\n");
  }

  if (meta_collect != NULL) {
    delete meta_collect;
    meta_collect = NULL;
  }

  if (element != NULL)
    gst_object_unref (element);

  update_pipeline_state (appctx->pipeline, appctx->messages, GST_STATE_NULL);

  g_main_loop_quit (appctx->mloop);
  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstAppContext *appctx;
  GOptionContext *optctx;
  GstElement *element = NULL;
  GstBus *bus = NULL;
  GIOChannel *gio = NULL;
  GThread *mthread = NULL;
  GError *error = NULL;
  FILE *ts_file = NULL, *umeta_file = NULL, *rmeta_file = NULL;
  gchar *pipeline = NULL, *ts_path = NULL, *umeta_path = NULL,
      *rmeta_path = NULL;
  guint bus_watch_id = 0, intrpt_watch_id = 0, stdin_watch_id = 0;
  gint status = -1;

  g_set_prgname ("gst-camera-metadata-example");

  // Initialize GST library.
  gst_init (&argc, &argv);

  GOptionEntry options[] = {
    {"custom-pipeline", 'p', 0, G_OPTION_ARG_STRING, &pipeline,
        "Provide pipeline manually", NULL},
    {"display", 'd', 0, G_OPTION_ARG_NONE, &display,
        "Show preview on display", NULL},
    {"timestamps-location", 't', 0, G_OPTION_ARG_FILENAME, &ts_path,
        "File in which original timestamps will be recorded", NULL},
    {"urgent-meta-location", 'u', 0, G_OPTION_ARG_FILENAME, &umeta_path,
        "File in which urgent-metadata tags' values will be recorded", NULL},
    {"result-meta-location", 'r', 0, G_OPTION_ARG_FILENAME, &rmeta_path,
        "File in which result-metadata tags' values will be recorded", NULL},
    {NULL}
  };

  optctx = g_option_context_new ("");
  g_option_context_add_main_entries (optctx, options, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());

  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("ERROR: Couldn't initialize: %s\n",
        GST_STR_NULL (error->message));

    g_option_context_free (optctx);
    g_clear_error (&error);

    gst_app_context_free (appctx);
    return -1;
  }
  g_option_context_free (optctx);

  if ((appctx = gst_app_context_new ()) == NULL) {
    g_printerr ("ERROR: Couldn't create app context!\n");
    return -1;
  }

  if (pipeline == NULL && display)
    pipeline = g_strdup (GST_CAMERA_PIPELINE_DISPLAY);
  else if (pipeline == NULL)
    pipeline = g_strdup (GST_CAMERA_PIPELINE);

  g_print ("Creating pipeline %s\n", pipeline);
  appctx->pipeline = gst_parse_launch (pipeline, &error);

  // Check for errors on pipe creation.
  if ((NULL == appctx->pipeline) && (error != NULL)) {
    g_printerr ("ERROR: Failed to create pipeline, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    goto exit;
  } else if ((NULL == appctx->pipeline) && (NULL == error)) {
    g_printerr ("ERROR: Failed to create pipeline, unknown error!\n");

    goto exit;
  } else if ((appctx->pipeline != NULL) && (error != NULL)) {
    g_printerr ("ERROR: Erroneous pipeline, error: %s!\n",
        GST_STR_NULL (error->message));

    g_clear_error (&error);
    goto exit;
  }

  // Open file for dumping camera timestamp from new-sample callback of appsink.
  if ((element =
      get_element_from_pipeline (appctx->pipeline, "appsink")) != NULL &&
          ts_path != NULL && (ts_file = fopen (ts_path, "w")) == NULL) {
    g_printerr ("ERROR: Failed to open file for recording camera timestamp\n");
    gst_object_unref (element);
    goto exit;
  } else if (element != NULL) {
    g_signal_connect (element, "new-sample", G_CALLBACK (new_sample), ts_file);
    gst_object_unref (element);
  }

  if ((element =
      get_element_from_pipeline (appctx->pipeline, "qtiqmmfsrc")) == NULL) {
    g_printerr ("ERROR: No camera plugin found in pipeline, can't proceed.\n");
    goto exit;
  }

  // Open file for dumping urgent-metadata tags' values.
  if (umeta_path != NULL && (umeta_file = fopen (umeta_path, "w")) == NULL) {
    g_printerr ("ERROR: Failed to open file for recording urgent-metadata tags\n");
    goto exit;
  } else if (umeta_path != NULL) {
    g_signal_connect (element, "urgent-metadata",
        G_CALLBACK (urgent_metadata), umeta_file);
  }

  // Open file for dumping result-metadata tags' values.
  if (rmeta_path != NULL && (rmeta_file = fopen (rmeta_path, "w")) == NULL) {
    g_printerr ("ERROR: Failed to open file for recording result-metadata tags\n");
    goto exit;
  } else if (rmeta_path != NULL) {
    g_signal_connect (element, "result-metadata",
        G_CALLBACK (result_metadata), rmeta_file);
  }

  gst_object_unref (element);

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    goto exit;
  }

  // Initiate the menu thread.
  if ((mthread = g_thread_new ("MainMenu", main_menu, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create menu thread!\n");
    goto exit;
  }

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    goto exit;
  }

  // Create a GIOChannel to listen to the standard input stream.
  if ((gio = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("ERROR: Failed to initialize I/O support! %.30s\n", SPACE);
    gst_object_unref (bus);
    goto exit;
  }

  // Watch for messages on the pipeline's bus.
  bus_watch_id = gst_bus_add_watch (bus, handle_bus_message, appctx);
  gst_object_unref (bus);

  // Watch for user's input on stdin.
  stdin_watch_id = g_io_add_watch (gio,
      GIOCondition (G_IO_PRI | G_IO_IN), handle_stdin_source, appctx);
  g_io_channel_unref (gio);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Run main loop.
  g_main_loop_run (appctx->mloop);

  // Wait until main menu thread finishes.
  g_thread_join (mthread);

  g_source_remove (bus_watch_id);
  g_source_remove (intrpt_watch_id);
  g_source_remove (stdin_watch_id);

  status = 0;

exit:
  g_free (pipeline);
  g_free (ts_path);
  g_free (umeta_path);
  g_free (rmeta_path);

  if (ts_file != NULL)
    fclose (ts_file);

  if (umeta_file != NULL)
    fclose (umeta_file);

  if (rmeta_file != NULL)
    fclose (rmeta_file);

  gst_app_context_free (appctx);

  gst_deinit ();
  return status;
}
