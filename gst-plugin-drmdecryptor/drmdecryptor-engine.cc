/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "drmdecryptor-engine.h"

#include <dlfcn.h>
#include <cutils/native_handle.h>

#include <media/hardware/CryptoAPI.h>
#include <media/drm/DrmAPI.h>

#define GST_CAT_DEFAULT           gst_drm_decryptor_engine_debug_category ()

#define SUBSAMPLE_INFO_LEN        6
#define CLEAR_BYTES_SIZE          2
#define ENCR_BYTES_SIZE           4
#define IV_SIZE                   16

#define GST_RETURN_VAL_IF_FAIL(expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return (value); \
  } \
}

#define GST_RETURN_VAL_IF_FAIL_WITH_CLEAN(expression, value, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return (value); \
  } \
}

typedef android::CryptoFactory *(*CreateCryptoFactoryFunc)();

struct _GstDrmDecryptorEngine {
  android::CryptoPlugin *crypto_plugin;
  void *lib_handle;
  native_handle_t *nh;
};

static GstDebugCategory *
gst_drm_decryptor_engine_debug_category (void) {
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtidrmdecryptor",
        0, "DRM Decryptor Engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *)catonce;
}

GstDrmDecryptorEngine *
gst_drm_decryptor_engine_new (const gchar *session_id)
{
  GstDrmDecryptorEngine *engine = NULL;
  // Playready DRM UUID
  const guint8 uuid[16] = {
    0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86,
    0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95
  };

  engine = g_slice_new0 (GstDrmDecryptorEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->lib_handle = dlopen ("/usr/lib/libprdrmengine.so", RTLD_NOW);

  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->lib_handle != NULL, NULL,
      gst_drm_decryptor_engine_free (engine), "Failed to open PR DRM engine library,"
      " dlerror: %s", dlerror());

  CreateCryptoFactoryFunc createCryptoFactory =
      (CreateCryptoFactoryFunc)dlsym(engine->lib_handle,
          "createCryptoFactory");

  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (createCryptoFactory != NULL, NULL,
      gst_drm_decryptor_engine_free (engine), "Cannot find symbol, dlerror: %s",
      dlerror());

  android::CryptoFactory *factory;
  factory = createCryptoFactory();

  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (factory != NULL, NULL,
      gst_drm_decryptor_engine_free (engine), "Create crypto factory failed !");

  gulong status = factory->createPlugin (uuid, static_cast<const void*>(session_id),
      static_cast<size_t>(strlen(session_id)), &(engine->crypto_plugin));

  delete factory;

  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == 0, NULL,
      gst_drm_decryptor_engine_free (engine),
      "DRM Create Crypto Plugin failed with error: %ld", status);

  engine->nh = native_handle_create(1 /*numFds*/, 0 /*numInts*/);

  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->nh != NULL, NULL,
      gst_drm_decryptor_engine_free (engine), "Invalid native handle");

  return engine;
}

void
gst_drm_decryptor_engine_free (GstDrmDecryptorEngine *engine)
{
  if (engine->crypto_plugin)
    delete engine->crypto_plugin;

  if (engine->lib_handle)
    dlclose (engine->lib_handle);

  if (engine->nh)
    native_handle_delete (engine->nh);

  g_slice_free (GstDrmDecryptorEngine, engine);
}

gboolean
gst_drm_decryptor_engine_execute (GstDrmDecryptorEngine *engine,
    GstBuffer *in_buffer, GstBuffer *out_buffer)
{
  GstProtectionMeta *pmeta = gst_buffer_get_protection_meta (in_buffer);
  GstBuffer *key_id_buf, *iv_buf, *subsample_buf;
  GstMapInfo  inbuff_map_info, keyid_map_info, iv_map_info, subsample_map_info;
  android::CryptoPlugin::SubSample *subsample;
  android::CryptoPlugin::Pattern pattern;
  android::CryptoPlugin::Mode mode;
  android::AString *error_detail_msg;
  gsize inbuf_size, decrypt_size = 0;
  guint subsample_count, idx, total_bytes = 0;
  gboolean secure, success = TRUE;
  guint8 iv_arr[IV_SIZE];

  gst_structure_get_boolean (pmeta->info, "encrypted", &secure);
  gst_structure_get_uint (pmeta->info, "subsample_count", &subsample_count);

  subsample = g_new (android::CryptoPlugin::SubSample, subsample_count);

  key_id_buf = gst_value_get_buffer (
      gst_structure_get_value (pmeta->info, "kid"));
  iv_buf = gst_value_get_buffer (
      gst_structure_get_value (pmeta->info, "iv"));
  subsample_buf = gst_value_get_buffer (
      gst_structure_get_value (pmeta->info, "subsamples"));

  gst_buffer_map (in_buffer, &inbuff_map_info, GST_MAP_READ);
  gst_buffer_map (subsample_buf, &subsample_map_info, GST_MAP_READ);
  gst_buffer_map (key_id_buf, &keyid_map_info, GST_MAP_READ);
  gst_buffer_map (iv_buf, &iv_map_info, GST_MAP_READ);

  // Playready API expects IV of size 16 bytes. If the IV of input is of 8 bytes,
  // the remaining 8 bytes should be appended as 0.

  memset (iv_arr, 0x00, IV_SIZE);
  for (idx = 0; idx < iv_map_info.size; idx++) {
    iv_arr[idx] = iv_map_info.data[idx];
  }

  // Extract number of encrypted and clear bytes from subsample buffer of GstProtectionMeta
  // As per ISO/IEC CD 23001-7 spec, size of the subsample value should be 6 bytes
  // where MSB 2 bytes specify the number of clear bytes and the next 4 bytes specify
  // the number of encrypted bytes

  for (idx = 0; idx < subsample_count; idx++) {
    guint pos = 0;
    subsample[idx].mNumBytesOfClearData = 0;
    subsample[idx].mNumBytesOfEncryptedData = 0;
    for (pos = 0; pos < SUBSAMPLE_INFO_LEN; pos++) {
      if (pos < CLEAR_BYTES_SIZE)
        subsample[idx].mNumBytesOfClearData =
            (subsample[idx].mNumBytesOfClearData << 8) |
                subsample_map_info.data[(SUBSAMPLE_INFO_LEN * idx) + pos];
      else
        subsample[idx].mNumBytesOfEncryptedData =
            (subsample[idx].mNumBytesOfEncryptedData << 8) |
                subsample_map_info.data[(SUBSAMPLE_INFO_LEN * idx) + pos];
    }

    GST_DEBUG_OBJECT (engine, "Subsample(%d): Number of clear bytes=%u, "
        "encrypted bytes=%u", idx, subsample[idx].mNumBytesOfClearData,
        subsample[idx].mNumBytesOfEncryptedData);

    total_bytes += subsample[idx].mNumBytesOfClearData +
        subsample[idx].mNumBytesOfEncryptedData;
  }

  inbuf_size = gst_buffer_get_size (in_buffer);

  // Incase of byte-stream stream format in AVC, 6 bytes Access Unit Delimiter
  // is added at the start of each NAL unit. This offset needs to be accounted
  // in clear data bytes.

  if (total_bytes < inbuf_size)
    subsample[0].mNumBytesOfClearData += inbuf_size - total_bytes;

  mode = android::CryptoPlugin::kMode_AES_CTR;

  {
    GstMemory *mem = gst_buffer_get_memory (out_buffer, 0);
    mem->size = inbuf_size;
    engine->nh->data[0] = gst_fd_memory_get_fd (mem);
  }

  decrypt_size = engine->crypto_plugin->decrypt (
    secure,
    keyid_map_info.data,
    iv_arr,
    mode,
    pattern,
    inbuff_map_info.data,
    subsample,
    subsample_count,
    static_cast<void*>(engine->nh),
    error_detail_msg
  );

  if (decrypt_size != inbuf_size) {
    GST_ERROR_OBJECT (engine, "Decrypted buffer size (%zu bytes) not equal to "
        "input buffer size (%zu bytes)", decrypt_size, inbuf_size);
    success = FALSE;
  } else {
    GST_INFO_OBJECT (engine, "Decrypted buffer size= %zu bytes "
        "input size= %zu bytes", decrypt_size, inbuf_size);
  }

  g_free (subsample);
  gst_buffer_unmap (in_buffer, &inbuff_map_info);
  gst_buffer_unmap (subsample_buf, &subsample_map_info);
  gst_buffer_unmap (key_id_buf, &keyid_map_info);
  gst_buffer_unmap (iv_buf, &iv_map_info);

  return success;
}