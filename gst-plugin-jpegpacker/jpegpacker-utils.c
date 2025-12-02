/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "jpegpacker-utils.h"

#include <libexif/exif-data.h>

struct _JfifData {
  /// Identifier of JFIF: "JFIF\0".
  gchar  id[5];

  /*
    Version of JFIF: 0x0102.
    Version 1.02 is the current released revision.
  */
  guint8 version[2];

  /*
    Density units of X and Y
      0: no units, X and Y specify the pixel aspect ratio.
      1: X and Y are dots per inch.
      2: X and Y are dots per cm.
  */
  guint8 units;

  /*
    xd: Horizontal pixel density.
    yd: Vertical pixel density
  */
  guint8 xd[2], yd[2];

  /*
    xt: Thumbnail(RGB) horizontal pixel count.
    yt: Thumbnail(RGB) vertical pixel count.
  */
  guint8 xt, yt;
};

struct _JfifDataExt {
  /// Identifier of JFIF: "JFXX\0"
  gchar  id[5];

  /*
     Code which identifies the extension.
       0x10: Thumbnail coded using JPEG.
       0x11: Thumbnail stored using 1 byte/pixel.
       0x13: Thumbnail stored using 3 bytes/pixel.
  */
  guint8 ext_code;

  /// Thumbnail data
};

/*
  EXIF data
    Identifier: "EXIF\0\0".
    TIFF Header: byte order(2 bytes) + ID Code(2 bytes) + PIFD(4 bytes).
    IFD 0: infomation of primary image.
    IFD 1: infomation of thumbnail image.
*/
gboolean
exif_data_create (guint16 app_size, const guint8 *app_data,
    guint thumbnail_size, guint16 *size, guint8 **data)
{
  ExifData *exif = NULL;
  ExifEntry *entry = NULL;
  ExifByteOrder order = EXIF_BYTE_ORDER_MOTOROLA;
  unsigned char *exif_buf = NULL;
  unsigned int exif_len = 0;

  exif = exif_data_new ();
  if (!exif) {
    GST_ERROR ("Failed to allocate exif data");
    return FALSE;
  }

  // IFD0, load from APP1 section
  if (app_size && app_data != NULL) {
    exif_data_load_data (exif,
        (const unsigned char *)app_data, (unsigned int)app_size);

    order = exif_data_get_byte_order (exif);
  } else {
    GST_WARNING ("Missing IFD0");
  }

  // IFD1
  // thumbnail offset
  entry = exif_entry_new ();
  if (!entry) {
    GST_ERROR ("Failed to allocate exif entry");

    exif_data_unref (exif);
    return FALSE;
  }

  entry->tag = EXIF_TAG_JPEG_INTERCHANGE_FORMAT;
  exif_content_add_entry (exif->ifd[EXIF_IFD_1], entry);
  exif_entry_initialize (entry, EXIF_TAG_JPEG_INTERCHANGE_FORMAT);
  exif_set_long (entry->data, order, 0);
  exif_entry_unref (entry);

  // thumbnail length
  entry = exif_entry_new ();
  if (!entry) {
    GST_ERROR ("Failed to allocate exif entry");

    exif_data_unref (exif);
    return FALSE;
  }

  entry->tag = EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH;
  exif_content_add_entry (exif->ifd[EXIF_IFD_1], entry);
  exif_entry_initialize (entry, EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH);
  exif_set_long (entry->data, order, thumbnail_size);
  exif_entry_unref (entry);

  // Get length to write offset of thumbnail
  exif_data_save_data (exif, &exif_buf, &exif_len);
  if (!exif_buf || exif_len == 0) {
    GST_ERROR ("Failed to export exif data");

    exif_data_unref (exif);
    return FALSE;
  }

  entry = exif_content_get_entry (exif->ifd[EXIF_IFD_1],
      EXIF_TAG_JPEG_INTERCHANGE_FORMAT);
  if (entry)
    // Skip Identifier, 6 bytes
    exif_set_long (entry->data, order, exif_len - 6);

  // Update EXIF data with offset of thumbnail
  free (exif_buf);
  exif_buf = NULL;
  exif_data_save_data (exif, &exif_buf, &exif_len);
  if (!exif_buf || exif_len == 0) {
    GST_ERROR ("Failed to export exif data with update offset");

    exif_data_unref (exif);
    return FALSE;
  }

  /*
    Convert data type
      exif_buf : unsigned char * -> guint8 *
      exif_size: unsigned int    -> guint16
  */
  if (exif_len > 0xFFFF)
    GST_WARNING ("The length of exif data %u will be cut", exif_len);

  *size = (guint16) exif_len;
  *data = (guint8 *) exif_buf;

  exif_data_unref(exif);

  return TRUE;
}

void
exif_free (guint8 *data)
{
  g_free (data);

  GST_TRACE ("EXIF data freed");
}

gboolean
jfif_data_create (guint16 *size, guint8 **data)
{
  JfifData *jfif_data = NULL;

  jfif_data = g_slice_new0 (JfifData);
  if (!jfif_data)
    return FALSE;

  // Initialization
  jfif_data->id[0] = 'J';
  jfif_data->id[1] = 'F';
  jfif_data->id[2] = 'I';
  jfif_data->id[3] = 'F';
  jfif_data->version[0] = 1;
  jfif_data->version[1] = 2;
  jfif_data->units = 0;
  jfif_data->xd[0] = 0;
  jfif_data->xd[1] = 1;
  jfif_data->yd[0] = 0;
  jfif_data->yd[1] = 1;
  jfif_data->xt = 0;
  jfif_data->yt = 0;

  *size = 14;
  *data = (guint8 *)jfif_data;

  return TRUE;
}

void
jfif_data_free (gpointer data)
{
  JfifData *jfif = (JfifData *)data;

  g_slice_free (JfifData, jfif);

  GST_TRACE ("JFIF data freed");
}

gboolean
jfif_data_ext_create (guint16 *size, guint8 **data)
{
  JfifDataExt *jfif_ext = NULL;

  jfif_ext = g_slice_new0 (JfifDataExt);
  if (!jfif_ext)
    return FALSE;

  jfif_ext->id[0] = 'J';
  jfif_ext->id[1] = 'F';
  jfif_ext->id[2] = 'X';
  jfif_ext->id[3] = 'X';
  jfif_ext->ext_code = 0x10;

  *size = 6;
  *data = (guint8 *)jfif_ext;

  return TRUE;
}

void
jfif_data_ext_free (gpointer data)
{
  JfifDataExt *jfif_ext = (JfifDataExt *)data;

  g_slice_free (JfifDataExt, jfif_ext);

  GST_TRACE ("JFIF extension data freed");
}
