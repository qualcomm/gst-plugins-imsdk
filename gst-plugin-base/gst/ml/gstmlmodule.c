/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "gstmlmodule.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>


#define GST_CAT_DEFAULT gst_ml_module_debug
GST_DEBUG_CATEGORY (gst_ml_module_debug);

#define GST_ML_MODULE_OPEN_FUNC      "gst_ml_module_open"
#define GST_ML_MODULE_CLOSE_FUNC     "gst_ml_module_close"
#define GST_ML_MODULE_CAPS_FUNC      "gst_ml_module_caps"
#define GST_ML_MODULE_CONFIGURE_FUNC "gst_ml_module_configure"
#define GST_ML_MODULE_PROCESS_FUNC   "gst_ml_module_process"

/**
 * _GstMLModule:
 * @handle: Library handle.
 * @name: Library (Module) name.
 * @submodule: Pointer to private module structure.
 *
 * @open: Function pointer to the submodule 'gst_ml_module_open' API.
 * @close: Function pointer to the submodule 'gst_ml_module_close' API.
 * @caps: Function pointer to the submodule 'gst_ml_module_caps' API.
 * @configure: Function pointer to the submodule 'gst_ml_module_configure' API.
 * @process: Function pointer to the submodule 'gst_ml_module_process' API.
 *
 * Machine learning interface for post-processing module.
 */
struct _GstMLModule {
  gpointer             handle;
  gchar                *name;
  gpointer             submodule;

  /// Interface functions.
  GstMLModuleOpen      open;
  GstMLModuleClose     close;
  GstMLModuleCaps      caps;
  GstMLModuleConfigure configure;
  GstMLModuleProcess   process;
};

static inline void
gst_ml_module_initialize_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_ml_module_debug, "mlmodule", 0,
        "QTI ML post-processing module");
    g_once_init_leave (&catonce, TRUE);
  }
}

static gboolean
gst_ml_module_symbol (gpointer handle, const gchar * name, gpointer * symbol)
{
  *(symbol) = dlsym (handle, name);
  if (NULL == *(symbol)) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name, dlerror());
    return FALSE;
  }
  return TRUE;
}

GstMLModule *
gst_ml_module_new (const gchar * name)
{
  GstMLModule *module = NULL;
  gchar *location = NULL;
  gboolean success = TRUE;

  // Initialize the debug category.
  gst_ml_module_initialize_debug_category ();

  module = g_new0 (GstMLModule, 1);
  location = g_strdup_printf ("%s/lib%s.so", GST_ML_MODULES_DIR, name);

  module->name = g_strdup (name);
  module->handle = dlopen (location, RTLD_NOW);

  g_free (location);

  if (module->handle == NULL) {
    GST_ERROR ("Failed to open %s library, error: %s!", module->name, dlerror());
    gst_ml_module_free (module);
    return NULL;
  }

  success &= gst_ml_module_symbol (module->handle, GST_ML_MODULE_OPEN_FUNC,
      (gpointer) &(module)->open);
  success &= gst_ml_module_symbol (module->handle, GST_ML_MODULE_CLOSE_FUNC,
      (gpointer) &(module)->close);
  success &= gst_ml_module_symbol (module->handle, GST_ML_MODULE_CAPS_FUNC,
      (gpointer) &(module)->caps);
  success &= gst_ml_module_symbol (module->handle, GST_ML_MODULE_CONFIGURE_FUNC,
      (gpointer) &(module)->configure);
  success &= gst_ml_module_symbol (module->handle, GST_ML_MODULE_PROCESS_FUNC,
      (gpointer) &(module)->process);

  if (!success) {
    gst_ml_module_free (module);
    return NULL;
  }

  GST_INFO ("Created %s module: %p", module->name, module);
  return module;
}

void
gst_ml_module_free (GstMLModule * module)
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
gst_ml_module_init (GstMLModule * module)
{
  g_return_val_if_fail (module != NULL, FALSE);

  if (module->submodule == NULL)
    module->submodule = module->open ();

  return (module->submodule != NULL) ? TRUE : FALSE;
}

GstCaps *
gst_ml_module_get_caps (GstMLModule * module)
{
  g_return_val_if_fail (module != NULL, FALSE);

  return module->caps ();
}

gboolean
gst_ml_module_set_opts (GstMLModule * module, GstStructure * options)
{
  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (options != NULL, FALSE);

  return module->configure (module->submodule, options);
}

gboolean
gst_ml_module_execute (GstMLModule * module, GstMLFrame * mlframe,
    gpointer output)
{
  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (output != NULL, FALSE);

  return module->process (module->submodule, mlframe, output);
}

GstLabel *
gst_ml_label_new (void)
{
  GstLabel *label = g_new (GstLabel, 1);

  label->name = NULL;
  label->color = 0x00000000;

  return label;
}

void
gst_ml_label_free (GstLabel * label)
{
  if (NULL == label)
    return;

  if (label->name != NULL)
    g_free (label->name);

  g_free (label);
}

gboolean
gst_ml_parse_labels (const gchar * input, GValue * list)
{
  g_return_val_if_fail (input != NULL, FALSE);
  g_return_val_if_fail (list != NULL, FALSE);

  g_value_init (list, GST_TYPE_LIST);

  if (g_file_test (input, G_FILE_TEST_IS_REGULAR)) {
    GString *string = NULL;
    GError *error = NULL;
    gchar *contents = NULL;
    gboolean success = FALSE;

    if (!g_file_get_contents (input, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get labels file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    }

    // Remove trailing space and replace new lines with a comma delimiter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    string = g_string_new (contents);
    g_free (contents);

    // Add opening and closing brackets.
    string = g_string_prepend (string, "{ ");
    string = g_string_append (string, " }");

    // Get the raw character data.
    contents = g_string_free (string, FALSE);

    success = gst_value_deserialize (list, contents);
    g_free (contents);

    if (!success) {
      GST_ERROR ("Failed to deserialize labels file contents!");
      return FALSE;
    }
  } else if (!gst_value_deserialize (list, input)) {
    GST_ERROR ("Failed to deserialize labels!");
    return FALSE;
  }

  return TRUE;
}

GHashTable *
gst_ml_load_labels (GValue * list)
{
  GHashTable *labels = NULL;
  guint idx = 0, id = 0;

  g_return_val_if_fail (list != NULL, NULL);

  labels = g_hash_table_new_full (NULL, NULL, NULL,
        (GDestroyNotify) gst_ml_label_free);

  for (idx = 0; idx < gst_value_list_get_size (list); idx++) {
    GstStructure *structure = NULL;
    GstLabel *label = NULL;

    structure = GST_STRUCTURE (
        g_value_get_boxed (gst_value_list_get_value (list, idx)));

    if (structure == NULL) {
      GST_ERROR ("Failed to extract structure!");
      return NULL;
    } else if (!gst_structure_has_field (structure, "id") ||
        !gst_structure_has_field (structure, "color")) {
      GST_DEBUG ("Structure does not contain 'id' and/or 'color' fields!");
      continue;
    }

    if ((label = gst_ml_label_new ()) == NULL) {
      GST_ERROR ("Failed to allocate label memory!");
      return NULL;
    }

    label->name = g_strdup (gst_structure_get_name (structure));
    label->name = g_strdelimit (label->name, "-", ' ');

    gst_structure_get_uint (structure, "color", &label->color);
    gst_structure_get_uint (structure, "id", &id);

    g_hash_table_insert (labels, GUINT_TO_POINTER (id), label);
  }

  return labels;
}

GEnumValue *
gst_ml_enumarate_modules (const gchar * type)
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

  directory = g_dir_open (GST_ML_MODULES_DIR, 0, NULL);
  prefix = g_strdup_printf ("lib%s", type);

  while ((directory != NULL) && (filename = g_dir_read_name (directory))) {
    gboolean isvalid = FALSE;

    if (!g_str_has_prefix (filename, prefix))
      continue;

    if (!g_str_has_suffix (filename, ".so"))
      continue;

    string = g_strdup_printf ("%s/%s", GST_ML_MODULES_DIR, filename);
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
