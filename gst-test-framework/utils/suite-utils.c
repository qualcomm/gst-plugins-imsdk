/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <gst/pbutils/pbutils.h>

static void
gst_mp4_print_tags (const GstTagList *tags, gchar *tag) {
  GValue val = G_VALUE_INIT;
  gchar *str;

  if (gst_tag_list_copy_value (&val, tags, tag)) {
    if (G_VALUE_HOLDS_STRING (&val)) {
      str = g_value_dup_string (&val);
    } else {
      str = gst_value_serialize (&val);
    }
    GST_DEBUG ("MP4 Tag: %s Value: %s ", tag, str);
  }

  g_free (str);
  g_value_unset (&val);
}

static gboolean
gst_mp4_check_video_info (GstDiscovererStreamInfo *info,
    gint inwidth, gint inheight, gdouble infps, gdouble diff) {
  gboolean ret = FALSE;
  GList *tmp, *streams;
  streams = gst_discoverer_container_info_get_streams (
      GST_DISCOVERER_CONTAINER_INFO (info));

  for (tmp = streams; tmp; tmp = tmp->next) {
    GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *) tmp->data;

    if (GST_IS_DISCOVERER_VIDEO_INFO (tmpinf)) {
      GstDiscovererVideoInfo *video_info = GST_DISCOVERER_VIDEO_INFO (tmpinf);
      gint width = gst_discoverer_video_info_get_width (video_info);
      gint height = gst_discoverer_video_info_get_height (video_info);
      gdouble fps = gst_discoverer_video_info_get_framerate_num (video_info) /
          (gdouble) gst_discoverer_video_info_get_framerate_denom (video_info);

      // There is video stream, it is expected.
      ret = TRUE;
      // Check if Mp4 info is expected.
      GST_DEBUG ("Mp4 width: %d, height: %d, framerate: %.2f fps.",
          width, height, fps);
      // Check if video info is expected.
      if ((inwidth && inwidth != width) || (inwidth && inheight != height) ||
          (infps && (infps - fps) > diff)) {
        ret = FALSE;
        GST_WARNING ("Mp4 info width:%d[%d], height:%d[%d], fps:%.2f[%.2f] is"
            " not expected!", width, inwidth, height, inheight, fps, infps);
      }
    }
  }

  gst_discoverer_stream_info_list_free (streams);
  return ret;
}

gboolean
gst_mp4_verification (gchar *location, gint width, gint height,
    gdouble fps, gdouble diff, guint induration, gchar **codec) {
  gchar *codectype = NULL;
  gchar *expand_location = g_strconcat ("file://", location, NULL);
  gboolean ret = TRUE;

  if (!g_file_test (location, G_FILE_TEST_EXISTS)) {
    ret = FALSE;
    goto DONE;
  }

  GstDiscoverer *discoverer = gst_discoverer_new (GST_SECOND, NULL);
  if (!discoverer) {
    ret = FALSE;
    goto DONE;
  }

  GstDiscovererInfo *info = gst_discoverer_discover_uri (discoverer,
    expand_location, NULL);
  GST_DEBUG ("Done discovering %s", gst_discoverer_info_get_uri (info));

  if (info) {
    GstClockTime duration = gst_discoverer_info_get_duration (info);
    GST_DEBUG ("Duration: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (duration));

    // Check Mp4 duration.
    if (induration && (guint) duration != induration) {
      ret = FALSE;
      goto DONE;
    }

    const GstTagList *tags = gst_discoverer_info_get_tags (info);
    if (tags) {
      gst_tag_list_foreach (tags, gst_mp4_print_tags, NULL);
      gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &codectype);
      if (*codec == NULL) {
        *codec = g_strdup (codectype);
      } else if (codectype && !g_str_has_prefix(codectype, *codec)) {
        GST_WARNING ("video-codec:%s verify failed with %s.",
            codectype, *codec);
        ret = FALSE;
        goto DONE;
      }
    } else {
      ret = FALSE;
      GST_WARNING ("Mp4 tags are not found.");
      goto DONE;
    }

    GstDiscovererStreamInfo *sinfo = gst_discoverer_info_get_stream_info (info);
    if (sinfo) {
      if (!gst_mp4_check_video_info (sinfo, width, height, fps, diff)) {
        ret = FALSE;
        goto DONE;
      }
    } else {
      GST_WARNING ("Mp4 streams are not found.");
      ret = FALSE;
      goto DONE;
    }
  } else {
    GST_WARNING ("Mp4 info is not found.");
    ret = FALSE;
    goto DONE;
  }

DONE:
  g_free (codectype);
  g_free (expand_location);

  if (info)
    gst_discoverer_info_unref (info);

  if (discoverer)
    g_object_unref (discoverer);

  return ret;
}

void
gst_element_on_pad_added (GstElement *element, GstPad *pad, gpointer data)
{
  GstPad *sinkpad = gst_element_get_static_pad ((GstElement *)data, "sink");
  if (gst_pad_is_linked (sinkpad)) {
    g_object_unref (sinkpad);
    return;
  }

  GstCaps *padcaps = gst_pad_get_current_caps (pad);
  GstStructure *padstruct = gst_caps_get_structure (padcaps, 0);
  const gchar *padtype = gst_structure_get_name (padstruct);

  if (g_str_has_prefix (padtype, "video/x-h264") ||
      g_str_has_prefix (padtype, "video/x-h265")) {
    gst_pad_link(pad, sinkpad);
  }

  gst_caps_unref (padcaps);
  g_object_unref (sinkpad);
}

gboolean
gst_element_send_eos (GstElement *element)
{
  GstMessage *msg;
  gboolean ret = TRUE;

  gst_element_send_event (element, gst_event_new_eos ());
  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (element),
      1 * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  // Expect EOS message.
  if (msg == NULL || GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS)
    ret = FALSE;

  if (msg)
    gst_message_unref (msg);

  return ret;
}

void
gst_destroy_pipeline (GstElement **pipeline, GList **plugins)
{
  GstElement * element_1;
  GstElement * element_2;

  g_return_if_fail (*pipeline != NULL);
  g_return_if_fail (*plugins != NULL);

  element_1 = GST_ELEMENT_CAST (g_list_nth_data (*plugins, 0));
  if (!element_1) {
    GST_WARNING ("First element in plugins list is NULL!");
    goto cleanup;
  }
  GList *list = (GList*)(*plugins)->next;

  for ( ; list != NULL; list = list->next) {
    element_2 = GST_ELEMENT_CAST (list->data);
    if (!element_2) {
      GST_WARNING ("Found NULL element in plugins list!");
      continue;
    }
    gst_element_unlink (element_1, element_2);
    gst_bin_remove (GST_BIN (*pipeline), element_1);
    element_1 = element_2;
  }
  gst_bin_remove (GST_BIN (*pipeline), element_1);

cleanup:
  g_list_free (*plugins);
  *plugins = NULL;
  gst_object_unref (*pipeline);
}
