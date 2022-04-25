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

// Color of the privacy mask
float8 luma = (float8) 0x0;
float8 chroma = (float8) 0x80;

kernel void overlay_cl_mask(__global uchar *mask, // 1
                       __global uchar *frame,     // 2
                       uint y_offset,             // 3
                       uint nv_offset,            // 4
                       ushort stride,             // 5
                       ushort swap_uv,            // 6
                       ushort mask_stride         // 7
) {
  uint x = get_global_id(0);
  uint y = get_global_id(1);

  // Get first row pixels
  uint rgb_coords = (mask_stride * (y * 2) + (x * 8));
  uchar8 mask_data_1 = *(__global uchar8 *)(mask + rgb_coords);

  // Get second row pixels
  rgb_coords = (mask_stride * (y * 2 + 1) + (x * 8));
  uchar8 mask_data_2 = *(__global uchar8 *)(mask + rgb_coords);

  // Check if has alpha value
  if (mask_data_1.s0 > 0 || mask_data_1.s1 > 0 ||
    mask_data_1.s2 > 0 || mask_data_1.s3 > 0 ||
    mask_data_1.s4 > 0 || mask_data_1.s5 > 0 ||
    mask_data_1.s6 > 0 || mask_data_1.s7 > 0 ||
    mask_data_2.s0 > 0 || mask_data_2.s1 > 0 ||
    mask_data_2.s2 > 0 || mask_data_2.s3 > 0 ||
    mask_data_2.s4 > 0 || mask_data_2.s5 > 0 ||
    mask_data_2.s6 > 0 || mask_data_2.s7 > 0)
  {
    // Read input yuv data
    uint offset1 = 2 * stride * y + x * 8;
    uint offset2 = stride * y + x * 8;

    // Read input yuv data
    uchar8 y_out1 = *(__global uchar8 *)(frame + y_offset + offset1);
    uchar8 y_out2 = *(__global uchar8 *)(frame + y_offset + offset1 + stride);
    uchar8 uv_out = *(__global uchar8 *)(frame + nv_offset + offset2);

    float8 alpha1 = convert_float8(mask_data_1) / 255.0f;
    float8 alpha2 = convert_float8(mask_data_2) / 255.0f;

    // Store output yuv data
    *(__global uchar8 *)(frame + y_offset + offset1) = convert_uchar8(alpha1 * luma + (1.0f - alpha1) * convert_float8(y_out1));
    *(__global uchar8 *)(frame + y_offset + offset1 + stride) = convert_uchar8(alpha2 * luma + (1.0f - alpha2) * convert_float8(y_out2));
    *(__global uchar8 *)(frame + nv_offset + offset2) = convert_uchar8(alpha1 * chroma + (1.0f - alpha1) * convert_float8(uv_out));
  }
}