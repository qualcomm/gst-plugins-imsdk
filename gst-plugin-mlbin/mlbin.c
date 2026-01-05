/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "mlbin.h"

#include <stdio.h>

#include <gst/ml/ml-info.h>
#include <gst/utils/common-utils.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

#define GST_ML_INFERENCE_PLUGIN_NAME G_PASTE (qtiml, GST_ML_BIN_CORE)
#define GST_ML_BIN_PLUGIN_NAME \
    G_PASTE (G_PASTE (qtiml, GST_ML_BIN_TYPE), G_PASTE (GST_ML_BIN_CORE, bin))
#define GST_CAT_DEFAULT gst_ml_bin_debug
GST_DEBUG_CATEGORY (gst_ml_bin_debug);

#define gst_ml_bin_parent_class parent_class
G_DEFINE_TYPE (GST_ML_BIN_STRUCT_NAME, gst_ml_bin, GST_TYPE_BIN);

#define GST_ML_BIN_SINK_CAPS \
    "video/x-raw(ANY)"

#define GST_ML_BIN_SRC_CAPS                    \
    "video/x-raw(ANY); "                       \
    "text/x-raw, format = (string) { utf8 }; " \
    "neural-network/tensors"

#define G_PARAM_SPEC_HAS_NAME(param, name) \
    (g_param_spec_get_name_quark (param) == g_quark_from_static_string (name))

enum {
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_ml_bin_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_ML_BIN_SINK_CAPS)
    );
static GstStaticPadTemplate gst_ml_bin_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_ML_BIN_SRC_CAPS)
    );


static gboolean
gst_caps_has_mimetype (const GstCaps * caps, const gchar * mimetype)
{
  GstStructure *structure = NULL;
  guint idx = 0, length = 0;

  length = gst_caps_get_size (caps);

  // Check what sets of caps id the downstream supporting.
  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (caps, idx);

    if (gst_structure_has_name (structure, mimetype))
      return TRUE;
  }

  return FALSE;
}

static void
gst_element_install_properties (GstElement * element, GObjectClass * mlbin_klass,
    const gchar * prefix, guint * prop_id)
{
  GParamSpec **propspecs = NULL;
  guint idx = 0, n_props = 0;

  propspecs =
      g_object_class_list_properties (G_OBJECT_GET_CLASS (element), &n_props);

  for (idx = 0; idx < n_props; idx++) {
    GParamSpec *param = propspecs[idx];
    GParamSpec *new_param = NULL;

    // Filter out default properties of GstBaseTransfom.
    if (G_PARAM_SPEC_HAS_NAME (param, "name") ||
        G_PARAM_SPEC_HAS_NAME (param, "parent") ||
        G_PARAM_SPEC_HAS_NAME (param, "qos"))
      continue;

    new_param = g_param_spec_copy (param, prefix);

    g_object_class_install_property (mlbin_klass, (*prop_id)++, new_param);
  }

  g_free (propspecs);
}

static void
gst_object_class_register_preprocess_properties (GObjectClass * klass,
    guint * prop_id)
{
  GstElementFactory *factory = NULL;
  GstElement *element = NULL;

  // Return immediately if element factory is not yet initlialized.
  if ((factory = gst_element_factory_find ("qtimlvconverter")) == NULL)
    return;

  element = gst_element_factory_create (factory, NULL);
  gst_element_install_properties (element, klass, "preprocess-", prop_id);

  gst_object_unref (element);
  gst_object_unref (factory);
}

static void
gst_object_class_register_inference_properties (GObjectClass * klass,
    guint * prop_id)
{
  GstElementFactory *factory = NULL;
  GstElement *element = NULL;

  factory = gst_element_factory_find (G_STRINGIFY (GST_ML_INFERENCE_PLUGIN_NAME));
  // Return immediately if element factory is not yet initlialized.
  if (factory == NULL)
    return;

  element = gst_element_factory_create (factory, NULL);
  gst_element_install_properties (element, klass, "inference-", prop_id);

  gst_object_unref (element);
  gst_object_unref (factory);
}

static void
gst_object_class_register_postprocess_properties (GObjectClass * klass,
    guint * prop_id)
{
  GstElementFactory *factory = NULL;
  GstElement *element = NULL;

  // Return immediately if post-process element factory is not yet initlialized.
  if ((factory = gst_element_factory_find ("qtimlpostprocess")) == NULL)
    return;

  element = gst_element_factory_create (factory, NULL);
  gst_element_install_properties (element, klass, "postprocess-", prop_id);

  gst_object_unref (element);
  gst_object_unref (factory);
}

static gboolean
gst_ml_bin_ghost_pad_set_target (GstMLBin * mlbin, GstGhostPad * gpad,
    GstElement * element, const gchar * padname)
{
  GstPad *pad = NULL;
  gboolean success = FALSE;

  // Link the element to the ghost/proxy pad of the bin.
  if ((pad = gst_element_get_static_pad (element, padname)) == NULL) {
    GST_ERROR_OBJECT (mlbin, "Failed to get static sink pad from '%s'!",
        GST_ELEMENT_NAME (element));
    return FALSE;
  }

  success = gst_ghost_pad_set_target (gpad, pad);
  gst_object_unref (pad);

  if (!success) {
    GST_ERROR_OBJECT (mlbin, "Failed to link '%s' with ghost/proxy pad '%s'!",
        GST_ELEMENT_NAME (element), GST_PAD_NAME (gpad));
    return FALSE;
  }

  GST_DEBUG_OBJECT (mlbin, "Linked ghost/proxy pad '%s' with the '%s' %s pad",
      GST_PAD_NAME (gpad), GST_ELEMENT_NAME (element), padname);

  return TRUE;
}

static GstElement *
gst_ml_bin_add_element (GstMLBin * mlbin, const gchar * factoryname,
    const gchar * name)
{
  GstElement *element = gst_element_factory_make (factoryname, name);

  if (element == NULL) {
    GST_ERROR_OBJECT (mlbin, "Failed to create '%s' with name '%s'!",
        factoryname, (name != NULL) ? name : "NULL");
    return NULL;
  }

  if (!gst_bin_add (GST_BIN (mlbin), element)) {
    GST_ERROR_OBJECT (mlbin, "Failed to add '%s'!", GST_ELEMENT_NAME (element));
    g_clear_pointer (&element, gst_object_unref);
  }

  return element;
}

static gboolean
gst_ml_bin_link_plugins (GstMLBin * mlbin)
{
  GstElement *mlpostprocess = NULL, *inqueue = NULL;
  GstGhostPad *gpad = NULL;
  GstCaps *caps = NULL;
  gboolean success = FALSE;

  mlpostprocess = gst_bin_get_by_name (GST_BIN (mlbin), "mlpostprocess");

  if (mlpostprocess == NULL)
    goto cleanup;

  if ((inqueue = gst_bin_get_by_name (GST_BIN (mlbin), "inqueue")) == NULL)
    goto cleanup;

  gpad = GST_GHOST_PAD_CAST (GST_ELEMENT (mlbin)->srcpads->data);
  caps = gst_pad_peer_query_caps (GST_PAD (gpad), NULL);

  if (gst_caps_has_mimetype (caps, "video/x-raw")) {
    GstElement *metamux = NULL, *tee = NULL, *filter = NULL;

    // Prefer muxing ML meta with the main buffer if output supports video caps.
    metamux = gst_ml_bin_add_element (mlbin, "qtimetamux", "metamux");

    if (!(success = (metamux != NULL)))
      goto cleanup;

    // Link the metamux element to the src ghost/proxy pad of the bin.
    if (!(success = gst_ml_bin_ghost_pad_set_target (mlbin, gpad, metamux, "src")))
      goto cleanup;

    // Filter caps between the post-processing and metamux elements.
    g_clear_pointer (&caps, gst_caps_unref);
    caps = gst_caps_new_simple ("text/x-raw", "format", G_TYPE_STRING,
        "utf8", NULL);

    filter = gst_ml_bin_add_element (mlbin, "capsfilter", "postprocess_filter");

    if (!(success = (filter != NULL)))
      goto cleanup;

    g_object_set (G_OBJECT (filter), "caps", caps, NULL);

    success = gst_element_link_many (mlpostprocess, filter, metamux, NULL);
    g_clear_pointer (&caps, gst_caps_unref);

    if (!success) {
      GST_ERROR_OBJECT (mlbin, "Failed to link post-processing with metamux!");
      goto cleanup;
    }

    GST_DEBUG_OBJECT (mlbin, "Linked: '%s' -> '%s'",
        GST_ELEMENT_NAME (mlpostprocess), GST_ELEMENT_NAME (metamux));

    // A tee element to split the input to 'mlpreprocess' and 'metamux'.
    tee = gst_ml_bin_add_element (mlbin, "tee", "mltee");

    if (!(success = (tee != NULL)))
      goto cleanup;

    gpad = GST_GHOST_PAD_CAST (GST_ELEMENT (mlbin)->sinkpads->data);

    // Link the input tee element to the sink ghost/proxy pad of the bin.
    if (!(success = gst_ml_bin_ghost_pad_set_target (mlbin, gpad, tee, "sink")))
      goto cleanup;

    if (!(success = gst_element_link (tee, metamux))) {
      GST_ERROR_OBJECT (mlbin, "Failed to link '%s' and '%s' elements!",
          GST_ELEMENT_NAME (tee), GST_ELEMENT_NAME (metamux));
      goto cleanup;
    }

    GST_DEBUG_OBJECT (mlbin, "Linked: '%s' -> '%s'", GST_ELEMENT_NAME (tee),
        GST_ELEMENT_NAME (metamux));

    // Establish linkage to the input queue of the pre-process element if present.
    if (!(success = gst_element_link (tee, inqueue))) {
      GST_ERROR_OBJECT (mlbin, "Failed to link 'mltee' and 'inqueue'!");
      goto cleanup;
    }

    GST_DEBUG_OBJECT (mlbin, "Linked: '%s' -> '%s'",
        GST_ELEMENT_NAME (tee), GST_ELEMENT_NAME (inqueue));
  } else {
    // Link the ghost/proxy pads of the bin to the appropriate elements.
    success = gst_ml_bin_ghost_pad_set_target (mlbin, gpad, mlpostprocess, "src");
    success &= gst_ml_bin_ghost_pad_set_target (mlbin, gpad, inqueue, "sink");
  }

cleanup:
  g_clear_pointer (&mlpostprocess, gst_object_unref);
  g_clear_pointer (&caps, gst_caps_unref);
  g_clear_pointer (&inqueue, gst_object_unref);

  return success;
}

static gboolean
gst_ml_bin_unlink_plugins (GstMLBin * mlbin)
{
  GstGhostPad *gpad = NULL;
  GstElement *tee = NULL, *metamux = NULL;
  gboolean success = TRUE;

  if ((tee = gst_bin_get_by_name (GST_BIN (mlbin), "mltee")) != NULL) {
    GstElement *queue = NULL;

    // Unlink the tee element from the sink ghost/proxy pad of the bin.
    gpad = GST_GHOST_PAD_CAST (GST_ELEMENT (mlbin)->sinkpads->data);
    gst_ghost_pad_set_target (gpad, NULL);

    gst_bin_remove (GST_BIN (mlbin), tee);
    GST_DEBUG_OBJECT (mlbin, "Removed: '%s'", GST_ELEMENT_NAME (tee));

    // Establish linkage between the input queue element and ghost pad.
    if ((queue = gst_bin_get_by_name (GST_BIN (mlbin), "inqueue")) != NULL)
      success &= gst_ml_bin_ghost_pad_set_target (mlbin, gpad, queue, "sink");

    g_clear_pointer (&queue, gst_object_unref);
  }

  if ((metamux = gst_bin_get_by_name (GST_BIN (mlbin), "metamux")) != NULL) {
    GstElement *postproc = NULL, *filter = NULL;

    // Unlink the metamux element from the src ghost/proxy pad of the bin.
    gpad = GST_GHOST_PAD_CAST (GST_ELEMENT (mlbin)->srcpads->data);
    gst_ghost_pad_set_target (gpad, NULL);

    gst_bin_remove (GST_BIN (mlbin), metamux);
    GST_DEBUG_OBJECT (mlbin, "Removed: '%s'", GST_ELEMENT_NAME (metamux));

    filter = gst_bin_get_by_name (GST_BIN (mlbin), "postprocess_filter");

    if (filter != NULL) {
      gst_bin_remove (GST_BIN (mlbin), filter);
      GST_DEBUG_OBJECT (mlbin, "Removed: '%s'", GST_ELEMENT_NAME (filter));
    }

    // Establish linkage between the post-process element and ghost pad.
    postproc = gst_bin_get_by_name (GST_BIN (mlbin), "mlpostprocess");

    if (postproc != NULL)
      success &= gst_ml_bin_ghost_pad_set_target (mlbin, gpad, postproc, "src");

    g_clear_pointer (&postproc, gst_object_unref);
    g_clear_pointer (&filter, gst_object_unref);
  }

  g_clear_pointer (&metamux, gst_object_unref);
  g_clear_pointer (&tee, gst_object_unref);

  return success;
}

static GstStateChangeReturn
gst_ml_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstMLBin *mlbin = GST_ML_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_ml_bin_link_plugins (mlbin))
        return GST_STATE_CHANGE_FAILURE;

      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_ml_bin_unlink_plugins (mlbin))
        return GST_STATE_CHANGE_FAILURE;

      break;
    default:
      break;
  }

  return ret;
}

static void
gst_ml_bin_constructed (GObject * object)
{
  GstMLBin *mlbin = GST_ML_BIN (object);
  GstElement *mlinference = NULL, *inmlqueue = NULL, *outmlqueue = NULL;
  GstElement *preprocess = NULL, *inqueue = NULL, *postprocess = NULL;
  gboolean success = FALSE;

  // A queue element between the 'mltee' and 'mlpreprocess'.
  inqueue = gst_ml_bin_add_element (mlbin, "queue", "inqueue");
  g_assert_nonnull (inqueue);

  // TODO: Expand to cover audio cases.
  preprocess = gst_ml_bin_add_element (mlbin, "qtimlvconverter", "mlpreprocess");
  g_assert_nonnull (preprocess);

  // A queue element between the 'mlpreprocess' and 'mlinference'.
  inmlqueue = gst_ml_bin_add_element (mlbin, "queue", "inmlqueue");
  g_assert_nonnull (inmlqueue);

  // Create the core inference element.
  mlinference = gst_ml_bin_add_element (mlbin,
      G_STRINGIFY (GST_ML_INFERENCE_PLUGIN_NAME), "mlinference");
  g_assert_nonnull (mlinference);

  // A queue element between the 'mlinference' and 'mlpostprocess' or ghost pad.
  outmlqueue = gst_ml_bin_add_element (mlbin, "queue", "outmlqueue");
  g_assert_nonnull (outmlqueue);

  postprocess =
      gst_ml_bin_add_element (mlbin, "qtimlpostprocess", "mlpostprocess");
  g_assert_nonnull (postprocess);

  success = gst_element_link_many (inqueue, preprocess, inmlqueue, mlinference,
      outmlqueue, postprocess, NULL);
  g_assert_true (success);

  GST_DEBUG_OBJECT (mlbin, "Linked: '%s' -> '%s' -> '%s' -> '%s' -> '%s' -> '%s'",
      GST_ELEMENT_NAME (inqueue), GST_ELEMENT_NAME (preprocess),
      GST_ELEMENT_NAME (inmlqueue), GST_ELEMENT_NAME (mlinference),
      GST_ELEMENT_NAME (outmlqueue), GST_ELEMENT_NAME (postprocess));
}

static void
gst_ml_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLBin *mlbin = GST_ML_BIN (object);
  GstElement *element = NULL;
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (mlbin);
  guint offset = 0;

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_ML_BIN_LOCK (mlbin);

  if (g_str_has_prefix (propname, "preprocess-")) {
    element = gst_bin_get_by_name (GST_BIN (mlbin), "mlpreprocess");
    offset = strlen ("preprocess-");
  } else if (g_str_has_prefix (propname, "inference-")) {
    element = gst_bin_get_by_name (GST_BIN (mlbin), "mlinference");
    offset = strlen ("inference-");
  } else if (g_str_has_prefix (propname, "postprocess-")) {
    element = gst_bin_get_by_name (GST_BIN (mlbin), "mlpostprocess");
    offset = strlen ("postprocess-");
  } else {
    GST_WARNING_OBJECT (mlbin, "Unknown property '%s'", propname);
  }

  GST_ML_BIN_UNLOCK (mlbin);

  if (element == NULL)
    return;

  g_object_set_property (G_OBJECT (element), propname + offset, value);
  gst_object_unref (element);
}

static void
gst_ml_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLBin *mlbin = GST_ML_BIN (object);
  GstElement *element = NULL;
  const gchar *propname = g_param_spec_get_name (pspec);
  guint offset = 0;

  GST_ML_BIN_LOCK (mlbin);

  if (g_str_has_prefix (propname, "preprocess-")) {
    element = gst_bin_get_by_name (GST_BIN (mlbin), "mlpreprocess");
    offset = strlen ("preprocess-");
  } else if (g_str_has_prefix (propname, "inference-")) {
    element = gst_bin_get_by_name (GST_BIN (mlbin), "mlinference");
    offset = strlen ("inference-");
  } else if (g_str_has_prefix (propname, "postprocess-")) {
    element = gst_bin_get_by_name (GST_BIN (mlbin), "mlpostprocess");
    offset = strlen ("postprocess-");
  } else {
    GST_WARNING_OBJECT (mlbin, "Unknown property '%s'", propname);
  }

  GST_ML_BIN_UNLOCK (mlbin);

  if (element == NULL)
    return;

  g_object_get_property (G_OBJECT (element), propname + offset, value);
  gst_object_unref (element);
}

static void
gst_ml_bin_finalize (GObject * object)
{
  GstMLBin *mlbin = GST_ML_BIN (object);

  g_mutex_clear (&mlbin->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (mlbin));
}

static void
gst_ml_bin_class_init (GstMLBinClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  guint prop_id = PROP_0 + 1;

  gobject->constructed = GST_DEBUG_FUNCPTR (gst_ml_bin_constructed);
  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_bin_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_bin_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_bin_finalize);

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_ml_bin_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_ml_bin_src_template));

  gst_object_class_register_preprocess_properties (gobject, &prop_id);
  gst_object_class_register_inference_properties (gobject, &prop_id);
  gst_object_class_register_postprocess_properties (gobject, &prop_id);

  gst_element_class_set_static_metadata (element,
      "Machine Learning Inference Bin", "Filter/Effect/Converter/Bin",
      "Machine Learning bin combining pre-processing, inference and post-processing",
      "QTI");

  element->change_state = GST_DEBUG_FUNCPTR (gst_ml_bin_change_state);
}

static void
gst_ml_bin_init (GstMLBin * mlbin)
{
  GstPadTemplate *template = NULL;
  GstPad *pad = NULL;

  g_mutex_init (&mlbin->lock);

  // Create sink proxy pad.
  template = gst_static_pad_template_get (&gst_ml_bin_sink_template);
  pad = gst_ghost_pad_new_no_target_from_template ("sink", template);

  gst_element_add_pad (GST_ELEMENT_CAST (mlbin), pad);
  gst_object_unref (template);

  // Create src proxy pad.
  template = gst_static_pad_template_get (&gst_ml_bin_src_template);
  pad = gst_ghost_pad_new_no_target_from_template ("src", template);

  gst_element_add_pad (GST_ELEMENT_CAST (mlbin), pad);
  gst_object_unref (template);

  g_warning ("Currently image-segmentation post-process modules are not supported!");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  // Initializes a new GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_ml_bin_debug, G_STRINGIFY (GST_ML_BIN_PLUGIN_NAME),
      0, "QTI Machine Learning Inference Bin");

  return gst_element_register (plugin, G_STRINGIFY (GST_ML_BIN_PLUGIN_NAME),
      GST_RANK_NONE, GST_TYPE_ML_BIN);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    GST_ML_BIN_PLUGIN_NAME,
    "QTI Machine Learnig Bin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
