/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
* Help:
*  Example of Json Content
*         |
*         |        +------------------------------------------------------+
*         |        | ...                                                  |
*         |        |   "gst_plugin_example": {                            |
*         |        |     ...                                              |
*         +------> |     "CurrentPluginsAttribute": "Data Content"        |
*                  |     ...                                              |
*                  |   },                                                 |
*                  | ...                                                  |
*                  +------------------------------------------------------+
*/

#ifndef RUNTIME_FLAGS_PARSER_H
#define RUNTIME_FLAGS_PARSER_H

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/// @brief Initializes RuntimeFlagsParser
/// @param plugin target plugin
/// @return RuntimeFlagsParser instance
void* init_runtime_flags_parser (const char* plugin);

/// @brief Deinitializes RuntimeFlagsParser
/// @param obj RuntimeFlagsParser instance
void deinit_runtime_flags_parser (void* object);

/// @brief Get the current platform
/// @param obj RuntimeFlagsParser instance
/// @return current platform as string
char* get_platform (void* object);

/// @brief Get the data of target plugin's attribute
/// @param obj RuntimeFlagsParser instance
/// @param key name of the target json attribute
/// @return content of the target json attribute as string
const gchar * get_flag_as_string (void* object, const char* key);

/// @brief Get the data of target plugin's attribute
/// @param obj RuntimeFlagsParser instance
/// @param key name of the target json attribute
/// @return content of the target json attribute as boolean
gboolean get_flag_as_bool (void* object, const char* key);

/// @brief Get the data of target plugin's attribute
/// @param obj RuntimeFlagsParser instance
/// @param key name of the target json attribute
/// @return content of the target json attribute as float
float get_flag_as_float (void* object, const char* key);

/// @brief Get the data of target plugin's attribute
/// @param obj RuntimeFlagsParser instance
/// @param key name of the target json attribute
/// @return content of the target json attribute as integer
int get_flag_as_int (void* object, const char* key);

/// @brief Get RuntimeFlagsParser of qmmfsrc
/// @return RuntimeFlagsParser instance
void* get_qmmfsrc_parser ();

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // RUNTIME_FLAGS_PARSER_H
