/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "parsermodule.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>


#define GST_CAT_DEFAULT gst_parser_module_debug
GST_DEBUG_CATEGORY (gst_parser_module_debug);

#define GST_PARSER_MODULE_OPEN_FUNC      "gst_parser_module_open"
#define GST_PARSER_MODULE_CLOSE_FUNC     "gst_parser_module_close"
#define GST_PARSER_MODULE_CONFIGURE_FUNC "gst_parser_module_configure"
#define GST_PARSER_MODULE_PROCESS_FUNC   "gst_parser_module_process"

/**
 * _GstParserModule:
 * @handle: Library handle.
 * @name: Library (Module) name.
 * @submodule: Pointer to private module structure.
 *
 * @open: Function pointer to the submodule 'gst_parser_module_open' API.
 * @close: Function pointer to the submodule 'gst_parser_module_close' API.
 * @configure: Function pointer to the submodule 'gst_parser_module_configure' API.
 * @process: Function pointer to the submodule 'gst_parser_module_process' API.
 *
 * Machine learning interface for post-processing module.
 */
struct _GstParserModule {
  gpointer             handle;
  gchar                *name;
  gpointer             submodule;

  /// Interface functions.
  GstParserModuleOpen      open;
  GstParserModuleClose     close;
  GstParserModuleConfigure configure;
  GstParserModuleProcess   process;
};

static inline void
gst_parser_module_initialize_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_parser_module_debug,
        "mlmetaparsermodule", 0, "QTI ML meta parser module");
    g_once_init_leave (&catonce, TRUE);
  }
}

static gboolean
gst_parser_module_symbol (gpointer handle, const gchar * name, gpointer * symbol)
{
  *(symbol) = dlsym (handle, name);
  if (NULL == *(symbol)) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

GstParserModule *
gst_parser_module_new (const gchar * name)
{
  GstParserModule *module = NULL;
  gchar *location = NULL;
  gboolean success = TRUE;

  // Initialize the debug category.
  gst_parser_module_initialize_debug_category ();

  module = g_new0 (GstParserModule, 1);
  location = g_strdup_printf ("%s/lib%s.so", GST_PARSER_MODULES_DIR, name);

  module->name = g_strdup (name);
  module->handle = dlopen (location, RTLD_NOW);

  g_free (location);

  if (module->handle == NULL) {
    GST_ERROR ("Failed to open %s library, error: %s!", module->name, dlerror());
    gst_parser_module_free (module);
    return NULL;
  }

  success &= gst_parser_module_symbol (module->handle,
      GST_PARSER_MODULE_OPEN_FUNC, (gpointer) &(module)->open);
  success &= gst_parser_module_symbol (module->handle,
      GST_PARSER_MODULE_CLOSE_FUNC, (gpointer) &(module)->close);
  success &= gst_parser_module_symbol (module->handle,
      GST_PARSER_MODULE_CONFIGURE_FUNC, (gpointer) &(module)->configure);
  success &= gst_parser_module_symbol (module->handle,
      GST_PARSER_MODULE_PROCESS_FUNC, (gpointer) &(module)->process);

  if (!success) {
    gst_parser_module_free (module);
    return NULL;
  }

  GST_INFO ("Created %s module: %p", module->name, module);
  return module;
}

void
gst_parser_module_free (GstParserModule * module)
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
gst_parser_module_init (GstParserModule * module)
{
  g_return_val_if_fail (module != NULL, FALSE);

  if (module->submodule == NULL)
    module->submodule = module->open ();

  return (module->submodule != NULL) ? TRUE : FALSE;
}

gboolean
gst_parser_module_set_opts (GstParserModule * module, GstStructure * options)
{
  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (options != NULL, FALSE);

  return module->configure (module->submodule, options);
}

gboolean
gst_parser_module_execute (GstParserModule * module, GstBuffer * inbuffer,
    gchar ** output)
{
  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (inbuffer != NULL, FALSE);
  g_return_val_if_fail (output != NULL, FALSE);

  return module->process (module->submodule, inbuffer, output);
}

GEnumValue *
gst_parser_enumarate_modules (const gchar * type)
{
  GEnumValue *variants = NULL;
  GDir *directory = NULL;
  const gchar *filename = NULL;
  gchar *prefix = NULL, *string = NULL, *name = NULL, *shortname = NULL;
  guint idx = 0, n_bytes = 0;

  n_bytes = sizeof (GEnumValue);
  variants = g_malloc (n_bytes * 2);

  // Initialize the default value.
  variants[idx].value = idx;
  variants[idx].value_name = "No module, default invalid mode";
  variants[idx].value_nick = "none";

  idx++;

  directory = g_dir_open (GST_PARSER_MODULES_DIR, 0, NULL);
  prefix = g_strdup_printf ("lib%s", type);

  while ((directory != NULL) && (filename = g_dir_read_name (directory))) {
    gboolean isvalid = FALSE;

    if (!g_str_has_prefix (filename, prefix))
      continue;

    if (!g_str_has_suffix (filename, ".so"))
      continue;

    string = g_strdup_printf ("%s/%s", GST_PARSER_MODULES_DIR, filename);
    isvalid = !g_file_test (string, G_FILE_TEST_IS_DIR | G_FILE_TEST_IS_SYMLINK);
    g_free (string);

    if (!isvalid)
      continue;

    // Trim the 'lib' prefix and '.so' suffix.
    name = g_strndup (filename + 3, strlen (filename) - 6);

    // Extract only the unique module name.
    shortname = g_utf8_strdown (name + strlen (type), -1);

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

  g_free (prefix);

  if (directory != NULL)
    g_dir_close (directory);

  return variants;
}
