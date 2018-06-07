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

#ifndef __c2d_blend_H__
#define __c2d_blend_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>

#define ALIGN4K 4096
#define ALIGN128 128
#define ALIGN64 64
#define ALIGN32 32
#define ALIGN16 16
#define ALIGN( num, to ) (((num) + (to-1)) & (~(to-1)))

#define GBM_ERROR_NONE                              0x0
#define GBM_PERFORM_GET_BO_SIZE                     0x8 /*Query BO buffer size*/
#define GBM_PERFORM_GET_METADATA_ION_FD             0x24 /* Get Metadata ion fd from BO*/
#define __gbm_fourcc_code(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
                              ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define GBM_FORMAT_ARGB8888 __gbm_fourcc_code('A', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */
#define GBM_FORMAT_RGBA8888 __gbm_fourcc_code('R', 'A', '2', '4') /* [31:0] R:G:B:A 8:8:8:8 little endian */
#define GBM_FORMAT_NV12     __gbm_fourcc_code('N', 'V', '1', '2') /* 2x2 subsampled Cr:Cb plane */

enum ColorConvertFormat {
    RGB565 = 1,
    YCbCr420Tile,
    YCbCr420SP,
    YCbCr420P,
    YCrCb420P,
    RGBA8888,
    RGBA8888_NO_PREMULTIPLIED,
    ARGB8888,
    ARGB8888_NO_PREMULTIPLIED,
    NV12_2K,
    NV12_128m,
    NV12_UBWC,
    CbYCrY,
};

typedef struct C2DBuffer{
    int fd;
    int handle;
    int size;
    int gbm_format;
    int width;
    int height;
    int pitch;
    int meta_fd;
    struct gbm_bo *gbm_bo;
    void *ptr;
}C2DBuffer;

typedef struct {
    int32_t numerator;
    int32_t denominator;
} C2DBytesPerPixel;

typedef struct {
  int32_t width;
  int32_t height;
  int32_t stride;
  int32_t sliceHeight;
  int32_t lumaAlign;
  int32_t sizeAlign;
  int32_t size;
  C2DBytesPerPixel bpp;
} C2DBuffReq;

typedef enum {
  C2D_INPUT = 0,
  C2D_OUTPUT,
} C2D_PORT;

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

class C2DColorConverterBase {

public:
    virtual ~C2DColorConverterBase(){};
    virtual int convertC2D(int srcFd, void *srcBase, void * srcData, int dstFd, void *dstBase, void * dstData) = 0;
    virtual int32_t getBuffReq(int32_t port, C2DBuffReq *req) = 0;
    virtual int32_t dumpOutput(char * filename, char mode) = 0;
    virtual int32_t dumpInput(char * filename, char mode) = 0;
    virtual int SourceCrop(int x, int y, size_t srcWidth, size_t srcHeight) = 0;
    virtual int SetSourceConfigFlags(int flags) = 0;
    virtual int SetBlend(int x, int y, size_t srcWidth, size_t srcHeight, size_t dstWidth, size_t dstHeight, ColorConvertFormat srcFormat, ColorConvertFormat dstFormat) = 0;
};

typedef C2DColorConverterBase* createC2DColorConverter_t(size_t srcWidth, size_t srcHeight, size_t dstWidth, size_t dstHeight, ColorConvertFormat srcFormat, ColorConvertFormat dstFormat, int32_t flags, size_t srcStride);
typedef void destroyC2DColorConverter_t(C2DColorConverterBase*);

class c2d_blend
{
public:
    c2d_blend();
    ~c2d_blend();
    bool Init();
    bool Open(unsigned int src_height,unsigned int src_width,
              unsigned int dst_height, unsigned int dst_width,
              ColorConvertFormat src, ColorConvertFormat dest, unsigned int flag, unsigned int src_stride);
    bool Convert(int src_fd, void *src_base, void *src_data,
                 int dest_fd, void *dest_base, void *dest_data);
    bool GetBufferSize(int port,unsigned int &buf_size);
    int GetSrcFormat();
    void Close();
    bool AllocateBuffer(int port, struct C2DBuffer *buffer);
    bool FreeBuffer(struct C2DBuffer *buffer);
    int32_t DumpOutput(char * filename, char mode);
    int32_t DumpInput(char * filename, char mode);
    void SetInputCrop(int x, int y, int width, int height);
    void Blend(int x, int y,
               unsigned int src_width, unsigned int src_height,
               unsigned int dst_width, unsigned int dst_height,
               ColorConvertFormat src, ColorConvertFormat dest);

private:
    C2DColorConverterBase *m_c2d_conv;
    pthread_mutex_t m_lock;
    void *m_handle;
    void *m_gbmhandle;
    ColorConvertFormat m_src_format;
    createC2DColorConverter_t *mOpen;
    destroyC2DColorConverter_t *mClose;
    int m_gbm_client_fd;
    int (*gbm_bo_get_fd)(struct gbm_bo *bo);
    int (*gbm_perform )(int operation,...);
    struct gbm_bo * (*gbm_bo_create)(struct gbm_device *gbm,uint32_t width, uint32_t height,uint32_t format, uint32_t flags);
    void (*gbm_bo_destroy)(struct gbm_bo *bo);
    uint32_t (*gbm_bo_get_width)(struct gbm_bo *bo);
    uint32_t (*gbm_bo_get_height)(struct gbm_bo *bo);
    uint32_t (*gbm_bo_get_stride)(struct gbm_bo *bo);
    void (*gbm_device_destroy)(struct gbm_device *gbm);
    struct gbm_device * (*gbm_create_device)(int fd);
    struct gbm_device *m_gbm_dev;

    bool m_bInit;
};

#endif
