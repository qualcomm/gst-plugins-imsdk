/*
 * Copyright (c) 2016, 2018-2021 The Linux Foundation. All rights reserved.
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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*! @file overlay.h
 */

#pragma once

#include <sys/types.h>
#include <map>
#include <mutex>
#include <vector>
#include <memory>

#ifdef ENABLE_C2D
#include <adreno/c2d2.h>
#endif // ENABLE_C2D

#ifdef ENABLE_GLES
#include <iot-core-algs/ib2c.h>
#endif // ENABLE_GLES

/// @namespace overlay
namespace overlay {

#define MAX_STRING_LENGTH 128

static const uint32_t kColorRed = 0xFF0000FF;
static const uint32_t kColorLightGray = 0xCCCCCCFF;
static const uint32_t kColorDarkGray = 0x202020FF;
static const uint32_t kColorYellow = 0xFFFF00FF;
static const uint32_t kColorBlue = 0x0000CCFF;
static const uint32_t kColorWhilte = 0xFFFFFFFF;
static const uint32_t kColorOrange = 0xFF8000FF;
static const uint32_t kColorLightGreen = 0x33CC00FF;
static const uint32_t kColorLightBlue = 0x189BF2FF;

#define OVERLAY_GRAPH_NODES_MAX_COUNT 20
#define OVERLAY_GRAPH_CHAIN_MAX_COUNT 40

enum class OverlayType {
  kDateType,
  kUserText,
  kStaticImage,
  kBoundingBox,
  kPrivacyMask,
  kGraph,
  kArrow,
};

enum class OverlayTimeFormatType {
  kHHMMSS_24HR,
  kHHMMSS_AMPM,
  kHHMM_24HR,
  kHHMM_AMPM
};

enum class OverlayDateFormatType {
  kYYYYMMDD,
  kMMDDYYYY
};

struct OverlayDateTimeType {
  OverlayTimeFormatType time_format;
  OverlayDateFormatType date_format;
};

struct BoundingBox {
  char box_name[MAX_STRING_LENGTH];
};

struct OverlayRect {
  uint32_t start_x;
  uint32_t start_y;
  uint32_t width;
  uint32_t height;
};

struct Overlaycircle {
  uint32_t center_x;
  uint32_t center_y;
  uint32_t radius;
};

struct OverlayPolygon {
  uint32_t n_sides;
  uint32_t *x_coords;
  uint32_t *y_coords;
};

struct OverlayImageInfo {
  char * image_buffer;
  uint32_t image_size;
  OverlayRect source_rect;
  bool buffer_updated;
};

struct OverlayKeyPoint {
  int32_t x;
  int32_t y;
};

enum class OverlayPrivacyMaskType {
  kRectangle,
  kInverseRectangle,
  kCircle,
  kInverseCircle,
  kPolygon,
  kInversePolygon
};

struct OverlayPrivacyMask {
  OverlayPrivacyMaskType type;
  union {
    Overlaycircle  circle;
    OverlayRect    rectangle;
    OverlayPolygon polygon;
  };
};

struct OverlayGraph {
  uint32_t points_count;
  struct OverlayKeyPoint points[OVERLAY_GRAPH_NODES_MAX_COUNT];
  uint32_t chain_count;
  int32_t chain[OVERLAY_GRAPH_CHAIN_MAX_COUNT][2];
};

struct OverlayArrow {
  uint32_t start_x;
  uint32_t start_y;
  uint32_t end_x;
  uint32_t end_y;
};

struct OverlayParam {
  OverlayType type;
  uint32_t color;
  uint32_t font_size;
  uint32_t bbox_stroke_width;
  OverlayRect dst_rect;
  uint32_t arrows_count;
  union {
    OverlayDateTimeType date_time;
    char user_text[MAX_STRING_LENGTH];
    OverlayImageInfo image_info;
    BoundingBox bounding_box;
    OverlayPrivacyMask privacy_mask;
    OverlayGraph graph;
    OverlayArrow *arrows;
  };
};

enum class TargetBufferFormat {
  kYUVNV12,
  kYUVNV21,
  kYUVNV12UBWC,
};

struct OverlayTargetBuffer {
  TargetBufferFormat format;
  uint32_t width;
  uint32_t height;
  uint32_t offset[2];
  uint32_t stride[2];
  uint32_t ion_fd;
  uint32_t frame_len;
};

struct OverlayParamInfo {
  uint32_t *id;
  OverlayParam param;
  bool is_active;
};

enum class OverlayBlitType {
  kC2D,
  kOpenCL,
  kGLES,
};

class OverlayItem;

class OpenClKernel;

// This class provides facility to embed different
// Kinds of overlay on top of Camera stream buffers.
/// This class provides facility to embed different
/// Kinds of overlay on top of Camera stream buffers.
class Overlay {
public:
  Overlay ();

  ~Overlay ();

  // Initialise overlay with format of buffer.
  /// Initialise overlay with format of buffer.
  int32_t Init (OverlayBlitType blit_type);

  // Create overlay item of type static image, date/time, bounding box,
  // simple text, or privacy mask. this Api provides overlay item id which
  // can be use for further configurartion change to item.
  /// Create overlay item of type static image, date/time, bounding box,
  /// simple text, or privacy mask. this Api provides overlay item id which
  /// can be use for further configurartion change to item.
  int32_t CreateOverlayItem (OverlayParam& param, uint32_t* overlay_id);

  // Overlay item can be deleted at any point of time after creation.
  /// Overlay item can be deleted at any point of time after creation.
  int32_t DeleteOverlayItem (uint32_t overlay_id);

  // Overlay item can be deleted at any point of time after creation.
  /// Overlay item can be deleted at any point of time after creation.
  int32_t DeleteOverlayItems ();

  // Overlay item's parameters can be queried using this Api, it is recommended
  // to call get parameters first before setting new parameters using Api
  // updateOverlayItem.
/// Overlay item's parameters can be queried using this Api, it is recommended
/// to call get parameters first before setting new parameters using Api
/// updateOverlayItem.
  int32_t GetOverlayParams (uint32_t overlay_id, OverlayParam& param);

  // Overlay item's configuration can be change at run time using this Api.
  // user has to provide overlay Id and updated parameters.
  /// Overlay item's configuration can be change at run time using this Api.
  /// user has to provide overlay Id and updated parameters.
  int32_t UpdateOverlayParams (uint32_t overlay_id, OverlayParam& param);

  // Overlay Item can be enable/disable at run time.
  /// Overlay Item can be enable/disable at run time.
  int32_t EnableOverlayItem (uint32_t overlay_id);
  int32_t DisableOverlayItem (uint32_t overlay_id);

  // Provide input YUV buffer to apply overlay.
  /// Provide input YUV buffer to apply overlay.
  int32_t ApplyOverlay (const OverlayTargetBuffer& buffer);

  // Process a batch of overlay requests
  // The overlay items are specified as vector and processed
  // This method creates and enables specified overlay items,
  // updates specified overlay items, disables inactive overlay items.
  int32_t ProcessOverlayItems (const std::vector<OverlayParam>& overlay_list);

  // Disables the caching of input surfaces
  // we are maintaining the map of surface ID and FD from buffer
  // In case the input buffers are not acquired from buffer pool, the map grows
  // monotonically. To avoid this, we need to disable caching
  void DisableInputSurfaceCache ();

private:

#ifdef ENABLE_C2D
  uint32_t GetC2dColorFormat (const TargetBufferFormat& format);
#endif // ENABLE_C2D

#ifdef ENABLE_GLES
  uint32_t GetGlesColorFormat (const TargetBufferFormat& format);
#endif // ENABLE_GLES

  bool IsOverlayItemValid (uint32_t overlay_id);

#ifdef ENABLE_C2D
  int32_t ApplyOverlay_C2D (const OverlayTargetBuffer& buffer);
#endif // ENABLE_C2D

#ifdef ENABLE_GLES
  int32_t ApplyOverlay_GLES (const OverlayTargetBuffer& buffer);
#endif // ENABLE_GLES

  int32_t ApplyOverlay_CL (const OverlayTargetBuffer& buffer);

  std::map<uint32_t, OverlayItem*> overlay_items_;

#ifdef ENABLE_C2D
  uint32_t target_c2dsurface_id_;
#endif // ENABLE_C2D

#ifdef ENABLE_GLES
  std::shared_ptr<::ib2c::IEngine> ib2c_engine_;
  std::map<int32_t, uint64_t> ib2c_surfaces_;
#endif // ENABLE_GLES

  int32_t ion_device_;
  uint32_t id_;
  std::mutex lock_;
  OverlayBlitType blit_type_;
  bool in_surf_cache_ = true;
};

}; // namespace overlay
