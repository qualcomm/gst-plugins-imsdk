/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _JPEG_PACKER_UTILS_H_
#define _JPEG_PACKER_UTILS_H_

#include <gst/gst.h>

G_BEGIN_DECLS

/*
 * JPEG Markers
 */

#define JPEG_MARKER_SOI       0xd8  /* Start of image */
#define JPEG_MARKER_EOI       0xd9  /* End Of Image */
#define JPEG_MARKER_SOS       0xda  /* Start Of Scan */

/* Application marker */
#define JPEG_MARKER_APP0      0xe0
#define JPEG_MARKER_APP1      0xe1

typedef struct _JfifData    JfifData;
typedef struct _JfifDataExt JfifDataExt;

/** exif_create
 * @app_size: the size of original app1 section.
 * @app_data: pointer point to origial app1 data.
 * @thumbnail_size: size of thumbnail image.
 * @size: size of exif data.
 * @data: pointer point to the exif data.
 *
 * Create exif data for APP1 section.
 *
 * Return: TRUE if exif data is created successfully.
 */
gboolean
exif_data_create (guint16 app_size, const guint8 *app_data,
                  guint thumbnail_size, guint16 *size, guint8 **data);

/** exif_free
 * @data: pointer point to exif data.
 *
 * Free data exported by function exif_data_create.
 *
 * Return: NULL.
 */
void
exif_free (guint8 *data);

/** jfif_data_create
 * @size: size of JfifData.
 * @data: pointer point to JfifData.
 *
 * Create JfifData for APP0 section.
 *
 * Return: TRUE if creation is successful.
 */
gboolean
jfif_data_create (guint16 *size, guint8 **data);

/** jfif_data_free
 * @data: pointer point to JfifData.
 *
 * Free JfifData used in APP0 section.
 *
 * Return: NULL.
 */
void
jfif_data_free (gpointer data);

/** jfif_data_ext_create
 * @size: size of JfifDataExt.
 * @data: pointer point to JfifDataExt.
 *
 * Create JfifDataExt for APP0 extension section.
 *
 * Return: TRUE if creation is successful.
 */
gboolean
jfif_data_ext_create (guint16 *size, guint8 **data);

/** jfif_data_ext_free
 * @data: pointer point to JfifDataExt.
 *
 * Free JfifDataExt used in APP0 extension section.
 *
 * Return: NULL.
 */
void
jfif_data_ext_free (gpointer data);

G_END_DECLS

#endif // _JPEG_PACKER_UTILS_H_
