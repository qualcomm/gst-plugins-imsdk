/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <string>

namespace ib2c {

enum class ShaderType : uint32_t {
  kNone,
  kRGB,
  kYUV,
  kUnaligned8,
  kUnaligned16F,
  kUnaligned32F,
};

static const std::string kVertexShaderCode = R"(
#version 320 es

precision mediump float;

uniform float rotationAngle;

in vec2 vPosition;
in vec2 inTexCoord;

out vec2 texCoord;

void main() {
    mat4 rotationMatrix = mat4(
       cos(rotationAngle), sin(rotationAngle), 0.0, 0.0,
      -sin(rotationAngle), cos(rotationAngle), 0.0, 0.0,
                      0.0,                0.0, 1.0, 0.0,
                      0.0,                0.0, 0.0, 1.0
    );

    gl_Position = rotationMatrix * vec4(vPosition.x, vPosition.y, 0.0, 1.0);
    texCoord = vec2(inTexCoord.x, inTexCoord.y);
}
)";

static const std::string kRgbFragmentShaderCode = R"(
#version 320 es
#extension GL_OES_EGL_image_external_essl3 : require

precision mediump float;

uniform samplerExternalOES extTex;

uniform vec4 rgbaOffset;
uniform vec4 rgbaScale;

uniform bool rgbaInverted;
uniform bool rbSwapped;
uniform float globalAlpha;

in vec2 texCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 source = texture(extTex, texCoord);
    source = clamp(source, 0.0, 1.0);

    source.a = source.a * globalAlpha;
    source = (source - rgbaOffset) * rgbaScale;

    if (rgbaInverted && rbSwapped) {
        source.abgr = source.rgba;
    } else if (rgbaInverted && !rbSwapped) {
        source.abgr = source.bgra;
    } else if (!rgbaInverted && rbSwapped) {
        source = source.bgra;
    }

    outColor = source;
}
)";

static const std::string kYuvFragmentShaderCode = R"(
#version 320 es
#extension GL_OES_EGL_image_external_essl3 : require
#extension GL_EXT_YUV_target : require

precision highp sampler2D;
precision highp samplerExternalOES;
precision highp float;
precision highp int;

uniform sampler2D stageTex;
uniform samplerExternalOES extTex;

uniform int colorSpace;
uniform bool stageInput;

in vec2 texCoord;
layout(yuv) out vec4 outColor;

void main() {
    vec4 source = stageInput ? texture(stageTex, texCoord) : texture(extTex, texCoord);
    source = clamp(source, 0.0, 1.0);

    yuvCscStandardEXT csStandard;

    switch (colorSpace)
    {
        case (1 << 11):
            csStandard = itu_601;
            break;
        case (2 << 11):
            csStandard = itu_601_full_range;
            break;
        case (3 << 11):
            csStandard = itu_709;
            break;
        default:
            csStandard = itu_709;
            break;
    }

    outColor = vec4(rgb_2_yuv(source.rgb, csStandard), 1.0);
}
)";


static const std::string kComputeHeader = R"(
#version 320 es

uniform int targetWidth;
uniform int imageWidth;
uniform int numPixels;
uniform int numChannels;

uniform sampler2D inTex;

layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

)";

static const std::string kComputeOutputRgba8 = R"(
layout (binding = 1, rgba8) writeonly mediump uniform image2D outTex;

)";

static const std::string kComputeOutputRgba16F = R"(
layout (binding = 1, rgba16f) writeonly mediump uniform image2D outTex;

)";

static const std::string kComputeOutputRgba32F = R"(
layout (binding = 1, rgba32f) writeonly mediump uniform image2D outTex;

)";

static const std::string kComputeMainUnaligned = R"(
void main() {
    int pixelId = int(gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x);
    pixelId = 4 * (int(gl_GlobalInvocationID.x) + pixelId);

    if ((pixelId + 3) < numPixels) {
        ivec2 pos0 = ivec2(pixelId % targetWidth, pixelId / targetWidth);
        ivec2 pos1 = ivec2((pixelId + 1) % targetWidth, (pixelId + 1) / targetWidth);
        ivec2 pos2 = ivec2((pixelId + 2) % targetWidth, (pixelId + 2) / targetWidth);
        ivec2 pos3 = ivec2((pixelId + 3) % targetWidth, (pixelId + 3) / targetWidth);

        vec4 p0 = texelFetch(inTex, pos0, 0);
        vec4 p1 = texelFetch(inTex, pos1, 0);
        vec4 p2 = texelFetch(inTex, pos2, 0);
        vec4 p3 = texelFetch(inTex, pos3, 0);

        if (numChannels == 4) {
            vec4 out0 = vec4(p0.x, p0.y, p0.z, p0.w);
            vec4 out1 = vec4(p1.x, p1.y, p1.z, p1.w);
            vec4 out2 = vec4(p2.x, p2.y, p2.z, p2.w);
            vec4 out3 = vec4(p3.x, p3.y, p3.z, p3.w);

            ivec2 outPos0 = ivec2(pixelId % imageWidth, pixelId / imageWidth);
            ivec2 outPos1 = ivec2((pixelId + 1) % imageWidth, (pixelId + 1) / imageWidth);
            ivec2 outPos2 = ivec2((pixelId + 2) % imageWidth, (pixelId + 2) / imageWidth);
            ivec2 outPos3 = ivec2((pixelId + 3) % imageWidth, (pixelId + 3) / imageWidth);

            imageStore(outTex, outPos0, out0);
            imageStore(outTex, outPos1, out1);
            imageStore(outTex, outPos2, out2);
            imageStore(outTex, outPos3, out3);
        } else {
            // Recalculate the pixelId for the output 3 channeled (RGB) texture.
            // 3 / 4 because we process 4 pixels form the stage RGBA input texture which are
            // then compressed in 3 pixels of the output RGBA texture which is actually RGB.
            pixelId = (pixelId * 3) / 4;

             vec4 out0 = vec4(p0.x, p0.y, p0.z, p1.x);
             vec4 out1 = vec4(p1.y, p1.z, p2.x, p2.y);
             vec4 out2 = vec4(p2.z, p3.x, p3.y, p3.z);

            ivec2 outPos0 = ivec2(pixelId % imageWidth, pixelId / imageWidth);
            ivec2 outPos1 = ivec2((pixelId + 1) % imageWidth, (pixelId + 1) / imageWidth);
            ivec2 outPos2 = ivec2((pixelId + 2) % imageWidth, (pixelId + 2) / imageWidth);

            imageStore(outTex, outPos0, out0);
            imageStore(outTex, outPos1, out1);
            imageStore(outTex, outPos2, out2);
        }
    }
}
)";

} // namespace ib2c
