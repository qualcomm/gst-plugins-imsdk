/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <dma_apps.h>
#include <rpcmem.h>

#include <dlfcn.h>
#include <gst/video/video.h>
#include <gst/utils/common-utils.h>

#define URI_DOMAIN  dma_apps_URI CDSP_DOMAIN

#define GST_HEXAGON_SUB_MODULE_CAST(obj) ((GstHexagonSubModule*)(obj))

#define GST_HEXAGON_MODULE_CAPS \
    "video/x-raw(memory:GBM), " \
    "width = (int) [ 128, 3840 ], " \
    "height = (int) [ 8, 2160 ], " \
    "format=(string){ NV12, NV12_Q08C }"

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_HEXAGON_MODULE_CAPS);

typedef struct _GstHexagonSubModule GstHexagonSubModule;

/**
 * _GstHexagonSubModule:
 * @skel_handle: Skel Library handle.
 * @stub_handle: Stub Library handle.
 * @rpc_handle: Adsprpc Library handle.
 *
 * Hexagon SubModule Structure for UBWC DMA Task.
 */
struct _GstHexagonSubModule {
  remote_handle64   skel_handle;
  void              *stub_handle;
  void              *rpc_handle;

  void              *(*rpcmem_alloc)
                    (int heapid,
                     uint32 flags,
                     int size);
  void              (*rpcmem_free)
                    (void* po);
  int               (*remote_session_control)
                    (uint32_t req,
                    void* data,
                    uint32_t data_len);

  int               (*dma_apps_open)
                    (const char* uri,
                     remote_handle64* h);
  int               (*dma_apps_close)
                    (remote_handle64 h);

  AEEResult         (*dma_apps_setClocks)
                    (remote_handle64 _h,
                     int32 powerLevel,
                     int32 latency,
                     int32 dcvsEnable);
  AEEResult         (*dma_apps_memcpy_scratch_size)
                    (remote_handle64 _h,
                     int* size);
  AEEResult         (*dma_apps_memcpy_open)
                    (remote_handle64 _h,
                     const dma_apps_cfg_t* cfg,
                     dma_apps_hdl_t* p_hdl);
  AEEResult         (*dma_apps_memcpy_run)
                    (remote_handle64 _h,
                     const dma_apps_hdl_t* p_hdl,
                     const unsigned char* src,
                     int srcLen,
                     unsigned char* dst,
                     int dstLen);
  AEEResult         (*dma_apps_memcpy_close)
                    (remote_handle64 _h,
                     const dma_apps_hdl_t* hdl);
};

static gboolean
load_symbol (gpointer* method, gpointer handle, const gchar* name)
{
  *(method) = dlsym (handle, name);
  if (NULL == *(method)) {
    GST_ERROR ("Failed to find symbol %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

gpointer
gst_hexagon_submodule_open (void)
{
  GstHexagonSubModule *submodule = NULL;
  gboolean success = FALSE;

  submodule = g_slice_new0 (GstHexagonSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  submodule->skel_handle = (remote_handle64) -1;

  submodule->rpc_handle = dlopen ("libadsprpc.so", RTLD_NOW | RTLD_GLOBAL);
  if (submodule->rpc_handle == NULL) {
    GST_ERROR ("Fail to load adsprpc lib: %s", dlerror());
    return NULL;
  }

  submodule->stub_handle = dlopen ("libdma_apps_stub.so", RTLD_LAZY);
  if (submodule->stub_handle == NULL) {
    GST_ERROR ("Fail to load ubwcdma stub lib: %s", dlerror());
    dlclose (submodule->rpc_handle);
    return NULL;
  }

  success = load_symbol ((gpointer*)&submodule->rpcmem_alloc,
      submodule->rpc_handle, "rpcmem_alloc");
  success &= load_symbol ((gpointer*)&submodule->rpcmem_free,
      submodule->rpc_handle, "rpcmem_free");
  success &= load_symbol ((gpointer*)&submodule->remote_session_control,
      submodule->rpc_handle, "remote_session_control");

  success &= load_symbol ((gpointer*)&submodule->dma_apps_open,
      submodule->stub_handle, "dma_apps_open");
  success &= load_symbol ((gpointer*)&submodule->dma_apps_close,
      submodule->stub_handle, "dma_apps_close");

  success &= load_symbol ((gpointer*)&submodule->dma_apps_setClocks,
      submodule->stub_handle, "dma_apps_setClocks");
  success &= load_symbol ((gpointer*)&submodule->dma_apps_memcpy_scratch_size,
      submodule->stub_handle, "dma_apps_memcpy_scratch_size");
  success &= load_symbol ((gpointer*)&submodule->dma_apps_memcpy_open,
      submodule->stub_handle, "dma_apps_memcpy_open");
  success &= load_symbol ((gpointer*)&submodule->dma_apps_memcpy_run,
      submodule->stub_handle, "dma_apps_memcpy_run");
  success &= load_symbol ((gpointer*)&submodule->dma_apps_memcpy_close,
      submodule->stub_handle, "dma_apps_memcpy_close");

  if (!success) {
    GST_ERROR ("Fail to load symbols from ubwcdma stub lib");

    dlclose (submodule->stub_handle);
    dlclose (submodule->rpc_handle);

    g_slice_free (GstHexagonSubModule, submodule);

    return NULL;
  }

  return (gpointer) submodule;
}

void
gst_hexagon_submodule_close (gpointer instance)
{
  GstHexagonSubModule *submodule = GST_HEXAGON_SUB_MODULE_CAST (instance);

  if (NULL == submodule)
    return;

  if (submodule->skel_handle != (remote_handle64) -1)
    submodule->dma_apps_close (submodule->skel_handle);

  if (submodule->stub_handle != NULL)
    dlclose (submodule->stub_handle);

  if (submodule->rpc_handle != NULL)
    dlclose (submodule->rpc_handle);

  g_slice_free (GstHexagonSubModule, submodule);
  submodule = NULL;

  return;
}

gboolean
gst_hexagon_submodule_init (gpointer instance)
{
  GstHexagonSubModule *submodule = GST_HEXAGON_SUB_MODULE_CAST (instance);

  int ret = 0;
  int32_t power_level = 6, latency = 100, dcvs_enable = 0;
  struct remote_rpc_control_unsigned_module ctr_module = { 0, };

  ctr_module.enable = 1;
  ctr_module.domain = CDSP_DOMAIN_ID;

  submodule->remote_session_control (DSPRPC_CONTROL_UNSIGNED_MODULE,
      (void*) &ctr_module, sizeof(ctr_module));

  ret = submodule->dma_apps_open (URI_DOMAIN, &(submodule->skel_handle));
  if (ret) {
    GST_ERROR ("Error 0x%x: unable to create fastrpc session on CDSP", ret);
    goto cleanup;
  }

  ret = submodule->dma_apps_setClocks (submodule->skel_handle,
      power_level, latency, dcvs_enable);

  GST_DEBUG ("Calling dma_apps_setClocks() : 0x%x ", ret);

  if (ret != 0) {
    GST_ERROR ("Error initializing the Q6 for this application");
    goto cleanup;
  }

  return TRUE;

cleanup:
  if (submodule->skel_handle != (remote_handle64) -1)
    submodule->dma_apps_close (submodule->skel_handle);

  return FALSE;
}

GstCaps *
gst_hexagon_submodule_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&modulecaps);
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

gboolean
gst_hexagon_submodule_process (gpointer instance, GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstHexagonSubModule *submodule = GST_HEXAGON_SUB_MODULE_CAST (instance);
  GstVideoMeta *vmeta = NULL;
  dma_apps_hdl_t dma_apps_hdl = { 0, };
  dma_apps_cfg_t dma_apps_cfg = { 0, };
  GstMapInfo inmap = { 0, }, outmap = { 0, };
  gint ret = 0;

  if (!gst_buffer_map (inbuffer, &inmap, GST_MAP_READ)) {
    GST_ERROR ("Failed to map input buffer!");
    return FALSE;
  }

  if (!gst_buffer_map (outbuffer, &outmap, GST_MAP_READWRITE)) {
    GST_ERROR ("Failed to map output buffer!");
    gst_buffer_unmap (inbuffer, &inmap);

    return FALSE;
  }

  ret = submodule->dma_apps_memcpy_scratch_size (submodule->skel_handle,
      &dma_apps_hdl.app_scratchLen);

  if (ret != AEE_SUCCESS) {
    GST_ERROR ("Error querying the scratch size required by the dma memcpy");
    goto cleanup;
  }

  dma_apps_hdl.app_scratch = (uint8_t*) submodule->rpcmem_alloc (RPCMEM_HEAP_ID_SYSTEM,
      RPCMEM_DEFAULT_FLAGS, dma_apps_hdl.app_scratchLen);

  if (dma_apps_hdl.app_scratch == NULL) {
    GST_ERROR ("Error allocating scratch space for the dma memcpy");
    goto cleanup;
  }

  if (!(vmeta = gst_buffer_get_video_meta (outbuffer))) {
    GST_ERROR ("Output buffer has no meta!");
    goto cleanup;
  }

  // dma_apps_cfg needs to accpet align size
  dma_apps_cfg.frm_wd = GST_ROUND_UP_128 (vmeta->width);
  dma_apps_cfg.frm_ht = GST_ROUND_UP_32 (vmeta->height);
  dma_apps_cfg.fmt = (dma_apps_pix_fmt) FMT_NV12;
  dma_apps_cfg.src_is_ubwc =
      (vmeta->format == GST_VIDEO_FORMAT_NV12) ? TRUE : FALSE;
  dma_apps_cfg.dst_is_ubwc =
      (vmeta->format == GST_VIDEO_FORMAT_NV12_Q08C) ? TRUE : FALSE;

  ret = submodule->dma_apps_memcpy_open (submodule->skel_handle,
      &dma_apps_cfg, &dma_apps_hdl);

  if (ret != AEE_SUCCESS) {
    GST_ERROR ("Error opening the dma memcpy app");
    goto cleanup;
  }

  ret = submodule->dma_apps_memcpy_run (submodule->skel_handle,
      &dma_apps_hdl, inmap.data, inmap.size, outmap.data, outmap.size);

  if (ret != AEE_SUCCESS) {
    GST_ERROR ("Error running the dma memcpy app");

    ret = submodule->dma_apps_memcpy_close (submodule->skel_handle,
        &dma_apps_hdl);

    goto cleanup;
  }

  ret = submodule->dma_apps_memcpy_close (submodule->skel_handle,
      &dma_apps_hdl);

  if (ret != AEE_SUCCESS)
    GST_ERROR ("Error closing the dma memcpy app");

  if (dma_apps_hdl.app_scratch != NULL)
    submodule->rpcmem_free (dma_apps_hdl.app_scratch);

  gst_buffer_unmap (inbuffer, &inmap);
  gst_buffer_unmap (outbuffer, &outmap);

  return (ret == AEE_SUCCESS) ? TRUE : FALSE;

cleanup:
  gst_buffer_unmap (inbuffer, &inmap);
  gst_buffer_unmap (outbuffer, &outmap);

  if (dma_apps_hdl.app_scratch != NULL)
    submodule->rpcmem_free (dma_apps_hdl.app_scratch);

  return FALSE;
}
