/*
* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "c2-wrapper.h"

#include <dlfcn.h>

#define GST_CAT_DEFAULT gst_c2_venc_context_debug_category()
static GstDebugCategory *
gst_c2_venc_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtic2engine", 0,
        "C2 encoder context");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

struct _GstC2Wrapper {
  std::shared_ptr<C2ComponentStore> compstore;
  gpointer dlhandle;
  C2ComponentWrapper *component;
};

struct QC2ComponentStoreFactory {
    virtual ~QC2ComponentStoreFactory() = default;
    virtual std::shared_ptr<C2ComponentStore> getInstance() = 0;
};

using QC2ComponentStoreFactoryGetter_t
    = QC2ComponentStoreFactory* (*)(int majorVersion, int minorVersion);


GstC2Wrapper *
gst_c2_wrapper_new ()
{
  GstC2Wrapper *wrapper = NULL;
  wrapper = new GstC2Wrapper();
  g_return_val_if_fail (wrapper != NULL, NULL);

  wrapper->dlhandle = dlopen("libqcodec2_core.so", RTLD_NOW);
  if (wrapper->dlhandle == NULL) {
    g_slice_free (GstC2Wrapper, wrapper);
    GST_ERROR("failed to open %s: %s", "libqcodec2_core.so", dlerror());
    return NULL;
  }

  auto factoryGetter =
      (QC2ComponentStoreFactoryGetter_t) dlsym (wrapper->dlhandle,
      "QC2ComponentStoreFactoryGetter");

  if (factoryGetter == NULL) {
    GST_ERROR("failed to load symbol QC2ComponentStoreFactoryGetter: %s",
        dlerror());
    dlclose(wrapper->dlhandle);
    g_free(wrapper);
    return NULL;
  }

  auto c2StoreFactory = (*factoryGetter) (1, 0); // get version 1.0
  if (c2StoreFactory == NULL) {
    GST_ERROR("failed to get Store factory !");
    dlclose(wrapper->dlhandle);
    g_free(wrapper);
    return NULL;
  } else {
    GST_INFO ("Successfully get store factory");
  }

  wrapper->compstore = c2StoreFactory->getInstance();
  if (wrapper->compstore == NULL) {
    GST_ERROR("failed to get Component Store instance!");
    dlclose(wrapper->dlhandle);
    g_free(wrapper);
    return NULL;
  }

  GST_INFO ("Created C2 wrapper: %p", wrapper);
  return wrapper;
}

void
gst_c2_wrapper_free (GstC2Wrapper * wrapper)
{
  dlclose (wrapper->dlhandle);
  GST_INFO ("Destroyed C2 wrapper: %p", wrapper);
  delete (wrapper);
}

gboolean
gst_c2_wrapper_create_component (GstC2Wrapper * wrapper,
    const gchar * name, event_handler_cb callback, gpointer userdata) {
  gboolean ret = FALSE;
  c2_status_t c2Status = C2_NO_INIT;

  if (wrapper->component) {
    GST_INFO ("Delete previous component");
    delete wrapper->component;
  }

  wrapper->component = new C2ComponentWrapper (wrapper->compstore, name);
  wrapper->component->SetHandler (callback, userdata);

  if (wrapper->component) {
    c2Status = wrapper->component->CreateBlockpool(C2BlockPool::BASIC_LINEAR);
    if (c2Status == C2_OK) {
      ret = TRUE;
    } else {
      GST_ERROR("Failed(%d) to allocate block pool(%d)",
          c2Status, C2BlockPool::BASIC_LINEAR);
    }
    c2Status = wrapper->component->CreateBlockpool(C2BlockPool::BASIC_GRAPHIC);
    if (c2Status == C2_OK) {
      ret = TRUE;
    } else {
      GST_ERROR("Failed(%d) to allocate block pool(%d)",
          c2Status, C2BlockPool::BASIC_GRAPHIC);
    }

#ifdef CODEC2_CONFIG_VERSION_2_0
    c2Status = wrapper->component->CreateBlockpool(C2AllocatorStore::GRAPHIC_NON_CONTIGUOUS);
    if (c2Status == C2_OK) {
      ret = TRUE;
    } else {
      GST_ERROR("Failed(%d) to allocate block pool(%d)",
          c2Status, C2AllocatorStore::GRAPHIC_NON_CONTIGUOUS);
    }
#endif
  }
  GST_INFO ("Created C2 component");
  return ret;
}

gboolean
gst_c2_wrapper_delete_component (GstC2Wrapper * wrapper) {

  if (wrapper->component) {
    GST_INFO ("Delete component");
    delete wrapper->component;
  }
  GST_INFO ("Delete C2 component");
  return TRUE;
}

gint
gst_c2_wrapper_get_block_pool_id (GstC2Wrapper * wrapper) {

  GST_INFO ("Get C2 output block pool id");
  if (wrapper->component) {
    return wrapper->component->GetBlockPoolId();
  }

  return -1;
}

gboolean
gst_c2_wrapper_config_component (GstC2Wrapper * wrapper,
    GPtrArray* config) {

  GST_INFO ("Config C2 component");
  if (wrapper->component) {
    return wrapper->component->Config(config);
  }

  return FALSE;
}

gboolean
gst_c2_wrapper_component_start (GstC2Wrapper * wrapper) {

  GST_INFO ("Start C2 component");
  if (wrapper->component) {
    return wrapper->component->Start();
  }

  return FALSE;
}

gboolean
gst_c2_wrapper_component_stop (GstC2Wrapper * wrapper) {

  GST_INFO ("Stop C2 component");
  if (wrapper->component) {
    return wrapper->component->Stop();
  }

  return FALSE;
}

gboolean
gst_c2_wrapper_component_queue (GstC2Wrapper * wrapper,
    BufferDescriptor * buffer) {

  if (wrapper->component) {
    return wrapper->component->Queue(buffer);
  }

  return FALSE;
}

gboolean
gst_c2_wrapper_free_output_buffer (GstC2Wrapper * wrapper,
    uint64_t buf_idx) {

  if (wrapper->component) {
    return wrapper->component->FreeOutputBuffer(buf_idx);
  }
  return FALSE;
}
