/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __QTI_VIDEO_TEMPLATE_DEFS__
#define __QTI_VIDEO_TEMPLATE_DEFS__

typedef struct _GstBuffer GstBuffer;

typedef enum
{
  CUSTOM_STATUS_OK,
  CUSTOM_STATUS_FAIL,
} CustomCmdStatus;

/*
BufferAllocMode:
  NONE:   unused
  INLINE: Input buffer modified in place (for effiency)
  ALLOC:  Output buffer allocated for each input buffer
  CUSTOM: Allocation and lifetime of buffers owned by custom library
*/
typedef enum
{
  BUFFER_ALLOC_MODE_NONE,
  BUFFER_ALLOC_MODE_INPLACE,
  BUFFER_ALLOC_MODE_ALLOC,
  BUFFER_ALLOC_MODE_CUSTOM,
} BufferAllocMode;

#define MAX_FORMATS_SIZE 256

typedef struct
{
  int min_width;
  int max_width;
  int min_height;
  int max_height;
  // ',' delimited formats
  char formats[MAX_FORMATS_SIZE];
} VideoCfgRanges;

typedef struct
{
  int selected_width;
  int selected_height;

  char selected_format[MAX_FORMATS_SIZE];
} VideoCfg;

typedef struct
{
  // Invoke whenever writing to output buffer
  void (*lock_buf_for_writing) (GstBuffer * buffer);

  // Invoke when done with writing to output buffer
  void (*unlock_buf_for_writing) (GstBuffer * buffer);

  // Request output buffer (valid in BUFFER_ALLOC_MODE_CUSTOM only)
  void (*allocate_outbuffer) (GstBuffer ** outbuffer, void *priv_data);

  // Output buffer processing done (valid in BUFFER_ALLOC_MODE_CUSTOM only)
  CustomCmdStatus (*buffer_done) (GstBuffer * buf, void *priv_data);

} VideoTemplateCb;

#endif
