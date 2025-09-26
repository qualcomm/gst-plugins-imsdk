/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __PACKER_UTILS_H__
#define __PACKER_UTILS_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
#include <jpeglib.h>

#define TIFF_INFO_EXTRA_SIZE       1024

typedef enum _DngPackerCFAPattern {
  DNGPACKER_CFA_RGGB,
  DNGPACKER_CFA_BGGR,
  DNGPACKER_CFA_GBRG,
  DNGPACKER_CFA_GRBG,
  DNGPACKER_CFA_UNKNOWN,
} DngPackerCFAPattern;

typedef struct _DngPackRequest {
  size_t                raw_size;
  uint8_t               *raw_buf;
  uint32_t              raw_width;
  uint32_t              raw_height;
  uint32_t              raw_bpp;
  uint32_t              raw_stride;
  DngPackerCFAPattern   cfa;

  size_t                jpg_size;
  uint8_t               *jpg_buf;

  uint8_t               *output;
  size_t                output_size;
} DngPackRequest;

typedef void (*log_callback)(void *context, const char * file,
                             const char * function, int line,
                             const char *fmt, va_list args);

typedef void (*error_callback)(const char *fmt, va_list args);

typedef struct _DngPackerUtils DngPackerUtils;

/**
 * dngpacker_utils_init
 * @cb_func: calback function
 * @cb_context: context for callback function
 *
 * create a dng packer instance with callback parameters
 *
 * Return: instance pointer for dngpacker
 */
DngPackerUtils *dngpacker_utils_init (log_callback cb_func, void *cb_context);

/**
 * dngpacker_utils_register_error_cb
 * @error_callback: callback function for error report
 *
 * register global error callback function for dng packer module
 *
 * Return: NULL
 */
void dngpacker_utils_register_error_cb (error_callback cb);

/**
 * dngpacker_utils_pack_dng
 * @utils: dng packer instance
 * @request: dng packing request parameters
 *
 * execute dng pack operation with configured parameters
 *
 * Return: 0 success, 1 failure
 */
int dngpacker_utils_pack_dng (DngPackerUtils *utils, DngPackRequest *request);

#endif // __PACKER_UTILS_H__
