/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <tuple>

#ifdef ANDROID
#include <nativebase/nativebase.h>
#endif // ANDROID

namespace ib2c {

/** ColorFormat
 *
 * Definitions of supported RGB and YUV formats.
 */
enum ColorFormat : uint32_t {
  kGRAY8,

  kRGB565,
  kBGR565,

  kRGB888,
  kBGR888,

  kARGB1555,
  kARGB4444,
  kARGB8888,
  kXRGB8888,

  kABGR1555,
  kABGR4444,
  kABGR8888,
  kXBGR8888,

  kRGBA5551,
  kRGBA4444,
  kRGBA8888,
  kRGBX8888,

  kBGRA4444,
  kBGRA5551,
  kBGRA8888,
  kBGRX8888,

  kYUYV,
  kYVYU,
  kUYVY,
  kVYUY,

  kNV12,
  kNV21,
  kNV16,
  kNV61,
  kNV24,
  kNV42,

  kYUV410,
  kYVU410,
  kYUV411,
  kYVU411,
  kYUV420,
  kYVU420,
  kYUV422,
  kYVU422,
  kYUV444,
  kYVU444,
};

/** ColorMode
 * @kUBWC: Format has Universal Bandwidth Compression.
 * @kBT601: YUV format is following BT 601 standard.
 * @kBT601FullRange: YUV format is full range following full BT 601 standard.
 * @kBT709: YUV format is following BT 709 standard.
 * @kUnsigned: The pixels in a RGB format are represented by unsigned 8-bit. (Default)
 * @kFloat16: The pixels in a RGB format are represented by 16-bit float.
 * @kFloat32: The pixels in a RGB format are represented by 32-bit float.
 * @kSigned: The pixels in a RGB format are represented by signed 8-bit.
 *
 * Definitions of color format modes, used together with color formats.
 */
enum ColorMode : uint32_t {
  kUBWC           = (1 << 8),
  kBT601          = (1 << 9),
  kBT601FullRange = (2 << 9),
  kBT709          = (3 << 9),
  kUnsigned       = (0 << 11),
  kFloat16        = (1 << 11),
  kFloat32        = (2 << 11),
  kSigned         = (3 << 11),
};

/** ConfigMask
 * @kHFlip: Enables horizontal flipping.
 * @kVFlip: Enables vertical flipping.
 *
 * Configuration bits, used in the mask field of Object struct.
 */
enum ConfigMask : uint32_t {
  kHFlip = (1 << 0),
  kVFlip = (1 << 1),
};

/** SurfaceFlags
 * @kInput: Allows surface to be used as a source.
 * @kOutput: Allows surface to be used as a destination.
 *
 * Whether the surface will be used as source, destination of both.
 */
enum SurfaceFlags : uint32_t {
  kInput  = (1 << 0),
  kOutput = (1 << 1),
};

#if defined(ANDROID)
/** Surface
 * @buffer: Pointer to a Android Native Window buffer.
 * @format: Color format plus additional mode bits.
 * @size: Total size of the image surface in bytes.
 *
 * Structure for registering an image as a blit surface on Android platforms.
 */
struct Surface {
  ANativeWindowBuffer* buffer;
  uint32_t             format;
  uint64_t             size;

  Surface() : buffer(nullptr), format(ColorFormat::kGRAY8), size(0) {}

  Surface(const Surface& s) : buffer(s.buffer), format(s.format), size(s.size) {}
};
#else   // ANDROID
/** Surface
 * @fd: Defines the image File Descriptor.
 * @format: Color format plus additional mode bits.
 * @width: Defines width in pixels.
 * @height: Defines height in pixels.
 * @size: Total size of the image surface in bytes.
 * @nplanes: Number of available/active planes.
 * @stride0: Defines stride in bytes for whole buffer if not planar.
 * @offset0: Defines the offset to plane 0 or whole buffer if not planar.
 * @stride1: Defines stride in bytes for plane 1, ignored if not planar.
 * @offset1: Defines the offset to plane 1, ignored if not planar.
 * @stride2: Defines stride in bytes for plane 2, ignored if not planar.
 * @offset2: Defines the offset to plane 2, ignored if not planar.
 *
 * Structure for registering an image as a blit surface on Linux platforms.
 */
struct Surface {
  uint32_t fd;
  uint32_t format;
  uint32_t width;
  uint32_t height;
  uint64_t size;
  uint32_t nplanes;
  uint32_t stride0;
  uint32_t offset0;
  uint32_t stride1;
  uint32_t offset1;
  uint32_t stride2;
  uint32_t offset2;

  Surface()
      : fd(0),
        format(ColorFormat::kGRAY8),
        width(0),
        height(0),
        size(0),
        nplanes(0),
        stride0(0),
        offset0(0),
        stride1(0),
        offset1(0),
        stride2(0),
        offset2(0) {}

  Surface(const Surface& s)
      : fd(s.fd),
        format(s.format),
        width(s.width),
        height(s.height),
        size(s.size),
        nplanes(s.nplanes),
        stride0(s.stride0),
        offset0(s.offset0),
        stride1(s.stride1),
        offset1(s.offset1),
        stride2(s.stride1),
        offset2(s.offset2) {}
};
#endif  // !ANDROID

/** Normalize
 * @scale: Defines the scale factor with which the channel will be multiplied.
 * @offset: Defines the value with which the channel will be offset.
 *
 * Scale and offset values for normalization of quantized RGB formats.
 *
 * Default values are 0.0 (offset) and 1 / 255 (scale). These values when used
 * on standard UINT8 (0 - 255) image will produce values in the range 0.0 - 1.0
 *
 * Normalization formula: (value - offset) * scale
 */
struct Normalize {
  float scale;
  float offset;

  Normalize ()
      : scale(1.0), offset(0.0) {}

  Normalize (float s, float o)
      : scale(s), offset(o) {}

  Normalize (const Normalize& n)
      : scale(n.scale), offset(n.offset) {}
};

/** Region
 * @x: Upper-left X axis coordinate.
 * @y: Upper-left Y axis coordinate.
 * @w: Width.
 * @h: Height.
 *
 * Rectangle definition.
 */
struct Region {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;

  Region ()
      : x(0), y(0), w(0), h(0) {}

  Region (int32_t rw, int32_t rh)
      : x(0), y(0), w(rw), h(rh) {}

  Region (int32_t rx, int32_t ry, int32_t rw, int32_t rh)
      : x(rx), y(ry), w(rw), h(rh) {}

  Region (const Region& r)
      : x(r.x), y(r.y), w(r.w), h(r.h) {}
};

/** Object
 * @id: ID of the source surface associated with this object.
 * @mask: Defines configuration mask.
 * @source: Region from the source surface.
 * @destination: Position and scaling in target surface.
 * @alpha: Object alpha value. 0 = fully transparent, 255 = fully opaque.
 * @rotation: Clock-wise rotation around the Z-axis.
 *
 * Encapsulates the blit parameters for a source surface.
 */
struct Object {
  uint64_t id;

  uint32_t mask;

  Region   source;
  Region   destination;

  uint8_t  alpha;
  float    rotation;

  Object ()
      : id(0), mask(0), source(), destination(), alpha(255), rotation(0.0) {}

  Object (const Object& o)
      : id(o.id),
        mask(o.mask),
        source(o.source),
        destination(o.destination),
        alpha(o.alpha),
        rotation(o.rotation) {}
};

typedef std::vector<Normalize> Normalization;
typedef std::vector<Object> Objects;

// Tuple of <Surface ID, Color, Clear Background, Normalization Values, Blit Objects>
typedef std::tuple<uint64_t, uint32_t, bool, Normalization, Objects> Composition;

typedef std::vector<Composition> Compositions;

/** IEngine
 *
 * Engine interface.
 **/
class IEngine {
 public:
  virtual ~IEngine() {};

  /** CreateSurface
   * @surface: Surface definition.
   * @flags: Surface bit flags.
   *
   * Register a IB2C surface for use into the internal layers.
   *
   * return: Positive surface ID on success or exception on failure.
   **/
  virtual uint64_t CreateSurface(const Surface& surface, uint32_t flags) = 0;

  /** DestroySurface
   * @surface_id: Indentifaction number of the surface.
   *
   * Deregister a IB2C surface from the internal layers.
   *
   * return: Exception on failure.
   **/
  virtual void DestroySurface(uint64_t surface_id) = 0;

  /** Compose
   * @compositions: List of Composition parameters.
   * @synchronous: Whether or not to block until all composition have finished.
   *
   * Execute the given set of blending compositions.
   *
   * return: Pointer to the fence object or nullptr if synchronous is true.
   **/
  virtual std::uintptr_t Compose(const Compositions& compositions,
                                 bool synchronous = false) = 0;

  /** Finish
   * @fence: Fence object .
   *
   * Wait for a submitted compotions to finish.
   *
   * return: Exception on failure.
   **/
  virtual void Finish(std::uintptr_t fence) = 0;
};

/* NewGlEngine
 *
 * Main API for loading an instance of OpenGLES based engine.
 *
 * return: Pointer to new engine instance.
 **/
extern "C" IEngine* NewGlEngine();

} // namespace ib2c
