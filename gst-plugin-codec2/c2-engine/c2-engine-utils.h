/*
* Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __GST_C2_ENGINE_UTILS_H__
#define __GST_C2_ENGINE_UTILS_H__

#include <map>
#include <unordered_map>
#include <vector>
#include <sstream>
#include <cstring>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/allocators/allocators.h>
#include <C2AllocatorGBM.h>
#include <C2Config.h>
#include <QC2V4L2Config.h>

#include "c2-engine-params.h"
#include "c2-module.h"

/** GstC2Utils
 *
 * Engine helper C++ class for assisting in the translation between the engine
 * GST/GLIB based parameters and the Codec2 component parameters.
 **/
class GstC2Utils {
 public:
  /** ParamIndex
   * @type: Engine parameter type.
   *
   * Find and return the corresponding Codec2 component parameter.
   *
   * return: The index of the corresponding Codec2 parameter.
   **/
  static C2Param::Index ParamIndex(uint32_t type);

  /** ParamName
   * @type: Engine parameter type.
   *
   * Get the parameter name in string format.
   *
   * return: The name of the parameter type.
   **/
  static const char* ParamName(uint32_t type);

  /** PixelFormat
   * @format: GStreamer video format.
   * @n_subframes: The number of subframes inside one buffer.
   *
   * Get the equivalent Codec2 pixel format.
   *
   * return: The corresponding Codec2 pixel format.
   **/
  static C2PixelFormat PixelFormat(GstVideoFormat format, guint32 n_subframes);

  /** VideoFormat
   * @format: Codec2 pixel format.
   *
   * Get the equivalent GStreamer video format.
   *
   * return: Tuple with corresponding GStreamer video format and
   * number of subframes inside one buffer.
   **/
  static std::tuple<GstVideoFormat, uint32_t> VideoFormat(C2PixelFormat format);

  /** UnpackPayload
   * @type: Engine parameter type.
   * @payload: Pointer to the structure or variable that corresponds to the
   *           given parameter type.
   * @c2param: Reference to Codec2 parameter structure which will be filled
   *           with the corrsponding data from the payload.
   *
   * Takes the data from the given engine parameter and fills the corresponding
   * fields in the Codec2 parameter structure.
   *
   * return: True on success or false on failure.
   **/
  static bool UnpackPayload(uint32_t type, void* payload,
                            std::unique_ptr<C2Param>& c2param);

  /** PackPayload
   * @type: Engine parameter type.
   * @c2param: Reference to Codec2 parameter structure containing the data.
   * @payload: Pointer to the structure or variable that corresponds to the
   *           given parameter type and which will be filled with data.
   *
   * Takes the data from the given Codec2 parameter and fills the corresponding
   * fields in the engine payload structure.
   *
   * return: True on success or false on failure.
   **/
  static bool PackPayload(uint32_t type, std::unique_ptr<C2Param>& c2param,
                          void* payload);

  /** ImportHandleInfo
   * @buffer: Pointer to GStreamer buffer.
   * @handle: Pointer to Codec2 GBM handle to be filled with data.
   * @n_subframes: The number of subframes inside one buffer.
   *
   * Fills Codec2 GBM handle with the information (fd, width, height, etc.)
   * imported from the GStreamer buffer.
   *
   * return: True on success or false on failure.
   **/
  static bool ImportHandleInfo(GstBuffer* buffer,
                               ::android::C2HandleGBM* handle,
                               uint32_t n_subframes);

  /** ExtractHandleInfo
   * @buffer: Pointer to GStreamer buffer to which video metadata will be added.
   * @handle: Pointer to Codec2 GBM handle.
   *
   * Extacts the video information contained in the Codec2 GBM handle and
   * attaches it as GstVideoMeta to the buffer.
   *
   * return: True on success or false on failure.
   **/
  static bool ExtractHandleInfo(GstBuffer* buffer,
                                const ::android::C2HandleGBM* handle);

  /** AppendCodecMeta
   * @buffer: Pointer to GStreamer buffer to which video metadata will be added.
   * @handle: Reference to Codec2  buffer.
   *
   * Extacts the encoded information contained in the Codec2 buffer and
   * attaches it as CodecInfo to the GStreamer buffer.
   *
   * return: True on success or false on failure.
   **/
  static bool AppendCodecMeta(GstBuffer* buffer,
      std::shared_ptr<C2Buffer>& c2buffer);

  /** CreateBuffer
   * @buffer: Pointer to GStreamer buffer.
   * @block: Reference to Codec2 graphic block.
   *
   * Copy the data from the GStreamer buffer into the Codec2 graphic block
   * and place it into a Codec2 buffer wrapper.
   *
   * return: Empty shared pointer on failure.
   **/
  static std::shared_ptr<C2Buffer> CreateBuffer(
      GstBuffer* buffer, std::shared_ptr<C2GraphicBlock>& block);

  /** CreateBuffer
   * @buffer: Pointer to GStreamer buffer.
   * @block: Reference to Codec2 linear block.
   *
   * Copy the data from the GStreamer buffer into the Codec2 linear block
   * and place it into a Codec2 buffer wrapper.
   *
   * return: Empty shared pointer on failure.
   **/
  static std::shared_ptr<C2Buffer> CreateBuffer(
      GstBuffer* buffer, std::shared_ptr<C2LinearBlock>& block);

#if defined(ENABLE_AUDIO_PLUGINS)
  /** CreateBuffer
   * @buffer: Pointer to GStreamer buffer.
   * @c2Buffer: Reference to Codec2 C2Buffer.
   *
   * Copy the data from the GStreamer buffer into the Codec2 C2Buffer
   *
   * return: Empty shared pointer on failure.
   **/
  static std::shared_ptr<C2Buffer> CreateBuffer(
      GstBuffer* buffer, std::shared_ptr<qc2audio::QC2Buffer>& qc2Buffer);
#endif //ENABLE_AUDIO_PLUGINS

  /** ImportGraphicBuffer
   * @buffer: Pointer to GStreamer buffer.
   * @n_subframes: The number of subframes inside one buffer.
   *
   * Create Graphic Codec2 buffer from GStreamer buffer without copy.
   *
   * return: Empty shared pointer on failure.
   **/
  static std::shared_ptr<C2Buffer> ImportGraphicBuffer(GstBuffer* buffer,
                                                       uint32_t n_subframes);

#if defined(ENABLE_LINEAR_DMABUF)
  /** ImportLinearBuffer
   * @buffer: Pointer to GStreamer buffer.
   *
   * Create Linear Codec2 buffer from GStreamer buffer without copy.
   *
   * return: Empty shared pointer on failure.
   **/
  static std::shared_ptr<C2Buffer> ImportLinearBuffer(GstBuffer* buffer);
#endif // ENABLE_LINEAR_DMABUF
};

#endif // __GST_C2_ENGINE_UTILS_H__
