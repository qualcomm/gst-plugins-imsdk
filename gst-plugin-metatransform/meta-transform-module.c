/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "meta-transform-module.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>


#define GST_CAT_DEFAULT gst_meta_transform_module_debug
GST_DEBUG_CATEGORY (gst_meta_transform_module_debug);

#define GST_META_MODULE_OPEN_FUNC      "gst_meta_module_open"
#define GST_META_MODULE_CLOSE_FUNC     "gst_meta_module_close"
#define GST_META_MODULE_PROCESS_FUNC   "gst_meta_module_process"


/**
 * GstMetaModuleOpen:
 *
 * Create a new instance of the private meta processing module structure.
 *
 * Module must implement function called 'gst_meta_module_open' with the same
 * arguments and return type.
 *
 * return: Pointer to private module instance on success or NULL on failure
 */
typedef gpointer (*GstMetaModuleOpen) (GstStructure * settings);

/**
 * GstMetaModuleClose:
 * @submodule: Pointer to the private meta processing module instance.
 *
 * Deinitialize and free the private meta processing module instance.
 *
 * Module must implement function called 'gst_meta_module_close' with the
 * same arguments and return type.
 *
 * return: NONE
 */
typedef void (*GstMetaModuleClose) (gpointer submodule);

/**
 * GstMetaModuleProcess:
 * @submodule: Pointer to the private meta processing module instance.
 * @buffer: Buffer with possible meta which will be processed.
 *
 * Parses incoming buffer containing GST metas and perform module specific
 * filtering, processing, conversion, etc. on the chosen meta.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean (*GstMetaModuleProcess) (gpointer submodule, GstBuffer * buffer);

/**
 * GstMetaTransformModule:
 * @handle: Library handle.
 * @name: Library (Module) name.
 * @submodule: Pointer to private module structure.
 *
 * @open: Function pointer to the submodule 'gst_meta_module_open' API.
 * @close: Function pointer to the submodule 'gst_meta_module_close' API.
 * @process: Function pointer to the submodule 'gst_meta_module_process' API.
 *
 * Interface for meta processing module.
 */
struct _GstMetaTransformModule {
  gpointer               handle;
  gchar                  *name;
  gpointer               submodule;

  /// Interface functions.
  GstMetaModuleOpen      open;
  GstMetaModuleClose     close;
  GstMetaModuleProcess   process;
};

static inline void
gst_meta_transform_module_init_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_meta_transform_module_debug,
        "meta-transform-module", 0, "QTI Meta Transform Module");
    g_once_init_leave (&catonce, TRUE);
  }
}

static inline gboolean
gst_load_symbol (gpointer handle, const gchar * name, gpointer * symbol)
{
  *(symbol) = dlsym (handle, name);
  if (NULL == *(symbol)) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

static GEnumValue *
gst_enumarate_module_libraries (const gchar * dirname, const gchar * prefix)
{
  GEnumValue *variants = NULL;
  GDir *directory = NULL;
  const gchar *filename = NULL;
  gchar *string = NULL, *name = NULL, *shortname = NULL;
  guint idx = 0, n_bytes = 0;

  n_bytes = sizeof (GEnumValue);
  variants = g_malloc (n_bytes * 2);

  // Initialize the default value.
  variants[idx].value = idx;
  variants[idx].value_name = "No module, default invalid mode";
  variants[idx].value_nick = "none";

  idx++;

  directory = g_dir_open (dirname, 0, NULL);

  while ((directory != NULL) && (filename = g_dir_read_name (directory))) {
    gboolean isvalid = FALSE;

    if (!g_str_has_prefix (filename, prefix))
      continue;

    if (!g_str_has_suffix (filename, ".so"))
      continue;

    string = g_strdup_printf ("%s/%s", dirname, filename);
    isvalid = !g_file_test (string, G_FILE_TEST_IS_DIR | G_FILE_TEST_IS_SYMLINK);
    g_free (string);

    if (!isvalid)
      continue;

    // Trim the 'lib' prefix and '.so' suffix.
    name = g_strndup (filename + 3, strlen (filename) - 6);

    // Extract only the unique module name.
    shortname = g_utf8_strdown (name + strlen (prefix) - 3, -1);

    variants = g_realloc (variants, n_bytes * (idx + 2));

    variants[idx].value = idx;
    variants[idx].value_name = name;
    variants[idx].value_nick = shortname;

    idx++;
  }

  // Last enum entry should be zero.
  variants[idx].value = 0;
  variants[idx].value_name = NULL;
  variants[idx].value_nick = NULL;

  if (directory != NULL)
    g_dir_close (directory);

  return variants;
}

GType
gst_meta_transform_backend_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_enumarate_module_libraries (GST_META_TRANSFORM_MODULES_DIR,
      "libmeta-transform-");
  gtype = g_enum_register_static ("GstMetaTranformModules", variants);

  return gtype;
}

GstMetaTransformModule *
gst_meta_transform_module_new (const gchar * name, GstStructure * settings)
{
  GstMetaTransformModule *module = NULL;
  gchar *location = NULL;
  gboolean success = TRUE;

  // Initialize the debug category.
  gst_meta_transform_module_init_debug_category ();

  module = g_new0 (GstMetaTransformModule, 1);
  location = g_strdup_printf ("%s/lib%s.so", GST_META_TRANSFORM_MODULES_DIR, name);

  module->name = g_strdup (name);
  module->handle = dlopen (location, RTLD_NOW);

  g_free (location);

  if (module->handle == NULL) {
    GST_ERROR ("Failed to open %s library, error: %s!", module->name, dlerror());
    gst_meta_transform_module_free (module);
    return NULL;
  }

  success &= gst_load_symbol (module->handle, GST_META_MODULE_OPEN_FUNC,
      (gpointer) &(module)->open);
  success &= gst_load_symbol (module->handle, GST_META_MODULE_CLOSE_FUNC,
      (gpointer) &(module)->close);
  success &= gst_load_symbol (module->handle, GST_META_MODULE_PROCESS_FUNC,
      (gpointer) &(module)->process);

  if (!success) {
    gst_meta_transform_module_free (module);
    return NULL;
  }

  if ((module->submodule = module->open (settings)) == NULL) {
    GST_ERROR ("Failed to open sibmodule for %s!", module->name);
    gst_meta_transform_module_free (module);
    return NULL;
  }

  GST_INFO ("Created %s module: %p", module->name, module);
  return module;
}

void
gst_meta_transform_module_free (GstMetaTransformModule * module)
{
  if (NULL == module)
    return;

  if (module->submodule != NULL)
    module->close (module->submodule);

  if (module->handle != NULL)
    dlclose (module->handle);

  GST_INFO ("Destroyed %s module: %p", module->name, module);

  if (module->name != NULL)
    g_free (module->name);

  g_free (module);
  return;
}

gboolean
gst_meta_transform_module_process (GstMetaTransformModule * module,
    GstBuffer * buffer)
{
  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);

  return module->process (module->submodule, buffer);
}
