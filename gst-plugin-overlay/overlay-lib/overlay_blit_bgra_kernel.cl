/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

const sampler_t smp =
    CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;

kernel void overlay_bgra_blit(__read_only image2d_t mask, // 1
                              __global uchar *frame,      // 2
                              uint y_offset,              // 3
                              uint nv_offset,             // 4
                              ushort stride,              // 5
                              ushort swap_uv,             // 6
                              ushort mask_stride          // 7
) {
  uint x = get_global_id(0);
  uint y = get_global_id(1);

  // Read and resize mask data
  float2 coord;
  coord.s0 = (x) / (get_global_size(0) - 0.5f);
  coord.s1 = (y) / (get_global_size(1) - 0.5f);
  uchar4 mask_data1 = convert_uchar4(read_imageui(mask, smp, coord));

  coord.s0 = (x + 0.25f) / (get_global_size(0) - 0.5f);
  coord.s1 = (y) / (get_global_size(1) - 0.5f);
  uchar4 mask_data2 = convert_uchar4(read_imageui(mask, smp, coord));

  coord.s0 = (x + 0.5f) / (get_global_size(0) - 0.5f);
  coord.s1 = (y) / (get_global_size(1) - 0.5f);
  uchar4 mask_data3 = convert_uchar4(read_imageui(mask, smp, coord));

  coord.s0 = (x + 0.75f) / (get_global_size(0) - 0.5f);
  coord.s1 = (y) / (get_global_size(1) - 0.5f);
  uchar4 mask_data4 = convert_uchar4(read_imageui(mask, smp, coord));

  coord.s0 = (x) / (get_global_size(0) - 0.5f);
  coord.s1 = (y + 0.5f) / (get_global_size(1) - 0.5f);
  uchar4 mask_data5 = convert_uchar4(read_imageui(mask, smp, coord));

  coord.s0 = (x + 0.25f) / (get_global_size(0) - 0.5f);
  coord.s1 = (y + 0.5f) / (get_global_size(1) - 0.5f);
  uchar4 mask_data6 = convert_uchar4(read_imageui(mask, smp, coord));

  coord.s0 = (x + 0.5f) / (get_global_size(0) - 0.5f);
  coord.s1 = (y + 0.5f) / (get_global_size(1) - 0.5f);
  uchar4 mask_data7 = convert_uchar4(read_imageui(mask, smp, coord));

  coord.s0 = (x + 0.75f) / (get_global_size(0) - 0.5f);
  coord.s1 = (y + 0.5f) / (get_global_size(1) - 0.5f);
  uchar4 mask_data8 = convert_uchar4(read_imageui(mask, smp, coord));

  // Convert rgb to yuv
  float4 luma1;
  float4 luma2;
  float4 chroma;

  luma1.s0 =
      0.257f * mask_data1.s0 +
      0.504f * mask_data1.s1 +
      0.098f * mask_data1.s2 + 16;
  luma1.s1 =
      0.257f * mask_data2.s0 +
      0.504f * mask_data2.s1 +
      0.098f * mask_data2.s2 + 16;
  luma1.s2 =
      0.257f * mask_data3.s0 +
      0.504f * mask_data3.s1 +
      0.098f * mask_data3.s2 + 16;
  luma1.s3 =
      0.257f * mask_data4.s0 +
      0.504f * mask_data4.s1 +
      0.098f * mask_data4.s2 + 16;

  luma2.s0 =
      0.257f * mask_data5.s0 +
      0.504f * mask_data5.s1 +
      0.098f * mask_data5.s2 + 16;
  luma2.s1 =
      0.257f * mask_data6.s0 +
      0.504f * mask_data6.s1 +
      0.098f * mask_data6.s2 + 16;
  luma2.s2 =
      0.257f * mask_data7.s0 +
      0.504f * mask_data7.s1 +
      0.098f * mask_data7.s2 + 16;
  luma2.s3 =
      0.257f * mask_data8.s0 +
      0.504f * mask_data8.s1 +
      0.098f * mask_data8.s2 + 16;

  chroma.s0 =
     -0.148f * mask_data1.s0 -
      0.291f * mask_data1.s1 +
      0.439f * mask_data1.s2;
  chroma.s1 =
      0.439f * mask_data2.s0 -
      0.368f * mask_data2.s1 -
      0.071f * mask_data2.s2;
  chroma.s2 =
     -0.148f * mask_data3.s0 -
      0.291f * mask_data3.s1 +
      0.439f * mask_data3.s2;
  chroma.s3 =
      0.439f * mask_data4.s0 -
      0.368f * mask_data4.s1 -
      0.071f * mask_data4.s2;
  chroma += 128;

  if (swap_uv) {
    chroma.s0123 = chroma.s1032;
  }

  luma1 = clamp(luma1, 0.0f, 255.0f);
  luma2 = clamp(luma2, 0.0f, 255.0f);
  chroma = clamp(chroma, 0.0f, 255.0f);

  // Apply alpha blending
  float4 alpha1;
  float4 alpha2;

  alpha1.s0 = mask_data1.s3 / 255.0f;
  alpha1.s1 = mask_data2.s3 / 255.0f;
  alpha1.s2 = mask_data3.s3 / 255.0f;
  alpha1.s3 = mask_data4.s3 / 255.0f;

  alpha2.s0 = mask_data5.s3 / 255.0f;
  alpha2.s1 = mask_data6.s3 / 255.0f;
  alpha2.s2 = mask_data7.s3 / 255.0f;
  alpha2.s3 = mask_data8.s3 / 255.0f;

  // Read input yuv data
  uint offset1 = 2 * stride * y + x * 4;
  uint offset2 = stride * y + x * 4;

  uchar4 y_out1 = *(__global uchar4 *)(frame + y_offset + offset1);
  uchar4 y_out2 = *(__global uchar4 *)(frame + y_offset + offset1 + stride);
  uchar4 uv_out = *(__global uchar4 *)(frame + nv_offset + offset2);

  // Store output yuv data
  *(__global uchar4 *)(frame + y_offset + offset1) = convert_uchar4(alpha1 * luma1 + (1.0f - alpha1) * convert_float4(y_out1));
  *(__global uchar4 *)(frame + y_offset + offset1 + stride) = convert_uchar4(alpha2 * luma2 + (1.0f - alpha2) * convert_float4(y_out2));
  *(__global uchar4 *)(frame + nv_offset + offset2) = convert_uchar4(alpha1 * chroma + (1.0f - alpha1) * convert_float4(uv_out));
}