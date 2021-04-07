/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
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

#pragma once

#include <adreno/c2d2.h>
#include <cutils/properties.h>
#include <ion/ion.h>
#include <linux/msm_ion.h>
#include <linux/msm_kgsl.h>
#include <gst/gst.h>
#include <cairo/cairo.h>
#include <sys/time.h>

#include <CL/cl.h>
#include <CL/cl_ext.h>

namespace overlay {

#define OVERLAYITEM_X_MARGIN_PERCENT  0.5
#define OVERLAYITEM_Y_MARGIN_PERCENT  0.5
#define MAX_LEN       128
#define MAX_OVERLAYS  10
#define BG_TRANSPARENT_COLOR 0xFFFFFF00
#define BG_DEBUG_COLOR       0xFFE5CC80 //Light gray.
#define DOWNSCALE_FACTOR     4

// Remove comment marker to enable backgroud surface drawing of overlay objects.
//#define DEBUG_BACKGROUND_SURFACE

// Remove comment marker to measure time taken in overlay drawing.
//#define DEBUG_BLIT_TIME

#define PROP_DUMP_BLOB_IMAGE        "persist.overlay.dump.blob"
#define PROP_BOX_STROKE_WIDTH       "persist.overlay.stroke.width"

#define OV_UNUSED(a) (void)(a)

struct OpenClFrame {
  cl_mem cl_buffer;
  cl_uint plane0_offset;
  cl_uint plane1_offset;
  cl_ushort stride0;
  cl_ushort stride1;
  cl_ushort swap_uv;
};

enum CLKernelIds {
  CL_KERNEL_BLIT_RGBA,
  CL_KERNEL_BLIT_BGRA,
  CL_KERNEL_PRIVACY_MASK,
  CL_KERNEL_NONE, // use C2D
};

struct CLKernelDescriptor {
  CLKernelIds id;
  std::string kernel_path;
  std::string kernel_name;
  bool use_alpha_only;
  bool use_2D_image;
  size_t global_devider_w;
  size_t global_devider_h;
  size_t local_size_w;
  size_t local_size_h;
  std::shared_ptr<OpenClKernel> instance;
};

struct DrawInfo {
  uint32_t width;
  uint32_t height;
  uint32_t x;
  uint32_t y;
  cl_mem mask;
  std::shared_ptr<OpenClKernel> blit_inst;
  uint32_t c2dSurfaceId;
  uint32_t in_width;
  uint32_t in_height;
  uint32_t in_x;
  uint32_t in_y;
  size_t global_devider_w;
  size_t global_devider_h;
  size_t local_size_w;
  size_t local_size_h;
};

class OpenClKernel {
 public:

  OpenClKernel (const std::string &kernel_name) :
          kernel_name_ (kernel_name),
          prog_ (nullptr),
          kernel_ (nullptr),
          kernel_dimensions_ (2),
          local_size_ { 0, 0 },
          global_size_ { 0, 0 },
          global_offset_ { 0, 0 }
  {
    g_cond_init (&sync_.signal_);
    g_mutex_init (&sync_.lock_);
  }

  OpenClKernel (const OpenClKernel &other) :
          kernel_name_ (other.kernel_name_),
          prog_ (other.prog_),
          kernel_ (nullptr),
          kernel_dimensions_ (other.kernel_dimensions_),
          local_size_ { 0, 0 },
          global_size_ { 0, 0 },
          global_offset_ { 0, 0 }
  {
    g_cond_init (&sync_.signal_);
    g_mutex_init (&sync_.lock_);
  }

  ~OpenClKernel ();

  static std::shared_ptr<OpenClKernel> New (const std::string &path_to_src,
      const std::string &name);

  std::shared_ptr<OpenClKernel> AddInstance ();

  int32_t BuildProgram (const std::string &path_to_src);

  int32_t SetKernelArgs (OpenClFrame &frame, DrawInfo args);

  int32_t RunCLKernel (bool wait_to_finish);

  static int32_t MapBuffer (cl_mem &cl_buffer, void *vaddr, int32_t fd,
      uint32_t size);

  static int32_t UnMapBuffer (cl_mem &cl_buffer);

  static int32_t MapImage (cl_mem &cl_buffer, void *vaddr, int32_t fd,
      size_t width, size_t height, uint32_t stride);

  static int32_t unMapImage (cl_mem &cl_buffer);

  static CLKernelDescriptor supported_kernels [];

 private:

  static int32_t OpenCLInit ();

  static int32_t OpenCLDeInit ();

  int32_t CreateKernelInstance ();

  static void ClCompleteCallback (cl_event event,
      cl_int event_command_exec_status, void *user_data);

  std::string CreateCLKernelBuildLog ();

  static cl_device_id device_id_;
  static cl_context context_;
  static cl_command_queue command_queue_;
  static std::mutex lock_;
  static int32_t ref_count;

  std::string kernel_name_;
  cl_program prog_;
  cl_kernel kernel_;
  cl_uint kernel_dimensions_;
  size_t local_size_[2];
  size_t global_size_[2];
  size_t global_offset_[2];

  static const gint64 kWaitProcessTimeout =
      G_GINT64_CONSTANT (2000000); // 2 sec.

  struct SyncObject {
    bool done_;
    GCond signal_;
    GMutex lock_;
  } sync_;
};

struct RGBAValues {
  double red;
  double green;
  double blue;
  double alpha;
};

struct C2dObjects {
  C2D_OBJECT objects[MAX_OVERLAYS * 2];
};

enum class SurfaceFormat {
  kARGB,
  kABGR,
  kRGB,
  kA8,
  kA1
};

class OverlaySurface {
 public:
  OverlaySurface () :
          width_ (0),
          height_ (0),
          stride_ (0),
          format_ (SurfaceFormat::kARGB),
          gpu_addr_ (nullptr),
          vaddr_ (nullptr),
          ion_fd_ (0),
          size_ (0),
          cl_buffer_ (nullptr),
          blit_inst_ (nullptr),
          c2dsurface_id_ (-1) {}

  uint32_t width_;
  uint32_t height_;
  uint32_t stride_;
  SurfaceFormat format_;
  void * gpu_addr_;
  void * vaddr_;
  int32_t ion_fd_;
  uint32_t size_;

  cl_mem cl_buffer_;
  std::shared_ptr<OpenClKernel> blit_inst_;
  uint32_t c2dsurface_id_;
};

//Base class for all types of overlays.
class OverlayItem {
 public:

  OverlayItem (int32_t ion_device, OverlayType type, CLKernelIds kernel_id);

  virtual ~OverlayItem ();

  virtual int32_t Init (OverlayParam& param) = 0;

  virtual int32_t UpdateAndDraw () = 0;

  virtual void GetDrawInfo (uint32_t target_width, uint32_t target_height,
      std::vector<DrawInfo>& draw_infos) = 0;

  virtual void GetParameters (OverlayParam& param) = 0;

  virtual int32_t UpdateParameters (OverlayParam& param) = 0;

  OverlayType& GetItemType ()
  {
    return type_;
  }

  void MarkDirty (bool dirty);

  void Activate (bool value);

  bool IsActive ()
  {
    return is_active_;
  }

  uint32_t CalcStride (uint32_t width, SurfaceFormat format);

  uint32_t GetC2DFormat (SurfaceFormat format);

  cairo_format_t GetCairoFormat (SurfaceFormat format);

protected:

  struct IonMemInfo {
    uint32_t size;
    int32_t fd;
    void * vaddr;
  };

  int32_t AllocateIonMemory (IonMemInfo& mem_info, uint32_t size);

  void FreeIonMemory (void *&vaddr, int32_t &ion_fd, uint32_t size);

  int32_t MapOverlaySurface (OverlaySurface &surface, IonMemInfo &mem_info);

  void UnMapOverlaySurface (OverlaySurface &surface);

  void ExtractColorValues (uint32_t hex_color, RGBAValues* color);

  virtual int32_t CreateSurface () = 0;

  void ClearSurface ();

  virtual void DestroySurface ();

  int32_t x_;
  int32_t y_;
  uint32_t width_;
  uint32_t height_;
  OverlaySurface surface_;
  bool dirty_;
  int32_t ion_device_;
  OverlayType type_;
  cairo_surface_t* cr_surface_;
  cairo_t* cr_context_;
  OverlayBlitType blit_type_;
  CLKernelIds kernel_id_;
  std::shared_ptr<OpenClKernel> blit_;
  bool use_alpha_only_;
  bool use_2D_image_;
  size_t global_devider_w_;
  size_t global_devider_h_;
  size_t local_size_w_;
  size_t local_size_h_;

 private:

  bool is_active_;
};

class OverlayItemStaticImage : public OverlayItem {
 public:

  OverlayItemStaticImage (int32_t ion_device, CLKernelIds kernel_id) :
          OverlayItem (ion_device, OverlayType::kStaticImage, kernel_id) {};

  virtual ~OverlayItemStaticImage () {};

  int32_t Init (OverlayParam& param) override;

  int32_t UpdateAndDraw () override;

  void GetDrawInfo (uint32_t target_width, uint32_t target_height,
      std::vector<DrawInfo>& draw_infos) override;

  void GetParameters (OverlayParam& param) override;

  int32_t UpdateParameters (OverlayParam& param) override;

 private:

  int32_t CreateSurface ();

  void DestroySurface ();

  char * image_buffer_;
  uint32_t image_size_;
  uint32_t crop_rect_x_;
  uint32_t crop_rect_y_;
  uint32_t crop_rect_width_;
  uint32_t crop_rect_height_;
  bool blob_buffer_updated_;
  std::mutex update_param_lock_;
};

class OverlayItemDateAndTime : public OverlayItem {
 public:

  OverlayItemDateAndTime (int32_t ion_device, CLKernelIds kernel_id);

  virtual ~OverlayItemDateAndTime ();

  int32_t Init (OverlayParam& param) override;

  int32_t UpdateAndDraw () override;

  void GetDrawInfo (uint32_t target_width, uint32_t target_height,
      std::vector<DrawInfo>& draw_infos) override;

  void GetParameters (OverlayParam& param) override;

  int32_t UpdateParameters (OverlayParam& param) override;

private:

  static const int kTextSize = 20;
  static const int kCairoBufferMinWidth = kTextSize * 6;
  static const int kCairoBufferMinHeight = kTextSize * 2;

  int32_t CreateSurface ();

  OverlayDateTimeType date_time_type_;
  uint32_t text_color_;
  time_t prev_time_;
};

class OverlayItemBoundingBox : public OverlayItem {
 public:

  OverlayItemBoundingBox (int32_t ion_device, CLKernelIds kernel_id);

  virtual ~OverlayItemBoundingBox ();

  int32_t Init (OverlayParam& param) override;

  int32_t UpdateAndDraw () override;

  void GetDrawInfo (uint32_t target_width, uint32_t target_height,
      std::vector<DrawInfo>& draw_infos) override;

  void GetParameters (OverlayParam& param) override;

  int32_t UpdateParameters (OverlayParam& param) override;

 private:

  static const int32_t kBoxBuffWidth = 320;
  static const int32_t kStrokeWidth = 4;
  static const int32_t kTextLimit = 20;
  static const int32_t kTextSize = 25;
  static const int32_t kTextPercent = 20;
  static const int32_t kTextMargin = kStrokeWidth + 4;

  int32_t CreateSurface ();

  void ClearTextSurface ();

  void DestroyTextSurface ();

  uint32_t bbox_color_;
  std::string bbox_name_;
  uint32_t text_height_ = 0;

  OverlaySurface text_surface_;
  uint32_t box_stroke_width_;
  cairo_surface_t* text_cr_surface_;
  cairo_t* text_cr_context_;
};

class OverlayItemText : public OverlayItem {
 public:

  OverlayItemText (int32_t ion_device, CLKernelIds kernel_id) :
      OverlayItem (ion_device, OverlayType::kUserText, kernel_id) {};

  virtual ~OverlayItemText ();

  int32_t Init (OverlayParam& param) override;

  int32_t UpdateAndDraw () override;

  void GetDrawInfo (uint32_t target_width, uint32_t target_height,
      std::vector<DrawInfo>& draw_infos) override;

  void GetParameters (OverlayParam& param) override;

  int32_t UpdateParameters (OverlayParam& param) override;

 private:

  static const uint32_t kTextSize = 40;
  static const uint32_t kCairoBufferMinWidth = kTextSize * 4;
  static const uint32_t kCairoBufferMinHeight = kTextSize;

  int32_t CreateSurface ();

  uint32_t text_color_;
  std::string text_;
};

class OverlayItemPrivacyMask : public OverlayItem {
 public:

  OverlayItemPrivacyMask (int32_t ion_device, CLKernelIds kernel_id) :
          OverlayItem (ion_device, OverlayType::kPrivacyMask, kernel_id) {};

  virtual ~OverlayItemPrivacyMask () {};

  int32_t Init (OverlayParam& param) override;

  int32_t UpdateAndDraw () override;

  void GetDrawInfo (uint32_t target_width, uint32_t target_height,
      std::vector<DrawInfo>& draw_infos) override;

  void GetParameters (OverlayParam& param) override;

  int32_t UpdateParameters (OverlayParam& param) override;

 private:

  static const uint32_t kMaskBoxBufWidth = 3840;

  int32_t CreateSurface ();
  uint32_t mask_color_;
  OverlayPrivacyMask config_;
};

class OverlayItemGraph : public OverlayItem {
 public:

  OverlayItemGraph (int32_t ion_device, CLKernelIds kernel_id) :
      OverlayItem (ion_device, OverlayType::kGraph, kernel_id) {};

  virtual ~OverlayItemGraph () {};

  int32_t Init (OverlayParam& param) override;

  int32_t UpdateAndDraw () override;

  void GetDrawInfo (uint32_t target_width, uint32_t target_height,
      std::vector<DrawInfo>& draw_infos) override;

  void GetParameters (OverlayParam& param) override;

  int32_t UpdateParameters (OverlayParam& param) override;

 private:

  int32_t CreateSurface ();

  static const int kDotRadius = 3;
  static const int kLineWidth = 2;
  static const int kGraphBufWidth = 480;
  static const int kGraphBufHeight = 270;

  uint32_t graph_color_;
  float downscale_ratio_;
  OverlayGraph graph_;
};


#ifdef DEBUG_BLIT_TIME
class Timer {
  std::string str;
  uint64_t begin;
  uint64_t *avr_time;

public:

  Timer (std::string s) : str(s), avr_time(nullptr) {
    begin = GetMicroSeconds();
  }

  Timer (std::string s, uint64_t * avrg) : str(s), avr_time(avrg) {
    begin = GetMicroSeconds();
  }

  ~Timer () {
    uint64_t diff = GetMicroSeconds() - begin;

    if (avr_time) {
      if (!(*avr_time)) {
        *avr_time = diff;
      }
      *avr_time = (15 * (*avr_time) + diff) / 16;
      OVDBG_INFO ("%s: Current: %.2f ms Avrg: %.2f ms", str.c_str(),
          diff / 1000.0, (*avr_time) / 1000.0);
    } else {
      OVDBG_INFO ("%s: %.2f ms", str.c_str(), diff / 1000.0);
    }
  }

  uint64_t GetMicroSeconds() {
    timespec time;

    clock_gettime(CLOCK_MONOTONIC, &time);

    uint64_t microSeconds = (static_cast<uint32_t>(time.tv_sec) * 1000000ULL) +
                            (static_cast<uint32_t>(time.tv_nsec)) / 1000;

    return microSeconds;
  }
};
#endif // DEBUG_BLIT_TIME

}; // namespace overlay
