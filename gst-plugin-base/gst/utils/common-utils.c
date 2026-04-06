/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "common-utils.h"

#include <json-glib/json-glib.h>

static const gchar* mux_stream_names[] = {
    "mux-stream-00", "mux-stream-01", "mux-stream-02", "mux-stream-03",
    "mux-stream-04", "mux-stream-05", "mux-stream-06", "mux-stream-07",
    "mux-stream-08", "mux-stream-09", "mux-stream-10", "mux-stream-11",
    "mux-stream-12", "mux-stream-13", "mux-stream-14", "mux-stream-15",
    "mux-stream-16", "mux-stream-17", "mux-stream-18", "mux-stream-19",
    "mux-stream-20", "mux-stream-21", "mux-stream-22", "mux-stream-23",
    "mux-stream-24", "mux-stream-25", "mux-stream-26", "mux-stream-27",
    "mux-stream-28", "mux-stream-29", "mux-stream-30", "mux-stream-31",
};

static void gst_value_array_from_json_node (GValue * value, JsonNode * node);
static GstStructure * gst_structure_from_json_node (JsonNode * node);

static gboolean gst_json_builder_set_value (JsonBuilder * builder,
    const GValue * value);
static gboolean gst_json_builder_structure_entry (GQuark field,
    const GValue * value, gpointer userdata);

static void
gst_value_array_from_json_node (GValue * value, JsonNode * node)
{
  JsonArray *jsonarray = NULL;
  guint idx = 0, length = 0;

  g_value_init (value, GST_TYPE_ARRAY);

  jsonarray = json_node_get_array (node);
  length = json_array_get_length (jsonarray);

  for (idx = 0; idx < length; idx++) {
    JsonNode *subnode = json_array_get_element (jsonarray, idx);
    GValue subvalue = G_VALUE_INIT;

    if (JSON_NODE_HOLDS_OBJECT (subnode)) {
      GstStructure *substructure = gst_structure_from_json_node (subnode);

      g_value_init (&subvalue, GST_TYPE_STRUCTURE);
      g_value_take_boxed (&subvalue, substructure);
    } else if (JSON_NODE_HOLDS_ARRAY (subnode)) {
      gst_value_array_from_json_node (&subvalue, subnode);
    } else if (JSON_NODE_HOLDS_VALUE (subnode)) {
      json_node_get_value (subnode, &subvalue);
    }

    gst_value_array_append_and_take_value (value, &subvalue);
  }
}

static GstStructure *
gst_structure_from_json_node (JsonNode * node)
{
  GstStructure *structure = NULL;
  JsonObject *object = NULL;
  JsonNode *subnode = NULL;
  JsonObjectIter iter = { 0, };
  const gchar *name = NULL;

  object = json_node_get_object (node);
  structure = gst_structure_new_empty ("Object");

  json_object_iter_init (&iter, object);

  while (json_object_iter_next (&iter, &name, &subnode)) {
    GValue value = G_VALUE_INIT;

    if (JSON_NODE_HOLDS_OBJECT (subnode)) {
      GstStructure *substructure = gst_structure_from_json_node (subnode);

      g_value_init (&value, GST_TYPE_STRUCTURE);
      g_value_take_boxed (&value, substructure);
    } else if (JSON_NODE_HOLDS_ARRAY (subnode)) {
      gst_value_array_from_json_node (&value, subnode);
    } else if (JSON_NODE_HOLDS_VALUE (subnode)) {
      json_node_get_value (subnode, &value);
    } else {
      // NULL JSON value, nothing to do.
      continue;
    }

    gst_structure_take_value (structure, name, &value);
  }

  return structure;
}

static gboolean
gst_json_builder_set_value (JsonBuilder * builder, const GValue * value)
{
  if (G_VALUE_HOLDS (value, G_TYPE_STRING)) {
    json_builder_add_string_value (builder, g_value_get_string (value));
  } else if (G_VALUE_HOLDS (value, G_TYPE_INT)) {
    json_builder_add_int_value (builder, g_value_get_int (value));
  } else if (G_VALUE_HOLDS (value, G_TYPE_UINT)) {
    json_builder_add_int_value (builder, g_value_get_uint (value));
  } else if (G_VALUE_HOLDS (value, G_TYPE_DOUBLE)) {
    json_builder_add_double_value (builder, g_value_get_double (value));
  } else if (G_VALUE_HOLDS (value, G_TYPE_FLOAT)) {
    json_builder_add_double_value (builder, g_value_get_float (value));
  } else if (G_VALUE_HOLDS (value, GST_TYPE_STRUCTURE)) {
    GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (value));

    json_builder_begin_object (builder);
    gst_structure_foreach (structure, gst_json_builder_structure_entry, builder);

    json_builder_end_object (builder);
  } else if (G_VALUE_HOLDS (value, GST_TYPE_ARRAY) ||
        G_VALUE_HOLDS (value, GST_TYPE_LIST)) {
    const GValue *val = NULL;
    guint idx = 0, length = 0;

    length = GST_VALUE_HOLDS_LIST (value) ? gst_value_list_get_size (value) :
        gst_value_array_get_size (value);
    json_builder_begin_array (builder);

    for (idx = 0; idx < length; idx++) {
      val = GST_VALUE_HOLDS_LIST (value) ? gst_value_list_get_value (value, idx) :
          gst_value_array_get_value (value, idx);

      gst_json_builder_set_value (builder, val);
    }

    json_builder_end_array (builder);
  } else {
    GST_ERROR ("Field has unknown value type!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_json_builder_structure_entry (GQuark field, const GValue * value,
    gpointer userdata)
{
  JsonBuilder *builder = (JsonBuilder*) (userdata);

  json_builder_set_member_name (builder, g_quark_to_string (field));
  return gst_json_builder_set_value (builder, value);
}

void
gst_class_label_reset (GstClassLabel * label)
{
  g_return_if_fail (label != NULL);

  label->name = 0;
  label->confidence = 0.0;
  label->color = 0;

  g_clear_pointer (&label->xtraparams, gst_structure_free);
}

const gchar *
gst_mux_stream_name (guint index)
{
  g_return_val_if_fail ((G_N_ELEMENTS (mux_stream_names) > index), NULL);
  return mux_stream_names[index];
}

gint
gst_mux_buffer_get_memory_stream_id (GstBuffer * buffer, gint mem_idx)
{
  gint num = -1;

  if (GST_BUFFER_OFFSET (buffer) == GST_BUFFER_OFFSET_NONE)
    return -1;

  // Find the set bit index corresponding to the given memory index.
  while (mem_idx >= 0)
    mem_idx -= ((GST_BUFFER_OFFSET (buffer) >> (++num)) & 0b01) ? 1 : 0;

  return num;
}

gboolean
gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;

  while (idx != gst_caps_get_size (caps)) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    if (feature == NULL && ((gst_caps_features_get_size (features) == 0) ||
            gst_caps_features_is_any (features)))
      return TRUE;

    // Skip ANY caps and return immediately if feature is present.
    if ((feature != NULL) && !gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;

    idx++;
  }
  return FALSE;
}

gboolean
gst_value_deserialize_file (GValue * value, const gchar * filename)
{
  GError *error = NULL;
  gchar *contents = NULL;
  gboolean success = FALSE;

  if (!g_file_get_contents (filename, &contents, NULL, &error)) {
    GST_ERROR ("Failed to get file contents, error: '%s'!",
        GST_STR_NULL (error->message));
    g_clear_error (&error);
    return FALSE;
  }

  // Remove trailing space and replace new lines with a comma delimiter.
  contents = g_strstrip (contents);
  contents = g_strdelimit (contents, "\n", ',');

  // Add opening and closing brackets if output value is of type list.
  if (G_VALUE_HOLDS (value, GST_TYPE_LIST)) {
    GString *string = g_string_new (contents);

    string = g_string_prepend (string, "{ ");
    string = g_string_append (string, " }");

    g_free (contents);

    // Get the raw character data.
    contents = g_string_free (string, FALSE);
  }

  success = gst_value_deserialize (value, contents);
  g_free (contents);

  if (!success)
    GST_ERROR ("Failed to deserialize file contents!");

  return success;
}

GstStructure *
gst_structure_from_json_file (const gchar * filename)
{
  GstStructure *structure = NULL;
  GError *error = NULL;
  gchar *contents = NULL;

  if (!g_file_get_contents (filename, &contents, NULL, &error)) {
    GST_ERROR ("Failed to get JSON file contents, error: '%s'!",
        GST_STR_NULL (error->message));
    g_clear_error (&error);
    return NULL;
  }

  // Remove trailing space and replace new lines with a empty space delimiter.
  contents = g_strstrip (contents);
  contents = g_strdelimit (contents, "\n", ' ');

  structure = gst_structure_from_json_string (contents);
  g_free (contents);

  return structure;
}

GstStructure *
gst_structure_from_json_string (const gchar * string)
{
  JsonParser *parser = json_parser_new ();
  JsonNode *root = NULL;
  GstStructure *structure = NULL;
  GError *error = NULL;

  if (!json_parser_load_from_data (parser, string, -1, &error)) {
    GST_ERROR ("Failed to parse JSON string, error: '%s'!",
        GST_STR_NULL (error->message));
    goto cleanup;
  }

  root = json_parser_get_root (parser);

  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    GST_ERROR ("JSON sring does not hold a object!");
    goto cleanup;
  }

  structure = gst_structure_from_json_node (root);

cleanup:
  if (error != NULL)
    g_clear_error (&error);

  g_object_unref (parser);
  return structure;
}

gchar *
gst_structure_to_json_string (GstStructure * structure)
{
  JsonBuilder *builder = NULL;
  JsonNode *root = NULL;
  JsonGenerator *generator = NULL;
  gchar *string = NULL;

  builder = json_builder_new ();

  json_builder_begin_object (builder);
  gst_structure_foreach (structure, gst_json_builder_structure_entry, builder);
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  generator = json_generator_new ();

  json_generator_set_root (generator, root);
  string = json_generator_to_data (generator, NULL);

  g_object_unref (generator);
  json_node_free (root);
  g_object_unref (builder);

  return string;
}

GstProtectionMeta *
gst_buffer_get_protection_meta_id (GstBuffer * buffer, const gchar * name)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_PROTECTION_META_API_TYPE))) {
    if (gst_structure_has_name (GST_PROTECTION_META_CAST (meta)->info, name))
      return GST_PROTECTION_META_CAST (meta);
  }

  return NULL;
}

void
gst_buffer_copy_protection_meta (GstBuffer * destination, GstBuffer * source)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (source, &state,
              GST_PROTECTION_META_API_TYPE))) {
    gst_buffer_add_protection_meta (destination,
        gst_structure_copy (GST_PROTECTION_META_CAST (meta)->info));
  }
}

#if GLIB_MAJOR_VERSION < 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 62)
GArray *
g_array_copy (GArray * array)
{
  GArray *newarray = NULL;
  guint size = 0;

  size = g_array_get_element_size (array);
  newarray = g_array_sized_new (FALSE, FALSE, size, array->len);

  newarray = g_array_set_size (newarray, array->len);
  memcpy (newarray->data, array->data, array->len * size);

  return newarray;
}
#endif // GLIB_MAJOR_VERSION < 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 62)

GParamSpec *
g_param_spec_copy (GParamSpec * param, const gchar * prefix)
{
  GParamSpec *new_param = NULL;
  const gchar *name = NULL, *nick = NULL, *blurb = NULL;
  gchar *new_name = NULL;
  GParamFlags flags = param->flags;

  // Remove flags as strings are not static and will not call property at register.
  flags &= ~(G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  name = g_param_spec_get_name (param);
  nick = g_param_spec_get_nick (param);
  blurb = g_param_spec_get_blurb (param);

  new_name = g_strconcat ((prefix != NULL) ? prefix : "", name, NULL);

  switch (G_PARAM_SPEC_VALUE_TYPE (param)) {
    case G_TYPE_UINT:
    {
      GParamSpecUInt *spec = G_PARAM_SPEC_UINT (param);

      new_param = g_param_spec_uint (new_name, nick, blurb, spec->minimum,
          spec->maximum, spec->default_value, flags);

      break;
    }
    case G_TYPE_INT:
    {
      GParamSpecInt *spec = G_PARAM_SPEC_INT (param);

      new_param = g_param_spec_int (new_name, nick, blurb, spec->minimum,
          spec->maximum, spec->default_value, flags);

      break;
    }
    case G_TYPE_UINT64:
    {
      GParamSpecUInt64 *spec = G_PARAM_SPEC_UINT64 (param);

      new_param = g_param_spec_uint64 (new_name, nick, blurb, spec->minimum,
          spec->maximum, spec->default_value, flags);

      break;
    }
    case G_TYPE_INT64:
    {
      GParamSpecInt64 *spec = G_PARAM_SPEC_INT64 (param);

      new_param = g_param_spec_int64 (new_name, nick, blurb, spec->minimum,
          spec->maximum, spec->default_value, flags);

      break;
    }
    case G_TYPE_FLOAT:
    {
      GParamSpecFloat *spec = G_PARAM_SPEC_FLOAT (param);

      new_param = g_param_spec_float (new_name, nick, blurb, spec->minimum,
          spec->maximum, spec->default_value, flags);

      break;
    }
    case G_TYPE_DOUBLE:
    {
      GParamSpecDouble *spec = G_PARAM_SPEC_DOUBLE (param);

      new_param = g_param_spec_double (new_name, nick, blurb, spec->minimum,
          spec->maximum, spec->default_value, flags);

      break;
    }
    case G_TYPE_BOOLEAN:
    {
      GParamSpecBoolean *spec = G_PARAM_SPEC_BOOLEAN (param);

      new_param = g_param_spec_boolean (new_name, nick, blurb,
          spec->default_value, flags);

      break;
    }
    case G_TYPE_STRING:
    {
      GParamSpecString *spec = G_PARAM_SPEC_STRING (param);

      new_param = g_param_spec_string (new_name, nick, blurb,
          spec->default_value, flags);

      break;
    }
    default:
      if (G_IS_PARAM_SPEC_ENUM (param)) {
        GParamSpecEnum *spec = G_PARAM_SPEC_ENUM (param);

        new_param = g_param_spec_enum (new_name, nick, blurb, param->value_type,
            spec->default_value, flags);
      } else if (param->value_type == GST_TYPE_ARRAY) {
        GstParamSpecArray *parray = GST_PARAM_SPEC_ARRAY_LIST (param);

        new_param = gst_param_spec_array (new_name, nick, blurb,
            parray->element_spec, flags);
      } else if (param->value_type == GST_TYPE_STRUCTURE) {
        new_param = g_param_spec_boxed (new_name, nick, blurb, param->value_type,
            flags);
      }

      break;
    }

  g_free (new_name);

  return new_param;
}
