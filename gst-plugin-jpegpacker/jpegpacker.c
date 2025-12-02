/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "jpegpacker.h"

#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>

#include <gst/utils/common-utils.h>
#include "jpegpacker-utils.h"

#define GST_CAT_DEFAULT gst_jpeg_packer_debug
GST_DEBUG_CATEGORY (gst_jpeg_packer_debug);

G_DEFINE_TYPE (GstJpegPacker, gst_jpeg_packer, GST_TYPE_ELEMENT);

#define DEFAULT_PROP_PACK_TYPE PACK_TYPE_EXIF

#define GST_TYPE_JPEG_PACKER_PACK_TYPE (gst_jpeg_packer_pack_type_get_type())

enum {
  PROP_0,
  PROP_PACK_TYPE,
};

typedef struct _GstJpegSection
{
  /// Section Identifier(0xFF), 1 Byte

  /// Section Type, 1 Byte
  guint8       type;

  /// Section Size, 2 Bytes
  guint16      size;

  /// Section Data
  const guint8 *data;

  /// True if data is newly allocated, need free after usage
  gboolean     owned;
} GstJpegSection;

static GType
gst_jpeg_packer_pack_type_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { PACK_TYPE_EXIF,
        "JPEG interchange format: EXIF", "exif"
    },
    { PACK_TYPE_JFIF,
        "JPEG interchange format: JFIF", "jfif"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstPackerType", variants);

  return gtype;
}

static GstStaticPadTemplate gst_jpeg_packer_src_pad_template =
  GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("image/jpeg, "
          "width = (int) [ 1, 65535 ], "
          "height = (int) [ 1, 65535 ], "
          "framerate = (fraction) [ 0/1, MAX ]"));

static GstStaticPadTemplate gst_jpeg_packer_sink_pad_template =
  GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_REQUEST,
      GST_STATIC_CAPS ("image/jpeg, "
          "width = (int) [ 1, 65535 ], "
          "height = (int) [ 1, 65535 ], "
          "framerate = (fraction) [ 0/1, MAX ]"));


static GstJpegSection*
gst_jpeg_section_new (guint8 type, guint16 size, const guint8 *data,
    gboolean owned)
{
  GstJpegSection* section = g_slice_new (GstJpegSection);

  section->type = type;
  section->size = size;
  section->data = data;
  section->owned = owned;

  return section;
}

static void
gst_jpeg_section_free (gpointer data, gpointer user_data)
{
  GstJpegSection *section = (GstJpegSection*)data;
  GstJpegPacker *packer = (GstJpegPacker *)user_data;

  if (!section)
    return;

  switch (section->type) {
    case JPEG_MARKER_APP0:
      if (section->owned && packer &&
          (section->size - 2 - packer->thumbnail_size == 6))
        // APP0 section with exif extension data
        jfif_data_ext_free ((guint8 *)section->data);
      else if (section->owned && (section->size - 2 == 14))
        // APP0 section with exif data
        jfif_data_free ((guint8 *)section->data);

      break;
    case JPEG_MARKER_APP1:
      if (section->owned)
        exif_free ((guint8 *)section->data);

      break;
    default:
      break;
  }

  g_slice_free (GstJpegSection, section);
}

static void
gst_jpeg_packer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstJpegPacker *packer = GST_JPEG_PACKER (object);
  GstState state = GST_STATE (packer);
  const gchar *propname = g_param_spec_get_name (pspec);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  switch (prop_id) {
    case PROP_PACK_TYPE:
      packer->pack_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_jpeg_packer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstJpegPacker *packer = GST_JPEG_PACKER (object);

  switch (prop_id) {
    case PROP_PACK_TYPE:
      g_value_set_enum (value, packer->pack_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_jpeg_packer_finalize (GObject * object)
{
  GstJpegPacker *packer = GST_JPEG_PACKER (object);

  if (packer->collectpad)
    g_object_unref (packer->collectpad);

  if (packer->buffers)
    g_async_queue_unref (packer->buffers);

  if (packer->sections) {
    g_list_foreach (packer->sections, gst_jpeg_section_free, packer);
    g_list_free (packer->sections);
    packer->sections = NULL;
  }

  packer->primary_data = NULL;
  packer->thumbnail_data = NULL;

  G_OBJECT_CLASS (gst_jpeg_packer_parent_class)->finalize (object);
}

static void
gst_jpeg_packer_collectdata_destroy (GstCollectData * data)
{
  // Empty implementation
}

static GstPad*
gst_jpeg_packer_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstJpegPacker *packer = GST_JPEG_PACKER (element);
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstPadTemplate *sinkpad_templ = NULL;
  GstPad *sinkpad = NULL;

  sinkpad_templ = gst_element_class_get_pad_template (klass, "sink");
  if (templ != sinkpad_templ) {
    GST_ERROR_OBJECT (packer, "Invalid pad template");
    return NULL;
  }

  sinkpad = gst_pad_new (NULL, GST_PAD_SINK);
  g_return_val_if_fail (sinkpad != NULL, NULL);

  gst_pad_set_active (sinkpad, TRUE);

  if (!gst_collect_pads_add_pad (packer->collectpad,
      sinkpad, sizeof (GstCollectData),
      (GstCollectDataDestroyNotify) gst_jpeg_packer_collectdata_destroy, TRUE)) {
    GST_ERROR_OBJECT (packer, "Failed to add sinkpad");

    gst_pad_set_active (sinkpad, FALSE);
    gst_pad_set_query_function (sinkpad, NULL);
    gst_pad_set_event_function (sinkpad, NULL);
    gst_pad_set_activatemode_function (sinkpad, NULL);

    g_object_unref (sinkpad);
    return NULL;
  }

  gst_element_add_pad (element, GST_PAD (sinkpad));

  return sinkpad;
}

static void
gst_jpeg_packer_release_pad (GstElement * element, GstPad * pad)
{
  GstJpegPacker *packer = GST_JPEG_PACKER (element);

  if (!gst_collect_pads_remove_pad (packer->collectpad, pad)) {
    GST_ERROR_OBJECT (packer, "CollectPads failed to remove pad");
    return;
  }

  gst_element_remove_pad (element, pad);

  gst_object_unref (pad);
}

/*
  JPEG Section Spec
    -------------------------------
    | Head | Type | Length | Data |
    -------------------------------

    Head:   1 byte,
            fixed value (0xFF), the start of one section.
    Type:   1 byte,
            the pre-defined type of one section seen in JPEG spec.
    Length: 2 bytes,
            the size of Length + Data.
    Data:   variable bytes,
            data of section.
*/
static gboolean
gst_jpeg_packer_parse_image (GstBuffer **buffer, guint idx, gpointer user_data)
{
  GstJpegPacker *packer = NULL;
  GstBuffer *buf = NULL;
  GstJpegSection *section = NULL;
  GstByteReader reader;
  GstMapInfo map;
  const guint8 *data = NULL;
  guint8 marker = 0;
  guint16 section_size = 0;
  gint eoi_pos = -1;

  g_return_val_if_fail (*buffer != NULL, FALSE);
  g_return_val_if_fail (user_data != NULL, FALSE);

  buf = *buffer;
  packer = (GstJpegPacker *)user_data;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  gst_byte_reader_init (&reader, map.data, map.size);

  GST_LOG_OBJECT (packer, "Received buffer of size: %" G_GSIZE_FORMAT, map.size);

  // Retrieve EOI position
  if (map.size < 5) {
    GST_ERROR_OBJECT (packer, "Buffer too small (size: %" G_GSIZE_FORMAT ")",
        map.size);
    gst_buffer_unmap (buf, &map);
    return FALSE;
  }

  for (gint i = map.size - 5; i >= 0 && i + 1 < (gint)map.size; ++i) {
    if (map.data[i] == 0xFF && map.data[i+1] == JPEG_MARKER_EOI) {
      eoi_pos = i;
      break;
    }
  }
  if (eoi_pos == -1) {
    GST_WARNING_OBJECT (packer, "Couldn't find an EOI marker");
    eoi_pos = map.size;
  }

  if (!gst_byte_reader_peek_uint8 (&reader, &marker))
    goto error;

  if (idx == 0) {
    // Primary image
    while (marker == 0xFF) {
      if (!gst_byte_reader_skip (&reader, 1))
        goto error;

      if (!gst_byte_reader_get_uint8 (&reader, &marker))
        goto error;

      switch (marker) {
        case JPEG_MARKER_SOI:
        case JPEG_MARKER_EOI:
          section = gst_jpeg_section_new (marker, 0, NULL, FALSE);
          packer->sections = g_list_append (packer->sections, section);

          GST_DEBUG_OBJECT (packer, "marker = %2x", section->type);
          break;
        default:
          if (!gst_byte_reader_get_uint16_be (&reader, &section_size))
            goto error;

          if (!gst_byte_reader_get_data (&reader, section_size - 2, &data))
            goto error;

          section = gst_jpeg_section_new (marker, section_size, data, FALSE);
          packer->sections = g_list_append (packer->sections, section);

          GST_DEBUG_OBJECT (packer, "marker = %2x, size = %u", section->type,
              section->size);
          break;
      }

      if (marker == JPEG_MARKER_EOI) {
        GST_DEBUG_OBJECT (packer, "done parsing at 0x%x / 0x%x",
            gst_byte_reader_get_pos (&reader), (guint) map.size);
        break;
      } else if (marker == JPEG_MARKER_SOS) {
        // Scan data
        packer->primary_size = eoi_pos - gst_byte_reader_get_pos (&reader);

        if (!gst_byte_reader_get_data (&reader, packer->primary_size,
            &packer->primary_data))
          goto error;

        GST_DEBUG_OBJECT (packer, "Primary data, size = %u", packer->primary_size);
      }

      if (!gst_byte_reader_peek_uint8 (&reader, &marker))
        goto error;
    }
  } else {
    // Thumbnail image
    packer->thumbnail_size = map.size;
    if (packer->thumbnail_size > 0xFFFD) {
      GST_ERROR_OBJECT (packer, "Thumbnail(%u) exceeds maximum size(0xFFFD)",
          packer->thumbnail_size);

      gst_buffer_unmap (buf, &map);
      return FALSE;
    }

    if (!gst_byte_reader_get_data (&reader, packer->thumbnail_size,
        &packer->thumbnail_data))
      goto error;

    GST_DEBUG_OBJECT (packer, "Thumbnail data, size = %u", packer->thumbnail_size);
  }

  gst_buffer_unmap (buf, &map);

  return TRUE;

error:
  GST_WARNING_OBJECT (packer, "Error parsing image header (need more that "
    "%u bytes available)", gst_byte_reader_get_remaining (&reader));

  gst_buffer_unmap (buf, &map);

  return FALSE;
}

static gboolean
gst_jpeg_packer_mangle (GstJpegPacker *packer)
{
  GstJpegSection *section = NULL, *sec_tmp = NULL;
  GList *iter = NULL, *iter_tmp = NULL;
  guint8 *section_data = NULL;
  guint16 section_size = 0;

  g_return_val_if_fail (packer != NULL, FALSE);

  // Jpeg sections should contain SOI, SOS and EOI at least
  if (g_list_length (packer->sections) < 3) {
    GST_ERROR_OBJECT (packer, "Wrong parsing results");
    return FALSE;
  }

  /* Make sure sections orders (SOI - others - EOI) */
  // SOI
  iter = g_list_first (packer->sections);
  while (iter != NULL) {
    sec_tmp = (GstJpegSection *) iter->data;

    if (sec_tmp->type == JPEG_MARKER_SOI) {
      if (iter == g_list_first (packer->sections)) {
        break;
      } else {
        // Move SOI as head
        packer->sections = g_list_remove_link (packer->sections, iter);
        packer->sections = g_list_prepend (packer->sections, iter->data);
      }

      GST_DEBUG_OBJECT (packer, "SOI section index: %d",
          g_list_position (packer->sections, iter));
      break;
    }

    iter = g_list_next (iter);
  }
  // EOI
  iter = g_list_last (packer->sections);
  while (iter != NULL) {
    sec_tmp = (GstJpegSection *) iter->data;

    if (sec_tmp->type == JPEG_MARKER_EOI) {
      if (iter == g_list_last (packer->sections)) {
        break;
      } else {
        // Move EOI as tail
        packer->sections = g_list_remove_link (packer->sections, iter);
        packer->sections = g_list_append (packer->sections, iter->data);
      }

      GST_DEBUG_OBJECT (packer, "EOI section index: %d",
          g_list_position (packer->sections, iter));
      break;
    }

    iter = g_list_previous (iter);
  }

  // Clean original APPx section
  iter = g_list_first (packer->sections);
  switch (packer->pack_type) {
    case PACK_TYPE_EXIF:
      // Remove all APP0 sections which contain jfif data
      while (iter != NULL) {
        iter_tmp = iter->next;
        sec_tmp = (GstJpegSection *)iter->data;

        if (sec_tmp->type == JPEG_MARKER_APP0) {
          gst_jpeg_section_free (sec_tmp, NULL);
          packer->sections = g_list_delete_link (packer->sections, iter);

          GST_DEBUG_OBJECT (packer, "Cleaned original APP0 section for EXIF");
        }

        iter = iter_tmp;
      }
      break;
    case PACK_TYPE_JFIF:
      // Remove all APP1 sections which contain exif data
      while (iter != NULL) {
        iter_tmp = iter->next;
        sec_tmp = (GstJpegSection *)iter->data;

        if (sec_tmp->type == JPEG_MARKER_APP1) {
          gst_jpeg_section_free (sec_tmp, NULL);
          packer->sections = g_list_delete_link (packer->sections, iter);

          GST_DEBUG_OBJECT (packer, "Cleaned original APP1 section for JFIF");
        }

        iter = iter_tmp;
      }
      break;
    default:
      break;
  }

  // Create APPx section
  switch (packer->pack_type) {
    case PACK_TYPE_EXIF:
      // Retrieve original APP1 section
      iter = g_list_first (packer->sections);
      sec_tmp = NULL;
      while (iter != NULL) {
        sec_tmp = (GstJpegSection *) iter->data;
        if (sec_tmp->type == JPEG_MARKER_APP1)
          break;

        iter = g_list_next (iter);
      }

      if (!iter)
        GST_WARNING_OBJECT (packer, "Failed to retrieve APP1 section");

      // Create new exif data in case we have thumbnail data to fill
      if (packer->thumbnail_size && packer->thumbnail_data != NULL) {
        if (!exif_data_create ((sec_tmp? sec_tmp->size: 0),
            (sec_tmp? sec_tmp->data : NULL), packer->thumbnail_size,
            &section_size, &section_data)) {
          GST_ERROR_OBJECT (packer, "Failed to create EXIF data");
          return FALSE;
        }
        GST_DEBUG_OBJECT (packer, "Created EXIF data size: %u", section_size);

        /*
          APP1 section size spec
            Identifier: 1 byte (0xFF).
            Type: 1 byte.
            Length: 2 bytes.
            Data: variable bytes,
                  EXIF data size + Thumbnail data size.
        */
        if ((section_size + packer->thumbnail_size) > 0xFFFD) {
          GST_WARNING_OBJECT (packer, "APP1 exceeds maximum size, cut");
          packer->thumbnail_size = 0xFFFD - section_size;
        }

        section_size += packer->thumbnail_size;

        section = gst_jpeg_section_new (JPEG_MARKER_APP1, section_size + 2,
            section_data, TRUE);
        if (!section) {
          GST_ERROR_OBJECT (packer, "Failed to create APP1 section");

          exif_free ((guint8 *)section_data);
          return FALSE;
        }

        // Remove original APP1 section
        if (iter && section) {
          gst_jpeg_section_free (sec_tmp, NULL);
          packer->sections = g_list_delete_link (packer->sections, iter);

          GST_DEBUG_OBJECT (packer, "Removed original APP1 section");
        }

        // APP1 section should be inserted right after SOI
        packer->sections = g_list_insert (packer->sections, section, 1);

        GST_INFO_OBJECT (packer, "Added marker = %2x, size = %u", section->type,
            section->size);
      }
      break;
    case PACK_TYPE_JFIF:
      // Retrieve original APP0 section
      iter = g_list_first (packer->sections);
      while (iter != NULL) {
        sec_tmp = (GstJpegSection *) iter->data;
        if (sec_tmp->type == JPEG_MARKER_APP0)
          break;

        iter = g_list_next (iter);
      }

      if (!iter) {
        GST_DEBUG_OBJECT (packer, "No original APP0 section");

        if (!jfif_data_create (&section_size, &section_data)) {
          GST_ERROR_OBJECT (packer, "Failed to create JFIF data");
          return FALSE;
        }
        GST_DEBUG_OBJECT (packer, "Created JFIF data size: %u", section_size);

        /*
          APP0 section size spec
            Identifier: 1 byte (0xFF).
            Type: 1 byte.
            Length: 2 bytes.
            Data: 14 bytes =
                  5 bytes ("JFIF\0") + 2 bytes (version) + 1 byte (units) +
                  4 bytes (pixel density) + 2 bytes (thumbnail pixel count).
        */
        if (section_size > 0xFFFD) {
          GST_WARNING_OBJECT (packer, "APP0 exceeds maximum size, cut");
          section_size = 0xFFFD;
        }

        section = gst_jpeg_section_new (JPEG_MARKER_APP0, section_size + 2,
            section_data, TRUE);
        if (!section) {
          GST_ERROR_OBJECT (packer, "Failed to create APP0 section");

          jfif_data_free (section_data);
          return FALSE;
        }

        packer->sections = g_list_insert (packer->sections, section, 1);

        GST_INFO_OBJECT (packer, "Added marker = %2x, size = %u", section->type,
            section->size);
      }

      if (packer->thumbnail_size && packer->thumbnail_data != NULL) {
        // Retrieve APP0 extension section, skip SOI and APP0 section
        iter = g_list_nth (packer->sections, 2);
        while (iter != NULL) {
          sec_tmp = (GstJpegSection *) iter->data;
          if (sec_tmp->type == JPEG_MARKER_APP0)
            break;

          iter = g_list_next (iter);
        }

        if (!iter)
          GST_DEBUG_OBJECT (packer, "No original APP0 extension section");

        if (!jfif_data_ext_create (&section_size, &section_data)) {
          GST_ERROR_OBJECT (packer, "Failed to create JFIF extension data");
          return FALSE;
        }
        GST_DEBUG_OBJECT (packer, "Created JFIF extension size: %u", section_size);

        /*
          APP0 extension section size spec
            Identifier: 1 byte (0xFF).
            Type: 1 byte.
            Length: 2 bytes.
            Data: 6 bytes + variable bytes =
                  5 bytes ("JFXX\0\0") + 1 byte (ext code) + Thumbnail data size.
        */
        if ((section_size + packer->thumbnail_size) > 0xFFFD) {
          GST_WARNING_OBJECT (packer, "APP0 exceeds maximum size, cut");

          packer->thumbnail_size = 0xFFFD - section_size;
        }

        section_size += packer->thumbnail_size;

        section = gst_jpeg_section_new (JPEG_MARKER_APP0, section_size + 2,
            section_data, TRUE);
        if (!section) {
          GST_ERROR_OBJECT (packer, "Failed to create APP0 section");

          jfif_data_ext_free (section_data);
          return FALSE;
        }

        if (iter && section) {
          gst_jpeg_section_free (sec_tmp, NULL);
          packer->sections = g_list_delete_link (packer->sections, iter);

          GST_DEBUG_OBJECT (packer, "Removed original APP0 extention section");
        }

        // APP0 extension section should be inserted right after APP0 section
        packer->sections = g_list_insert (packer->sections, section, 2);

        GST_INFO_OBJECT (packer, "Added marker = %2x, size = %u", section->type,
            section->size);
      }
      break;
    default:
      GST_WARNING_OBJECT (packer, "Unsupported type");
      break;
  }

  return TRUE;
}

static GstBuffer *
gst_jpeg_packer_recombine (GstJpegPacker *packer, GstBufferList *bufs)
{
  GstJpegSection *section = NULL;
  GstBuffer *buffer = NULL;
  GstByteWriter *writer;
  GstMapInfo map;
  GList *list = NULL, *r_maps = NULL;
  guint size = 0;

  g_return_val_if_fail (packer != NULL, NULL);
  g_return_val_if_fail (bufs != NULL, NULL);
  g_return_val_if_fail (packer->sections != NULL, NULL);

  // Check SOI and EOI
  list = g_list_first (packer->sections);
  section = (GstJpegSection *)list->data;
  if (section->type != JPEG_MARKER_SOI) {
    GST_ERROR_OBJECT (packer, "SOI is not the first one");
    return NULL;
  }

  list = g_list_last (packer->sections);
  section = (GstJpegSection *)list->data;
  if (section->type != JPEG_MARKER_EOI) {
    GST_ERROR_OBJECT (packer, "EOI is not the last one");
    return NULL;
  }

  // Calculate total size
  list = g_list_first (packer->sections);
  while (list != NULL) {
    section = (GstJpegSection *)list->data;
    /*
      Jpeg section size
        SOI:    (2 + 0) Bytes
        EOI:    (2 + 0) Bytes
        Others: (2 + size) Bytes
    */
    size += (section->size)? (2 + section->size): 2;
    list = g_list_next (list);
  }
  size += (packer->primary_size + packer->thumbnail_size);

  buffer = gst_buffer_new_allocate (NULL, size, NULL);
  if (!buffer) {
    GST_ERROR_OBJECT (packer, "Failed to allocate a new GstBuffer");
    return NULL;
  }
  GST_DEBUG_OBJECT (packer, "Allocated size %" G_GSIZE_FORMAT,
      gst_buffer_get_size (buffer));

  // Copy buffer metadata
  gst_buffer_copy_into (buffer, gst_buffer_list_get (bufs, 0),
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  writer = gst_byte_writer_new_with_data (map.data, map.size, TRUE);

  // Input buffers are mapped again to copy data
  for (guint i = 0; i < gst_buffer_list_length (bufs); ++i) {
    GstMapInfo *m = g_new0 (GstMapInfo, 1);

    gst_buffer_map (gst_buffer_list_get (bufs, i), m, GST_MAP_READ);
    r_maps = g_list_append (r_maps, m);
  }

  // Write data to output buffer
  list = g_list_first (packer->sections);
  while (list != NULL) {
    section = (GstJpegSection *)list->data;
    gboolean status = TRUE;

    // Section Identifier
    status &= gst_byte_writer_put_uint8 (writer, 0xff);

    // Section Type
    status &= gst_byte_writer_put_uint8 (writer, section->type);

    GST_DEBUG_OBJECT (packer, "marker = %2x, size = %u", section->type,
        section->size);

    if (section->size) {
      // Section Length
      status &= gst_byte_writer_put_uint16_be (writer, section->size);

      // Section Data
      switch (section->type) {
        case JPEG_MARKER_APP0:
          if (section->owned &&
              (section->size - 2 - packer->thumbnail_size == 6)) {
            // APP0 extension section
            status &= gst_byte_writer_put_data (writer, section->data,
                section->size - 2 - packer->thumbnail_size);

            status &= gst_byte_writer_put_data (writer, packer->thumbnail_data,
                packer->thumbnail_size);
            break;
          }

          status &= gst_byte_writer_put_data (writer, section->data,
              section->size - 2);
          break;
        case JPEG_MARKER_APP1:
          if (section->owned) {
            // APP1 section
            status &= gst_byte_writer_put_data (writer, section->data,
                section->size - 2 - packer->thumbnail_size);

            status &= gst_byte_writer_put_data (writer, packer->thumbnail_data,
                packer->thumbnail_size);
            break;
          }

          status &= gst_byte_writer_put_data (writer, section->data,
              section->size - 2);
          break;
        default:
          status &= gst_byte_writer_put_data (writer, section->data,
              section->size - 2);
          break;
      }
    }

    if (section->type == JPEG_MARKER_SOS) {
      status &= gst_byte_writer_put_data (writer, packer->primary_data,
          packer->primary_size);

      GST_DEBUG_OBJECT (packer, "Scan data, size = %u", packer->primary_size);
    }

    if (!status) {
      GST_WARNING_OBJECT (packer, "Failed to write to output buffer");

      gst_buffer_unref (buffer);
      buffer = NULL;
      break;
    }

    list = g_list_next (list);
  }

  // Clean
  for (guint i = 0;i < g_list_length (r_maps); ++i) {
    GstMapInfo *m = (GstMapInfo *)g_list_nth_data (r_maps, i);

    gst_buffer_unmap (gst_buffer_list_get (bufs, i), m);
    g_free (m);
  }
  g_list_free (r_maps);

  if (buffer)
    gst_buffer_unmap (buffer, &map);

  gst_byte_writer_free (writer);

  return buffer;
}

static void
gst_jpeg_packer_srcpad_task (gpointer user_data)
{
  GstJpegPacker *packer = NULL;
  GstBufferList *list = NULL;
  GstBuffer *buffer = NULL;
  GstTaskState state = GST_TASK_STARTED;
  GstFlowReturn ret = GST_FLOW_OK;

  g_return_if_fail (user_data != NULL);

  packer = (GstJpegPacker *)user_data;

  state = gst_pad_get_task_state (packer->srcpad);
  if (state != GST_TASK_STARTED) {
    GST_ERROR_OBJECT (packer, "Task has not been started");
    return;
  }

  list = (GstBufferList *) g_async_queue_try_pop (packer->buffers);
  if (!list) {
    gst_pad_pause_task (packer->srcpad);

    GST_INFO_OBJECT (packer, "No data, task paused");
    return;
  } else if (list && gst_buffer_list_length (list) == 0) {
    // EOS
    GST_INFO_OBJECT (packer, "Received empty buffer list, EOS");

    gst_buffer_list_unref (list);

    gst_pad_push_event (packer->srcpad, gst_event_new_eos ());
    return;
  }

  GST_DEBUG_OBJECT (packer, "Start process image");

  if (!gst_buffer_list_foreach (list, gst_jpeg_packer_parse_image, packer)) {
    GST_ERROR_OBJECT (packer, "Failed to parse images");

    gst_buffer_list_unref (list);
    return;
  }

  if (packer->thumbnail_size && packer->thumbnail_data != NULL) {
    if (!gst_jpeg_packer_mangle (packer)) {
      g_list_foreach (packer->sections, gst_jpeg_section_free, packer);
      g_list_free (packer->sections);
      packer->sections = NULL;

      gst_buffer_list_unref (list);

      GST_DEBUG_OBJECT (packer, "Cleared all sections");
      return;
    }
  }

  buffer = gst_jpeg_packer_recombine (packer, list);

  GST_DEBUG_OBJECT (packer, "End process image");

  gst_buffer_list_unref (list);

  g_list_foreach (packer->sections, gst_jpeg_section_free, packer);
  g_list_free (packer->sections);
  packer->sections = NULL;

  ret = gst_pad_push (packer->srcpad, buffer);
  if (ret < GST_FLOW_OK) {
    GST_ERROR_OBJECT (packer, "Failed to push buffer downstream.");
    return;
  }

  GST_DEBUG_OBJECT (packer, "Pushed buffer %p", buffer);
}

static void
gst_jpeg_packer_srcpad_task_destroy (gpointer user_data)
{
  GstJpegPacker *packer = NULL;
  GstBufferList *list = NULL;

  g_return_if_fail (user_data != NULL);

  packer = (GstJpegPacker *)user_data;

  if (packer->buffers) {
    do {
      list = (GstBufferList *) g_async_queue_try_pop (packer->buffers);

      if (list)
        gst_buffer_list_unref (list);

    } while (list);

    g_async_queue_unref (packer->buffers);
  }
  packer->buffers = NULL;

  GST_DEBUG_OBJECT (packer, "Pad %s:%s task destroied",
      GST_DEBUG_PAD_NAME (packer->srcpad));
}

static GstStateChangeReturn
gst_jpeg_packer_change_state (GstElement * element, GstStateChange transition)
{
  GstJpegPacker *packer = NULL;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (element != NULL, GST_STATE_CHANGE_FAILURE);

  packer = GST_JPEG_PACKER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (packer->collectpad);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (packer->collectpad);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_jpeg_packer_parent_class)->change_state (
      element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    GST_ERROR_OBJECT (packer, "Failed to change state in parent class");

  return ret;
}

static gboolean
gst_jpeg_packer_srcpad_activate_mode (GstPad *pad, GstObject *parent,
    GstPadMode mode, gboolean active)
{
  GstJpegPacker *packer = NULL;
  GstEvent *event = NULL;
  gchar *stream_start = NULL, *pad_name = NULL;
  gboolean ret = TRUE;

  g_return_val_if_fail (parent != NULL, FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);

  packer = GST_JPEG_PACKER (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        // Stream start
        pad_name = gst_pad_get_name (pad);
        stream_start = g_strconcat ("jpegpacker/", pad_name, NULL);
        event = gst_event_new_stream_start (stream_start);
        gst_event_set_group_id (event, gst_util_group_id_next ());

        ret = gst_pad_push_event (packer->srcpad ,event);

        g_free (stream_start);
        g_free (pad_name);

        ret = gst_pad_start_task (packer->srcpad, gst_jpeg_packer_srcpad_task,
            packer, gst_jpeg_packer_srcpad_task_destroy);
      } else {
        ret = gst_pad_stop_task (packer->srcpad);
      }
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_jpeg_packer_srcpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstJpegPacker *packer = GST_JPEG_PACKER (parent);
  gboolean success = TRUE;

  g_return_val_if_fail (packer != NULL, FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);

  GST_DEBUG_OBJECT (packer, "Got '%s' event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      GST_DEBUG_OBJECT (packer, "Pushing STREAM_START downstream");

      success = gst_pad_push_event (pad, event);
      break;
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (packer, "Pushing EOS downstream");

      success = gst_pad_push_event (pad, event);
      break;
    case GST_EVENT_SEGMENT:
      GST_DEBUG_OBJECT (packer, "Pushing SEGMENT downstream");

      success = gst_pad_push_event (pad, event);
      break;
    default:
      success = gst_pad_event_default (pad, parent, event);
      break;
  }

  return success;
}

static GstFlowReturn
gst_jpeg_packer_collectpads_collected (GstCollectPads *pads, gpointer user_data)
{
  GstJpegPacker *packer = NULL;
  GSList *data_list = NULL;
  GstBufferList *buf_list = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  gsize max_size = 0;

  g_return_val_if_fail (pads != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (user_data != NULL, GST_FLOW_ERROR);

  data_list = pads->data;
  packer = (GstJpegPacker *)user_data;
  buf_list = gst_buffer_list_new_sized (g_slist_length (data_list));

  while (data_list != NULL) {
    GstCollectData *collectdata = (GstCollectData *)data_list->data;
    GstBuffer *buf = NULL;
    gsize tmp_size = 0;

    buf = gst_collect_pads_pop (pads, collectdata);
    if (!buf) {
      ret = GST_FLOW_EOS;
      break;
    }

    // First GstBuffer contains primary image data
    tmp_size = gst_buffer_get_size (buf);
    if (tmp_size > max_size) {
      gst_buffer_list_insert (buf_list, 0, buf);
      max_size = gst_buffer_get_size (buf);
    } else {
      gst_buffer_list_add (buf_list, buf);
    }

    GST_LOG_OBJECT (packer, "Popped buffer %p from pad %s:%s", buf,
        GST_DEBUG_PAD_NAME (collectdata->pad));

    data_list = g_slist_next (data_list);

    // Segment check
    if (!GST_COLLECT_PADS_STATE_IS_SET (collectdata,
          GST_COLLECT_PADS_STATE_NEW_SEGMENT))
      continue;

    gst_pad_push_event (packer->srcpad,
        gst_event_new_segment (&collectdata->segment));
  }

  g_async_queue_push (packer->buffers, buf_list);
  GST_LOG_OBJECT (packer, "Buffers collected.");

  if (ret == GST_FLOW_EOS ||
      gst_pad_get_task_state (packer->srcpad) != GST_TASK_STARTED) {
    gboolean success = TRUE;

    success = gst_task_set_state (
        GST_PAD_TASK (packer->srcpad), GST_TASK_STARTED);

    GST_INFO_OBJECT (packer, "Start task again, success: %d", success);
  }

  return ret;
}

static void
gst_jpeg_packer_class_init (GstJpegPackerClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_jpeg_packer_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_jpeg_packer_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_jpeg_packer_finalize);

  gst_element_class_set_static_metadata (
      element, "JPEG Packer", "Filter/Editor/Image/Compositor",
      "Pack jpeg images with jpeg interchange format", "QTI");

  g_object_class_install_property (gobject, PROP_PACK_TYPE,
      g_param_spec_enum ("pack-type", "Pack Type",
          "Output JPEG interchange format",
          GST_TYPE_JPEG_PACKER_PACK_TYPE, DEFAULT_PROP_PACK_TYPE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element,
      &gst_jpeg_packer_src_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_jpeg_packer_sink_pad_template);

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_jpeg_packer_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_jpeg_packer_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_jpeg_packer_change_state);

  GST_DEBUG_CATEGORY_INIT (gst_jpeg_packer_debug, "qtijpegpacker", 0,
      "QTI JPEG Packer");
}

static void
gst_jpeg_packer_init (GstJpegPacker *packer)
{
  GstElement *parent = GST_ELEMENT (packer);
  GstPadTemplate *srcpad_templ = NULL;

  // SrcPad
  srcpad_templ =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (parent), "src");
  g_return_if_fail (srcpad_templ != NULL);

  packer->srcpad = gst_pad_new_from_template (srcpad_templ, "src");

  gst_pad_set_activatemode_function (packer->srcpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_packer_srcpad_activate_mode));
  gst_pad_set_event_function (packer->srcpad,
      GST_DEBUG_FUNCPTR (gst_jpeg_packer_srcpad_event));
  gst_element_add_pad (parent, packer->srcpad);

  // CollectPads
  packer->collectpad = gst_collect_pads_new ();

  gst_collect_pads_set_function (packer->collectpad,
      gst_jpeg_packer_collectpads_collected, packer);

  packer->buffers = g_async_queue_new ();
  packer->primary_data = NULL;
  packer->primary_size = 0;
  packer->thumbnail_data = NULL;
  packer->thumbnail_size = 0;

  GST_INFO_OBJECT (packer, "JpegPacker plugin instance inited.");
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "qtijpegpacker", GST_RANK_PRIMARY,
      GST_TYPE_JPEG_PACKER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtijpegpacker,
    "Pack jpeg images with jpeg interchange format",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
