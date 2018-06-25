/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
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
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <linux/msm_ion.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <gst/gstinfo.h>
#include "c2d_blend.h"
#define ALIGN4K 4096

c2d_blend::c2d_blend()
    : m_c2d_conv(NULL),
    m_handle(NULL),
    m_src_format(RGBA8888),
    mOpen(NULL),
    mClose(NULL),
    m_gbm_client_fd(-1),
    m_gbm_dev(NULL),
    m_bInit(false)
{
    pthread_mutex_init(&m_lock, NULL);
}

c2d_blend::~c2d_blend()
{
    if (m_handle)
    {
        if (mClose && m_c2d_conv)
        {
            pthread_mutex_lock(&m_lock);
            mClose(m_c2d_conv);
            pthread_mutex_unlock(&m_lock);
        }
        dlclose(m_handle);
    }
    if (m_gbm_dev) gbm_device_destroy (m_gbm_dev);
    m_gbm_dev = NULL;
    if (m_gbm_client_fd != -1) close(m_gbm_client_fd);
    m_gbm_client_fd = -1;
    pthread_mutex_destroy(&m_lock);
}

bool c2d_blend::Init()
{
    bool bStatus = true;

    if (m_bInit) bStatus = false;

    if (bStatus)
    {
        m_handle = dlopen("libc2dcolorconvert.so", RTLD_LAZY);
        if (m_handle)
        {
            mOpen = (createC2DColorConverter_t *)
                dlsym(m_handle,"createC2DColorConverter");
            mClose = (destroyC2DColorConverter_t *)
                dlsym(m_handle,"destroyC2DColorConverter");
            if (!mOpen || !mClose)
                bStatus = false;
        }
        else
        {
            bStatus = false;
        }
    }

    if (bStatus)
    {
        m_gbmhandle = dlopen("libgbm.so", RTLD_NOW);
        if (m_gbmhandle)
        {
            gbm_create_device = (struct gbm_device * (*)(int)) dlsym(m_gbmhandle,"gbm_create_device");
            gbm_device_destroy = (void (*)(struct gbm_device *)) dlsym(m_gbmhandle,"gbm_device_destroy");
            gbm_bo_get_height = (uint32_t (*)(struct gbm_bo *)) dlsym(m_gbmhandle,"gbm_bo_get_height");
            gbm_bo_get_stride = (uint32_t (*)(struct gbm_bo *)) dlsym(m_gbmhandle,"gbm_bo_get_stride");
            gbm_bo_create = (struct gbm_bo * (*)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t)) dlsym(m_gbmhandle,"gbm_bo_create");
            gbm_bo_destroy = (void (*)(struct gbm_bo *)) dlsym(m_gbmhandle,"gbm_bo_destroy");
            gbm_bo_get_fd = (int (*)(struct gbm_bo *)) dlsym(m_gbmhandle,"gbm_bo_get_fd");
            gbm_perform = (int (* )(int, ...)) dlsym(m_gbmhandle,"gbm_perform");
            GST_DEBUG("gbm %p %p %p %p %p %p %p %p",
                gbm_create_device,
                gbm_device_destroy,
                gbm_bo_get_height,
                gbm_bo_get_stride,
                gbm_bo_create,
                gbm_bo_destroy,
                gbm_bo_get_fd,
                gbm_perform);
            if (!gbm_create_device || !gbm_device_destroy
                || !gbm_bo_get_height || !gbm_bo_get_stride
                || !gbm_bo_create || !gbm_bo_destroy
                || !gbm_bo_get_fd || !gbm_perform)
                bStatus = false;
        }
        else
        {
            bStatus = false;
        }
    }

    if (bStatus)
    {
        m_gbm_client_fd = ::open ("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (m_gbm_client_fd < 0) {
            GST_ERROR("failed to open gbm device");
            bStatus = false;
        }

        m_gbm_dev = gbm_create_device (m_gbm_client_fd);
        if (NULL == m_gbm_dev) {
            GST_ERROR ("failed to create gbm_device");
            bStatus = false;
        }
    }

    if (!bStatus)
    {
        if (m_handle) dlclose(m_handle);
        if (m_gbm_dev)
            gbm_device_destroy (m_gbm_dev);
        m_gbm_dev = NULL;
        if (m_gbm_client_fd != -1) close (m_gbm_client_fd);
        m_gbm_client_fd = -1;
        m_handle = NULL;
        mOpen = NULL;
        mClose = NULL;
    }
    if(bStatus) m_bInit = true;
    return bStatus;
}

bool c2d_blend::Convert(int src_fd, void *src_base, void *src_data,
                        int dest_fd, void *dest_base, void *dest_data)
{
    int result;
    if (!src_base || !src_data || !dest_base || !dest_data || !m_c2d_conv)
    {
        GST_ERROR("Invalid arguments c2d_blend::convert");
        return false;
    }
    pthread_mutex_lock(&m_lock);
    result =  m_c2d_conv->convertC2D(src_fd, src_base, src_data,
                                     dest_fd, dest_base, dest_data);
    pthread_mutex_unlock(&m_lock);
    return ((result < 0)?false:true);
}

bool c2d_blend::Open(unsigned int src_height,unsigned int src_width,
                     unsigned int dst_height, unsigned int dst_width,
                     ColorConvertFormat src, ColorConvertFormat dest, unsigned int flag, unsigned int src_stride)
{
    bool bStatus = false;
    pthread_mutex_lock(&m_lock);
    if (!m_c2d_conv) {
        m_c2d_conv = mOpen(src_width, src_height, dst_width, dst_height,
                           src, dest, flag ,src_stride);
        if (m_c2d_conv) {
            m_src_format = src;
            bStatus = true;
        } else
            GST_ERROR("mOpen failed");
    }
    pthread_mutex_unlock(&m_lock);
    return bStatus;
}

void c2d_blend::Close()
{
    if (m_handle) {
        pthread_mutex_lock(&m_lock);
        if (mClose && m_c2d_conv)
            mClose(m_c2d_conv);
        pthread_mutex_unlock(&m_lock);
        m_c2d_conv = NULL;
    }
}

int c2d_blend::GetSrcFormat()
{
    return m_src_format;
}

bool c2d_blend::GetBufferSize(int port,unsigned int &buf_size)
{
    bool ret = false;
    C2DBuffReq buffer_req;
    if (m_c2d_conv)
    {
        memset(&buffer_req, 0, sizeof(buffer_req));
        pthread_mutex_lock(&m_lock);
        if (true == m_c2d_conv->getBuffReq(port,&buffer_req))
            ret = false;
        else
            ret = true;
        pthread_mutex_unlock(&m_lock);
        buf_size = buffer_req.size;
    }
    return ret;
}

bool c2d_blend::AllocateBuffer(int port,struct C2DBuffer *buffer)
{
    if ((port != C2D_INPUT) && (port != C2D_OUTPUT))
    {
        GST_ERROR("bad port param, port = %d",port);
        return false;
    }

    if (!buffer)
    {
        GST_ERROR("Buffer param pointer is NULL");
        return false;
    }

    if (!buffer->width || !buffer->height) {
        GST_ERROR("Buffer param width/height is NULL");
        return false;
    }
    if (!buffer->gbm_format) {
        GST_ERROR("Buffer format is NULL");
        return false;
    }

    buffer->gbm_bo = gbm_bo_create (m_gbm_dev, buffer->width, buffer->height, buffer->gbm_format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING | GBM_BO_USE_WRITE);
    if (NULL == buffer->gbm_bo) {
        GST_ERROR ("failed to create a bo");
        return false;
    }

    buffer->fd = -1;
    buffer->meta_fd = -1;
    buffer->fd = gbm_bo_get_fd (buffer->gbm_bo);
    gbm_perform (GBM_PERFORM_GET_METADATA_ION_FD, buffer->gbm_bo, &buffer->meta_fd);
    if (buffer->fd <= 0 || buffer->meta_fd <= 0) {
        GST_ERROR ("the fds(bo_fd:%d, meta_fd:%d) are invalid",
                   buffer->fd, buffer->meta_fd);
        gbm_bo_destroy (buffer->gbm_bo);
        buffer->gbm_bo = NULL;
        return false;
    }

    buffer->pitch = gbm_bo_get_stride (buffer->gbm_bo);
    int result;

    result = gbm_perform(GBM_PERFORM_GET_BO_SIZE, buffer->gbm_bo, &buffer->size);
    if (GBM_ERROR_NONE != result)
    {
        GST_ERROR ("ERROR: get length error");
        gbm_bo_destroy (buffer->gbm_bo);
        buffer->gbm_bo = NULL;
        return false;
    }

    void *va = mmap(NULL, buffer->size, PROT_READ|PROT_WRITE, MAP_SHARED, buffer->fd, 0);
    if (MAP_FAILED == va) {
        GST_ERROR("failed to map buffer of size = %u, fd = 0x%x",
                  buffer->size, buffer->fd);
        gbm_bo_destroy (buffer->gbm_bo);
        buffer->gbm_bo = NULL;
        return false;
    }

    buffer->ptr = va;
    return true;
}

bool c2d_blend::FreeBuffer(struct C2DBuffer *buffer)
{
    if (!buffer)
    {
        GST_ERROR("Buffer param pointer is NULL");
        return false;
    }

    if (buffer->ptr) {
        munmap (buffer->ptr, buffer->size);
        buffer->ptr = NULL;
    }

    if (buffer->gbm_bo)
        gbm_bo_destroy (buffer->gbm_bo);
    buffer->gbm_bo = NULL;
    buffer->fd = -1;
    buffer->meta_fd = -1;

    return true;
}

int32_t c2d_blend::DumpOutput(char * filename, char mode)
{
    if (!filename)
    {
        GST_ERROR("Null pointer for file name");
        return -1;
    }
    return m_c2d_conv->dumpOutput(filename, mode);
}

int32_t c2d_blend::DumpInput(char * filename, char mode)
{
    if (!filename)
    {
        GST_ERROR("Null pointer for file name");
        return -1;
    }
    return m_c2d_conv->dumpInput(filename, mode);
}

void c2d_blend::SetInputCrop(int x, int y, int width, int height)
{
    m_c2d_conv->SourceCrop(x, y, width, height);
}

void c2d_blend::Blend(int x, int y, unsigned int src_width,unsigned int src_height, unsigned int dst_width, unsigned int dst_height, ColorConvertFormat src, ColorConvertFormat dest)
{
    m_c2d_conv->SetBlend(x, y, src_width, src_height, dst_width, dst_height, src, dest);
}
