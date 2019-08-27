/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright ? 2008 Kristian H?gsberg
 * Copyright ? 2012-2013 Collabora, Ltd.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Copyright (C) 2014 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <unistd.h> // for close
#include "qscreencaputil.h"
#include "media/msm_media_info.h"
#include <gst/video/video.h>

QGbm_info * gbm_memory_alloc(GstQCtx * qctx,int w,int h);
void gbm_memory_free(GstQCtx * qctx,QGbm_info *buf_gbm_info);
GType
gst_meta_qscreencap_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstMetaQScreenCapSrcAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_meta_qscreencap_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstMetaQScreenCap *sm = (GstMetaQScreenCap *) meta;

  sm->parent = NULL;
  sm->qscreencap = NULL;

  sm->width = 0;
  sm->height = 0;
  sm->size = 0;
  sm->return_func = NULL;

  return TRUE;
}

const GstMetaInfo *
gst_meta_qscreencap_get_info (void)
{
  static const GstMetaInfo *meta_qscreencap_info = NULL;

  if (g_once_init_enter (&meta_qscreencap_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_meta_qscreencap_api_get_type (), "GstMetaQScreenCapSrc",
        sizeof (GstMetaQScreenCap), (GstMetaInitFunction) gst_meta_qscreencap_init,
        (GstMetaFreeFunction) NULL, (GstMetaTransformFunction) NULL);
    g_once_init_leave (&meta_qscreencap_info, meta);
  }
  return meta_qscreencap_info;
}
static void
sync_callback(void *data, struct wl_callback *callback, uint32_t serial)
{
   int *done = data;
   *done = 1;
   GST_DEBUG("sync_callback done!");
   wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_listener = {
   sync_callback
};
static gint
gst_qsc_wl_display_roundtrip ( QDisplay *display)
{
  struct wl_callback *callback;
  gint ret = 0;
  gboolean done = FALSE;

  g_return_val_if_fail (display != NULL, -1);

  /* We don't own the display, process only our queue */
  callback = wl_display_sync (display->display);
  wl_callback_add_listener (callback, &sync_listener, &done);
  wl_proxy_set_queue ((struct wl_proxy *) callback, display->queue);
  while (ret != -1 && !done) {
        ret = wl_display_dispatch_queue (display->display, display->queue);
  }
  GST_DEBUG("gst_qsc_wl_display_roundtrip end");
  return ret;
}

gpointer
gst_qscreencap_display_thread_run (gpointer data)
{
  QDisplay *self = data;
  GstPollFD pollfd = GST_POLL_FD_INIT;
  int ret = 0;
  pollfd.fd = wl_display_get_fd (self->display);
  gst_poll_add_fd (self->wl_fd_poll, &pollfd);
  gst_poll_fd_ctl_read (self->wl_fd_poll, &pollfd, TRUE);

  /* main loop */
  while (1) {
       while (wl_display_prepare_read_queue (self->display, self->queue) != 0)
          wl_display_dispatch_queue_pending (self->display, self->queue);
       wl_display_flush(self->display);
       if (gst_poll_wait (self->wl_fd_poll, GST_CLOCK_TIME_NONE) < 0) {
          gboolean normal = (errno == EBUSY);
          wl_display_cancel_read (self->display);
          if (normal)
            break;
          else
           goto error;
       } else {
          wl_display_read_events(self->display);
          wl_display_dispatch_queue_pending (self->display, self->queue);

       }

  }
  GST_DEBUG ("gst_qscreencap_display_thread_run end\n");
  return NULL;

error:
  GST_ERROR ("Error communicating with the wayland server");
  return NULL;
}

static void
create_screen_succeeded(void *data, struct screen_capture *screen_capture)
{
    QDisplay *qdisplay = data;
    qdisplay->is_screen_created = TRUE;
    GST_DEBUG("create_screen_succeeded");
        return;
}

static void
create_screen_failed(void *data, struct screen_capture *screen_capture)
{
        QDisplay *qdisplay = data;
        screen_capture_destroy(screen_capture);
        qdisplay->screen_cap = NULL;
        GST_ERROR("Error:screen_capture_create_screen.create failed.");
}

static void
capture_started(void *data, struct screen_capture *screen_capture)
{
    QDisplay *qdisplay = data;
    qdisplay->is_capture_started = TRUE;
    GST_DEBUG("capture screen is started");
    return;
}

static void
capture_stopped(void *data, struct screen_capture *screen_capture)
{
    QDisplay *qdisplay = data;
    qdisplay->is_capture_started = FALSE;
    GST_DEBUG("capture is stopped");
    return;
}

static void
capture_screen_destroyed(void *data, struct screen_capture *screen_capture)
{
    QDisplay *qdisplay = data;
    qdisplay->is_screen_created = FALSE;
    GST_DEBUG("capture screen is destroyed");
    return;
}

static const struct screen_capture_listener capture_listener = {
        create_screen_succeeded,
        create_screen_failed,
        capture_screen_destroyed,
        capture_started,
        capture_stopped
};

static void
output_listener_geometry(void *data,
                         struct wl_output *wl_output,
                         int32_t x,
                         int32_t y,
                         int32_t physical_width,
                         int32_t physical_height,
                         int32_t subpixel,
                         const char *make,
                         const char *model,
                         int32_t transform)
{
           struct output *output = wl_output_get_user_data(wl_output);

        if (wl_output == output->output) {
                output->offset_x = x;
                output->offset_y = y;
                output->name = strdup(model);
        }
}

static void
output_listener_mode(void *data,
                     struct wl_output *wl_output,
                     uint32_t flags,
                     int32_t width,
                     int32_t height,
                     int32_t refresh)
{
   struct output *output = data;

   if (flags & WL_OUTPUT_MODE_CURRENT) {
     output->width = width;
     output->height = height;
   }
   GST_DEBUG("the mode in hardware units w %d h %d",width,height);
}

static void
output_listener_done(void *data,
                     struct wl_output *output)
{
    (void)data;
    (void)output;
}

static void
output_listener_scale(void *data,
                      struct wl_output *output,
                      int32_t factor)
{
    (void)data;
    (void)output;
    (void)factor;
}

static const struct wl_output_listener output_listener = {
    output_listener_geometry,
    output_listener_mode,
    output_listener_done,
    output_listener_scale
};
static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
  QDisplay *d = (QDisplay *)data;

  d->formats |= (1 << format);
}

struct wl_shm_listener shm_listener = {
  shm_format
};

static void
xdg_shell_ping(void *data, struct xdg_shell *shell, uint32_t serial)
{
	xdg_shell_pong(shell, serial);
}

static const struct xdg_shell_listener xdg_shell_listener = {
	xdg_shell_ping,
};

#define XDG_VERSION 5 /* The version of xdg-shell that we implement */

static void
registry_handle_global(void *data, struct wl_registry *registry,
           uint32_t id, const char *interface, uint32_t version)
{
  QDisplay *self = (QDisplay  *)data;
  if (strcmp(interface, "wl_compositor") == 0) {
    self->compositor =(struct wl_compositor *)
        wl_registry_bind(registry,
        id, &wl_compositor_interface, 1);
  } else if (strcmp(interface, "wl_shm") == 0) {
    self->shm = (struct wl_shm *)wl_registry_bind(registry,
      id, &wl_shm_interface, 1);
    wl_shm_add_listener(self->shm, &shm_listener, self);
  } else if (strcmp(interface, "xdg_shell") == 0) {
     self->shell = wl_registry_bind(registry,
         id, &xdg_shell_interface, 1);
     xdg_shell_use_unstable_version(self->shell, XDG_VERSION);
     xdg_shell_add_listener(self->shell, &xdg_shell_listener, self);
  } else if (strcmp(interface, "ivi_application") == 0) {
    self->ivi_application = (struct ivi_application *)
       wl_registry_bind(registry, id,
       &ivi_application_interface, 1);
  } else  if (strcmp(interface, "wl_output") == 0) {
    self->output = malloc(sizeof (struct output));
    if(self->output) {
      self->output->output = wl_registry_bind(registry, id, &wl_output_interface, 1);
	wl_list_insert(&self->output_list, &self->output->link);
	 wl_output_add_listener(self->output->output, &output_listener, self->output);
    } else {
        GST_ERROR("self->output malloc failed\n");
    }
  } else if (strcmp(interface, "screen_capture") == 0) {
                self->screen_cap = wl_registry_bind(registry,
                                             id, &screen_capture_interface, 1);
  } else if (strcmp(interface, "gbm_buffer_backend") == 0) {
        self->gbmbuf = wl_registry_bind(registry,
               id, &gbm_buffer_backend_interface, 1);
  }

}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
          uint32_t name)
{
}
static const struct wl_registry_listener registry_listener = {
 registry_handle_global,
 registry_handle_global_remove
};

static void
handle_surface_configure(void *data, struct xdg_surface *surface,
                 int32_t width, int32_t height,
                 struct wl_array *states, uint32_t serial)
{
	xdg_surface_ack_configure(surface, serial);
}

static void
handle_surface_delete(void *data, struct xdg_surface *xdg_surface)
{
}

static const struct xdg_surface_listener xdg_surface_listener = {
	handle_surface_configure,
	handle_surface_delete,
};

static void
handle_ivi_surface_configure(void *data, struct ivi_surface *ivi_surface,
                             int32_t width, int32_t height)
{
}

static const struct ivi_surface_listener ivi_surface_listener = {
	handle_ivi_surface_configure,
};

#define IVI_SURFACE_ID 9000

QDisplay *create_display(void)
{
	struct output *output, *next;
	int found = 0;
	GError *err = NULL;

	QDisplay *qdisplay;
	qdisplay = g_new0 (QDisplay, 1);
	if (qdisplay == NULL)
	{
	   GST_ERROR("out of memory for qdisplay\n");
           return NULL;
	}
        qdisplay->buffers = g_hash_table_new (g_direct_hash, g_direct_equal);
	g_mutex_init (&qdisplay->capture_lock);
        g_cond_init (&qdisplay->wlbuf_cond);
        g_mutex_init (&qdisplay->wlbuf_lock);
	qdisplay->display = wl_display_connect(NULL);
	GST_DEBUG( "opened display %p", qdisplay->display);
	if (!qdisplay->display) {
	  g_free (qdisplay);
	  return NULL;
	}
	wl_list_init(&qdisplay->output_list);
	qdisplay->wl_fd_poll = gst_poll_new (TRUE);
	qdisplay->queue = wl_display_create_queue (qdisplay->display);
	qdisplay->formats = 0;
	qdisplay->registry = wl_display_get_registry(qdisplay->display);
        wl_proxy_set_queue ((struct wl_proxy *) qdisplay->registry, qdisplay->queue);
	wl_registry_add_listener(qdisplay->registry,
		&registry_listener, qdisplay);
	gst_qsc_wl_display_roundtrip(qdisplay);
        if (qdisplay->shm == NULL) {
	  GST_ERROR("display->shm not exist");
	  g_free (qdisplay);
	  return NULL;
	}
	gst_qsc_wl_display_roundtrip(qdisplay);

	if (!qdisplay->screen_cap) {
		GST_ERROR("display screen_cap not exist");
		g_free (qdisplay);
		return NULL;
	}

	qdisplay->surface = wl_compositor_create_surface(qdisplay->compositor);
	wl_proxy_set_queue ((struct wl_proxy *)qdisplay->surface , qdisplay->queue);
	if (qdisplay->shell) {
		qdisplay->xdg_surface =
			xdg_shell_get_xdg_surface(qdisplay->shell,
				qdisplay->surface);


		xdg_surface_add_listener(qdisplay->xdg_surface,
			&xdg_surface_listener, qdisplay);

		xdg_surface_set_title(qdisplay->xdg_surface, "screen_capture_surface");
                gst_qsc_wl_display_roundtrip(qdisplay);

	} else if (qdisplay->ivi_application) {
		uint32_t id_ivisurf = IVI_SURFACE_ID + (uint32_t)getpid();
		qdisplay->ivi_surface =
			ivi_application_surface_create(qdisplay->ivi_application,
				id_ivisurf, qdisplay->surface);

		if (qdisplay->ivi_surface == NULL) {
			GST_ERROR("Failed to create ivi_client_surface");
			return NULL;
		}

		ivi_surface_add_listener(qdisplay->ivi_surface,
			&ivi_surface_listener, qdisplay);
                gst_qsc_wl_display_roundtrip(qdisplay);
	} else {
                GST_ERROR("no ivi or xdg");
	}

	/* Select output */
	wl_list_for_each_safe(output, next, &qdisplay->output_list, link) {
                GST_DEBUG("%s",output->name);
		if (!strcmp(output->name, "HDMI-A-1")) {
			found = 1;
			break;
		}
	}
	if (!found) {
		GST_ERROR("fail to find match output");
		g_free (qdisplay);
		return NULL;
	}

	screen_capture_add_listener(qdisplay->screen_cap, &capture_listener, qdisplay);
        wl_proxy_set_queue ((struct wl_proxy *)qdisplay->screen_cap , qdisplay->queue);
        GST_DEBUG("create creen w h output cap %d %d %p %p",output->width,output->height,output->output,qdisplay->screen_cap);
	screen_capture_create_screen(qdisplay->screen_cap, output->output, output->width, output->height);
        gst_qsc_wl_display_roundtrip(qdisplay);
	if (!qdisplay->is_screen_created) {
		GST_ERROR("fail to create capture screen");
		g_free (qdisplay);
		return NULL;
	}
	screen_capture_start(qdisplay->screen_cap);
        gst_qsc_wl_display_roundtrip(qdisplay);
   return qdisplay;

}

/* get resouces for capture */
GstQCtx *
qscreencap_qctx_get (GstElement * parent)
{
  GstQCtx *qctx = NULL;

  qctx = g_new0 (GstQCtx, 1);
  if(!qctx) {
    GST_ERROR("g_new0 (GstQCtx) failed");
    return NULL;
  }

  qctx->gbmhandle = dlopen("libgbm.so",  RTLD_NOW);
  if(!qctx->gbmhandle) {
    GST_ERROR("dlopen libgbm.so failed");
    g_free(qctx);
    return NULL;
  } else {
    qctx->gbm_create_device = (void *) dlsym(qctx->gbmhandle,"gbm_create_device");
    qctx->gbm_device_destroy = (void *) dlsym(qctx->gbmhandle,"gbm_device_destroy");
    qctx->gbm_bo_get_height = (void *) dlsym(qctx->gbmhandle,"gbm_bo_get_height");
    qctx->gbm_bo_get_stride = (void *) dlsym(qctx->gbmhandle,"gbm_bo_get_stride");
    qctx->gbm_bo_create = (void *) dlsym(qctx->gbmhandle,"gbm_bo_create");
    qctx->gbm_bo_destroy = (void *) dlsym(qctx->gbmhandle,"gbm_bo_destroy");
    qctx->gbm_bo_get_fd = (void *) dlsym(qctx->gbmhandle,"gbm_bo_get_fd");
    qctx->gbm_perform = (void *) dlsym(qctx->gbmhandle,"gbm_perform");

    if ( !qctx->gbm_create_device || !qctx->gbm_device_destroy ||
        !qctx->gbm_bo_get_height || !qctx->gbm_bo_get_stride ||
        !qctx->gbm_bo_create || !qctx->gbm_bo_destroy ||
        !qctx->gbm_bo_get_fd || !qctx->gbm_perform ) {
      GST_ERROR("no gbm module %p %p %p %p %p %p %p %p",qctx->gbm_create_device,
               qctx->gbm_device_destroy,qctx->gbm_bo_get_height,
               qctx->gbm_bo_get_stride,qctx->gbm_bo_create,
               qctx->gbm_bo_destroy,qctx->gbm_bo_get_fd,qctx->gbm_perform);
      dlclose(qctx->gbmhandle);
      g_free(qctx);
      return NULL;
    }

  }

  qctx->qdisplay = create_display();
  if(qctx->qdisplay == NULL) {
    GST_ERROR("create_displaycreate_display failed");
    dlclose(qctx->gbmhandle);
    g_free(qctx);
    return NULL;
  }

  qctx->width = qctx->qdisplay->output->width;
  qctx->height = qctx->qdisplay->output->height;

  qctx->gbm_device_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
  if (qctx->gbm_device_fd < 0) {
         GST_ERROR("opening gbm device failed");
         return NULL;
  }
  qctx->gbm = qctx->gbm_create_device(qctx->gbm_device_fd);
  if (qctx->gbm == NULL) {
     GST_ERROR("gbm_create_device failed");
     return NULL;
  }
  qctx->caps = NULL;
  GST_DEBUG_OBJECT (parent, "Q reports %dx%d pixels",
      qctx->width, qctx->height);

  return qctx;
}
void
gst_buffer_force_release(GstBuffer * self)
{
  /* Force a buffer release.*/
    GST_DEBUG("gst_buffer_force_release %p",self);
    gst_buffer_unref (self);
}

void destroy_display(QDisplay *qdisplay)
{
    if(qdisplay == NULL)
      return;

    if (qdisplay->is_capture_started)
    {
       screen_capture_stop(qdisplay->screen_cap);
    }
    if (qdisplay->is_screen_created)
    {
       screen_capture_destroy_screen(qdisplay->screen_cap);
    }
    if (qdisplay->wl_fd_poll) {
      gst_poll_set_flushing (qdisplay->wl_fd_poll, TRUE);
      if (qdisplay->thread)
         g_thread_join (qdisplay->thread);
      gst_poll_free (qdisplay->wl_fd_poll);
      qdisplay->wl_fd_poll = NULL;
    }

    if(qdisplay->buffers) {
	 g_hash_table_foreach (qdisplay->buffers,
		 (GHFunc) gst_buffer_force_release, NULL);
	 g_hash_table_remove_all (qdisplay->buffers);
	 g_hash_table_unref (qdisplay->buffers);
         qdisplay->buffers == NULL;
    }
    if (qdisplay->screen_cap)
       screen_capture_destroy(qdisplay->screen_cap);

    if (qdisplay->xdg_surface)
       xdg_surface_destroy(qdisplay->xdg_surface);
    if (qdisplay->ivi_application)
       ivi_surface_destroy(qdisplay->ivi_surface);
    if (qdisplay->surface)
       wl_surface_destroy(qdisplay->surface);

    if (qdisplay->shm)
       wl_shm_destroy(qdisplay->shm);

    if (qdisplay->shell)
       xdg_shell_destroy(qdisplay->shell);

    if (qdisplay->compositor)
       wl_compositor_destroy(qdisplay->compositor);

    if (qdisplay->registry)
       wl_registry_destroy(qdisplay->registry);

    if (qdisplay->queue)
       wl_event_queue_destroy(qdisplay->queue);

    if (qdisplay->display) {
       wl_display_flush(qdisplay->display);
       wl_display_disconnect(qdisplay->display);
    }
    g_mutex_clear (&qdisplay->wlbuf_lock);
    g_cond_clear (&qdisplay->wlbuf_cond);
    g_mutex_clear (&qdisplay->capture_lock);

    if(qdisplay)
     g_free(qdisplay);

}

/* clean the screen capture context. Closing the Display  */
void
qscreencap_qctx_clear (GstQCtx *qctx)
{
  g_return_if_fail (qctx != NULL);

  if (qctx->caps != NULL)
    gst_caps_unref (qctx->caps);

  if(qctx->qdisplay)
   destroy_display(qctx->qdisplay);

  if (qctx->gbm)
    qctx->gbm_device_destroy(qctx->gbm);
  qctx->gbm = NULL;

  if(qctx->gbm_device_fd)
    close(qctx->gbm_device_fd);
  qctx->gbm_device_fd = -1;

  if (qctx->gbmhandle)
    dlclose(qctx->gbmhandle);

  g_free (qctx);
}


static gboolean
gst_qscreencap_buffer_dispose (GstBuffer * qscreencapbuf)
{
  GstElement *parent;
  GstMetaQScreenCap *meta;
  gboolean ret = TRUE;

  meta = GST_META_QSCREENCAP_GET (qscreencapbuf);
  g_assert(meta != NULL);

  parent = meta->parent;
  if (parent == NULL) {
    g_warning ("qscreencap meta src == NULL");
    goto teak;
  }
  GST_DEBUG("gst_qscreencap_buffer_dispose qscreencapbuf %p",qscreencapbuf);
  if (meta->return_func)
    ret = meta->return_func (parent, qscreencapbuf);

teak:
  return ret;
}

void
gst_qscreencap_buffer_free (GstBuffer * qscreencapbuf)
{
  GstMetaQScreenCap *meta;
  meta = GST_META_QSCREENCAP_GET (qscreencapbuf);
  g_assert(meta != NULL);

  /* make sure it is not recycled */
  meta->width = -1;
  meta->height = -1;
  gst_buffer_unref (qscreencapbuf);
}

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
  QWl_buf *qwlbuf = data;
  QDisplay *qdisplay = qwlbuf->qdisplay;

  g_mutex_lock (&qdisplay->capture_lock);
  GST_DEBUG("buffer_release gstbuf %p",qwlbuf->gstbuf);
  g_queue_push_tail (&qdisplay->pending_buffers, qwlbuf->gstbuf);
  g_mutex_unlock (&qdisplay->capture_lock);

  qwlbuf->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
  buffer_release
};

static void
create_succeeded(void *data,
                 struct gbm_buffer_params *params,
                 struct wl_buffer *new_buffer)
{
  QWl_buf *qwlbuf = data;
  QDisplay *qdisplay = qwlbuf->qdisplay;
  g_mutex_lock (&qdisplay->wlbuf_lock);
  qwlbuf->wlbuf = new_buffer;
  gbm_buffer_params_destroy(params);
  g_cond_signal (&qdisplay->wlbuf_cond);
  g_mutex_unlock (&qdisplay->wlbuf_lock);
  wl_proxy_set_queue((struct wl_proxy*)qwlbuf->wlbuf, qdisplay->queue);
  wl_buffer_add_listener(qwlbuf->wlbuf, &buffer_listener, qwlbuf);
}

static void
create_failed(void *data, struct gbm_buffer_params *params)
{
  QWl_buf * qwlbuf = data;
  QDisplay *qdisplay = qwlbuf->qdisplay;
  g_mutex_lock (&qdisplay->wlbuf_lock);
  qwlbuf->wlbuf = NULL;
  gbm_buffer_params_destroy(params);
  g_cond_signal (&qdisplay->wlbuf_cond);
  g_mutex_unlock (&qdisplay->wlbuf_lock);
  GST_ERROR( "Error:gbm_buffer_params_create.create failed.");
}

static const struct gbm_buffer_params_listener params_listener = {
        create_succeeded,
        create_failed
};


GstBuffer *
gst_qscreencapbuf_new (GstQCtx * qctx,
    GstElement * parent, int width, int height, BufferReturnFunc return_func)
{
  GstBuffer *qscreencapbuf = NULL;
  GstMetaQScreenCap *meta;
  gboolean succeeded = FALSE;
  struct gbm_buffer_params *params;
  uint32_t flags;
  gint64 timeout;
  qscreencapbuf = gst_buffer_new ();
  GST_MINI_OBJECT_CAST (qscreencapbuf)->dispose =
      (GstMiniObjectDisposeFunction) gst_qscreencap_buffer_dispose;

  meta = GST_META_QSCREENCAP_ADD (qscreencapbuf);
  g_assert(meta != NULL);

  meta->width = width;
  meta->height = height;
  meta->qwlbuf.qdisplay = qctx->qdisplay;
  {
    int bostride,boheight;

    meta->gbminfo = gbm_memory_alloc (qctx,width,height);
    if (!meta->gbminfo)
      goto teak;
    boheight = qctx->gbm_bo_get_height(meta->gbminfo->bo);
    bostride = qctx->gbm_bo_get_stride(meta->gbminfo->bo);
    meta->size = bostride * height;
    meta->stride = bostride;
    meta->data = (unsigned char *)mmap(NULL, meta->size,
                                PROT_READ|PROT_WRITE, MAP_SHARED,
                                meta->gbminfo->bo_fd,0);
    GST_DEBUG("allocated data size %p %d boheight bostride width height %d %d %d %d",
       meta->data,meta->size,boheight,bostride,width,height);
    if (meta->data == MAP_FAILED)
    {
      GST_ERROR("mmap failed\n");
      return NULL;
    }

  }
  flags =  GBM_BUFFER_PARAMS_FLAGS_SCREEN_CAPTURE;

  params = gbm_buffer_backend_create_params(qctx->qdisplay->gbmbuf);
  if (!params) {
    GST_ERROR("gbm_buffer_backend_create_params failed\n");
    return NULL;
  }

  gbm_buffer_params_add_listener(params, &params_listener, &meta->qwlbuf);
  gbm_buffer_params_create(params, meta->gbminfo->bo_fd,meta->gbminfo->meta_fd, width,height,DRM_FORMAT_ABGR8888, flags);

  wl_display_roundtrip(qctx->qdisplay->display);
  wl_display_dispatch_queue_pending (qctx->qdisplay->display, qctx->qdisplay->queue);

  timeout = g_get_monotonic_time () + G_TIME_SPAN_SECOND;
  while (meta->qwlbuf.wlbuf == NULL) {
    if (!g_cond_wait_until (&qctx->qdisplay->wlbuf_cond, &qctx->qdisplay->wlbuf_lock, timeout)) {
      GST_ERROR ("create wl_buffer failed...");
      if(params)
        gbm_buffer_params_destroy(params);
    }
  }

  GST_DEBUG("meta->qwlbuf.wlbuf %p",meta->qwlbuf.wlbuf);
  succeeded = TRUE;

  gst_buffer_append_memory (qscreencapbuf,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, meta->data,
          meta->size, 0, meta->size, NULL, NULL));

  meta->qwlbuf.gstbuf = qscreencapbuf;
  /* Keep a ref to our src */
  meta->parent = gst_object_ref (parent);
  meta->return_func = return_func;
teak:
  if (!succeeded) {
    gst_qscreencap_buffer_free (qscreencapbuf);
    qscreencapbuf = NULL;
  }

  return qscreencapbuf;
}

/* This function free the gbm memory */
void
gst_qscreencapbuf_destroy (GstQCtx * qctx, GstBuffer * qscreencapbuf)
{
  GstMetaQScreenCap *meta;

  meta = GST_META_QSCREENCAP_GET (qscreencapbuf);
  g_assert(meta != NULL);

   if (!qctx)
    goto teak;

  g_return_if_fail (qscreencapbuf != NULL);

  GST_DEBUG("destroy wlbuffer from buffer %p ",qscreencapbuf);
  if(meta->qwlbuf.wlbuf)
       wl_buffer_destroy (meta->qwlbuf.wlbuf);

  GST_DEBUG("destroy gmb from buffer %p ",qscreencapbuf);
  if (meta->gbminfo) {
      gbm_memory_free (qctx,meta->gbminfo);
  }

teak:
  if (meta->parent) {
    /* Release the ref to our parent */
    gst_object_unref (meta->parent);
    meta->parent = NULL;
  }

  return;
}


QGbm_info * gbm_memory_alloc(GstQCtx * qctx,int w,int h)
{

    QGbm_info *op_buf_gbm_info = g_malloc(sizeof(QGbm_info));
    struct gbm_bo *bo = NULL;
    int bo_fd = -1, meta_fd = -1;
    int gbm_flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;

    memset(op_buf_gbm_info,0,sizeof(QGbm_info));
    if (!op_buf_gbm_info) {
        GST_ERROR("Invalid arguments to alloc_map_ion_memory");
        return NULL;
    }

    GST_DEBUG("create NV12 gbm_bo with width=%d, height=%d", w, h);
    if(qctx->format ==  GST_VIDEO_FORMAT_RGBA_UBWC){
      gbm_flags |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
    }
    bo = qctx->gbm_bo_create(qctx->gbm, w, h,GBM_FORMAT_ABGR8888,
              gbm_flags);


    if (bo == NULL) {
      GST_ERROR("Create bo failed");
      return NULL;
    }

    bo_fd = qctx->gbm_bo_get_fd(bo);
    if (bo_fd < 0) {
      GST_ERROR("Get bo fd failed");
      qctx->gbm_bo_destroy(bo);
      return NULL;
    }

    qctx->gbm_perform(GBM_PERFORM_GET_METADATA_ION_FD, bo, &meta_fd);
    if (meta_fd < 0) {
      GST_ERROR("Get bo meta fd failed");
     qctx->gbm_bo_destroy(bo);
      return NULL;
    }
    op_buf_gbm_info->bo = bo;
    op_buf_gbm_info->bo_fd = bo_fd;
    op_buf_gbm_info->meta_fd = meta_fd;

    printf("allocate gbm bo fd meta fd  %p %d %d\n",bo,bo_fd,meta_fd);
    return op_buf_gbm_info;
}

void gbm_memory_free(GstQCtx * qctx,QGbm_info *buf_gbm_info) {
     if(!buf_gbm_info) {
       GST_ERROR(" GBM: free called with invalid fd/allocdata");
       return;
     }
     printf("free gbm bo fd meta fd  %p %d %d\n",
            buf_gbm_info->bo,buf_gbm_info->bo_fd,buf_gbm_info->meta_fd);

     if (buf_gbm_info->bo)
       qctx->gbm_bo_destroy(buf_gbm_info->bo);
     buf_gbm_info->bo = NULL;
     buf_gbm_info->bo_fd = -1;
     buf_gbm_info->meta_fd = -1;
     g_free(buf_gbm_info);
}
