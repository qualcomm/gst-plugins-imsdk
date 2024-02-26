/*
* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "c2-engine-params.h"

#include <string>
#include <unordered_map>
#include <algorithm>

static const std::unordered_map<std::string, uint32_t> kH264Profiles = {
  { "baseline", GST_C2_PROFILE_AVC_BASELINE },
  { "constrained-baseline", GST_C2_PROFILE_AVC_CONSTRAINED_BASELINE },
  { "main", GST_C2_PROFILE_AVC_MAIN },
  { "high", GST_C2_PROFILE_AVC_HIGH },
  { "constrained-high", GST_C2_PROFILE_AVC_CONSTRAINED_HIGH },
};

static const std::unordered_map<std::string, uint32_t> kH265Profiles = {
  { "main", GST_C2_PROFILE_HEVC_MAIN },
  { "main-10", GST_C2_PROFILE_HEVC_MAIN10 },
  { "main-still-picture", GST_C2_PROFILE_HEVC_MAIN_STILL },
};

static const std::unordered_map<std::string, uint32_t> kAACProfiles = {
  { "lc", GST_C2_PROFILE_AAC_LC },
  { "main", GST_C2_PROFILE_AAC_MAIN },
};

static const std::unordered_map<std::string, uint32_t> kH264Levels = {
  { "1", GST_C2_LEVEL_AVC_1 },
  { "1b", GST_C2_LEVEL_AVC_1B },
  { "1.1", GST_C2_LEVEL_AVC_1_1 },
  { "1.2", GST_C2_LEVEL_AVC_1_2 },
  { "1.3", GST_C2_LEVEL_AVC_1_3 },
  { "2", GST_C2_LEVEL_AVC_2 },
  { "2.1", GST_C2_LEVEL_AVC_2_1 },
  { "2.2", GST_C2_LEVEL_AVC_2_2 },
  { "3", GST_C2_LEVEL_AVC_3 },
  { "3.1", GST_C2_LEVEL_AVC_3_1 },
  { "3.2", GST_C2_LEVEL_AVC_3_2 },
  { "4", GST_C2_LEVEL_AVC_4 },
  { "4.1", GST_C2_LEVEL_AVC_4_1 },
  { "4.2", GST_C2_LEVEL_AVC_4_2 },
  { "5", GST_C2_LEVEL_AVC_5 },
  { "5.1", GST_C2_LEVEL_AVC_5_1 },
  { "5.2", GST_C2_LEVEL_AVC_5_2 },
  { "6", GST_C2_LEVEL_AVC_6 },
  { "6.1", GST_C2_LEVEL_AVC_6_1 },
  { "6.2", GST_C2_LEVEL_AVC_6_2 },
};

static const std::unordered_map<std::string, uint32_t> kH265MainLevels = {
  { "1", GST_C2_LEVEL_HEVC_MAIN_1 },
  { "2", GST_C2_LEVEL_HEVC_MAIN_2 },
  { "2.1", GST_C2_LEVEL_HEVC_MAIN_2_1 },
  { "3", GST_C2_LEVEL_HEVC_MAIN_3 },
  { "3.1", GST_C2_LEVEL_HEVC_MAIN_3_1 },
  { "4", GST_C2_LEVEL_HEVC_MAIN_4 },
  { "4.1", GST_C2_LEVEL_HEVC_MAIN_4_1 },
  { "5", GST_C2_LEVEL_HEVC_MAIN_5 },
  { "5.1", GST_C2_LEVEL_HEVC_MAIN_5_1 },
  { "5.2", GST_C2_LEVEL_HEVC_MAIN_5_2 },
  { "6", GST_C2_LEVEL_HEVC_MAIN_6 },
  { "6.1", GST_C2_LEVEL_HEVC_MAIN_6_1 },
  { "6.2", GST_C2_LEVEL_HEVC_MAIN_6_2 },
};

static const std::unordered_map<std::string, uint32_t> kH265HighLevels = {
  { "4", GST_C2_LEVEL_HEVC_HIGH_4 },
  { "4.1", GST_C2_LEVEL_HEVC_HIGH_4_1 },
  { "5", GST_C2_LEVEL_HEVC_HIGH_5 },
  { "5.1", GST_C2_LEVEL_HEVC_HIGH_5_1 },
  { "5.2", GST_C2_LEVEL_HEVC_HIGH_5_2 },
  { "6", GST_C2_LEVEL_HEVC_HIGH_6 },
  { "6.1", GST_C2_LEVEL_HEVC_HIGH_6_1 },
  { "6.2", GST_C2_LEVEL_HEVC_HIGH_6_2 },
};

static const std::unordered_map<std::string, uint32_t> kAACLevels = {
  { "1", GST_C2_LEVEL_UNUSED },
  { "2", GST_C2_LEVEL_UNUSED },
};

guint
gst_c2_utils_h264_profile_from_string (const gchar * profile)
{
  if (kH264Profiles.count(profile) != 0)
    return kH264Profiles.at(profile);

  return GST_C2_PROFILE_INVALID;
}

guint
gst_c2_utils_h265_profile_from_string (const gchar * profile)
{
  if (kH265Profiles.count(profile) != 0)
    return kH265Profiles.at(profile);

  return GST_C2_PROFILE_INVALID;
}

guint
gst_c2_utils_aac_profile_from_string (const gchar * profile)
{
  if (kAACProfiles.count(profile) != 0)
    return kAACProfiles.at(profile);

  return GST_C2_PROFILE_INVALID;
}

const gchar *
gst_c2_utils_h264_profile_to_string (guint profile)
{
  auto it = std::find_if(kH264Profiles.begin(), kH264Profiles.end(),
      [&](const auto& m) { return m.second == profile; });

  return (it != kH264Profiles.end()) ? it->first.c_str() : NULL;
}

const gchar *
gst_c2_utils_h265_profile_to_string (guint profile)
{
  auto it = std::find_if(kH265Profiles.begin(), kH265Profiles.end(),
      [&](const auto& m) { return m.second == profile; });

  return (it != kH265Profiles.end()) ? it->first.c_str() : NULL;
}

const gchar *
gst_c2_utils_aac_profile_to_string (guint profile)
{
  auto it = std::find_if(kAACProfiles.begin(), kAACProfiles.end(),
      [&](const auto& m) { return m.second == profile; });

  return (it != kAACProfiles.end()) ? it->first.c_str() : NULL;
}

guint
gst_c2_utils_h264_level_from_string (const gchar * level)
{
  if (kH264Levels.count(level) != 0)
    return kH264Levels.at(level);

  return GST_C2_LEVEL_INVALID;
}

guint
gst_c2_utils_h265_level_from_string (const gchar * level, const gchar * tier)
{
  // If tier is null, returns main level.
  if ((tier == NULL || g_str_equal (tier, "main")) &&
      (kH265MainLevels.count(level) != 0))
    return kH265MainLevels.at(level);
  else if (g_str_equal (tier, "high") && (kH265HighLevels.count(level) != 0))
    return kH265HighLevels.at(level);

  return GST_C2_LEVEL_INVALID;
}

guint
gst_c2_utils_aac_level_from_string (const gchar * level)
{
  if (kAACLevels.count(level) != 0)
    return kAACLevels.at(level);

  return GST_C2_LEVEL_INVALID;
}

const gchar *
gst_c2_utils_h264_level_to_string (guint level)
{
  auto it = std::find_if(kH264Levels.begin(), kH264Levels.end(),
      [&](const auto& m) { return m.second == level; });

  return (it != kH264Levels.end()) ? it->first.c_str() : NULL;
}

const gchar *
gst_c2_utils_h265_level_to_string (guint level)
{
  auto it = std::find_if(kH265MainLevels.begin(), kH265MainLevels.end(),
      [&](const auto& m) { return m.second == level; });

  if (it != kH265MainLevels.end())
    return it->first.c_str();

  auto iter = std::find_if(kH265HighLevels.begin(), kH265HighLevels.end(),
      [&](const auto& m) { return m.second == level; });

  if (iter != kH265HighLevels.end())
    return iter->first.c_str();

  return NULL;
}

const gchar *
gst_c2_utils_aac_level_to_string (guint level)
{
  auto it = std::find_if(kAACLevels.begin(), kAACLevels.end(),
      [&](const auto& m) { return m.second == level; });

  return (it != kAACLevels.end()) ? it->first.c_str() : NULL;
}
