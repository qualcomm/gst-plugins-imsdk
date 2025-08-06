/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "ml-type.h"

#define GST_ML_TYPE_INT8_NAME    "INT8"
#define GST_ML_TYPE_UINT8_NAME   "UINT8"
#define GST_ML_TYPE_INT16_NAME   "INT16"
#define GST_ML_TYPE_UINT16_NAME  "UINT16"
#define GST_ML_TYPE_INT32_NAME   "INT32"
#define GST_ML_TYPE_UINT32_NAME  "UINT32"
#define GST_ML_TYPE_INT64_NAME   "INT64"
#define GST_ML_TYPE_UINT64_NAME  "UINT64"
#define GST_ML_TYPE_FLOAT16_NAME "FLOAT16"
#define GST_ML_TYPE_FLOAT32_NAME "FLOAT32"

guint
gst_ml_type_get_size (GstMLType type)
{
  switch (type) {
    case GST_ML_TYPE_INT8:
    case GST_ML_TYPE_UINT8:
      return 1;
    case GST_ML_TYPE_INT16:
    case GST_ML_TYPE_UINT16:
    case GST_ML_TYPE_FLOAT16:
      return 2;
    case GST_ML_TYPE_INT32:
    case GST_ML_TYPE_UINT32:
    case GST_ML_TYPE_FLOAT32:
      return 4;
    case GST_ML_TYPE_INT64:
    case GST_ML_TYPE_UINT64:
      return 8;
    default:
      // Unknown type, no additional multiplication will be done.
      break;
  }

  return 1;
}

GstMLType
gst_ml_type_from_string (const gchar * type)
{
  g_return_val_if_fail (type != NULL, GST_ML_TYPE_UNKNOWN);

  if (strcmp (GST_ML_TYPE_INT8_NAME, type) == 0)
    return GST_ML_TYPE_INT8;
  else if (strcmp (GST_ML_TYPE_UINT8_NAME, type) == 0)
    return GST_ML_TYPE_UINT8;
  else if (strcmp (GST_ML_TYPE_INT16_NAME, type) == 0)
    return GST_ML_TYPE_INT16;
  else if (strcmp (GST_ML_TYPE_UINT16_NAME, type) == 0)
    return GST_ML_TYPE_UINT16;
  else if (strcmp (GST_ML_TYPE_INT32_NAME, type) == 0)
    return GST_ML_TYPE_INT32;
  else if (strcmp (GST_ML_TYPE_UINT32_NAME, type) == 0)
    return GST_ML_TYPE_UINT32;
  else if (strcmp (GST_ML_TYPE_INT64_NAME, type) == 0)
    return GST_ML_TYPE_INT64;
  else if (strcmp (GST_ML_TYPE_UINT64_NAME, type) == 0)
    return GST_ML_TYPE_UINT64;
  else if (strcmp (GST_ML_TYPE_FLOAT16_NAME, type) == 0)
    return GST_ML_TYPE_FLOAT16;
  else if (strcmp (GST_ML_TYPE_FLOAT32_NAME, type) == 0)
    return GST_ML_TYPE_FLOAT32;

  return GST_ML_TYPE_UNKNOWN;
}

const gchar *
gst_ml_type_to_string (GstMLType type)
{
  g_return_val_if_fail (type != GST_ML_TYPE_UNKNOWN, NULL);

  if (GST_ML_TYPE_INT8 == type)
    return GST_ML_TYPE_INT8_NAME;
  else if (GST_ML_TYPE_UINT8 == type)
    return GST_ML_TYPE_UINT8_NAME;
  else if (GST_ML_TYPE_INT16 == type)
    return GST_ML_TYPE_INT16_NAME;
  else if (GST_ML_TYPE_UINT16 == type)
    return GST_ML_TYPE_UINT16_NAME;
  else if (GST_ML_TYPE_INT32 == type)
    return GST_ML_TYPE_INT32_NAME;
  else if (GST_ML_TYPE_UINT32 == type)
    return GST_ML_TYPE_UINT32_NAME;
  else if (GST_ML_TYPE_INT64 == type)
    return GST_ML_TYPE_INT64_NAME;
  else if (GST_ML_TYPE_UINT64 == type)
    return GST_ML_TYPE_UINT64_NAME;
  else if (GST_ML_TYPE_FLOAT16 == type)
    return GST_ML_TYPE_FLOAT16_NAME;
  else if (GST_ML_TYPE_FLOAT32 == type)
    return GST_ML_TYPE_FLOAT32_NAME;

  return NULL;
}
