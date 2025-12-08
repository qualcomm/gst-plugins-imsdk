/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "hexagon-module.h"

#include <dlfcn.h>

#define GST_CAT_DEFAULT gst_hexagon_module_debug
GST_DEBUG_CATEGORY (gst_hexagon_module_debug);

#define GST_HEXAGON_MODULE_OPEN_FUNC      "gst_hexagon_submodule_open"
#define GST_HEXAGON_MODULE_CLOSE_FUNC     "gst_hexagon_submodule_close"
#define GST_HEXAGON_MODULE_INIT_FUNC      "gst_hexagon_submodule_init"
#define GST_HEXAGON_MODULE_CAPS_FUNC      "gst_hexagon_submodule_caps"
#define GST_HEXAGON_MODULE_PROCESS_FUNC   "gst_hexagon_submodule_process"

/**
 * _GstHexagonModule:
 * @handle: Library handle.
 * @name: Library (Module) name.
 * @submodule: Pointer to private module structure.
 *
 * @open: Function pointer to the submodule 'gst_hexagon_submodule_open' API.
 * @close: Function pointer to the submodule 'gst_hexagon_submodule_close' API.
 * @init: Function pointer to the submodule 'gst_hexagon_submodule_open' and
 *        'gst_hexagon_submodule_init' APIs.
 * @caps: Function pointer to the submodule 'gst_hexagon_submodule_caps' API.
 * @process: Function pointer to the submodule 'gst_hexagon_submodule_process' API.
 *
 * Hexagon processing interface for submodule.
 */
struct _GstHexagonModule {
  gpointer             handle;
  gchar                *name;
  gpointer             submodule;

  /// Interface functions.
  GstHexagonModuleOpen      open;
  GstHexagonModuleClose     close;
  GstHexagonModuleInit      init;
  GstHexagonModuleCaps      caps;
  GstHexagonModuleProcess   process;
};

GType
gst_hexagon_modules_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_HEXAGON_MODULE_NONE,
        "None of Component will be implemented", "none"
    },
    { GST_HEXAGON_MODULE_UBWC_DMA,
        "Implementing UBWC-DMA Component from Hexagon", "ubwcdma"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
      gtype = g_enum_register_static ("GstHexagonModules", variants);

  return gtype;
}

static inline void
gst_hexagon_module_initialize_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_hexagon_module_debug, "hexagon-module", 0,
        "QTI Hexagon processing module");
    g_once_init_leave (&catonce, TRUE);
  }
}

static gboolean
gst_hexagon_module_symbol (gpointer handle, const gchar * name, gpointer * symbol)
{
  *(symbol) = dlsym (handle, name);
  if (NULL == *(symbol)) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

GstHexagonModule *
gst_hexagon_module_new (const gchar * type, const gchar * name)
{
  GstHexagonModule *module = NULL;
  gchar *location = NULL;
  gboolean success = TRUE;

  // Initialize the debug category.
  gst_hexagon_module_initialize_debug_category ();

  module = g_new0 (GstHexagonModule, 1);
  location = g_strdup_printf ("%s/lib%s%s.so", GST_HEXAGON_MODULES_DIR, type, name);

  module->name = g_strdup_printf ("%s%s", type, name);
  module->handle = dlopen (location, RTLD_NOW);

  g_free (location);

  if (module->handle == NULL) {
    GST_ERROR ("Failed to open %s library, error: %s!", module->name, dlerror());
    gst_hexagon_module_close (module);
    return NULL;
  }

  success &= gst_hexagon_module_symbol (module->handle,
      GST_HEXAGON_MODULE_OPEN_FUNC, (gpointer) &(module)->open);
  success &= gst_hexagon_module_symbol (module->handle,
      GST_HEXAGON_MODULE_CLOSE_FUNC, (gpointer) &(module)->close);
  success &= gst_hexagon_module_symbol (module->handle,
      GST_HEXAGON_MODULE_INIT_FUNC, (gpointer) &(module)->init);
  success &= gst_hexagon_module_symbol (module->handle,
      GST_HEXAGON_MODULE_CAPS_FUNC, (gpointer) &(module)->caps);
  success &= gst_hexagon_module_symbol (module->handle,
      GST_HEXAGON_MODULE_PROCESS_FUNC, (gpointer) &(module)->process);

  if (!success) {
    gst_hexagon_module_close (module);
    return NULL;
  }

  GST_INFO ("Created %s module: %p", module->name, module);
  return module;
}

void
gst_hexagon_module_close (GstHexagonModule * module)
{
  if (NULL == module)
    return;

  if (module->submodule != NULL)
    module->close (module->submodule);

  if (module->handle != NULL) {
    dlclose (module->handle);
    module->handle = NULL;
  }

  GST_INFO ("Destroyed %s module: %p", module->name, module);

  if (module->name != NULL) {
    g_free (module->name);
    module->name = NULL;
  }

  g_free (module);
  module = NULL;

  return;
}

gboolean
gst_hexagon_module_init (GstHexagonModule * module)
{
  g_return_val_if_fail (module != NULL, FALSE);

  if (module->submodule == NULL)
    module->submodule = module->open ();

  if (module->submodule == NULL)
    return FALSE;

  return module->init (module->submodule);
}

GstCaps *
gst_hexagon_module_get_caps (GstHexagonModule * module)
{
  g_return_val_if_fail (module != NULL, NULL);

  if (module->submodule == NULL)
    module->submodule = module->open ();

  if (module->submodule == NULL)
    return NULL;

  return module->caps ();
}

gboolean
gst_hexagon_module_process  (GstHexagonModule * module,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (inbuffer != NULL, FALSE);
  g_return_val_if_fail (outbuffer != NULL, FALSE);

  return module->process (module->submodule, inbuffer, outbuffer);
}
