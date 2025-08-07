/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gfx-utils.h"

#ifdef HAVE_GLES2_H
#include "ib2c-utils.h"
#endif // HAVE_GLES2_H

gint
gst_gfx_adreno_get_alignment ()
{
  gint alignment = -1;

#ifdef HAVE_GLES2_H
  alignment = ::ib2c::QueryAlignment();
#else
  // Default alignment when platform doesn't have GPU.
  alignment = 4;
#endif // HAVE_GLES2_H

  return alignment;
}
