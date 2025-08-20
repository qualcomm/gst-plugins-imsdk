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
  kPlanarRGB,

  kYUV,
  kLuma,
  kChroma,

  kCompute8,
  kCompute16,
  kCompute16F,
  kCompute32F,

  kComputePlanar8,
  kComputePlanar16,
  kComputePlanar16F,
  kComputePlanar32F,
};

static const std::string kVertexShader = R"(
#version 310 es

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

static const std::string kRgbFragmentHeader = R"(
#version 310 es
#extension GL_OES_EGL_image_external_essl3 : require

precision mediump samplerExternalOES;
precision mediump float;

uniform samplerExternalOES extTex;

uniform vec4 rgbaOffset;
uniform vec4 rgbaScale;

uniform bool rgbaInverted;
uniform bool rbSwapped;
uniform float globalAlpha;

in vec2 texCoord;

)";

static const std::string kRgbFragmentInterleavedOutput = R"(
layout(location = 0) out vec4 outColor;

void assignColor(in vec4 source)
{
  outColor = source;
}

)";

static const std::string kRgbFragmentPlanarOutput = R"(
layout(location = 0) out vec4 outColor0;
layout(location = 1) out vec4 outColor1;
layout(location = 2) out vec4 outColor2;

void assignColor(in vec4 source)
{
    outColor0 = vec4(source.r, 0.0, 0.0, source.a);
    outColor1 = vec4(source.g, 0.0, 0.0, source.a);
    outColor2 = vec4(source.b, 0.0, 0.0, source.a);
}

)";

static const std::string kRgbFragmentMain = R"(
void main() {
    vec4 source = texture(extTex, texCoord);

    source.a = source.a * globalAlpha;
    source = (source - rgbaOffset) * rgbaScale;

    if (rgbaInverted && rbSwapped) {
        source.abgr = source.rgba;
    } else if (rgbaInverted && !rbSwapped) {
        source.abgr = source.bgra;
    } else if (!rgbaInverted && rbSwapped) {
        source = source.bgra;
    }

    assignColor(source);
}
)";

static const std::string kYuvFragmentShader = R"(
#version 310 es
#extension GL_OES_EGL_image_external_essl3 : require
#extension GL_EXT_YUV_target : require

precision mediump samplerExternalOES;
precision mediump float;

uniform sampler2D stageTex;
uniform samplerExternalOES extTex;

uniform int colorSpace;
uniform bool stageInput;

in vec2 texCoord;

layout(yuv) out vec4 outColor;

void main() {
    vec4 source = stageInput ? texture(stageTex, texCoord) : texture(extTex, texCoord);
    source = clamp(source, 0.0, 1.0);

    yuvCscStandardEXT csStandard = itu_601;

    switch (colorSpace)
    {
        case (1 << 9):
            csStandard = itu_601;
            break;
        case (2 << 9):
            csStandard = itu_601_full_range;
            break;
        case (3 << 9):
            csStandard = itu_709;
            break;
    }

    outColor = vec4(rgb_2_yuv(source.rgb, csStandard), 1.0);
}
)";

static const std::string kLumaFragmentShader = R"(
#version 310 es
#extension GL_OES_EGL_image_external_essl3 : require

precision mediump samplerExternalOES;
precision mediump float;

uniform samplerExternalOES extTex;

uniform int colorSpace;
uniform float globalAlpha;

in vec2 texCoord;

layout(location = 0) out vec4 lumaColor;

void main() {
    vec4 source = texture(extTex, texCoord);
    source = clamp(source, 0.0, 1.0);

    float luminosity = 0.0;

    switch (colorSpace)
    {
        case (1 << 9):
            luminosity = 0.299 * source.r + 0.587 * source.g + 0.114 * source.b;
            break;
        case (2 << 9):
            luminosity = 0.299 * source.r + 0.587 * source.g + 0.114 * source.b;
            break;
        case (3 << 9):
            luminosity = 0.2126 * source.r + 0.7152 * source.g + 0.0722 * source.b;
            break;
    }

    float alpha = source.a * globalAlpha;
    lumaColor = vec4(luminosity, 0.0, 0.0, alpha);
}
)";

static const std::string kChromaFragmentShader = R"(
#version 310 es
#extension GL_OES_EGL_image_external_essl3 : require

precision mediump samplerExternalOES;
precision mediump float;

uniform samplerExternalOES extTex;

uniform bool rbSwapped;
uniform int colorSpace;
uniform float globalAlpha;

in vec2 texCoord;

layout(location = 0) out vec4 chromaColor;

void main() {
    vec4 source = texture(extTex, texCoord);
    source = clamp(source, 0.0, 1.0);

    float cr = 0.0;
    float cb = 0.0;

    switch (colorSpace)
    {
        case (1 << 9):
            cr = -0.147 * source.r - 0.289 * source.g + 0.436 * source.b + 0.5;
            cb = 0.615 * source.r - 0.515 * source.g - 0.100 * source.b + 0.5;
            break;
        case (2 << 9):
            cr = -0.169 * source.r - 0.331 * source.g + 0.500 * source.b + 0.5;
            cb = 0.500 * source.r - 0.419 * source.g - 0.081 * source.b + 0.5;
            break;
        case (3 << 9):
            cr = -0.1146 * source.r - 0.3854 * source.g + 0.5000 * source.b + 0.5;
            cb = 0.5000 * source.r - 0.4542 * source.g - 0.0458 * source.b + 0.5;
            break;
    }

    float alpha = source.a * globalAlpha;
    chromaColor = rbSwapped ? vec4(cb, cr, 0.0, alpha) : vec4(cr, cb, 0.0, alpha);
}
)";

static const std::string kComputeHeader = R"(
#version 310 es
#extension GL_NV_image_formats : warn

uniform int targetWidth;
uniform int targetHeight;
uniform int imageWidth;
uniform int numPixels;
uniform int numChannels;

uniform sampler2D inTex;

layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

)";

static const std::string kComputeOutputRGBA8 = R"(
layout (binding = 1, rgba8) writeonly mediump uniform image2D outTex;

)";

static const std::string kComputeOutputRGBA16 = R"(
layout (binding = 1, rgba16) writeonly mediump uniform image2D outTex;

)";

static const std::string kComputeOutputRGBA16F = R"(
layout (binding = 1, rgba16f) writeonly mediump uniform image2D outTex;

)";

static const std::string kComputeOutputRGBA32F = R"(
layout (binding = 1, rgba32f) writeonly mediump uniform image2D outTex;

)";

static const std::string kComputeMain = R"(
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
            // 3 / 4 because we process 4 pixels from the stage RGBA input texture which are
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

static const std::string kComputeMainPlanar = R"(
void main() {
    int pixelId = int(gl_GlobalInvocationID.y * gl_NumWorkGroups.x * gl_WorkGroupSize.x);
    pixelId = 4 * (int(gl_GlobalInvocationID.x) + pixelId);

    if ((pixelId + 3) < numPixels) {
        int plane0PixelId = pixelId / 4;
        int plane1PixelId = plane0PixelId + targetWidth * targetHeight / 4;
        int plane2PixelId = plane1PixelId + targetWidth * targetHeight / 4;

        ivec2 pos0 = ivec2(pixelId % targetWidth, pixelId / targetWidth);
        ivec2 pos1 = ivec2((pixelId + 1) % targetWidth, (pixelId + 1) / targetWidth);
        ivec2 pos2 = ivec2((pixelId + 2) % targetWidth, (pixelId + 2) / targetWidth);
        ivec2 pos3 = ivec2((pixelId + 3) % targetWidth, (pixelId + 3) / targetWidth);

        vec4 p0 = texelFetch(inTex, pos0, 0);
        vec4 p1 = texelFetch(inTex, pos1, 0);
        vec4 p2 = texelFetch(inTex, pos2, 0);
        vec4 p3 = texelFetch(inTex, pos3, 0);

        vec4 outp0 = vec4(p0.x, p1.x, p2.x, p3.x);
        vec4 outp1 = vec4(p0.y, p1.y, p2.y, p3.y);
        vec4 outp2 = vec4(p0.z, p1.z, p2.z, p3.z);

        ivec2 outPos0 = ivec2(plane0PixelId % imageWidth, plane0PixelId / imageWidth);
        ivec2 outPos1 = ivec2(plane1PixelId % imageWidth, plane1PixelId / imageWidth);
        ivec2 outPos2 = ivec2(plane2PixelId % imageWidth, plane2PixelId / imageWidth);

        imageStore(outTex, outPos0, outp0);
        imageStore(outTex, outPos1, outp1);
        imageStore(outTex, outPos2, outp2);

        if (numChannels == 4) {
            vec4 outp3 = vec4(p0.w, p1.w, p2.w, p3.w);

            int plane3PixelId = plane2PixelId + targetWidth * targetHeight / 4;
            ivec2 outPos3 = ivec2(plane3PixelId % imageWidth, plane3PixelId / imageWidth);

            imageStore(outTex, outPos3, outp3);
        }
    }
}
)";

} // namespace ib2c
