/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <glib.h>
#include <gst/gst.h>
#include <string>
#include <algorithm>
#include "GenieVLM.h"

std::string
to_lower_copy (std::string value)
{
  std::transform (
      value.begin (), value.end (), value.begin (),
      [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
  return value;
}

bool
logger_level_from_string (const std::string & value, GenieLog_Level_t & level)
{
  const std::string normalized = to_lower_copy (value);
  if (normalized.empty () || normalized == "none")
    return false;

  if (normalized == "error") {
    level = GENIE_LOG_LEVEL_ERROR;
    return true;
  }
  if (normalized == "warn" || normalized == "warning") {
    level = GENIE_LOG_LEVEL_WARN;
    return true;
  }
  if (normalized == "info") {
    level = GENIE_LOG_LEVEL_INFO;
    return true;
  }
  level = GENIE_LOG_LEVEL_VERBOSE;
  return true;
}

gboolean
read_raw_buffer (guint8 ** out_buf, gsize * out_size)
{
  g_return_val_if_fail (out_buf != NULL, FALSE);
  g_return_val_if_fail (out_size != NULL, FALSE);

  const gchar * path = "/etc/media/in_buff.bin";

  gchar * contents = NULL;
  gsize length = 0;
  GError * err = NULL;

  if (!g_file_get_contents (path, &contents, &length, &err)) {
    if (err)
      g_error_free (err);
    return FALSE;
  }

  *out_buf = (guint8 *)contents;
  *out_size = length;
  return TRUE;
}

void
submit_to_vlm (GenieVLM * pool)
{
  guint8 * buf = NULL;
  gsize size = 0;
  read_raw_buffer (&buf, &size);

  printf ("pool->submit\n");
  pool->submit (
    buf,
    "You are a vision description assistant. Always answer with one short "
    "sentence under 100 characters using only letters and spaces with "
    "no punctuation or symbols.",
    "Describe only the people in this image",
    1.5, // temperature
    0.5, // top_p
    0.0, // seed
    0.0, // presence_penalty
    0.0, // frequency_penalty
    NULL // user_data
  );

  g_free (buf);
}

gint
main (gint argc, gchar * argv[])
{
  std::string logger_choice = "none";
  bool profile_enabled = false;
  for (int i = 1; i < argc; ++i) {
    if (!g_strcmp0 (argv[i], "--logger") || !g_strcmp0 (argv[i], "-L")) {
      if (i + 1 < argc) {
        logger_choice = argv[++i];
      } else {
        g_printerr ("Missing argument after %s\n", argv[i]);
        return -1;
      }
    } else if (!g_strcmp0 (argv[i], "--profile")) {
      profile_enabled = true;
    } else if (!g_strcmp0 (argv[i], "--no-profile")) {
      profile_enabled = false;
    }
  }

  GenieVLMConfig cfg;
  cfg.instances = {
    {
      .engine = VlmEngine::NPU0,
      .pipeline_config_path = "",
      .nodes = {
        { .type="ImageEncoder",
          .name="image_encoder",
          .config_path="qwen_veg_NPU_0.json"
        },
        { .type="LutEncoder",
          .name="lut_encoder",
          .config_path="text-encoder.json"
        },
        { .type="TextGenerator",
          .name="text_generator",
          .config_path="qwen-htp_NPU_0.json"
        },
      }
    },
    {
      .engine = VlmEngine::NPU1,
      .pipeline_config_path = "",
      .nodes = {
        { .type="ImageEncoder",
          .name="image_encoder",
          .config_path="qwen_veg_NPU_1.json"
        },
        { .type="LutEncoder",
          .name="lut_encoder",
          .config_path="text-encoder.json"
        },
        { .type="TextGenerator",
          .name="text_generator",
          .config_path="qwen-htp_NPU_1.json"
        },
      }
    }
  };
  cfg.lb_mode = GenieVLMConfig::LBMode::RoundRobin;
  cfg.max_queue_per_instance = 32;
  GenieLog_Level_t parsed_level;
  if (logger_level_from_string (logger_choice, parsed_level)) {
    cfg.logging_enabled = true;
    cfg.log_level = parsed_level;
    g_print ("Standalone VLM logging enabled (%s)\n",
      to_lower_copy (logger_choice).c_str ());
  } else {
    cfg.logging_enabled = false;
    g_print ("Standalone VLM logging disabled\n");
  }
  cfg.profiling_enabled = profile_enabled;
  g_print ("Standalone VLM profiling %s\n",
    profile_enabled ? "enabled" : "disabled");

  GenieVLM pool (cfg, [] (uint64_t id, bool ok, const VlmResult & r,
      const std::string & err) {
    (void) id;
    (void) ok;
    (void) err;
    // callback runs on separate dispatcher thread
    printf ("Result - %s\n", r.text.c_str ());

    GstClockTime time = GST_CLOCK_TIME_NONE;
    time = gst_util_get_timestamp ();
    g_print ("End time - %ld\n", (uint64_t)time);
  });

  GstClockTime time = GST_CLOCK_TIME_NONE;
  time = gst_util_get_timestamp ();
  g_print ("Start time - %ld\n", (uint64_t)time);

  while (1) {
    submit_to_vlm (&pool);
    usleep (500000);
  }

  return 0;
}
