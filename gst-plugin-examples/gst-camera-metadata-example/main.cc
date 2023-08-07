/*
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <camera/CameraMetadata.h>
#include <camera/VendorTagDescriptor.h>

#ifndef CAMERA_METADATA_1_0_NS
namespace camera = android;
#else
namespace camera = android::hardware::camera::common::V1_0::helper;
#endif

#define DASH_LINE   "----------------------------------------------------------------------"
#define SPACE       "                                                                      "

#define MAX_SIZE                       200
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
  STATIC_METADATA_OPTION
} GstMainMenuOption;

typedef enum {
  LIST_ALL_TAGS = 1,
  DUMP_ALL_TAGS,
  DUMP_CUSTOM_TAGS,
  GET_TAG,
  SET_TAG
} GstMetadataMenuOption;

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

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstState state = GST_STATE_VOID_PENDING;

  // Get the current state of the pipeline.
  gst_element_get_state (appctx->pipeline, &state, NULL, 0);

  if (state == GST_STATE_PLAYING) {
    // Signal menu thread to exit.
    g_async_queue_push (appctx->messages, 
        gst_structure_new_empty (TERMINATE_MESSAGE));
    g_print ("\nTerminating menu thread ...\n");
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
  g_free (*input);
  *input = NULL;

  // Block the thread until there's no input from the user or eos/error msg occurs.
  while ((message = (GstStructure *)g_async_queue_pop (queue)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      // Returning FALSE will cause menu thread to terminate.
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE)) {
      *input = g_strdup (gst_structure_get_string (message, "input"));
      break;
    }

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

  output = g_strdup_printf ("Camera timestamp: %" G_GUINT64_FORMAT "\n", timestamp);
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
    return FALSE;
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
      return FALSE;
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

      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("ERROR: Pipeline failed to PREROLL!\n");
        return FALSE;
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
  const ::android::sp<::camera::VendorTagDescriptor> vtags =
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

static gchar*
get_tag (const gchar * section_name, const gchar * tag_name,
    ::camera::CameraMetadata * meta, gchar ** type)
{
  gchar *tag_value = NULL;
  ::android::status_t status = 0;
  guint32 tag_id = 0;
  gint tag_type = -1;

  if ((tag_type = find_tag_by_name (section_name, tag_name, meta, &tag_id)) == -1)
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
        tag_value = g_strdup_printf ("%" G_GINT32_FORMAT "/%" G_GINT32_FORMAT,value_num, value_den);
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
    const gchar * tag_name, ::camera::CameraMetadata * meta, gchar * new_value)
{
  ::android::status_t status = -1;
  guint32 tag_id = 0;
  gint tag_type = -1;

  if ((tag_type = find_tag_by_name (section_name, tag_name, meta, &tag_id)) == -1)
    return 0;

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
    GstElement *camsrc = get_element_from_pipeline (pipeline, "qtiqmmfsrc");
    g_object_set (G_OBJECT (camsrc), "video-metadata", meta, NULL);
    gst_object_unref (camsrc);

    g_print ("The tag is set successfully.\n");
  } else {
    g_printerr ("ERROR: Couldn't set the value\n");
  }

  return 0;
}

static void
print_vendor_tags (::camera::CameraMetadata * meta, FILE * file)
{
  gchar *header = NULL;
  const ::android::sp<::camera::VendorTagDescriptor> vtags =
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

    g_snprintf (header, MAX_SIZE, "%.22s SECTION %.22s %.4s %.18s TAG %.18s %.4s "
        "%.8s VALUE %.8s\n", DASH_LINE, DASH_LINE, SPACE, DASH_LINE, DASH_LINE,
        SPACE, DASH_LINE, DASH_LINE);
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

    g_snprintf (header, MAX_SIZE, "%.8s SECTION %.8s %.4s %.15s TAG %.15s %.4s %.8s "
        "VALUE %.8s\n", DASH_LINE, DASH_LINE, SPACE, DASH_LINE, DASH_LINE,
        SPACE, DASH_LINE, DASH_LINE);
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
  ::camera::CameraMetadata *meta = static_cast<::camera::CameraMetadata*> (metadata);
  FILE* file = (FILE*) userdata;

  print_android_tags (meta, file);
  print_vendor_tags (meta, file);
  fputs ("\n\n\n\n\n", file);
}

static void
urgent_metadata (GstElement * element, gpointer metadata, gpointer userdata)
{
  ::camera::CameraMetadata *meta = static_cast<::camera::CameraMetadata*> (metadata);
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
dump_custom_tags (::camera::CameraMetadata * meta, gchar * file_path, gchar * prop)
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

  g_print ("\nValues of tags in the config file saved to %s successfully.\n", filename);

  g_free (outputline);
  g_free (configline);
  g_free (filename);
}

static void
print_metadata_menu (gchar * prop)
{
  gint spaces = (strlen (prop) > 14 ? 67 : 66);

  g_print ("\n%.25s %s %.25s\n", DASH_LINE, prop, DASH_LINE);
  g_print ("   (%d) %-25s\n", LIST_ALL_TAGS, "List all available tags");
  g_print ("   (%d) %-25s\n", DUMP_ALL_TAGS, "Dump all tags values in a file");
  g_print ("   (%d) %-25s\n", DUMP_CUSTOM_TAGS, "Dump custom tags values in a file");
  g_print ("   (%d) %-25s\n", GET_TAG, "Get a tag");

  if (g_str_equal (prop, "video-metadata"))
    g_print ("   (%d) %-25s\n", SET_TAG, "Set a tag");

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

  g_print ("%.56s\n", DASH_LINE);
  g_print ("   (%s) %-25s\n", QUIT_OPTION, "Quit");
  g_print ("\nChoose an option: ");
}

static gboolean
handle_tag_menu (GstAppContext * appctx, ::camera::CameraMetadata * meta,
    GstMetadataMenuOption option)
{
  gchar *str = NULL;
  gchar *section = NULL, *tag = NULL, *type = NULL, *value = NULL;
  gboolean active = TRUE;

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

    value = get_tag (section, tag, meta, &type);
    g_print ("Current value = %s\n", value);

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
        set_tag (appctx->pipeline, section, tag, meta, str);
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
handle_metadata_menu (GstAppContext * appctx, gchar ** prop)
{
  ::camera::CameraMetadata *meta = nullptr;
  GstElement *camsrc;
  gchar *str = NULL, *endptr;
  gboolean active = TRUE;
  gint input = 0;

  print_metadata_menu (*prop);

  if (!wait_stdin_message (appctx->messages, &str))
    return FALSE;

  if (g_str_equal (str, MENU_BACK_OPTION)) {
    *prop = NULL;
    goto exit;
  }

  camsrc = get_element_from_pipeline (appctx->pipeline, "qtiqmmfsrc");
  g_object_get (G_OBJECT (camsrc), *prop, &meta, NULL);
  gst_object_unref (camsrc);

  if (meta == NULL) {
    g_printerr ("ERROR: Meta not found\n");
    goto exit;
  }

  input = g_ascii_strtoll ((const gchar *) str, &endptr, 0);

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
        meta->clear ();
        delete meta;
        return FALSE;
      } else if (!g_str_equal (str, "\n")) {
        dump_custom_tags (meta, str, *prop);
      }

      break;
    case GET_TAG:
      active = handle_tag_menu (appctx, meta, GET_TAG);
      break;
    case SET_TAG:
      if (g_str_equal (*prop, "video-metadata"))
        active = handle_tag_menu (appctx, meta, SET_TAG);

      break;
    default:
      break;
  }

  meta->clear ();
  delete meta;

exit:
  g_free (str);
  return active;
}

static gboolean
handle_main_menu (GstAppContext * appctx, gchar ** prop)
{
  const gchar *prop_names[] = {"video-metadata", "image-metadata",
      "static-metadata"};
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

    if (input >= VIDEO_METADATA_OPTION && input <= STATIC_METADATA_OPTION)
      *prop = const_cast <gchar *> (prop_names[input-1]);
  }

  g_free (str);
  return TRUE;
}

static gpointer
main_menu (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gchar *prop = NULL;
  gboolean active = TRUE;

  // Transition to PLAYING state.
  if (!update_pipeline_state (appctx->pipeline, appctx->messages, GST_STATE_PLAYING)) {
    g_main_loop_quit (appctx->mloop);
    return NULL;
  }

  while (active) {
    if (prop == NULL)
      active = handle_main_menu (appctx, &prop);
    else
      active = handle_metadata_menu (appctx, &prop);
  }

  // Stop the pipeline.
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
  if ((element = get_element_from_pipeline (appctx->pipeline, "appsink")) != NULL &&
      ts_path != NULL && (ts_file = fopen (ts_path, "w")) == NULL) {
    g_printerr ("ERROR: Failed to open file for recording camera timestamp\n");
    gst_object_unref (element);
    goto exit;
  } else if (element != NULL) {
    g_signal_connect (element, "new-sample", G_CALLBACK (new_sample), ts_file);
    gst_object_unref (element);
  }

  if ((element = get_element_from_pipeline (appctx->pipeline, "qtiqmmfsrc")) == NULL) {
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
