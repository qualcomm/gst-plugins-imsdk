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

#define GST_CAT_DEFAULT           gst_drm_decryptor_engine_debug_category ()

#define SUBSAMPLE_INFO_LEN        6
#define CLEAR_BYTES_SIZE          2
#define ENCR_BYTES_SIZE           4
#define IV_SIZE                   16

#define DRM_LIB_PATH              "/usr/lib/libprdrmengine.so"

#define GST_RETURN_VAL_IF_FAIL(expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return (value); \
  } \
}

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

struct GstDrmPREngine : GstDrmDecryptorEngine {
  static GstDrmPREngine* get_instance (void *session_id)
  {
    static GstDrmPREngine *engine = nullptr;

    if (!engine) {
      engine = new GstDrmPREngine();
      if (!(engine->drm_plugin_init (session_id, nullptr))) {
        delete engine;
        engine = nullptr;
      }
    }

    return engine;
  }

  ~GstDrmPREngine () {
    delete drm_plugin;

    if (lib_handle)
      dlclose (lib_handle);
  }

  private:
    void                   *lib_handle;
    android::CryptoPlugin  *drm_plugin;

    GstDrmPREngine () : lib_handle (NULL), drm_plugin (nullptr) {};

    typedef android::CryptoFactory *(*CreateCryptoFactoryFunc)();

    void subsample_parse (android::CryptoPlugin::SubSample *subsample,
        guint count, gsize size, GstMapInfo &subsample_map_info,
        gboolean is_clear);

    gboolean drm_plugin_init (void *session_id, void* instance) override;
    gint decrypt (gboolean secure, GstMapInfo keyid_map_info,
        GstMapInfo inbuff_map_info, GstMapInfo subsample_map_info,
        guint subsample_count, const guint8 *iv_arr, native_handle_t *nh,
        gboolean is_clear) override;
};

#ifdef ENABLE_WIDEVINE
struct GstDrmWVEngine : GstDrmDecryptorEngine {
  static GstDrmWVEngine* get_instance (void *session_id, void* instance)
  {
    static GstDrmWVEngine  *engine = nullptr;

    if (!engine) {
      engine = new GstDrmWVEngine();
      if (!(engine->drm_plugin_init (session_id, instance))) {
        delete engine;
        engine = nullptr;
      }
    }

    return engine;
  }

  ~GstDrmWVEngine () {}

  private:
    widevine::Cdm   *drm_plugin;
    std::string     session_id;

    GstDrmWVEngine () : drm_plugin (nullptr) {}

    void subsample_parse (widevine::Cdm::Subsample* subsample, guint count,
        gsize size, GstMapInfo &subsample_map_info, gboolean is_clear);

    gboolean drm_plugin_init (void *session_id, void* instance) override;
    gint decrypt (gboolean secure, GstMapInfo keyid_map_info,
        GstMapInfo inbuff_map_info, GstMapInfo subsample_map_info,
        guint subsample_count, const guint8 *iv_arr, native_handle_t *nh,
        gboolean is_clear) override;
};
#endif

gboolean
GstDrmPREngine::drm_plugin_init (void *session_id, void* instance)
{
  GST_RETURN_VAL_IF_FAIL (session_id != NULL, FALSE,
      "PR DRM plugin session-id is null");

  // Playready DRM UUID
  const guint8 uuid[16] = {
    0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86,
    0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95
  };

  lib_handle = dlopen (DRM_LIB_PATH, RTLD_NOW);

  GST_RETURN_VAL_IF_FAIL (lib_handle != NULL, FALSE,
      "Failed to open PR DRM engine library,"
      " dlerror: %s", dlerror());

  CreateCryptoFactoryFunc createCryptoFactory =
      (CreateCryptoFactoryFunc)dlsym(lib_handle, "createCryptoFactory");

  GST_RETURN_VAL_IF_FAIL (createCryptoFactory != NULL, FALSE,
      "Cannot find symbol, dlerror: %s", dlerror());

  android::CryptoFactory *factory;
  factory = createCryptoFactory();

  GST_RETURN_VAL_IF_FAIL (factory != NULL, FALSE, "Create crypto factory failed !");

  gulong status = factory->createPlugin (uuid, session_id, static_cast<size_t>(
      strlen(static_cast<const gchar*> (session_id))), &(drm_plugin));

  delete factory;

  GST_RETURN_VAL_IF_FAIL (status == 0, FALSE,
      "DRM Create Crypto Plugin failed with error: %ld", status);

  GST_INFO_OBJECT (this, "PlayReady DRM plugin initialized !");

  return TRUE;
}

gint
GstDrmPREngine::decrypt (gboolean secure, GstMapInfo keyid_map_info,
    GstMapInfo inbuff_map_info, GstMapInfo subsample_map_info, guint subsample_count,
    const guint8 *iv_arr, native_handle_t *nh, gboolean is_clear)
{
  android::CryptoPlugin::Pattern pattern;
  android::AString *error_detail_msg;
  gint status = 0;

  android::CryptoPlugin::Mode mode = (is_clear ?
      android::CryptoPlugin::kMode_Unencrypted : android::CryptoPlugin::kMode_AES_CTR);
  android::CryptoPlugin::SubSample *subsample = g_new0 (
      android::CryptoPlugin::SubSample, (subsample_count | 1u));

  subsample_parse (subsample, subsample_count, inbuff_map_info.size,
      subsample_map_info, is_clear);

  gsize decrypt_size = drm_plugin->decrypt (
    secure,
    keyid_map_info.data,
    iv_arr,
    mode,
    pattern,
    inbuff_map_info.data,
    subsample,
    (subsample_count | 1u),
    static_cast<void*>(nh),
    error_detail_msg
  );

  if (decrypt_size != inbuff_map_info.size)
    status = -1;

  GST_INFO_OBJECT (this, "Decrypted buffer size= %zu bytes, input buffer "
      "size= %zu bytes", decrypt_size, inbuff_map_info.size);

  g_free (subsample);
  return status;
}

void
GstDrmPREngine::subsample_parse (android::CryptoPlugin::SubSample * subsample,
    guint count, gsize size, GstMapInfo &subsample_map_info, gboolean is_clear)
{
  gsize total_bytes = 0;

  if (is_clear) {
    subsample->mNumBytesOfClearData = size;
    subsample->mNumBytesOfEncryptedData = 0;
    return;
  }

  //In case of full sample encryption
  if (count == 0)
  {
    subsample->mNumBytesOfClearData = 0;
    subsample->mNumBytesOfEncryptedData = size;
    return;
  }

  // Extract number of encrypted and clear bytes from subsample buffer of GstProtectionMeta
  // As per ISO/IEC CD 23001-7 spec, size of the subsample value should be 6 bytes
  // where MSB 2 bytes specify the number of clear bytes and the next 4 bytes specify
  // the number of encrypted bytes

  for (gint idx = 0; idx < count; idx++) {
    subsample[idx].mNumBytesOfClearData = 0;
    subsample[idx].mNumBytesOfEncryptedData = 0;
    for (guint pos = 0; pos < SUBSAMPLE_INFO_LEN; pos++) {
      if (pos < CLEAR_BYTES_SIZE)
        subsample[idx].mNumBytesOfClearData =
            (subsample[idx].mNumBytesOfClearData << 8) |
                subsample_map_info.data[(SUBSAMPLE_INFO_LEN * idx) + pos];
      else
        subsample[idx].mNumBytesOfEncryptedData =
            (subsample[idx].mNumBytesOfEncryptedData << 8) |
                subsample_map_info.data[(SUBSAMPLE_INFO_LEN * idx) + pos];
    }

    GST_DEBUG ("Subsample(%d): Number of clear bytes=%u, "
        "encrypted bytes=%u", idx, subsample[idx].mNumBytesOfClearData,
        subsample[idx].mNumBytesOfEncryptedData);

    total_bytes += subsample[idx].mNumBytesOfClearData +
        subsample[idx].mNumBytesOfEncryptedData;
  }

  // Incase of byte-stream stream format in AVC, 6 bytes Access Unit Delimiter
  // is added at the start of each NAL unit. This offset needs to be accounted
  // in clear data bytes.
  if (total_bytes < size)
    subsample[0].mNumBytesOfClearData += size - total_bytes;
}

#ifdef ENABLE_WIDEVINE
gboolean
GstDrmWVEngine::drm_plugin_init (void *session_id, void* instance)
{
  drm_plugin = static_cast<widevine::Cdm *> (instance);
  this->session_id = static_cast<gchar *> (session_id);

  GST_RETURN_VAL_IF_FAIL (drm_plugin != NULL, FALSE,
      "Widevine CDM instance is null");

  GST_INFO_OBJECT (this, "Widevine DRM plugin initialized !");

  return TRUE;
}

gint
GstDrmWVEngine::decrypt (gboolean secure, GstMapInfo keyid_map_info,
    GstMapInfo inbuff_map_info, GstMapInfo subsample_map_info, guint subsample_count,
    const guint8 *iv_arr, native_handle_t *nh, gboolean is_clear)
{
  widevine::Cdm::Subsample *subsample = g_new0 (widevine::Cdm::Subsample,
      (subsample_count | 1u));

  subsample_parse (subsample, subsample_count, inbuff_map_info.size,
      subsample_map_info, is_clear);

  widevine::Cdm::InputBuffer *in_buf = g_new0 (widevine::Cdm::InputBuffer, 1);
  if (!is_clear) {
    in_buf->iv = iv_arr;
    in_buf->iv_length = IV_SIZE;
  }
  in_buf->data = static_cast<const uint8_t*> (inbuff_map_info.data);
  in_buf->data_length = inbuff_map_info.size;
  in_buf->subsamples = static_cast<const widevine::Cdm::Subsample*> (subsample);
  in_buf->subsamples_length = (subsample_count | 1u);

  widevine::Cdm::OutputBuffer *out_buf = g_new0 (widevine::Cdm::OutputBuffer, 1);
  out_buf->data = nh;
  out_buf->data_offset = 0;
  out_buf->data_length = inbuff_map_info.size;

  widevine::Cdm::Sample *sample = g_new0 (widevine::Cdm::Sample, 1);
  sample->input = *in_buf;
  sample->output = *out_buf;

  widevine::Cdm::Pattern pattern;
  widevine::Cdm::DecryptionBatch *batch = g_new0 (widevine::Cdm::DecryptionBatch, 1);
  batch->samples = static_cast<const widevine::Cdm::Sample*> (sample);
  batch->samples_length = 1;
  if (!is_clear) {
    batch->key_id = static_cast<const uint8_t*> (keyid_map_info.data);
    batch->key_id_length = keyid_map_info.size;
  }
  batch->pattern = pattern;
  batch->is_secure = secure;
  batch->encryption_scheme = (is_clear ? widevine::Cdm::kClear :
      widevine::Cdm::kAesCtr);

  widevine::Cdm::Status status = drm_plugin->decrypt (session_id,
      static_cast<const widevine::Cdm::DecryptionBatch> (*batch));

  g_free (subsample);
  g_free (in_buf);
  g_free (out_buf);
  g_free (sample);
  g_free (batch);

  return status;
}

void
GstDrmWVEngine::subsample_parse (widevine::Cdm::Subsample *subsample,
    guint count, gsize size, GstMapInfo &subsample_map_info, gboolean is_clear)
{
  gsize total_bytes = 0;

  if (is_clear) {
    subsample->clear_bytes = size;
    subsample->protected_bytes = 0;
    return;
  }

  //In case of full sample encryption
  if (count == 0)
  {
    subsample->clear_bytes = 0;
    subsample->protected_bytes = size;
    return;
  }

  // Extract number of encrypted and clear bytes from subsample buffer of GstProtectionMeta
  // As per ISO/IEC CD 23001-7 spec, size of the subsample value should be 6 bytes
  // where MSB 2 bytes specify the number of clear bytes and the next 4 bytes specify
  // the number of encrypted bytes

  for (gint idx = 0; idx < count; idx++) {
    subsample[idx].clear_bytes = 0;
    subsample[idx].protected_bytes = 0;
    for (guint pos = 0; pos < SUBSAMPLE_INFO_LEN; pos++) {
      if (pos < CLEAR_BYTES_SIZE)
        subsample[idx].clear_bytes =
            (subsample[idx].clear_bytes << 8) |
                subsample_map_info.data[(SUBSAMPLE_INFO_LEN * idx) + pos];
      else
        subsample[idx].protected_bytes =
            (subsample[idx].protected_bytes << 8) |
                subsample_map_info.data[(SUBSAMPLE_INFO_LEN * idx) + pos];
    }

    GST_DEBUG ("Subsample(%d): Number of clear bytes=%u, "
        "encrypted bytes=%u", idx, subsample[idx].clear_bytes,
        subsample[idx].protected_bytes);

    total_bytes += subsample[idx].clear_bytes +
        subsample[idx].protected_bytes;
  }

  // Incase of byte-stream stream format in AVC, 6 bytes Access Unit Delimiter
  // is added at the start of each NAL unit. This offset needs to be accounted
  // in clear data bytes.
  if (total_bytes < size)
    subsample[0].clear_bytes += size - total_bytes;
}
#endif

GstDrmDecryptorEngine*
gst_drm_decryptor_engine_new (const gchar *sys_id, gpointer session_id,
    gpointer instance)
{
  GstDrmDecryptorEngine *engine = nullptr;

  if (g_strcasecmp (sys_id, PLAYREADY_SYSTEM_ID) == 0) {
    engine = GstDrmPREngine::get_instance(session_id);
#ifdef ENABLE_WIDEVINE
  } else if (g_strcasecmp (sys_id, WIDEVINE_SYSTEM_ID) == 0) {
    engine = GstDrmWVEngine::get_instance(session_id, instance);
  } else {
    //TODO: Add a property for app to provide the DRM to select.
    GST_ERROR ("Invalid system id: %s Selecting Widevine DRM as default", sys_id);
    engine = GstDrmWVEngine::get_instance(session_id, instance);
#else
  } else {
    GST_ERROR ("Invalid system id: %s", sys_id);
#endif
  }
  return engine;
}

gint
gst_drm_decryptor_engine_execute (GstDrmDecryptorEngine* engine,
    GstBuffer *in_buffer,  GstBuffer *out_buffer)
{
  GstProtectionMeta *pmeta = gst_buffer_get_protection_meta (in_buffer);
  GstMapInfo inbuff_map_info, keyid_map_info, iv_map_info, subsample_map_info;
  GstBuffer *key_id_buf = nullptr, *iv_buf = nullptr, *subsample_buf = nullptr;
  guint subsample_count = 0;
  gboolean secure, is_clear = false;
  guint8 iv_arr[IV_SIZE];

  if (pmeta) {
    gst_structure_get_boolean (pmeta->info, "encrypted", &secure);
    gst_structure_get_uint (pmeta->info, "subsample_count", &subsample_count);

    key_id_buf = gst_value_get_buffer (
        gst_structure_get_value (pmeta->info, "kid"));
    gst_buffer_map (key_id_buf, &keyid_map_info, GST_MAP_READ);
    iv_buf = gst_value_get_buffer (
        gst_structure_get_value (pmeta->info, "iv"));
    gst_buffer_map (iv_buf, &iv_map_info, GST_MAP_READ);
    if (subsample_count > 0) {
      subsample_buf = gst_value_get_buffer (
          gst_structure_get_value (pmeta->info, "subsamples"));
      gst_buffer_map (subsample_buf, &subsample_map_info, GST_MAP_READ);
    }

    // Playready/Widevine API expects IV of size 16 bytes. If the IV of input is
    // of 8 bytes, the remaining 8 bytes should be appended as 0.
    memset (iv_arr, 0x00, IV_SIZE);
    for (gint idx = 0; idx < iv_map_info.size; idx++) {
      iv_arr[idx] = iv_map_info.data[idx];
    }
  } else {
    GST_WARNING_OBJECT (engine, "No protection metadata found! Passing data as "
        "clear content");
    is_clear = true;
    secure = true;
    subsample_count = 1;
  }

  gst_buffer_map (in_buffer, &inbuff_map_info, GST_MAP_READ);

  native_handle_t *nh = native_handle_create (1 /*numFds*/, 0 /*numInts*/);
  {
    GstMemory *mem = gst_buffer_get_memory (out_buffer, 0);
    mem->size = inbuff_map_info.size;
    nh->data[0] = gst_fd_memory_get_fd (mem);
  }

  gint status = engine->decrypt (secure, keyid_map_info, inbuff_map_info,
      subsample_map_info, subsample_count, iv_arr, nh, is_clear);
  if (status != 0)
    GST_ERROR_OBJECT (engine, "Decryption failed with error: %d", status);

  gst_buffer_unmap (in_buffer, &inbuff_map_info);
  if (pmeta) {
    if (subsample_buf != nullptr)
      gst_buffer_unmap (subsample_buf, &subsample_map_info);
    gst_buffer_unmap (key_id_buf, &keyid_map_info);
    gst_buffer_unmap (iv_buf, &iv_map_info);
  }
  native_handle_delete (nh);

  return status;
}