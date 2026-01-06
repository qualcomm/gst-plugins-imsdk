/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "heif-engine.h"

#include <unistd.h>
#include <dlfcn.h>
#define GST_USE_UNSTABLE_API
#include <gst/codecparsers/gsth265parser.h>

#include "libheif/heif.h"

#define GST_CAT_DEFAULT heif_engine_debug_category()

#define LOAD_HEIF_SYMBOL(c, name) \
  load_symbol ((gpointer*)&(c->name), c->heifhandle, "heif_context_"#name);

struct _GstHeifEngine {
  // The HEIF file context.
  heif_context  *ctx;

  // HEIF output width, aligned to tile width.
  uint32_t      width;
  // HEIF output height, aligned to tile height.
  uint32_t      height;
  // HEIF tile width.
  uint32_t      twidth;
  // HEIF tile height.
  uint32_t      theight;

  // Mutex
  GMutex        lock;

  // Heif library handle.
  gpointer      heifhandle;

  // Heif library APIs.
  LIBHEIF_API heif_context* (*alloc) (void);
  LIBHEIF_API void (*free) (heif_context*);
  LIBHEIF_API void (*image_handle_release) (heif_image_handle*);

  LIBHEIF_API heif_error (*add_grid_image) (
      heif_context* ctx, uint32_t width, uint32_t height, uint32_t columns,
      uint32_t rows, const heif_encoding_options* options,
      heif_image_handle** out_image_handle);
  LIBHEIF_API heif_error (*set_primary_image) (
      heif_context* ctx, heif_image_handle* handle);
  LIBHEIF_API heif_error (*add_encoded_image_tile) (
      heif_context* ctx, heif_image_handle* tiled_image, uint32_t tile_x,
      uint32_t tile_y, uint8_t* data, uint32_t length);
  LIBHEIF_API heif_error (*encoded_thumbnail) (
      heif_context* ctx, const heif_image_handle* handle, uint32_t format,
      uint32_t width, uint32_t height, uint8_t* data, uint32_t length);
  LIBHEIF_API heif_error (*write) (
      heif_context* ctx, heif_writer* writer, void* userdata);
};

static GstDebugCategory *
heif_engine_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtiheifmux", 0,
        "HEIF engine");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

static gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  *(method) = dlsym (handle, name);

  if (NULL == *(method)) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }

  return TRUE;
}

GstHeifEngine *
gst_heif_engine_new ()
{
  gboolean success = TRUE;
  GstHeifEngine * engine = g_slice_new0 (GstHeifEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  g_mutex_init (&engine->lock);

  // Initialize all pointers to NULL for safe cleanup.
  engine->heifhandle = NULL;
  engine->ctx = NULL;

  if ((engine->heifhandle = dlopen ("libheif.so", RTLD_NOW)) == NULL) {
    GST_ERROR ("Failed to open libheif library, error: %s!", dlerror());
    goto cleanup;
  }

  // Load HEIF library symbols.
  success &= LOAD_HEIF_SYMBOL (engine, alloc);
  success &= LOAD_HEIF_SYMBOL (engine, free);
  success &= LOAD_HEIF_SYMBOL (engine, add_grid_image);
  success &= LOAD_HEIF_SYMBOL (engine, set_primary_image);
  success &= LOAD_HEIF_SYMBOL (engine, add_encoded_image_tile);
  success &= LOAD_HEIF_SYMBOL (engine, encoded_thumbnail);
  success &= LOAD_HEIF_SYMBOL (engine, write);

  success &= load_symbol ((gpointer*)&(engine->image_handle_release),
      engine->heifhandle, "heif_image_handle_release");

  // Check whether symbol loading was successful.
  if (!success)
    goto cleanup;

  GST_INFO ("Heif engine is created : %p", engine);
  return engine;

cleanup:
  gst_heif_engine_free (engine);
  return NULL;
}

gboolean
gst_heif_context_create (GstHeifEngine * engine)
{
  gboolean ret = TRUE;
  engine->ctx = engine->alloc ();

  if (engine->ctx == NULL) {
    GST_ERROR ("Could not create HEIF context.");
    return FALSE;
  }

  GST_INFO ("Heif context is created.");
  return ret;
}

gboolean
gst_heif_context_destroy (GstHeifEngine * engine)
{
  if (engine == NULL || engine->ctx == NULL)
    return FALSE;

  if (engine->free)
    engine->free (engine->ctx);

  engine->ctx = NULL;

  GST_INFO ("Heif context destroyed.");
  return TRUE;
}

void
gst_heif_engine_free (GstHeifEngine * engine)
{
  if (engine == NULL)
    return;

  gst_heif_context_destroy(engine);

  if (engine->heifhandle)
    dlclose (engine->heifhandle);

  g_mutex_clear (&engine->lock);

  GST_INFO ("Destroyed Heif engine: %p", engine);
  g_slice_free (GstHeifEngine, engine);
}

static gboolean
gst_heif_get_tile_info (GstHeifEngine * engine, GstBuffer * buffer)
{
  GstH265Parser *parser;
  GstH265NalUnit nalu;
  GstH265SPS sps;
  GstH265ParserResult pres;
  GstMemory *memory = NULL;
  GstMapInfo map;

  g_return_val_if_fail (buffer != NULL, FALSE);

  parser = gst_h265_parser_new();
  g_return_val_if_fail (parser != NULL, FALSE);
  memset (&nalu, 0, sizeof (nalu));

  memory = gst_buffer_peek_memory (buffer, 0);
  if (memory == NULL) {
    GST_ERROR ("Failed to peek memory from buffer!");
    gst_h265_parser_free (parser);
    return FALSE;
  }

  if (!gst_memory_map (memory, &map, GST_MAP_READ)) {
    GST_ERROR ("Cannot map memory!");
    gst_h265_parser_free (parser);
    return FALSE;
  }

  // Get width/heigh from SPS.
  do {
    pres = gst_h265_parser_identify_nalu (parser, map.data,
        nalu.offset + nalu.size, map.size, &nalu);
    if (pres == GST_H265_PARSER_NO_NAL_END)
      pres = GST_H265_PARSER_OK;

    if (nalu.type == GST_H265_NAL_SPS) {
      pres = gst_h265_parser_parse_sps (parser, &nalu, &sps, TRUE);
      if (pres != GST_H265_PARSER_OK)
        GST_ERROR ("H265 parser SPS failed!");

      engine->twidth = sps.pic_width_in_luma_samples;
      engine->theight = sps.pic_height_in_luma_samples;
      break;
    }
  } while (pres == GST_H265_PARSER_OK);

  gst_memory_unmap (memory, &map);
  gst_h265_parser_free (parser);
  return pres == GST_H265_PARSER_OK ? TRUE : FALSE;
}

static heif_error
gst_heif_stream_write (heif_context * ctx, const void * data, size_t size,
    void * userdata)
{
  GstBuffer **outbuf = (GstBuffer **) userdata;
  GstMemory *memory = NULL;
  GstMapInfo outmap;

  if (outbuf == NULL || *outbuf == NULL) {
    GST_ERROR ("Invalid output buffer pointer!");
    return heif_error { heif_error_Invalid_input, heif_suberror_End_of_data,
        "heif_writer invalid input!"};
  }

  memory = gst_buffer_peek_memory (*outbuf, 0);
  if (memory == NULL) {
    GST_ERROR ("Failed to peek memory from buffer!");
    return heif_error { heif_error_Invalid_input, heif_suberror_End_of_data,
        "heif_writer invalid input!"};
  }

  if (!gst_memory_map (memory, &outmap, GST_MAP_WRITE)) {
    GST_ERROR ("Cannot map memory!");
    return heif_error { heif_error_Invalid_input, heif_suberror_End_of_data,
        "heif_writer invalid input!"};
  }

  // Resize the buffer to the encoded size.
  if (outmap.maxsize >= size) {
    memcpy (outmap.data, data, size);
    gst_memory_resize (memory, 0, size);
  } else {
    GST_ERROR ("Output memory size is too small!");
    goto error;
  }

  gst_memory_unmap (memory, &outmap);
  return heif_error { heif_error_Ok, heif_suberror_Unspecified,
      "sucessful!" };

error:
  gst_memory_unmap (memory, &outmap);
  return heif_error { heif_error_Invalid_input, heif_suberror_End_of_data,
      "heif_writer invalid input!"};
}

gboolean
gst_heif_engine_execute (GstHeifEngine * engine, GstBuffer * inbuf,
    GList * thframes, GstBuffer ** outbuf)
{
  GstVideoMeta *vmeta = NULL;
  GstMemory *mem = NULL;
  GstMapInfo map;
  heif_image_handle *gridimage = nullptr;
  heif_writer writer;
  uint32_t columns = 0, rows = 0;
  heif_error error;
  gboolean ret = TRUE;

  vmeta = gst_buffer_get_video_meta (inbuf);
  g_return_val_if_fail (vmeta, FALSE);

  g_mutex_lock (&engine->lock);

  ret = gst_heif_context_create (engine);
  if (ret == FALSE) {
    GST_ERROR ("Failed to create heif context!");
    goto clean;
  }

  ret =  gst_heif_get_tile_info (engine, inbuf);
  if (ret == FALSE) {
    GST_ERROR ("Failed to get tile width/height!");
    goto clean;
  }

  // Validate tile dimensions before division.
  if (engine->twidth == 0 || engine->theight == 0) {
    GST_ERROR ("Invalid tile dimensions: width=%u, height=%u",
        engine->twidth, engine->theight);
    ret = FALSE;
    goto clean;
  }

  engine->width = GST_ROUND_UP_N (vmeta->width, engine->twidth);
  engine->height =  GST_ROUND_UP_N (vmeta->height, engine->theight);
  columns = engine->width / engine->twidth;
  rows = engine->height / engine->theight;

  error = engine->add_grid_image (engine->ctx, engine->width, engine->height,
      columns, rows, NULL, &gridimage);
  if (error.code != heif_error_Ok) {
    GST_ERROR ("Failed to create grid image with error %d", error.code);
    ret = FALSE;
    goto clean;
  }

  error = engine->set_primary_image (engine->ctx, gridimage);
  if (error.code != heif_error_Ok) {
    GST_ERROR ("Failed to set primary image with error %d", error.code);
    ret = FALSE;
    goto clean;
  }

  // Add encoded tiles to grid image.
  for (uint32_t ty = 0; ty < rows; ty++) {
    for (uint32_t tx = 0; tx < columns; tx++) {
      mem = gst_buffer_get_memory (inbuf, tx + ty * columns);

      if (mem == NULL) {
        GST_ERROR ("Failed to get memory at tile (%u, %u)", tx, ty);
        ret = FALSE;
        goto clean;
      }

      if (!gst_memory_map (mem, &map, GST_MAP_READ)) {
        GST_ERROR ("Cannot map memory");
        gst_memory_unref (mem);
        ret = FALSE;
        goto clean;
      }

      error = engine->add_encoded_image_tile (
          engine->ctx, gridimage, tx, ty, map.data, map.size);
      gst_memory_unmap (mem, &map);
      gst_memory_unref (mem);

      if (error.code != heif_error_Ok) {
        GST_ERROR ("Failed to add image tile with error %d", error.code);
        ret = FALSE;
        goto clean;
      }
    }
  }

  // Add thubmbails.
  for (uint32_t idx = 0; idx < g_list_length (thframes); idx++) {
    GstVideoFrame *frame = (GstVideoFrame *) (g_list_nth_data (thframes, idx));
    gint width = GST_VIDEO_FRAME_WIDTH (frame);
    gint height = GST_VIDEO_FRAME_HEIGHT (frame);

    error = engine->encoded_thumbnail (engine->ctx, gridimage,
        heif_compression_HEVC, width, height, (guint8 *) (frame->map[0].data),
        frame->map[0].size);
    if (error.code != heif_error_Ok) {
      GST_ERROR ("Failed to add thumbnails with error %d", error.code);
      ret = FALSE;
      goto clean;
    }
  }

  // Write heif context to output buffer.
  writer.writer_api_version = 1;
  writer.write = gst_heif_stream_write;

  error = engine->write (engine->ctx, &writer, (void *)outbuf);
  if (error.code != heif_error_Ok) {
    GST_ERROR ("Failed to write HEIF context to gstbuffer with error %d",
        error.code);
    ret = FALSE;
    goto clean;
  }

clean:
  if (gridimage)
    engine->image_handle_release (gridimage);

  gst_heif_context_destroy (engine);
  g_mutex_unlock (&engine->lock);
  return ret;
}
