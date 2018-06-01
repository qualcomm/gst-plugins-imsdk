/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright ? 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Benjamin Franzke <benjaminfranzke@googlemail.com>
 */
#ifndef __GST_QSCREENCAPTUIL_H__
#define __GST_QSCREENCAPTUIL_H__

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <sys/ioctl.h>

#include <gst/gst.h>

#include <string.h>
#include <math.h>
#include <wayland-client.h>
#include "msm_drm.h"
#include <drm_fourcc.h>
#include "xdg-shell-client-protocol.h"
#include "gbm-buffer-backend-client-protocol.h"
#include "screen-capture-client-protocol.h"
#include "ivi-application-client-protocol.h"

G_BEGIN_DECLS
#define GBM_PERFORM_GET_METADATA_ION_FD             0x24 /* Get Metadata ion fd from BO*/
#define __gbm_fourcc_code(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                              ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define GBM_FORMAT_ABGR8888     __gbm_fourcc_code('A', 'B', '2', '4') /* [31:0] A:B:G:R 8:8:8:8 little endian */
/**
 * Flags to indicate the intended use for the buffer - these are passed into
 * gbm_bo_create(). The caller must set the union of all the flags that are
 * appropriate
 *
 * \sa Use gbm_device_is_format_supported() to check if the combination of format
 * and use flags are supported
 */
enum gbm_bo_flags {
   /**
    * Buffer is going to be presented to the screen using an API such as KMS
    */
   GBM_BO_USE_SCANOUT      = (1 << 0),
   /**
    * Buffer is going to be used as cursor
    */
   GBM_BO_USE_CURSOR       = (1 << 1),
   /**
    * Deprecated
    */
   GBM_BO_USE_CURSOR_64X64 = GBM_BO_USE_CURSOR,
   /**
    * Buffer is to be used for rendering - for example it is going to be used
    * as the storage for a color buffer
    */
   GBM_BO_USE_RENDERING    = (1 << 2),
   /**
    * Buffer can be used for gbm_bo_write.  This is guaranteed to work
    * with GBM_BO_USE_CURSOR. but may not work for other combinations.
    */
   GBM_BO_USE_WRITE    = (1 << 3),
};

typedef struct _GstQContext GstQCtx;
typedef struct _GstMetaQSCreenCap GstMetaQScreenCap;
typedef struct _GstGbm QGbm_info;
typedef struct _QWlBuffer QWl_buf;
typedef struct _QDisplay QDisplay;

struct output {
	struct wl_output *output;
	const char *name;
	int offset_x, offset_y;
	int width, height;
	struct wl_list link;
};

struct _QWlBuffer {
	QDisplay *qdisplay;
	struct wl_buffer *wlbuf;
        GstBuffer *gstbuf;
	int width, height;
	int stride;
	void *shm_data;
	int busy;
};
struct _QDisplay {
	struct wl_display *display;
        struct wl_list output_list;
	struct wl_registry *registry;
        struct wl_event_queue *queue;
	struct wl_compositor *compositor;
	struct output *output;
	struct xdg_shell *shell;
	struct wl_shm *shm;
	uint32_t formats;
	struct wl_surface *surface;
	struct wl_callback *callback;
	struct xdg_surface *xdg_surface;
	struct ivi_surface *ivi_surface;
        struct ivi_application *ivi_application;
	struct gbm_buffer_backend *gbmbuf;
	struct screen_capture *screen_cap;
    GQueue pending_buffers; /* Contains GstBuffer* */
    gboolean is_capture_started;
    gboolean is_screen_created;
    GThread *thread;
    GstPoll *wl_fd_poll;
    GMutex capture_lock;
    GMutex wlbuf_lock;
    GCond wlbuf_cond;
    GHashTable *buffers;
};


struct _GstQContext {
  QDisplay *qdisplay;
  struct gbm_device *gbm;
  struct gbm_bo *bo;
  int gbm_device_fd;
  gint width, height;  /*screen width and height*/
  GstCaps *caps;
  void *gbmhandle;
  int (*gbm_bo_get_fd)(struct gbm_bo *bo);
  int (*gbm_perform )(int operation,...);
  struct gbm_bo * (*gbm_bo_create)(struct gbm_device *gbm,uint32_t width, uint32_t height,uint32_t format, uint32_t flags);
  void (*gbm_bo_destroy)(struct gbm_bo *bo);
  uint32_t (*gbm_bo_get_width)(struct gbm_bo *bo);
  uint32_t (*gbm_bo_get_height)(struct gbm_bo *bo);
  uint32_t (*gbm_bo_get_stride)(struct gbm_bo *bo);
  void (*gbm_device_destroy)(struct gbm_device *gbm);
  struct gbm_device * (*gbm_create_device)(int fd);
};


GstQCtx *
qscreencap_qctx_get (GstElement * parent);
void qscreencap_qctx_clear (GstQCtx *qctx);


/* BufferReturnFunc is called when a buffer is finalised */
typedef gboolean (*BufferReturnFunc) (GstElement *parent, GstBuffer *buf);
struct _GstGbm {
    int gbm_device_fd;
    struct gbm_device *gbm;
    struct gbm_bo *bo;
    unsigned long bo_fd;
    unsigned long meta_fd;
};


struct _GstMetaQSCreenCap {
  GstMeta meta;

  /* Reference to the qscreencapsrc we belong to */
  GstElement *parent;
  QGbm_info *gbminfo;
  QWl_buf qwlbuf;
  void *qscreencap;
  gint width, height,stride;
  unsigned char *data;
  size_t size;

  BufferReturnFunc return_func;
};

GType gst_meta_qscreencap_api_get_type (void);
const GstMetaInfo * gst_meta_qscreencap_get_info (void);
#define GST_META_QSCREENCAP_GET(buf) ((GstMetaQScreenCap *)gst_buffer_get_meta(buf,gst_meta_qscreencap_api_get_type()))
#define GST_META_QSCREENCAP_ADD(buf) ((GstMetaQScreenCap *)gst_buffer_add_meta(buf,gst_meta_qscreencap_get_info(),NULL))

GstBuffer *gst_qscreencapbuf_new (GstQCtx * qctx,
    GstElement * parent, int width, int height, BufferReturnFunc return_func);

void gst_qscreencapbuf_destroy (GstQCtx * qctx, GstBuffer * qscreencapbuf);

/* Call to manually release a buffer */
void gst_qscreencap_buffer_free (GstBuffer * qscreencapbuf);

gpointer
gst_qscreencap_display_thread_run (gpointer data);

void
gst_buffer_force_release(GstBuffer * self);

void destroy_display(QDisplay *qdisplay);
G_END_DECLS

#endif /* __GST_QSCREENCAPTUIL_H__ */
