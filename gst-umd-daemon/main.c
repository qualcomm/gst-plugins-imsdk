/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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

#include <errno.h>

#include <dlfcn.h>
#include <glib-unix.h>
#include <gst/gst.h>

#include <ml-meta/ml_meta.h>
#include <iot-core-algs/umd-gadget.h>

#define HASH_LINE  "##################################################"
#define EQUAL_LINE "=================================================="
#define DASH_LINE  "--------------------------------------------------"

#define APPEND_SECTION_SEPARATOR(string) \
  g_string_append_printf (string, " %.*s%.*s\n", 39, DASH_LINE, 40, DASH_LINE);

#define APPEND_MENU_HEADER(string) \
  g_string_append_printf (string, "\n\n%.*s MENU %.*s\n\n", \
      37, HASH_LINE, 37, HASH_LINE);

#define APPEND_CONTROLS_SECTION(string) \
  g_string_append_printf (string, " %.*s Pipeline Controls %.*s\n", \
      30, EQUAL_LINE, 30, EQUAL_LINE);

#define ML_FRAMING_ENABLE_OPTION     "f"
#define ML_FRAMING_POS_THOLD_OPTION  "p"
#define ML_FRAMING_DIMS_THOLD_OPTION "d"
#define ML_FRAMING_MARGINS_OPTION    "m"
#define ML_FRAMING_SPEED_OPTION      "s"
#define ML_FRAMING_CROPTYPE_OPTION   "c"

#define STDIN_MESSAGE          "STDIN_MSG"
#define TERMINATE_MESSAGE      "TERMINATE_MSG"
#define PIPELINE_STATE_MESSAGE "PIPELINE_STATE_MSG"
#define PIPELINE_EOS_MESSAGE   "PIPELINE_EOS_MSG"
#define PIPELINE_ERROR_MESSAGE "PIPELINE_ERROR_MSG"

#define GST_SERVICE_CONTEXT_CAST(obj)  ((GstServiceContext*)(obj))

typedef struct _GstServiceContext GstServiceContext;
typedef struct _AutoFramingConfig AutoFramingConfig;
typedef struct _VideoRectangle VideoRectangle;
typedef struct _AutoFrmLib AutoFrmLib;
typedef struct _AutoFrmOps AutoFrmOps;
typedef struct _MainOps MainOps;

enum
{
  ML_CROP_INTERNAL = 0,
  ML_CROP_EXTERNAL = 1,
};

/// ML Auto Framing related command line options.
struct _AutoFrmOps
{
  gboolean enable;
  gint     posthold;
  gint     dimsthold;
  gint     margins;
  gint     speed;
  gint     croptype;
};

static AutoFrmOps afrmops = {
  FALSE, 8, 16, 10, 10, ML_CROP_INTERNAL
};

struct _MainOps
{
  gchar * video;
  gchar * audio;
};

static MainOps mainops = {
  NULL, NULL
};

static const GOptionEntry entries[] = {
    { "uvc", 'v', 0, G_OPTION_ARG_STRING,
      &mainops.video,
      "UVC device "
      "(default: NULL)",
      "USB-VIDEO-DEVICE"
    },
    { "uac", 'a', 0, G_OPTION_ARG_STRING,
      &mainops.audio,
      "UAC device "
      "(default: NULL)",
      "USB-AUDIO-DEVICE"
    },
    { "ml-auto-framing-enable", 'f', 0, G_OPTION_ARG_NONE,
      &afrmops.enable,
      "Enable Machine Learning based auto framing algorithm "
      "(default: false)",
      NULL
    },
    { "ml-framing-position-threshold", 'p', 0, G_OPTION_ARG_INT,
      &afrmops.posthold,
      "The acceptable delta (in percent), between previous ROI position and "
      "current one, at which it is considered that the ROI has moved "
      "(default: 8)",
      "THRESHOLD"
    },
    { "ml-framing-dimensions-threshold", 'd', 0, G_OPTION_ARG_INT,
      &afrmops.dimsthold,
      "The acceptable delta (in percent), between previous ROI dimensions and "
      "current one, at which it is considered that ROI has been resized "
      "(default: 16)",
      "THRESHOLD"
    },
    { "ml-framing-margins", 'm', 0, G_OPTION_ARG_INT,
      &afrmops.margins,
      "Used to additionally increase the final size of the ROI rectangle "
      "(default: 10)",
      "MARGINS"
    },
    { "ml-framing-speed", 's', 0, G_OPTION_ARG_INT,
      &afrmops.speed,
      "Used to specify the movement speed of the ROI rectangle "
      "(default: 10)",
      "SPEED"
    },
    { "ml-framing-crop-type", 'c', 0, G_OPTION_ARG_INT,
      &afrmops.croptype,
      "The type of cropping (internal or external) used for the ROI rectangle "
      "(default: 0 - internal)",
      "[0 - internal / 1 - external]"
    },
    {NULL}
};

// TODO: These AFA structs need to be removed once HY11 rules are properly set.
struct _AutoFramingConfig
{
  // Output stream dimensions
  gint out_width;
  gint out_height;

  // Input stream dimensions
  gint in_width;
  gint in_height;
};

// TODO: These AFA structs need to be removed once HY11 rules are properly set.
struct _VideoRectangle
{
  gint x;
  gint y;
  gint w;
  gint h;
};

// TODO: These AFA structs need to be modified once HY11 rules are properly set.
struct _AutoFrmLib
{
  // Library handle.
  gpointer       handle;
  //Auto Framing Algorithm instance.
  gpointer       instance;

  // Library APIs.
  gpointer       (*new) (AutoFramingConfig configuration);
  void           (*free) (gpointer instance);

  VideoRectangle (*process) (gpointer instance, VideoRectangle * rectangle);

  void           (*set_position_threshold) (gpointer instance, gint threshold);
  void           (*set_dims_threshold) (gpointer instance, gint threshold);
  void           (*set_margins) (gpointer instance, gint margins);
  void           (*set_movement_speed) (gpointer instance, gint speed);
};

struct _GstServiceContext
{
  // UMD Gadget instance.
  UmdGadget       *gadget;

  // GStreamer video pipeline instance.
  GstElement      *vpipeline;

  // GStreamer audio pipeline instance.
  GstElement      *apipeline;

  // Auto Framing Algorithm library instance.
  AutoFrmLib      *afrmalgo;

  // Asynchronous queue for signaling pipeline EOS and state changes.
  GAsyncQueue     *pipemsgs;

  // Asynchronous queue for signaling menu thread messages from stdin.
  GAsyncQueue     *menumsgs;
};

static gboolean
load_symbol (gpointer * method, gpointer handle, const gchar * name)
{
  *(method) = dlsym (handle, name);
  if (NULL == *(method)) {
    g_printerr ("\nFailed to link library method %s, error: '%s'!\n",
        name, dlerror());
    return FALSE;
  }
  return TRUE;
}

static void
gst_service_context_free (GstServiceContext * ctx)
{
  if (ctx->gadget != NULL)
    umd_gadget_free (ctx->gadget);

  if (ctx->vpipeline != NULL) {
    gst_element_set_state (ctx->vpipeline, GST_STATE_NULL);
    gst_object_unref (ctx->vpipeline);
  }

  if (ctx->apipeline != NULL) {
    gst_element_set_state (ctx->apipeline, GST_STATE_NULL);
    gst_object_unref (ctx->apipeline);
  }

  if ((ctx->afrmalgo != NULL) && (ctx->afrmalgo->instance != NULL))
    ctx->afrmalgo->free (ctx->afrmalgo->instance);

  if ((ctx->afrmalgo != NULL) && (ctx->afrmalgo->handle != NULL))
    dlclose (ctx->afrmalgo->handle);

  g_free (ctx->afrmalgo);

  if (ctx->menumsgs != NULL)
    g_async_queue_unref (ctx->menumsgs);

  if (ctx->pipemsgs != NULL)
    g_async_queue_unref (ctx->pipemsgs);

  g_free (ctx);
}

static GstServiceContext *
gst_service_context_new ()
{
  GstServiceContext *ctx = g_new0 (GstServiceContext, 1);
  if (NULL == ctx) {
    g_printerr ("\nFailed to allocate memory for service context!\n");
    return NULL;
  }

  ctx->apipeline = NULL;
  ctx->vpipeline = NULL;
  ctx->gadget = NULL;

  if ((ctx->afrmalgo = g_new0 (AutoFrmLib, 1)) == NULL) {
    g_printerr ("\nFailed to allocate memory for Auto Framing interface!\n");
    gst_service_context_free (ctx);
    return NULL;
  }

  // Open Auto Framing Algorithm library and load its symbols.
  ctx->afrmalgo->handle = dlopen ("libqtiafralgo.so", RTLD_NOW);

  if (ctx->afrmalgo->handle != NULL) {
    gboolean success = TRUE;

    success &= load_symbol (
        (gpointer*) &ctx->afrmalgo->new,
        ctx->afrmalgo->handle, "auto_framing_algo_new");
    success &= load_symbol (
        (gpointer*) &ctx->afrmalgo->free,
        ctx->afrmalgo->handle, "auto_framing_algo_free");
    success &= load_symbol (
        (gpointer*) &ctx->afrmalgo->process,
        ctx->afrmalgo->handle, "auto_framing_algo_process");
    success &= load_symbol (
        (gpointer*) &ctx->afrmalgo->set_position_threshold,
        ctx->afrmalgo->handle, "auto_framing_algo_set_position_threshold");
    success &= load_symbol (
        (gpointer*) &ctx->afrmalgo->set_dims_threshold,
        ctx->afrmalgo->handle, "auto_framing_algo_set_dims_threshold");
    success &= load_symbol (
        (gpointer*) &ctx->afrmalgo->set_margins,
        ctx->afrmalgo->handle, "auto_framing_algo_set_margins");
    success &= load_symbol (
        (gpointer*) &ctx->afrmalgo->set_movement_speed,
        ctx->afrmalgo->handle, "auto_framing_algo_set_movement_speed");

    if (!success) {
      g_printerr ("\nFailed to load Auto Framing Algorithm symbols\n");
      dlclose (ctx->afrmalgo->handle);
      g_clear_pointer (&ctx->afrmalgo, g_free);
    }
  } else {
    g_printerr ("\nFailed to open Auto Framing Algorithm library\n");
    g_clear_pointer (&ctx->afrmalgo, g_free);
  }

  ctx->pipemsgs = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->menumsgs = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);

  if ((NULL == ctx->pipemsgs) || (NULL == ctx->menumsgs)) {
    g_printerr ("\nFailed to allocate memory for message queues!\n");
    gst_service_context_free (ctx);
    return NULL;
  }

  return ctx;
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop *) userdata;

  g_print ("\n\nReceived an interrupt signal, quit main loop ...\n");
  g_main_loop_quit (mloop);

  return FALSE;
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition condition,
    gpointer userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  GIOStatus status = G_IO_STATUS_NORMAL;
  gchar *input = NULL;

  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((G_IO_STATUS_ERROR == status) && (error != NULL)) {
      g_printerr ("\nFailed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    } else if ((G_IO_STATUS_ERROR == status) && (NULL == error)) {
      g_printerr ("\nUnknown error!\n");
      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  // Clear trailing whitespace and newline.
  input = g_strchomp (input);

  // Push stdin string into the inputs queue.
  g_async_queue_push (srvctx->menumsgs, gst_structure_new (STDIN_MESSAGE,
      "input", G_TYPE_STRING, input, NULL));
  g_free (input);

  return TRUE;
}

static gboolean
wait_stdin_message (GAsyncQueue * messages, gchar ** input)
{
  GstStructure *message = NULL;

  // Cleanup input variable from previous uses.
  g_clear_pointer (input, g_free);

  // Wait for either a STDIN or TERMINATE message.
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE)) {
      *input = g_strdup (gst_structure_get_string (message, "input"));
      break;
    }

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  GstElement *pipeline = srvctx->vpipeline;

  if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (srvctx->apipeline))
    pipeline = srvctx->apipeline;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (srvctx->vpipeline))
        g_async_queue_push (srvctx->pipemsgs,
            gst_structure_new_empty (PIPELINE_ERROR_MESSAGE));

      g_print ("\nSetting %s pipeline to NULL ...\n",
          GST_MESSAGE_SRC_NAME (message));
      gst_element_set_state (pipeline, GST_STATE_NULL);
      break;
    }
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_warning (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);
      break;
    }
    case GST_MESSAGE_EOS:
      g_print ("\nReceived End-of-Stream from '%s' ...\n",
          GST_MESSAGE_SRC_NAME (message));

      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (srvctx->vpipeline))
        g_async_queue_push (srvctx->pipemsgs,
            gst_structure_new_empty (PIPELINE_EOS_MESSAGE));
      break;
    case GST_MESSAGE_REQUEST_STATE:
    {
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      GstState state;

      gst_message_parse_request_state (message, &state);
      g_print ("\nSetting %s state to %s as requested by %s...\n",
          GST_MESSAGE_SRC_NAME (message),
          gst_element_state_get_name (state), name);

      gst_element_set_state (pipeline, state);
      g_free (name);
      break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new, pending;
      if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
          break;

      gst_message_parse_state_changed (message, &old, &new, &pending);
      g_print ("\n%s state changed from %s to %s, pending: %s\n",
          gst_element_get_name (pipeline), gst_element_state_get_name (old),
          gst_element_state_get_name (new),
          gst_element_state_get_name (pending));

      if (pipeline == srvctx->vpipeline)
        g_async_queue_push (srvctx->pipemsgs, gst_structure_new (
            PIPELINE_STATE_MESSAGE, "new", G_TYPE_UINT, new,
            "pending", G_TYPE_UINT, pending, NULL));

      if (pipeline == srvctx->apipeline && (new == GST_STATE_PAUSED) &&
          (old == GST_STATE_READY) && (pending == GST_STATE_VOID_PENDING)) {
        g_print ("\nSetting %s to PLAYING state ...\n",
            gst_element_get_name (pipeline));

        if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
                GST_STATE_CHANGE_FAILURE)
          g_printerr ("\n%s doesn't want to transition to PLAYING state!\n",
              gst_element_get_name (pipeline));
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
set_crop_rectangle (GstElement * pipeline, gint x, gint y, gint w, gint h)
{
  GstElement *element = NULL;
  GValue crop = G_VALUE_INIT, value = G_VALUE_INIT;

  g_value_init (&crop, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_INT);

  g_value_set_int (&value, x);
  gst_value_array_append_value (&crop, &value);
  g_value_set_int (&value, y);
  gst_value_array_append_value (&crop, &value);
  g_value_set_int (&value, w);
  gst_value_array_append_value (&crop, &value);
  g_value_set_int (&value, h);
  gst_value_array_append_value (&crop, &value);

  element = gst_bin_get_by_name (GST_BIN (pipeline), "vtransform");

  if (element != NULL) {
    g_object_set_property (G_OBJECT (element), "crop", &crop);
  } else {
    GstPad *pad = NULL;

    element = gst_bin_get_by_name (GST_BIN (pipeline), "camsrc");
    pad = gst_element_get_static_pad (element, "video_1");

    g_object_set_property (G_OBJECT (pad), "crop", &crop);
    gst_object_unref (pad);
  }

  gst_object_unref (element);

  g_value_unset (&value);
  g_value_unset (&crop);
}

// Event handler for all data received from the MLE
static GstFlowReturn
mle_new_sample (GstElement *sink, gpointer userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo info;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("\nPulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("\nPulled buffer is NULL!\n");
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    g_printerr ("\nFailed to map the pulled buffer!\n");
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  {
    GSList *metalist = NULL, *list = NULL;
    VideoRectangle rectangle = {0};
    gfloat confidence = 0.0;

    metalist = list = gst_buffer_get_detection_meta (buffer);

    while (list != NULL) {
      GstMLDetectionMeta *meta = NULL;
      GstMLClassificationResult *classification = NULL;

      meta = (GstMLDetectionMeta *) list->data;
      classification =
          (GstMLClassificationResult *) g_slist_nth_data (meta->box_info, 0);

      // Get the ML detection rectangle with highest confidence.
      if (g_strcmp0 (classification->name, "person") == 0 &&
          classification->confidence >= confidence) {
        rectangle.x = meta->bounding_box.x;
        rectangle.y = meta->bounding_box.y;
        rectangle.w = meta->bounding_box.width;
        rectangle.h = meta->bounding_box.height;
        confidence = classification->confidence;
      }

      list = g_slist_next (list);
    }

    g_slist_free (metalist);

    rectangle = srvctx->afrmalgo->process (
        srvctx->afrmalgo->instance, (confidence > 0.0) ? &rectangle : NULL);

    set_crop_rectangle (srvctx->vpipeline, rectangle.x, rectangle.y,
        rectangle.w, rectangle.h);
  }

  gst_buffer_unmap (buffer, &info);
  gst_sample_unref (sample);

  return GST_FLOW_OK;
}

static GstFlowReturn
umd_new_sample (GstElement *sink, gpointer userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo info;
  gint bufidx = UMD_BUFFER_NOT_SUBMITTED;
  gint stream_id = -1;

  if (!g_strcmp0 ("umdvsink", gst_element_get_name (sink)))
    stream_id = UMD_VIDEO_STREAM_ID;
  else if (!g_strcmp0 ("umdasink", gst_element_get_name (sink)))
    stream_id = UMD_AUDIO_STREAM_ID;
  else
    return GST_FLOW_ERROR;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!");
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    g_printerr ("ERROR: Failed to map the pulled buffer!");
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  bufidx = umd_gadget_submit_buffer (srvctx->gadget, stream_id,
      info.data, info.size, info.maxsize,
      GST_BUFFER_TIMESTAMP (buffer) / 1000);
  umd_gadget_wait_buffer (srvctx->gadget, stream_id, bufidx);

  gst_buffer_unmap (buffer, &info);
  gst_sample_unref (sample);

  return GST_FLOW_OK;
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

    if (gst_structure_has_name (message, PIPELINE_ERROR_MESSAGE)) {
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
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_ERROR_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_STATE_MESSAGE)) {
      GstState new = GST_STATE_VOID_PENDING;
      gst_structure_get_uint (message, "new", (guint*) &new);

      if (new == state)
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

  if (ret != GST_STATE_CHANGE_SUCCESS) {
    g_printerr ("Failed to retrieve %s state!\n",
        gst_element_get_name (pipeline));
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
  if ((current == GST_STATE_PLAYING) && (state < GST_STATE_PLAYING)) {
    g_print ("EOS enabled -- Sending EOS on %s\n",
        gst_element_get_name (pipeline));

    if (!gst_element_send_event (pipeline, gst_event_new_eos ())) {
      g_printerr ("Failed to send EOS event on %s!\n",
          gst_element_get_name (pipeline));
      return FALSE;
    }

    if (!wait_pipeline_eos_message (messages))
      return FALSE;
  }

  g_print ("Setting %s to %s\n", gst_element_get_name (pipeline),
      gst_element_state_get_name (state));
  ret = gst_element_set_state (pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to %s state!\n",
          gst_element_state_get_name (state));
      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("%s is live and does not need PREROLL.\n",
          gst_element_get_name (pipeline));
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("%s is PREROLLING ...\n", gst_element_get_name (pipeline));

      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret != GST_STATE_CHANGE_SUCCESS) {
        g_printerr ("%s failed to PREROLL!\n", gst_element_get_name (pipeline));
        return FALSE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("%s state change was successful\n",
          gst_element_get_name (pipeline));
      break;
  }

  if (!wait_pipeline_state_message (messages, state))
    return FALSE;

  return TRUE;
}

static gboolean
create_audio_pipeline (GstServiceContext * srvctx)
{
  GstElement *pcmsrc = NULL, *afilter = NULL;
  GstElement *abufsplit = NULL, *umdasink = NULL;
  GstCaps *filtercaps = NULL;
  GstBus *bus = NULL;

  // Create the empty audio pipeline.
  if ((srvctx->apipeline = gst_pipeline_new ("audio-pipeline")) == NULL) {
    g_printerr ("\nFailed to create empty audio pipeline.\n");
    return FALSE;
  }
  pcmsrc    = gst_element_factory_make ("pulsesrc", "pcmsrc");
  afilter   = gst_element_factory_make ("capsfilter", "afilter");
  abufsplit = gst_element_factory_make ("audiobuffersplit", "abufsplit");
  umdasink  = gst_element_factory_make ("appsink", "umdasink");

  if (!pcmsrc || !afilter || !abufsplit || !umdasink) {
    g_printerr ("\nOne audio element could not be created. Exiting.\n");

    if (pcmsrc)
      gst_object_unref (pcmsrc);

    if (afilter)
      gst_object_unref (afilter);

    if (abufsplit)
      gst_object_unref (abufsplit);

    if (umdasink)
      gst_object_unref (umdasink);

    return FALSE;
  }

  // Add the elements to the pipeline.
  gst_bin_add_many (GST_BIN (srvctx->apipeline), pcmsrc, afilter, abufsplit,
      umdasink, NULL);

  g_object_set (G_OBJECT (pcmsrc), "volume", 10.0, NULL);

  // Set caps for the pulsesrc audio pad.
  filtercaps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, "S16LE",
      "channels", G_TYPE_INT, 2,
      "rate", G_TYPE_INT, 48000,
      NULL);
  g_object_set (G_OBJECT (afilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (G_OBJECT (abufsplit), "output-buffer-duration", 3, 100, NULL);

  // Connect a callback to the new-sample signal.
  g_object_set (G_OBJECT (umdasink), "emit-signals", TRUE, NULL);
  g_signal_connect (umdasink, "new-sample", G_CALLBACK (umd_new_sample), srvctx);

  g_object_set (G_OBJECT(umdasink), "wait-on-eos", FALSE, NULL);
  g_object_set (G_OBJECT(umdasink), "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT(umdasink), "sync", FALSE, NULL);

  if (!gst_element_link_many (pcmsrc, afilter, abufsplit, umdasink, NULL)) {
    g_printerr ("\nFailed to link audio pipeline elements.\n");
    gst_object_unref (srvctx->apipeline);
    return FALSE;
  }
    // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (srvctx->apipeline))) == NULL) {
    g_printerr ("\nERROR: Failed to retrieve audio pipeline bus!\n");
    gst_object_unref (srvctx->apipeline);
    return FALSE;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_watch (bus, handle_bus_message, srvctx);
  gst_object_unref (bus);

  // Set pipeline into PAUSED state.
  switch (gst_element_set_state (srvctx->apipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\nAudio pipeline failed to transition to PAUSED state!\n");
      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\nAudio pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
    {
      GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

      g_print ("\nAudio pipeline is PREROLLING ...\n");

      ret = gst_element_get_state (srvctx->apipeline, NULL, NULL,
          GST_CLOCK_TIME_NONE);

      if (ret != GST_STATE_CHANGE_SUCCESS) {
        g_printerr ("\nAudio pipeline failed to PREROLL!\n");
        return FALSE;
      }
      break;
    }
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("\nAudio pipeline state change was successful\n");
      break;
  }

  return TRUE;
}

static gboolean
create_video_pipeline (GstServiceContext * srvctx)
{
  GstElement *camsrc = NULL, *vtransform = NULL;
  GstElement *mlefilter = NULL, *mletflite = NULL, *mlesink = NULL;
  GstElement *in_mlequeue = NULL, *out_mlequeue = NULL, *vqueue = NULL;
  GstElement *umdvqueue = NULL, *umdvfilter = NULL, *umdvsink = NULL;

  GstCaps *filtercaps = NULL;
  GstBus *bus = NULL;

  // Create the empty video pipeline.
  if ((srvctx->vpipeline = gst_pipeline_new ("video-pipeline")) == NULL) {
    g_printerr ("\nFailed to create empty video pipeline.\n");
    return FALSE;
  }

  // Create the elements.
  camsrc = gst_element_factory_make ("qtiqmmfsrc", "camsrc");
  vqueue = gst_element_factory_make ("queue", "vqueue");
  vtransform = gst_element_factory_make ("qtivtransform", "vtransform");
  in_mlequeue = gst_element_factory_make ("queue", "inmlequeue");
  mlefilter = gst_element_factory_make ("capsfilter", "mlefilter");
  mletflite = gst_element_factory_make ("qtimletflite", "mletflite");
  out_mlequeue = gst_element_factory_make ("queue", "outmlequeue");
  mlesink  = gst_element_factory_make ("appsink", "mlesink");
  umdvfilter = gst_element_factory_make ("capsfilter", "umdvfilter");
  umdvqueue = gst_element_factory_make ("queue", "umdvqueue");
  umdvsink = gst_element_factory_make ("appsink", "umdvsink");

  if (!camsrc || !vtransform || !vqueue || !in_mlequeue || !out_mlequeue ||
      !mlefilter || !mletflite || !mlesink || !umdvfilter || !umdvqueue ||
      !umdvsink) {
    g_printerr ("\nNot all elements could be created.\n");

    if (camsrc)
      gst_object_unref (camsrc);

    if (vtransform)
      gst_object_unref (vtransform);

    if (vqueue)
      gst_object_unref (vqueue);

    if (mlefilter)
      gst_object_unref (mlefilter);

    if (in_mlequeue)
      gst_object_unref (in_mlequeue);

    if (out_mlequeue)
      gst_object_unref (out_mlequeue);

    if (mletflite)
      gst_object_unref (mletflite);

    if (mlesink)
      gst_object_unref (mlesink);

    if (umdvfilter)
      gst_object_unref (umdvfilter);

    if (umdvqueue)
      gst_object_unref (umdvqueue);

    if (umdvsink)
      gst_object_unref (umdvsink);

    return FALSE;
  }

  // Add the elements to the pipeline.
  gst_bin_add_many (GST_BIN (srvctx->vpipeline), camsrc, vtransform, vqueue,
      mlefilter, in_mlequeue, out_mlequeue, mletflite, mlesink, umdvfilter,
      umdvqueue, umdvsink, NULL);

  // Link the plugins in the MLE portion of the pipeline.
  if (!gst_element_link_many (camsrc, mlefilter, in_mlequeue, mletflite,
          out_mlequeue, mlesink, NULL)) {
    g_printerr ("\nFailed to link pipeline MLE elements.\n");
    return FALSE;
  }

  // Set caps for the MLE camera pad.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  g_object_set (G_OBJECT (mlefilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Set MLE properties
  g_object_set (G_OBJECT (mletflite),
      "delegate", 0, NULL);
  g_object_set (G_OBJECT (mletflite),
      "config", "/data/misc/camera/mle_tflite.config", NULL);
  g_object_set (G_OBJECT (mletflite),
      "model", "/data/misc/camera/detect.tflite", NULL);
  g_object_set (G_OBJECT (mletflite),
      "labels", "/data/misc/camera/labelmap.txt", NULL);
  g_object_set (G_OBJECT (mletflite),
      "postprocessing", "detection", NULL);
  g_object_set (G_OBJECT (mletflite),
      "preprocess-accel", 1, NULL);

  // Set emit-signals property and connect a callback to the new-sample signal.
  g_object_set (G_OBJECT (mlesink), "emit-signals", TRUE, NULL);
  g_signal_connect (mlesink, "new-sample", G_CALLBACK (mle_new_sample), srvctx);

  // Link the plugins in the UMD portion of the pipeline.
  if (!gst_element_link_many (camsrc, umdvfilter, vqueue, vtransform, umdvqueue,
          umdvsink, NULL)) {
    g_printerr ("\nFailed to link pipeline UMD elements.\n");
    return FALSE;
  }

  {
    // Set the camera ISO mode to manual.
    GParamSpec *propspecs = NULL;
    GValue value = G_VALUE_INIT;

    // Get the property specs to initialize the GValue type.
    propspecs = g_object_class_find_property (
        G_OBJECT_GET_CLASS (camsrc), "iso-mode");
    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));

    gst_value_deserialize (&value, "manual");
    g_object_set_property (G_OBJECT (camsrc), "iso-mode", &value);
  }

  // Set emit-signals property and connect a callback to the new-sample signal.
  g_object_set (G_OBJECT (umdvsink), "emit-signals", TRUE, NULL);
  g_signal_connect (umdvsink, "new-sample", G_CALLBACK (umd_new_sample), srvctx);

  g_object_set (G_OBJECT (umdvsink), "wait-on-eos", FALSE, NULL);
  g_object_set (G_OBJECT (umdvsink), "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT (umdvsink), "sync", FALSE, NULL);

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (srvctx->vpipeline))) == NULL) {
    g_printerr ("\nFailed to retrieve pipeline bus!\n");
    return FALSE;
  }

  // Watch for pipemsgs on the pipeline's bus.
  gst_bus_add_watch (bus, handle_bus_message, srvctx);
  gst_object_unref (bus);

  // Set pipeline into READY state.
  switch (gst_element_set_state (srvctx->vpipeline, GST_STATE_READY)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\nVideo pipeline failed to transition to READY state!\n");
      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\nVideo pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
    {
      GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

      g_print ("\nVideo pipeline is PREROLLING ...\n");

      ret = gst_element_get_state (srvctx->vpipeline, NULL, NULL,
          GST_CLOCK_TIME_NONE);

      if (ret != GST_STATE_CHANGE_SUCCESS) {
        g_printerr ("\nVideo pipeline failed to PREROLL!\n");
        return FALSE;
      }
      break;
    }
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("\nVideo pipeline state change was successful\n");
      break;
  }

  return TRUE;
}

static gboolean
mle_reconfigure_pipeline (GstServiceContext * srvctx, gboolean enable)
{
  GstElement *pipeline = srvctx->vpipeline;
  GstElement *mlefilter = NULL, *mletflite = NULL, *mlesink = NULL;
  GstElement *in_mlequeue = NULL, *out_mlequeue = NULL, *fakesink = NULL;
  gboolean success = TRUE;

  // Use the existance of fakesink as indicator for MLE status.
  fakesink = gst_bin_get_by_name (GST_BIN (pipeline), "fakesink");

  if (enable && (fakesink != NULL)) {
    gst_bin_remove_many (GST_BIN (pipeline), fakesink, NULL);

    // Set the element into NULL state before destroying it.
    gst_element_set_state (fakesink, GST_STATE_NULL);
    gst_object_unref (fakesink);

    in_mlequeue = gst_element_factory_make ("queue", "inmlequeue");
    mletflite = gst_element_factory_make ("qtimletflite", "mletflite");
    out_mlequeue = gst_element_factory_make ("queue", "outmlequeue");
    mlesink  = gst_element_factory_make ("appsink", "mlesink");

    if (!in_mlequeue || !mletflite || !out_mlequeue || !mlesink) {
      g_printerr ("\nFailed to create one or more MLE elements!\n");

      if (in_mlequeue)
        gst_object_unref (in_mlequeue);

      if (mletflite)
        gst_object_unref (mletflite);

      if (out_mlequeue)
        gst_object_unref (out_mlequeue);

      if (mlesink)
        gst_object_unref (mlesink);

      return FALSE;
    }

    // Add the new elements to the pipeline.
    gst_bin_add_many (GST_BIN (pipeline), in_mlequeue, mletflite,
        out_mlequeue, mlesink, NULL);

    // New elements need to be in the same state as the pipeline.
    success = gst_element_sync_state_with_parent (mlesink);
    success &= gst_element_sync_state_with_parent (out_mlequeue);
    success &= gst_element_sync_state_with_parent (mletflite);
    success &= gst_element_sync_state_with_parent (in_mlequeue);

    if (!success) {
      g_printerr ("\nFailed to set new MLE elements into proper state!\n");
      return FALSE;
    }

    // Set MLE properties
    g_object_set (G_OBJECT (mletflite),
        "delegate", 0, NULL);
    g_object_set (G_OBJECT (mletflite),
        "config", "/data/misc/camera/mle_tflite.config", NULL);
    g_object_set (G_OBJECT (mletflite),
        "model", "/data/misc/camera/detect.tflite", NULL);
    g_object_set (G_OBJECT (mletflite),
        "labels", "/data/misc/camera/labelmap.txt", NULL);
    g_object_set (G_OBJECT (mletflite),
        "postprocessing", "detection", NULL);
    g_object_set (G_OBJECT (mletflite),
        "preprocess-accel", 1, NULL);

    // Set emit-signals property and connect a callback to the new-sample signal.
    g_object_set (G_OBJECT(mlesink), "emit-signals", TRUE, NULL);
    g_signal_connect (mlesink, "new-sample", G_CALLBACK (mle_new_sample), srvctx);

    g_object_set (G_OBJECT(mlesink), "wait-on-eos", FALSE, NULL);
    g_object_set (G_OBJECT(mlesink), "enable-last-sample", FALSE, NULL);
    g_object_set (G_OBJECT(mlesink), "sync", FALSE, NULL);

    // Retrieve MLE filter plugin in order to link the new elements.
    mlefilter = gst_bin_get_by_name (GST_BIN (pipeline), "mlefilter");

    success = gst_element_link_many (mlefilter, in_mlequeue, mletflite,
        out_mlequeue, mlesink, NULL);

    gst_object_unref (mlefilter);
  } else if (!enable && (NULL == fakesink)) {
    in_mlequeue = gst_bin_get_by_name (GST_BIN (pipeline), "inmlequeue");
    mletflite = gst_bin_get_by_name (GST_BIN (pipeline), "mletflite");
    out_mlequeue = gst_bin_get_by_name (GST_BIN (pipeline), "outmlequeue");
    mlesink = gst_bin_get_by_name (GST_BIN (pipeline), "mlesink");

    gst_bin_remove_many (GST_BIN (pipeline), in_mlequeue, mletflite,
        out_mlequeue, mlesink, NULL);

    // Removed elements need to be in NULL state before deletion.
    gst_element_set_state (mlesink, GST_STATE_NULL);
    gst_element_set_state (out_mlequeue, GST_STATE_NULL);
    gst_element_set_state (mletflite, GST_STATE_NULL);
    gst_element_set_state (in_mlequeue, GST_STATE_NULL);

    gst_object_unref (mlesink);
    gst_object_unref (out_mlequeue);
    gst_object_unref (mletflite);
    gst_object_unref (in_mlequeue);

    fakesink = gst_element_factory_make ("fakesink", "fakesink");

    if (!fakesink) {
      g_printerr ("\nFailed to create fakesink element!\n");
      return FALSE;
    }

    // Add the new elements to the pipeline.
    gst_bin_add_many (GST_BIN (pipeline), fakesink, NULL);

    // New elements need to be in the same state as the pipeline.
    if (!gst_element_sync_state_with_parent (fakesink)) {
      g_printerr ("\nFailed to set fakesink into proper state!\n");
      return FALSE;
    }

    // Retrieve MLE filter plugin in order to link the new elements.
    mlefilter = gst_bin_get_by_name (GST_BIN (pipeline), "mlefilter");
    success = gst_element_link (mlefilter, fakesink);

    gst_object_unref (mlefilter);
  }

  return success;
}

static bool
setup_camera_stream (UmdVideoSetup * stmsetup, void * userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  gint idx = 0, fps_n = 0, fps_d = 0;

  g_print ("\nStream setup: %ux%u@%.2f - %c%c%c%c\n", stmsetup->width,
      stmsetup->height, stmsetup->fps, UMD_FMT_NAME (stmsetup->format));

  gst_util_double_to_fraction (stmsetup->fps, &fps_n, &fps_d);

  // In case Auto Framing library is missing forcefully disable ML stream.
  if (NULL == srvctx->afrmalgo) {
    g_printerr ("\nAuto Framing library doesn't exist, disabling ML!\n");
    afrmops.enable = FALSE;
  }

  // Cleanup pipeline queue from stale messages.
  while (g_async_queue_length (srvctx->pipemsgs) > 0) {
    GstStructure *message = g_async_queue_pop (srvctx->pipemsgs);
    gst_structure_free (message);
  }

  switch (stmsetup->format) {
    case UMD_VIDEO_FMT_YUYV:
    {
      GstElement *vqueue = NULL, *vtrans = NULL;
      GstElement  *umdvfilter = NULL, *umdvqueue = NULL;
      GstCaps *filtercaps = NULL;
      gboolean success = TRUE;

      umdvfilter = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "umdvfilter");
      umdvqueue = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "umdvqueue");
      vtrans = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "vtransform");
      vqueue = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "vqueue");

      // Update and set the UMD filter caps.
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "YUY2",
          "width", G_TYPE_INT, stmsetup->width,
          "height", G_TYPE_INT, stmsetup->height,
          "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
          NULL);
      gst_caps_set_features (filtercaps, 0,
          gst_caps_features_new ("memory:GBM", NULL));

      g_object_set (G_OBJECT (umdvfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);

      // Unlink and link pipeline only if elements are not already present.
      // Add vtransform plugin only if ML is enabled and crop is external.
      if (afrmops.enable && (afrmops.croptype == ML_CROP_EXTERNAL) &&
          (NULL == vtrans) && (NULL == vqueue)) {
        vtrans = gst_element_factory_make ("qtivtransform", "vtransform");
        vqueue = gst_element_factory_make ("queue", "vqueue");

        // Add the new elements to the pipeline.
        gst_bin_add_many (GST_BIN (srvctx->vpipeline), vtrans, vqueue, NULL);

        // New elements need to be in the same state as the pipeline.
        gst_element_sync_state_with_parent (vqueue);
        gst_element_sync_state_with_parent (vtrans);

        // Unlink the plugins where we want to add our new elements.
        gst_element_unlink (umdvfilter, umdvqueue);

        success =
            gst_element_link_many (umdvfilter, vqueue, vtrans, umdvqueue, NULL);
      } else if ((!afrmops.enable || (afrmops.croptype == ML_CROP_INTERNAL)) &&
                 (vtrans != NULL) && (vqueue != NULL)) {
        gst_bin_remove (GST_BIN (srvctx->vpipeline), vtrans);
        gst_bin_remove (GST_BIN (srvctx->vpipeline), vqueue);

        // Removed elements need to be in NULL state before deletion.
        gst_element_set_state (vtrans, GST_STATE_NULL);
        gst_element_set_state (vqueue, GST_STATE_NULL);

        gst_object_unref (vqueue);
        gst_object_unref (vtrans);

        success = gst_element_link (umdvfilter, umdvqueue);
      } else if (vtrans && vqueue) {
        gst_object_unref (vqueue);
        gst_object_unref (vtrans);
      }

      gst_object_unref (umdvqueue);
      gst_object_unref (umdvfilter);

      if (!success) {
        g_printerr ("\nFailed to link pipeline UMD elements.\n");
        return false;
      }
      break;
    }
    case UMD_VIDEO_FMT_MJPEG:
    {
      GstElement *vqueue = NULL, *vtrans = NULL;
      GstElement  *umdvfilter = NULL, *umdvqueue = NULL;
      GstCaps *filtercaps = NULL;
      gboolean success = TRUE;

      vtrans = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "vtransform");
      vqueue = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "vqueue");
      umdvfilter = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "umdvfilter");
      umdvqueue = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "umdvqueue");

      // Update and set the UMD filter caps.
      filtercaps = gst_caps_new_simple ("image/jpeg",
          "width", G_TYPE_INT, stmsetup->width,
          "height", G_TYPE_INT, stmsetup->height,
          "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
          NULL);

      g_object_set (G_OBJECT (umdvfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);

      // Unlink and link pipeline only if elements are already present.
      if ((vtrans != NULL) && (vqueue != NULL)) {
        gst_bin_remove (GST_BIN (srvctx->vpipeline), vtrans);
        gst_bin_remove (GST_BIN (srvctx->vpipeline), vqueue);

        // Removed elements need to be in NULL state before deletion.
        gst_element_set_state (vtrans, GST_STATE_NULL);
        gst_element_set_state (vqueue, GST_STATE_NULL);

        gst_object_unref (vqueue);
        gst_object_unref (vtrans);

        success = gst_element_link (umdvfilter, umdvqueue);
      }

      gst_object_unref (umdvqueue);
      gst_object_unref (umdvfilter);

      if (!success) {
        g_printerr ("\nFailed to link pipeline UMD elements.\n");
        return false;
      }

      if (afrmops.croptype == ML_CROP_EXTERNAL) {
        g_print ("\nExternal crop not supported for MJPEG stream, "
            "switching to internal crop mechanism!\n");
        afrmops.croptype = ML_CROP_INTERNAL;
      }
      break;
    }
    default:
      g_printerr ("\nUnsupported format %c%c%c%c!\n",
          UMD_FMT_NAME (stmsetup->format));
      return false;
  }

  // Reset the crop parameters.
  set_crop_rectangle (srvctx->vpipeline, 0, 0, 0, 0);

  if (!mle_reconfigure_pipeline (srvctx, afrmops.enable)) {
    g_printerr ("\nFailed to reconfigure pipeline MLE elements!\n");
    return false;
  }

  if (srvctx->afrmalgo != NULL) {
    AutoFramingConfig configuration = {0};

    // Initialization of the Auto Framing algorithm.
    configuration.out_width = stmsetup->width;
    configuration.out_height = stmsetup->height;

    configuration.in_width = 640;
    configuration.in_height = 360;

    // Destroy the previous instance and create a new one.
    if (srvctx->afrmalgo->instance != NULL)
      srvctx->afrmalgo->free (srvctx->afrmalgo->instance);

    srvctx->afrmalgo->instance = srvctx->afrmalgo->new (configuration);

    if (NULL == srvctx->afrmalgo->instance) {
      g_printerr ("\nFailed to create Auto Framing algorithm!\n");
      return false;
    }

    // Set the framing thresholds.
    srvctx->afrmalgo->set_position_threshold (
        srvctx->afrmalgo->instance, afrmops.posthold);
    srvctx->afrmalgo->set_dims_threshold (
        srvctx->afrmalgo->instance, afrmops.dimsthold);
    srvctx->afrmalgo->set_margins (
        srvctx->afrmalgo->instance, afrmops.margins);
    srvctx->afrmalgo->set_movement_speed (
        srvctx->afrmalgo->instance, afrmops.speed);
  }

  return true;
}

static bool
enable_camera_stream (void * userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  GstState state = GST_STATE_PLAYING;

  if (!update_pipeline_state (srvctx->vpipeline, srvctx->pipemsgs, state)) {
    g_printerr ("\nFailed to update video pipeline state!\n");
    return false;
  }

  // Send a empty message to the menu in order to reset it.
  g_async_queue_push (srvctx->menumsgs, gst_structure_new (STDIN_MESSAGE,
      "input", G_TYPE_STRING, "", NULL));

  g_print ("\nStream ON\n");
  return true;
}

static bool
disable_camera_stream (void * userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  GstState state = GST_STATE_NULL;

  if (!update_pipeline_state (srvctx->vpipeline, srvctx->pipemsgs, state)) {
    g_printerr ("\nFailed to update video pipeline state!\n");
    return false;
  }

  // Send a empty message to the menu in order to reset it.
  g_async_queue_push (srvctx->menumsgs, gst_structure_new (STDIN_MESSAGE,
      "input", G_TYPE_STRING, "", NULL));

  g_print ("\nStream OFF\n");
  return true;
}

static void
set_exposure_compensation_property (GstElement * element, gint16 compensation)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, compensation);

  g_object_set_property (G_OBJECT (element), "exposure-compensation", &value);
}

static void
get_exposure_compensation_property (GstElement * element, gint16 * compensation)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_object_get_property (G_OBJECT (element), "exposure-compensation", &value);

  *compensation = g_value_get_int (&value);
}

static void
set_contrast_property (GstElement * element, guint16 contrast)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, contrast);

  g_object_set_property (G_OBJECT (element), "contrast", &value);
}

static void
get_contrast_property (GstElement * element, guint16 * contrast)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_object_get_property (G_OBJECT (element), "contrast", &value);

  *contrast = g_value_get_int (&value);
}

static void
set_saturation_property (GstElement * element, guint16 saturation)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, saturation);

  g_object_set_property (G_OBJECT (element), "saturation", &value);
}

static void
get_saturation_property (GstElement * element, guint16 * saturation)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_object_get_property (G_OBJECT (element), "saturation", &value);

  *saturation = g_value_get_int (&value);
}

static void
set_sharpness_property (GstElement * element, guint16 sharpness)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, sharpness);

  g_object_set_property (G_OBJECT (element), "sharpness", &value);
}

static void
get_sharpness_property (GstElement * element, guint16 * sharpness)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_object_get_property (G_OBJECT (element), "sharpness", &value);

  *sharpness = g_value_get_int (&value);
}

static void
set_adrc_property (GstElement * element, guint16 adrc)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_value_set_boolean (&value, adrc);

  g_object_set_property (G_OBJECT (element), "adrc", &value);
}

static void
get_adrc_property (GstElement * element, guint16 * adrc)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_BOOLEAN);
  g_object_get_property (G_OBJECT (element), "adrc", &value);

  *adrc = g_value_get_boolean (&value);
}

static void
set_wb_temperature_property (GstElement * element, guint16 temperature)
{
  GValue value = G_VALUE_INIT;
  gchar *string = NULL;

  g_value_init (&value, G_TYPE_STRING);
  string = g_strdup_printf ("org.codeaurora.qcamera3.manualWB,"
      "color_temperature=%u;", temperature);

  g_value_set_string (&value, string);
  g_free (string);

  g_object_set_property (G_OBJECT (element), "manual-wb-settings", &value);
}

static void
get_wb_temperature_property (GstElement * element, guint16 * temperature)
{
  GValue value = G_VALUE_INIT;
  GstStructure *structure = NULL;

  g_value_init (&value, G_TYPE_STRING);
  g_object_get_property (G_OBJECT (element), "manual-wb-settings", &value);

  structure = gst_structure_new_from_string (g_value_get_string (&value));

  if (gst_structure_has_field (structure, "color_temperature")) {
    guint32 wbtemp = 4600;
    gst_structure_get_uint (structure, "color_temperature", &wbtemp);
    *temperature = wbtemp;
  }

  gst_structure_free (structure);
}

static void
set_wb_mode_property (GstElement * element, guint8 mode)
{
  GParamSpec *propspecs = NULL;
  GValue value = G_VALUE_INIT;

  // Get the property specs to initialize the GValue type.
  propspecs = g_object_class_find_property (
      G_OBJECT_GET_CLASS (element), "white-balance-mode");
  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));

  switch (mode) {
    case UMD_VIDEO_WB_MODE_AUTO:
      gst_value_deserialize (&value, "auto");
      break;
    case UMD_VIDEO_WB_MODE_MANUAL:
      gst_value_deserialize (&value, "manual-cc-temp");
      break;
    default:
      g_printerr ("\nUnsupported WB mode: %d!\n", mode);
      return;
  }

  g_object_set_property (G_OBJECT (element), "white-balance-mode", &value);
}

static void
get_wb_mode_property (GstElement * element, guint8 * mode)
{
  GParamSpec *propspecs = NULL;
  GEnumClass *enumklass = NULL;
  GValue value = G_VALUE_INIT;
  GEnumValue *v = NULL;

  // Get the property specs to initialize the GValue type.
  propspecs = g_object_class_find_property (
      G_OBJECT_GET_CLASS (element), "white-balance-mode");
  enumklass = G_ENUM_CLASS (g_type_class_ref (propspecs->value_type));

  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));
  g_object_get_property (G_OBJECT (element), "white-balance-mode", &value);

  v = g_enum_get_value (enumklass, g_value_get_enum (&value));

  if (g_strcmp0 (v->value_nick, "manual-cc-temp") == 0)
    *mode = UMD_VIDEO_WB_MODE_MANUAL;
  else if (g_strcmp0 (v->value_nick, "auto") == 0)
    *mode = UMD_VIDEO_WB_MODE_AUTO;

  g_type_class_unref (enumklass);
}

static void
set_exposure_time_property (GstElement * element, guint32 time)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT64);
  g_value_set_int64 (&value, (guint64) time * 100000);

  g_object_set_property (G_OBJECT (element), "manual-exposure-time", &value);
}

static void
get_exposure_time_property (GstElement * element, guint32 * time)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT64);
  g_object_get_property (G_OBJECT (element), "manual-exposure-time", &value);

  *time = g_value_get_int64 (&value) / 100000;
}

static void
set_exposure_mode_property (GstElement * element, guint8 mode)
{
  GParamSpec *propspecs = NULL;
  GValue value = G_VALUE_INIT;

  // Get the property specs to initialize the GValue type.
  propspecs = g_object_class_find_property (
      G_OBJECT_GET_CLASS (element), "exposure-mode");
  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));

  switch (mode) {
    case UMD_VIDEO_EXPOSURE_MODE_AUTO:
      gst_value_deserialize (&value, "auto");
      break;
    case UMD_VIDEO_EXPOSURE_MODE_SHUTTER:
      gst_value_deserialize (&value, "off");
      break;
    default:
      g_printerr ("\nUnsupported Exposure mode: %d!\n", mode);
      return;
  }

  g_object_set_property (G_OBJECT (element), "exposure-mode", &value);
}

static void
get_exposure_mode_property (GstElement * element, guint8 * mode)
{
  GParamSpec *propspecs = NULL;
  GEnumClass *enumklass = NULL;
  GValue value = G_VALUE_INIT;
  GEnumValue *v = NULL;

  // Get the property specs to initialize the GValue type.
  propspecs = g_object_class_find_property (
      G_OBJECT_GET_CLASS (element), "exposure-mode");
  enumklass = G_ENUM_CLASS (g_type_class_ref (propspecs->value_type));

  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));
  g_object_get_property (G_OBJECT (element), "exposure-mode", &value);

  v = g_enum_get_value (enumklass, g_value_get_enum (&value));

  if (g_strcmp0 (v->value_nick, "off") == 0)
    *mode = UMD_VIDEO_EXPOSURE_MODE_SHUTTER;
  else if (g_strcmp0 (v->value_nick, "auto") == 0)
    *mode = UMD_VIDEO_EXPOSURE_MODE_AUTO;

  g_type_class_unref (enumklass);
}

static void
set_focus_mode_property (GstElement * element, guint8 mode)
{
  GParamSpec *propspecs = NULL;
  GValue value = G_VALUE_INIT;

  // Get the property specs to initialize the GValue type.
  propspecs = g_object_class_find_property (
      G_OBJECT_GET_CLASS (element), "focus-mode");
  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));

  switch (mode) {
    case UMD_VIDEO_FOCUS_MODE_AUTO:
      gst_value_deserialize (&value, "auto");
      break;
    case UMD_VIDEO_FOCUS_MODE_MANUAL:
      gst_value_deserialize (&value, "off");
      break;
    default:
      g_printerr ("\nUnsupported Focus mode: %d!\n", mode);
      return;
  }

  g_object_set_property (G_OBJECT (element), "focus-mode", &value);
}

static void
get_focus_mode_property (GstElement * element, guint8 * mode)
{
  GParamSpec *propspecs = NULL;
  GEnumClass *enumklass = NULL;
  GValue value = G_VALUE_INIT;
  GEnumValue *v = NULL;

  // Get the property specs to initialize the GValue type.
  propspecs = g_object_class_find_property (
      G_OBJECT_GET_CLASS (element), "focus-mode");
  enumklass = G_ENUM_CLASS (g_type_class_ref (propspecs->value_type));

  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));
  g_object_get_property (G_OBJECT (element), "focus-mode", &value);

  v = g_enum_get_value (enumklass, g_value_get_enum (&value));

  if (g_strcmp0 (v->value_nick, "off") == 0)
    *mode = UMD_VIDEO_FOCUS_MODE_MANUAL;
  else if (g_strcmp0 (v->value_nick, "auto") == 0)
    *mode = UMD_VIDEO_FOCUS_MODE_AUTO;

  g_type_class_unref (enumklass);
}

static void
set_antibanding_property (GstElement * element, guint8 mode)
{
  GParamSpec *propspecs = NULL;
  GValue value = G_VALUE_INIT;

  // Get the property specs to initialize the GValue type.
  propspecs = g_object_class_find_property (
      G_OBJECT_GET_CLASS (element), "antibanding");
  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));

  switch (mode) {
    case UMD_VIDEO_ANTIBANDING_AUTO:
      gst_value_deserialize (&value, "auto");
      break;
    case UMD_VIDEO_ANTIBANDING_DISABLED:
      gst_value_deserialize (&value, "off");
      break;
    case UMD_VIDEO_ANTIBANDING_60HZ:
      gst_value_deserialize (&value, "60hz");
      break;
    case UMD_VIDEO_ANTIBANDING_50HZ:
      gst_value_deserialize (&value, "50hz");
      break;
    default:
      g_printerr ("\nUnsupported Antibanding mode: %d!\n", mode);
      return;
  }

  g_object_set_property (G_OBJECT (element), "antibanding", &value);
}

static void
get_antibanding_property (GstElement * element, guint8 * mode)
{
  GParamSpec *propspecs = NULL;
  GEnumClass *enumklass = NULL;
  GValue value = G_VALUE_INIT;
  GEnumValue *v = NULL;

  // Get the property specs to initialize the GValue type.
  propspecs = g_object_class_find_property (
      G_OBJECT_GET_CLASS (element), "antibanding");
  enumklass = G_ENUM_CLASS (g_type_class_ref (propspecs->value_type));

  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));
  g_object_get_property (G_OBJECT (element), "antibanding", &value);

  v = g_enum_get_value (enumklass, g_value_get_enum (&value));

  if (g_strcmp0 (v->value_nick, "off") == 0)
    *mode = UMD_VIDEO_ANTIBANDING_DISABLED;
  else if (g_strcmp0 (v->value_nick, "50hz") == 0)
    *mode = UMD_VIDEO_ANTIBANDING_50HZ;
  else if (g_strcmp0 (v->value_nick, "60hz") == 0)
    *mode = UMD_VIDEO_ANTIBANDING_60HZ;
  else if (g_strcmp0 (v->value_nick, "auto") == 0)
    *mode = UMD_VIDEO_ANTIBANDING_AUTO;

  g_type_class_unref (enumklass);
}

static void
set_iso_property (GstElement * element, guint16 isovalue)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, isovalue);

  g_object_set_property (G_OBJECT (element), "manual-iso-value", &value);
}

static void
get_iso_property (GstElement * element, guint16 * isovalue)
{
  GValue value = G_VALUE_INIT;

  g_value_init (&value, G_TYPE_INT);
  g_object_get_property (G_OBJECT (element), "manual-iso-value", &value);

  *isovalue = g_value_get_int (&value);
}

static void
set_zoom_property (GstElement * element, guint16 magnification,
    gint32 pan, gint32 tilt)
{
  GValue value = G_VALUE_INIT, v = G_VALUE_INIT;
  GstVideoRectangle sensor = {}, zoom = {};

  g_value_init (&value, GST_TYPE_ARRAY);
  g_object_get_property (G_OBJECT (element), "active-sensor-size", &value);

  sensor.x = g_value_get_int (gst_value_array_get_value (&value, 0));
  sensor.y = g_value_get_int (gst_value_array_get_value (&value, 1));
  sensor.w = g_value_get_int (gst_value_array_get_value (&value, 2));
  sensor.h = g_value_get_int (gst_value_array_get_value (&value, 3));

  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_ARRAY);

  g_object_get_property (G_OBJECT (element), "zoom", &value);

  zoom.x = g_value_get_int (gst_value_array_get_value (&value, 0));
  zoom.y = g_value_get_int (gst_value_array_get_value (&value, 1));
  zoom.w = g_value_get_int (gst_value_array_get_value (&value, 2));
  zoom.h = g_value_get_int (gst_value_array_get_value (&value, 3));

  zoom.w = (sensor.w - sensor.x) / (magnification / 100.0);
  zoom.h = (sensor.h - sensor.y) / (magnification / 100.0);

  // Normalize to degrees and shift range from 0-49 (25 is 0) to (-25)-24.
  pan = (pan / 3600) - 25;
  tilt = (tilt / 3600) - 25;

  zoom.x = ((sensor.w - sensor.x) - zoom.w) / 2;
  zoom.x += zoom.x * ((pan > 0) ? (pan / 24.0) : (pan / 25.0));

  zoom.y = ((sensor.h - sensor.y) - zoom.h) / 2;
  zoom.y += zoom.y * ((tilt > 0) ? (tilt / 24.0) : (tilt / 25.0));

  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_ARRAY);

  g_value_init (&v, G_TYPE_INT);

  g_value_set_int (&v, zoom.x);
  gst_value_array_append_value (&value, &v);

  g_value_set_int (&v, zoom.y);
  gst_value_array_append_value (&value, &v);

  g_value_set_int (&v, zoom.w);
  gst_value_array_append_value (&value, &v);

  g_value_set_int (&v, zoom.h);
  gst_value_array_append_value (&value, &v);

  g_object_set_property (G_OBJECT (element), "zoom", &value);
}

static void
get_zoom_property (GstElement * element, guint16 * magnification)
{
  GValue value = G_VALUE_INIT;
  GstVideoRectangle sensor = {}, zoom = {};

  g_value_init (&value, GST_TYPE_ARRAY);
  g_object_get_property (G_OBJECT (element), "zoom", &value);

  zoom.x = g_value_get_int (gst_value_array_get_value (&value, 0));
  zoom.y = g_value_get_int (gst_value_array_get_value (&value, 1));
  zoom.w = g_value_get_int (gst_value_array_get_value (&value, 2));
  zoom.h = g_value_get_int (gst_value_array_get_value (&value, 3));

  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_ARRAY);

  // Get the active sensor size in order to determine the magnification.
  g_object_get_property (G_OBJECT (element), "active-sensor-size", &value);

  sensor.x = g_value_get_int (gst_value_array_get_value (&value, 0));
  sensor.y = g_value_get_int (gst_value_array_get_value (&value, 1));
  sensor.w = g_value_get_int (gst_value_array_get_value (&value, 2));
  sensor.h = g_value_get_int (gst_value_array_get_value (&value, 3));

  // Zoom width and height of 0 means it is equal to the sensor size.
  zoom.w = (zoom.w == 0) ? sensor.w : zoom.w;
  zoom.h = (zoom.h == 0) ? sensor.h : zoom.h;

  *magnification = ((((gfloat) sensor.w / zoom.w) +
      ((gfloat) sensor.h / zoom.h)) / 2) * 100;
}

static bool
handle_camera_control (uint32_t ctrl, uint32_t request, void * payload,
    void * userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  GstElement *element = NULL;

  element = gst_bin_get_by_name (GST_BIN (srvctx->vpipeline), "camsrc");

  // Retrieve the property name corresponding the to control ID.
  switch (ctrl) {
    case UMD_VIDEO_CTRL_BRIGHTNESS:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_exposure_compensation_property (element, *((gint16*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_exposure_compensation_property (element, (gint16*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_CONTRAST:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_contrast_property (element, *((guint16*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_contrast_property (element, (guint16*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_SATURATION:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_saturation_property (element, *((guint16*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_saturation_property (element, (guint16*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_SHARPNESS:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_sharpness_property (element, *((guint16*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_sharpness_property (element, (guint16*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_BACKLIGHT_COMPENSATION:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_adrc_property (element, *((guint16*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_adrc_property (element, (guint16*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_ANTIBANDING:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_antibanding_property (element, *((guint8*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_antibanding_property (element, (guint8*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_GAIN:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_iso_property (element, *((guint16*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_iso_property (element, (guint16*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_WB_TEMPERTURE:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_wb_temperature_property (element, *((guint16*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_wb_temperature_property (element, (guint16*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_WB_MODE:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_wb_mode_property (element, *((guint8*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_wb_mode_property (element, (guint8*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_EXPOSURE_TIME:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_exposure_time_property (element, *((guint32*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_exposure_time_property (element, (guint32*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_EXPOSURE_MODE:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_exposure_mode_property (element, *((guint8*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_exposure_mode_property (element, (guint8*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_EXPOSURE_PRIORITY:
      break;
    case UMD_VIDEO_CTRL_FOCUS_MODE:
      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          set_focus_mode_property (element, *((guint8*) payload));
          break;
        case UMD_CTRL_GET_REQUEST:
          get_focus_mode_property (element, (guint8*) payload);
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    case UMD_VIDEO_CTRL_ZOOM:
    case UMD_VIDEO_CTRL_PANTILT:
    {
      // We need to cache the values.
      static gint32 pan = 0, tilt = 0;
      static guint16 magnification = 100;

      switch (request) {
        case UMD_CTRL_SET_REQUEST:
          if (ctrl == UMD_VIDEO_CTRL_ZOOM)
            magnification = *((guint16*) payload);

          if (ctrl == UMD_VIDEO_CTRL_PANTILT) {
            guint8 *data = (guint8*) payload;

            pan = (gint32) data[0] | (data[1] << 8) | (data[2] << 16) |
                (data[3] << 24);
            tilt = (gint32) data[4] | (data[5] << 8) | (data[6] << 16) |
                (data[7] << 24);
          }

          set_zoom_property (element, magnification, pan, tilt);
          break;
        case UMD_CTRL_GET_REQUEST:
          get_zoom_property (element, &magnification);

          if (ctrl == UMD_VIDEO_CTRL_ZOOM)
            *((guint16*) payload) = magnification;

          if (ctrl == UMD_VIDEO_CTRL_PANTILT) {
            guint8 *data = (guint8*) payload;

            data[0] = pan & 0xFF;
            data[1] = (pan >> 8) & 0xFF;
            data[2] = (pan >> 16) & 0xFF;
            data[3] = (pan >> 24) & 0xFF;

            data[4] = tilt & 0xFF;
            data[5] = (tilt >> 8) & 0xFF;
            data[6] = (tilt >> 16) & 0xFF;
            data[7] = (tilt >> 24) & 0xFF;
          }
          break;
        default:
          g_printerr ("\nUnknown control request 0x%X!\n", request);
          break;
      }
      break;
    }
    default:
      g_printerr ("\nUnknown control request 0x%X!\n", ctrl);
      break;
  }

  gst_object_unref (element);

  return true;
}

static gboolean
extract_integer_value (gchar * input, gint64 min, gint64 max, gint64 * value)
{
  // Convert string to integer value.
  gint64 newvalue = g_ascii_strtoll (input, NULL, 0);

  if (errno != 0) {
    g_printerr ("\nInvalid value format!\n");
    return FALSE;
  } else if (newvalue < min && newvalue > max) {
    g_printerr ("\nValue is outside range!\n");
    return FALSE;
  }

  *value = newvalue;
}

static gboolean
mle_ops_menu (GAsyncQueue * messages)
{
  GString *options = g_string_new (NULL);
  gchar *input = NULL;

  APPEND_MENU_HEADER (options);

  APPEND_CONTROLS_SECTION (options);
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      ML_FRAMING_ENABLE_OPTION, "ML Auto Framing",
      "Enable/Disable Machine Learning based auto framing algorithm");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      ML_FRAMING_POS_THOLD_OPTION, "Auto Framing Position Threshold",
      "Set the acceptable delta (in percent), between previous ROI position "
      "and current one, at which it is considered that the ROI has moved ");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      ML_FRAMING_DIMS_THOLD_OPTION, "Auto Framing Dimensions Threshold",
      "Set the acceptable delta (in percent), between previous ROI dimensions "
      "and current one, at which it is considered that ROI has been resized");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      ML_FRAMING_MARGINS_OPTION, "Auto Framing Margins",
      "Set additional margins (in percent) that will be used to increase the "
      "final size of the ROI rectangle");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      ML_FRAMING_SPEED_OPTION, "Auto Framing Speed",
      "Set the movement speed of the ROI rectangle");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      ML_FRAMING_CROPTYPE_OPTION, "Auto Framing Crop Type",
      "Set the type of cropping used for the ROI rectangle");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);

  g_print ("\n\nChoose an option: ");

  // If FALSE is returned termination signal has been issued.
  if (!wait_stdin_message (messages, &input))
    return FALSE;

  if (g_str_equal (input, ML_FRAMING_ENABLE_OPTION)) {
    gint64 value = afrmops.enable;

    g_print ("\nCurrent value: %d - [0 - disable, 1 - enable]\n",
        afrmops.enable);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 0, 1, &value);

    afrmops.enable = value;
  } else if (g_str_equal (input, ML_FRAMING_POS_THOLD_OPTION)) {
    gint64 value = afrmops.posthold;

    g_print ("\nCurrent value: %d - [0 - 100]\n", afrmops.posthold);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 0, 100, &value);

    afrmops.posthold = value;
  } else if (g_str_equal (input, ML_FRAMING_DIMS_THOLD_OPTION)) {
    gint64 value = afrmops.dimsthold;

    g_print ("\nCurrent value: %d - [0 - 100]\n", afrmops.dimsthold);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 0, 100, &value);

    afrmops.dimsthold = value;
  } else if (g_str_equal (input, ML_FRAMING_MARGINS_OPTION)) {
    gint64 value = afrmops.margins;

    g_print ("\nCurrent value: %d - [0 - 100]\n", afrmops.margins);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 0, 100, &value);

    afrmops.margins = value;
  } else if (g_str_equal (input, ML_FRAMING_SPEED_OPTION)) {
    gint64 value = afrmops.speed;

    g_print ("\nCurrent value: %d - [0 - 100]\n", afrmops.speed);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 0, 100, &value);

    afrmops.speed = value;
  } else if (g_str_equal (input, ML_FRAMING_CROPTYPE_OPTION)) {
    gint64 value = afrmops.croptype;

    g_print ("\nCurrent value: %d - [0 - internal, 1 - external]\n",
        afrmops.croptype);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 0, 1, &value);

    afrmops.croptype = value;
  }

  g_free (input);
  return TRUE;
}

static gpointer
main_menu (gpointer userdata)
{
  GstServiceContext *srvctx = GST_SERVICE_CONTEXT_CAST (userdata);
  gboolean active = TRUE;

  // Do now show main menu if Auto Framing Algorithm doesn't exist
  // TODO: needs rework if non-MLE options are added.
  if (NULL == srvctx->afrmalgo)
    active = FALSE;

  while (active)
    active = mle_ops_menu (srvctx->menumsgs);

  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstServiceContext *srvctx = gst_service_context_new ();
  GOptionContext *optsctx = NULL;
  GMainLoop *mloop = NULL;
  GIOChannel *iostdin = NULL;
  GThread *mthread = NULL;

  UmdVideoCallbacks callbacks = {
    &setup_camera_stream, &enable_camera_stream,
    &disable_camera_stream, &handle_camera_control
  };

  // Parse command line entries.
  if ((optsctx = g_option_context_new ("DESCRIPTION")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (optsctx, entries, NULL);
    g_option_context_add_group (optsctx, gst_init_get_option_group ());

    success = g_option_context_parse (optsctx, &argc, &argv, &error);
    g_option_context_free (optsctx);

    if (!success && (error != NULL)) {
      g_printerr ("\nFailed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);

      gst_service_context_free (srvctx);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("\nInitializing: Unknown error!\n");
      gst_service_context_free (srvctx);
      return -1;
    }
  } else {
    g_printerr ("\nFailed to create options context!\n");
    gst_service_context_free (srvctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  if (mainops.audio && !create_audio_pipeline (srvctx)) {
    g_printerr ("\nFailed to create audio pipeline!\n");
    gst_service_context_free (srvctx);
    return -1;
  }

  if (mainops.video && !create_video_pipeline (srvctx)) {
    g_printerr ("\nFailed to create video pipeline!\n");
    gst_service_context_free (srvctx);
    return -1;
  }

  // If a device is not to be initialized, NULL is passed for respective device
  srvctx->gadget = umd_gadget_new (mainops.video, mainops.audio, &callbacks, srvctx);
  if (NULL == srvctx->gadget) {
    g_printerr ("\nFailed to create UMD gadget!\n");
    gst_service_context_free (srvctx);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("Failed to create Main loop!\n");
    gst_service_context_free (srvctx);
    return -1;
  }

  // Register function for handling interrupt signals with the main loop.
  g_unix_signal_add (SIGINT, handle_interrupt_signal, mloop);

  // Create IO channel from the stdin stream.
  if ((iostdin = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("\nFailed to initialize STDIN channel!\n");
    gst_service_context_free (srvctx);
    return -1;
  }

  // Register handling function with the main loop for stdin channel data.
  g_io_add_watch (iostdin, G_IO_IN | G_IO_PRI, handle_stdin_source, srvctx);
  g_io_channel_unref (iostdin);

  // Initiate the main menu thread.
  if ((mthread = g_thread_new ("MainMenu", main_menu, srvctx)) == NULL) {
    g_printerr ("\nFailed to create event loop thread!\n");
    gst_service_context_free (srvctx);
    return -1;
  }

  // Run main loop.
  g_main_loop_run (mloop);

  // Signal pipeline to quit if it is waiting for EOS or state.
  g_async_queue_push (srvctx->pipemsgs,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  // Signal menu thread to quit.
  g_async_queue_push (srvctx->menumsgs,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  // Waits until main menu thread finishes.
  g_thread_join (mthread);

  g_main_loop_unref (mloop);
  gst_service_context_free (srvctx);

  gst_deinit ();

  return 0;
}
