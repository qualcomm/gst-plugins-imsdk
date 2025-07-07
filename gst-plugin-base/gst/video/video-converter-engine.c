/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "video-converter-engine.h"

#ifdef HAVE_ADRENO_C2D2_H
#include "c2d-video-converter.h"
#endif // HAVE_ADRENO_C2D2_H
#ifdef HAVE_GLES2_H
#include "gles-video-converter.h"
#endif // HAVE_GLES2_H
#ifdef HAVE_FASTCV_H
#include "fcv-video-converter.h"
#endif // HAVE_FASTCV_H


#define GST_CAT_DEFAULT gst_video_converter_engine_debug
GST_DEBUG_CATEGORY (gst_video_converter_engine_debug);


/**
 * GstVideoConvNewFunction:
 * @settings: Structure with backend specific settings.
 *
 * Function prototype for allocating and initializing the converter backend.
 *
 * Returns: Pointer to the allocated converter backend or NULL on failure
 */
typedef gpointer (*GstVideoConvNewFunction) (GstStructure * settings);
/**
 * GstVideoConvFreeFunction:
 * @converter: Pointer underlying converter backend.
 *
 * Function prototype for deinitializing and freeing the converter backend.
 *
 * Returns: NONE
 */
typedef void (*GstVideoConvFreeFunction) (gpointer converter);
/**
 * GstVideoConvComposeFunction:
 * @converter: Pointer underlying converter backend.
 * @compositions: Array of composition frames.
 * @n_compositions: Number of compositions.
 * @fence: Optional fence to be filled if provided and used for async operation.
 *
 * Function prototype for performing image blitting.
 *
 * Returns: TRUE on success or FALSE on failure
 */
typedef gboolean (*GstVideoConvComposeFunction) (
    gpointer converter, GstVideoComposition *compositions, guint n_compositions,
    gpointer *fence);
/**
 * GstVideoConvWaitFenceFunction:
 * @converter: Pointer underlying converter backend.
 * @fence: Asynchronously fence object associated with a compose request.
 *
 * Function prototype for waiting an async compose operation to finish.
 *
 * Returns: TRUE on success or FALSE on failure
 */
typedef gboolean (*GstVideoConvWaitFenceFunction) (gpointer converter,
                                                   gpointer fence);
/**
 * GstVideoConvFlushFunction:
 * @converter: Pointer underlying converter backend.
 *
 * Function prototype for clearing cached data and finishing pending operations.
 *
 * Returns: NONE
 */
typedef void (*GstVideoConvFlushFunction) (gpointer converter);

/**
 * GstVideoConvEngine:
 * @converter: Pointer underlying converter backend.
 * @new: Pointer to the new function of the underlying converter.
 * @free: Pointer to the free function of the underlying converter.
 * @compose: Pointer to the compose function of the underlying converter.
 * @wait_fence: Pointer to the wait_fence function of the underlying converter.
 * @flush: Pointer to the flush function of the underlying converter.
 *
 * Base class for video converter engine.
 */
struct _GstVideoConvEngine {
  gpointer                      converter;

  GstVideoConvNewFunction       new;
  GstVideoConvFreeFunction      free;

  GstVideoConvComposeFunction   compose;
  GstVideoConvWaitFenceFunction wait_fence;
  GstVideoConvFlushFunction     flush;
};

static inline void
gst_video_conv_engine_init_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_video_converter_engine_debug,
        "video-converter-engine", 0, "QTI Video Converter Engine ");
    g_once_init_leave (&catonce, TRUE);
  }
}

GType
gst_video_converter_backend_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_VCE_BACKEND_NONE, "No backend used", "none" },
#ifdef HAVE_ADRENO_C2D2_H
    { GST_VCE_BACKEND_C2D, "Use C2D based video converter", "c2d" },
#endif // HAVE_ADRENO_C2D2_H
#ifdef HAVE_GLES2_H
    { GST_VCE_BACKEND_GLES, "Use OpenGLES based video converter", "gles" },
#endif // HAVE_GLES2_H
#ifdef HAVE_FASTCV_H
    { GST_VCE_BACKEND_FCV, "Use FastCV based video converter", "fcv" },
#endif // HAVE_FASTCV_H
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstVideoConverterBackend", variants);

  return gtype;
}

GstVideoConvBackend
gst_video_converter_default_backend (void)
{
  GstVideoConvBackend backend = GST_VCE_BACKEND_FCV;

#if defined(HAVE_GLES2_H)
  backend = GST_VCE_BACKEND_GLES;
#elif defined(HAVE_ADRENO_C2D2_H)
  backend = GST_VCE_BACKEND_C2D;
#endif // !HAVE_GLES2_H && !HAVE_ADRENO_C2D2_H

  return backend;
}

GstVideoConvEngine *
gst_video_converter_engine_new (GstVideoConvBackend backend,
                                GstStructure * settings)
{
  GstVideoConvEngine *engine = NULL;

  // Initialize the debug category.
  gst_video_conv_engine_init_debug_category ();

  engine = g_new (GstVideoConvEngine, 1);
  g_return_val_if_fail (engine != NULL, FALSE);

  switch (backend) {
    case GST_VCE_BACKEND_NONE:
      // No engine required
      g_free (engine);
      return NULL;
#ifdef HAVE_ADRENO_C2D2_H
    case GST_VCE_BACKEND_C2D:
      engine->new = (GstVideoConvNewFunction) gst_c2d_video_converter_new;
      engine->free = (GstVideoConvFreeFunction) gst_c2d_video_converter_free;
      engine->compose =
          (GstVideoConvComposeFunction) gst_c2d_video_converter_compose;
      engine->wait_fence =
          (GstVideoConvWaitFenceFunction) gst_c2d_video_converter_wait_fence;
      engine->flush = (GstVideoConvFlushFunction) gst_c2d_video_converter_flush;
      break;
#endif // HAVE_ADRENO_C2D2_H
#ifdef HAVE_GLES2_H
    case GST_VCE_BACKEND_GLES:
      engine->new = (GstVideoConvNewFunction) gst_gles_video_converter_new;
      engine->free = (GstVideoConvFreeFunction) gst_gles_video_converter_free;
      engine->compose =
          (GstVideoConvComposeFunction) gst_gles_video_converter_compose;
      engine->wait_fence =
          (GstVideoConvWaitFenceFunction) gst_gles_video_converter_wait_fence;
      engine->flush = (GstVideoConvFlushFunction) gst_gles_video_converter_flush;
      break;
#endif // HAVE_GLES2_H
#ifdef HAVE_FASTCV_H
    case GST_VCE_BACKEND_FCV:
      engine->new = (GstVideoConvNewFunction) gst_fcv_video_converter_new;
      engine->free = (GstVideoConvFreeFunction) gst_fcv_video_converter_free;
      engine->compose =
          (GstVideoConvComposeFunction) gst_fcv_video_converter_compose;
      engine->wait_fence =
          (GstVideoConvWaitFenceFunction) gst_fcv_video_converter_wait_fence;
      engine->flush = (GstVideoConvFlushFunction) gst_fcv_video_converter_flush;
      break;
#endif // HAVE_FASTCV_H
    default:
      GST_ERROR ("Unsupported video converter backend: 0x%X !", backend);
      goto cleanup;
  }

  if ((engine->converter = engine->new (settings)) == NULL) {
    GST_ERROR ("Failed to create backend converter!");
    goto cleanup;
  }

  return engine;

cleanup:
  g_free (engine);
  return NULL;
}

void
gst_video_converter_engine_free (GstVideoConvEngine * engine)
{
  if (engine == NULL)
    return;

  engine->free (engine->converter);
  g_free (engine);
}

gboolean
gst_video_converter_engine_compose (GstVideoConvEngine * engine,
    GstVideoComposition * compositions, guint n_compositions, gpointer * fence)
{
  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail ((compositions != NULL) && (n_compositions != 0), FALSE);

  return engine->compose (engine->converter, compositions, n_compositions, fence);
}

gboolean
gst_video_converter_engine_wait_fence  (GstVideoConvEngine * engine,
    gpointer fence)
{
  g_return_val_if_fail (engine != NULL, FALSE);

  if (fence == NULL)
    return TRUE;

  return engine->wait_fence (engine->converter, fence);
}

void
gst_video_converter_engine_flush (GstVideoConvEngine * engine)
{
  g_return_if_fail (engine != NULL);

  engine->flush (engine->converter);
}
