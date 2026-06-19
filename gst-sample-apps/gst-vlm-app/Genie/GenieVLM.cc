/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "GenieVLM.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <dlfcn.h>

#define CHANNELS 3
#define PATCH_SIZE 14
#define MERGE_SIZE 2
#define TEMPORAL_GROUPING 2
#define MLVCONV_WIDTH 504
#define MLVCONV_HEIGHT 336

#define LOAD_SYMBOL(name)                                  \
  ctx.api.name = reinterpret_cast<decltype(ctx.api.name)>( \
      dlsym(ctx.genie_handle, #name));                     \
  if (!ctx.api.name) {                                     \
    throw std::runtime_error("Missing symbol: " #name);    \
  }

const char* to_string(VlmEngine e) {
  switch (e) {
    case VlmEngine::NPU0: return "NPU0";
    case VlmEngine::NPU1: return "NPU1";
    case VlmEngine::GPU:  return "GPU";
    default:              return "UNKNOWN";
  }
}

// ---------------------------
// Helpers
// ---------------------------
static std::string read_file_to_string_or_throw(const std::string& path) {

  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in) {
    throw std::runtime_error("Failed to open config file: " + path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// ---------------------------
// Genie SDK placeholders
// Replace with your actual Genie SDK headers/types/calls.
// ---------------------------
namespace genie {

struct Pipeline {
    GeniePipelineConfig_Handle_t config;
    GeniePipeline_Handle_t       handle;
};

struct Node {
    std::string              name;
    GenieNodeConfig_Handle_t config;
    GenieNode_Handle_t       handle;
    GenieSampler_Handle_t    sampler_handle;
};

// GenieLibrary-loaded functions
struct GenieApi {
  Genie_Status_t (*GeniePipeline_connect)(
    GeniePipeline_Handle_t,
    GenieNode_Handle_t,
    GenieNode_IOName_t,
    GenieNode_Handle_t,
    GenieNode_IOName_t
  );
  Genie_Status_t (*GenieLog_create)(
    GenieLogConfig_Handle_t,
    GenieLog_Callback_t,
    GenieLog_Level_t,
    GenieLog_Handle_t *
  );
  Genie_Status_t (*GenieLog_free)(
    GenieLog_Handle_t
  );
  Genie_Status_t (*GenieNode_setTextCallback)(
    GenieNode_Handle_t,
    GenieNode_IOName_t,
    GenieNode_TextOutput_Callback_t
  );
  Genie_Status_t (*GenieNode_free)(
    GenieNode_Handle_t
  );
  Genie_Status_t (*GenieNodeConfig_free)(
    GenieNodeConfig_Handle_t
  );
  Genie_Status_t (*GeniePipeline_free)(
    GeniePipeline_Handle_t
  );
  Genie_Status_t (*GeniePipelineConfig_free)(
    GeniePipelineConfig_Handle_t
  );
  Genie_Status_t (*GeniePipelineConfig_createFromJson)(
    const char *,
    GeniePipelineConfig_Handle_t *
  );
  Genie_Status_t (*GenieProfile_free)(
    GenieProfile_Handle_t
  );
  Genie_Status_t (*GenieNode_setData)(
    GenieNode_Handle_t,
    GenieNode_IOName_t,
    const void *,
    size_t,
    const char *
  );
  Genie_Status_t (*GenieNode_getSampler)(
    GenieNode_Handle_t,
    GenieSampler_Handle_t *
  );
  Genie_Status_t (*GenieSamplerConfig_createFromJson)(
    const char *,
    GenieSamplerConfig_Handle_t *
  );
  Genie_Status_t (*GenieSamplerConfig_setParam)(
    GenieSamplerConfig_Handle_t,
    const char *,
    const char *
  );
  Genie_Status_t (*GenieSampler_applyConfig)(
    GenieSampler_Handle_t,
    GenieSamplerConfig_Handle_t
  );
  Genie_Status_t (*GenieSamplerConfig_free)(
    GenieSamplerConfig_Handle_t
  );

  Genie_Status_t (*GeniePipeline_execute)(
    GeniePipeline_Handle_t,
    void *
  );
  Genie_Status_t (*GeniePipeline_reset)(
    GeniePipeline_Handle_t
  );
  Genie_Status_t (*GenieProfile_getJsonData)(
    GenieProfile_Handle_t,
    Genie_AllocCallback_t,
    const char **
  );
  Genie_Status_t (*GeniePipelineConfig_bindLogger)(
    GeniePipelineConfig_Handle_t,
    GenieLog_Handle_t
  );
  Genie_Status_t (*GenieProfileConfig_createFromJson)(
    const char *,
    GenieProfileConfig_Handle_t *
  );
  Genie_Status_t (*GenieProfileConfig_free)(
    GenieProfileConfig_Handle_t
  );
  Genie_Status_t (*GeniePipelineConfig_bindProfiler)(
    GeniePipelineConfig_Handle_t,
    GenieProfile_Handle_t
  );
  Genie_Status_t (*GenieProfile_create)(
    GenieProfileConfig_Handle_t,
    GenieProfile_Handle_t *
  );
  Genie_Status_t (*GenieNodeConfig_createFromJson)(
    const char *,
    GenieNodeConfig_Handle_t *
  );
  Genie_Status_t (*GeniePipeline_create)(
    GeniePipelineConfig_Handle_t,
    GeniePipeline_Handle_t *
  );
  Genie_Status_t (*GenieNode_create)(
    GenieNodeConfig_Handle_t,
    GenieNode_Handle_t *
  );
  Genie_Status_t (*GeniePipeline_addNode)(
    GeniePipeline_Handle_t,
    GenieNode_Handle_t
  );
  Genie_Status_t (*GenieNodeConfig_bindProfiler)(
    GenieNodeConfig_Handle_t,
    GenieProfile_Handle_t
  );
  Genie_Status_t (*GenieNodeConfig_bindLogger)(
    GenieNodeConfig_Handle_t,
    GenieLog_Handle_t
  );
};

struct Context {
    void* genie_handle;
    genie::GenieApi api;
};

static std::unique_ptr<genie::Context> init_context() {

  return std::make_unique<genie::Context>();
}

static void load_library(genie::Context& ctx) {

  ctx.genie_handle = dlopen ("/usr/lib/libGenie.so", RTLD_NOW);
  if (!ctx.genie_handle)
    throw std::runtime_error("GenieVLM: Could not load dynamic library!");

  LOAD_SYMBOL(GeniePipeline_connect);
  LOAD_SYMBOL(GenieLog_create);
  LOAD_SYMBOL(GenieNode_setTextCallback);
  LOAD_SYMBOL(GenieNode_free);
  LOAD_SYMBOL(GenieNodeConfig_free);
  LOAD_SYMBOL(GeniePipeline_free);
  LOAD_SYMBOL(GeniePipelineConfig_free);
  LOAD_SYMBOL(GeniePipelineConfig_createFromJson);
  LOAD_SYMBOL(GenieProfile_free);
  LOAD_SYMBOL(GenieNode_setData);
  LOAD_SYMBOL(GenieNode_getSampler);
  LOAD_SYMBOL(GenieSamplerConfig_createFromJson);
  LOAD_SYMBOL(GenieSamplerConfig_setParam);
  LOAD_SYMBOL(GenieSampler_applyConfig);
  LOAD_SYMBOL(GenieSamplerConfig_free);
  LOAD_SYMBOL(GeniePipeline_execute);
  LOAD_SYMBOL(GeniePipeline_reset);
  LOAD_SYMBOL(GenieProfile_getJsonData);
  LOAD_SYMBOL(GeniePipelineConfig_bindLogger);
  LOAD_SYMBOL(GenieProfileConfig_createFromJson);
  LOAD_SYMBOL(GenieProfileConfig_free);
  LOAD_SYMBOL(GeniePipelineConfig_bindProfiler);
  LOAD_SYMBOL(GenieProfile_create);
  LOAD_SYMBOL(GenieNodeConfig_createFromJson);
  LOAD_SYMBOL(GeniePipeline_create);
  LOAD_SYMBOL(GenieNode_create);
  LOAD_SYMBOL(GeniePipeline_addNode);
  LOAD_SYMBOL(GenieNodeConfig_bindProfiler);
  LOAD_SYMBOL(GenieNodeConfig_bindLogger);
}

static GenieNode_IOName_t string_to_IO_Node (const std::string& node_IO_string) {

  static const std::unordered_map<std::string, GenieNode_IOName_t> node_IO_Map = {
    {"GENIE_NODE_TEXT_GENERATOR_TEXT_INPUT",
      GENIE_NODE_TEXT_GENERATOR_TEXT_INPUT},
    {"GENIE_NODE_TEXT_GENERATOR_EMBEDDING_INPUT",
      GENIE_NODE_TEXT_GENERATOR_EMBEDDING_INPUT},
    {"GENIE_NODE_TEXT_GENERATOR_TEXT_OUTPUT",
      GENIE_NODE_TEXT_GENERATOR_TEXT_OUTPUT},
    {"GENIE_NODE_TEXT_ENCODER_TEXT_INPUT",
      GENIE_NODE_TEXT_ENCODER_TEXT_INPUT},
    {"GENIE_NODE_TEXT_ENCODER_EMBEDDING_OUTPUT",
      GENIE_NODE_TEXT_ENCODER_EMBEDDING_OUTPUT},
    {"GENIE_NODE_IMAGE_ENCODER_IMAGE_INPUT",
      GENIE_NODE_IMAGE_ENCODER_IMAGE_INPUT},
    {"GENIE_NODE_IMAGE_ENCODER_EMBEDDING_OUTPUT",
      GENIE_NODE_IMAGE_ENCODER_EMBEDDING_OUTPUT},
    {"GENIE_NODE_IMAGE_ENCODER_IMAGE_POS_SIN",
      GENIE_NODE_IMAGE_ENCODER_IMAGE_POS_SIN},
    {"GENIE_NODE_IMAGE_ENCODER_IMAGE_POS_COS",
      GENIE_NODE_IMAGE_ENCODER_IMAGE_POS_COS},
    {"GENIE_NODE_IMAGE_ENCODER_IMAGE_FULL_ATTN_MASK",
      GENIE_NODE_IMAGE_ENCODER_IMAGE_FULL_ATTN_MASK},
    {"GENIE_NODE_IMAGE_ENCODER_IMAGE_WINDOW_ATTN_MASK",
      GENIE_NODE_IMAGE_ENCODER_IMAGE_WINDOW_ATTN_MASK}
  };
  auto it = node_IO_Map.find(node_IO_string);
  if (it != node_IO_Map.end()) {
    return it->second;
  }
  throw std::invalid_argument("Invalid Node IO value passed: " + node_IO_string);
}

// Create pipeline from JSON content (already loaded)
static void create_pipeline_config(genie::Context& ctx,
                                    Pipeline &pipeline,
                                    const std::string& pipeline_json) {

  Genie_Status_t status = ctx.api.GeniePipelineConfig_createFromJson
    (pipeline_json.c_str (), &pipeline.config);

  if ((GENIE_STATUS_SUCCESS != status) || (!pipeline.config)) {
    throw std::runtime_error("Genie: Failed to create the pipe config");
  }
}

// Create pipeline from JSON content (already loaded)
static void create_pipeline(genie::Context& ctx, Pipeline &pipeline) {

  Genie_Status_t status = ctx.api.GeniePipeline_create
    (pipeline.config, &pipeline.handle);
  if ((GENIE_STATUS_SUCCESS != status) || (!pipeline.handle)) {
    throw std::runtime_error("Genie: Failed to create the pipeline");
  }
}

// Create node from JSON content (already loaded)
static void create_node_config(genie::Context& ctx, Node &node,
  const std::string& node_json) {

  Genie_Status_t status = ctx.api.GenieNodeConfig_createFromJson
    (node_json.c_str (), &node.config);
  if ((GENIE_STATUS_SUCCESS != status) || (!node.config)) {
    throw std::runtime_error("Genie: Failed to create the node config");
  }
}

// Create node from JSON content (already loaded)
static void create_node(genie::Context& ctx, Node &node) {

  Genie_Status_t status = ctx.api.GenieNode_create (node.config, &node.handle);
  if ((GENIE_STATUS_SUCCESS != status) || (!node.handle)) {
    throw std::runtime_error("Genie: Failed to create the Genie Node");
  }
}

// Attach node to pipeline (order matters if your graph needs it)
static void add_node(genie::Context& ctx, Pipeline &pipeline, Node &node) {

  Genie_Status_t status = ctx.api.GeniePipeline_addNode(pipeline.handle,
    node.handle);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to add node");
  }
}

// Create pipeline from JSON content (already loaded)
static void connect_node(genie::Context& ctx,
                          Pipeline &pipeline,
                          GenieNode_IOName_t out_type,
                          Node *node_out,
                          GenieNode_IOName_t in_type,
                          Node *node_in) {

  Genie_Status_t status = ctx.api.GeniePipeline_connect(pipeline.handle,
      node_out->handle, out_type, node_in->handle, in_type);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to connect");
  }
}

// Create pipeline from JSON content (already loaded)
static void set_responce_callback(genie::Context& ctx,
                                  GenieNode_IOName_t node_type,
                                  Node *node,
                                  GenieNode_TextOutput_Callback_t callback) {

  Genie_Status_t status =
      ctx.api.GenieNode_setTextCallback(node->handle, node_type, callback);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Failed to set the text output callback");
  }
}

// Free node
static void free_node(genie::Context& ctx, Node &node) {

  Genie_Status_t status = GENIE_STATUS_SUCCESS;

  status = ctx.api.GenieNode_free (node.handle);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to free node");
  }

  status = ctx.api.GenieNodeConfig_free (node.config);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to free node config");
  }
}

// Free Pipeline
static void free_pipeline(genie::Context& ctx, Pipeline &pipeline) {

  Genie_Status_t status = GENIE_STATUS_SUCCESS;

  status = ctx.api.GeniePipeline_free (pipeline.handle);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to free pipeline");
  }

  status = ctx.api.GeniePipelineConfig_free (pipeline.config);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to free pipeline config");
  }
}

// Synchronous inference placeholder (call your graph here)
static bool run_vlm(void *inst,
                    Pipeline& pipeline,
                    genie::Context& ctx,
                    Node *text_generation_node,
                    Node *lut_encoder_node,
                    Node *image_encoder_node,
                    const std::vector<float>& img,
                    const std::string& asistant_description,
                    const std::string& question,
                    float temperature,
                    float top_p,
                    float seed,
                    float presence_penalty,
                    float frequency_penalty,
                    void *user_data,
                    std::shared_ptr<VlmResult> out,
                    std::string& err) {

  (void) inst;
  (void) seed;
  (void) user_data;

  Genie_Status_t status = GENIE_STATUS_SUCCESS;

  std::vector<CustomInput> custom_inputs {
    {"imageEncoder", "GENIE_NODE_IMAGE_ENCODER_IMAGE_POS_COS",
      "veg_inputs/position_ids_cos.raw"},

    {"imageEncoder", "GENIE_NODE_IMAGE_ENCODER_IMAGE_POS_SIN",
      "veg_inputs/position_ids_sin.raw"},

    {"imageEncoder", "GENIE_NODE_IMAGE_ENCODER_IMAGE_WINDOW_ATTN_MASK",
      "veg_inputs/window_attention_mask.raw"},

    {"imageEncoder", "GENIE_NODE_IMAGE_ENCODER_IMAGE_FULL_ATTN_MASK",
      "veg_inputs/full_attention_mask.raw"}
  };


  std::vector<std::shared_ptr<void>> keep_alive;

  std::string system_prompt =
    "<|im_start|>system\n" + asistant_description + "<|im_end|>\n"
    "<|im_start|>user\n<|vision_start|>";
  status = ctx.api.GenieNode_setData(lut_encoder_node->handle,
    GENIE_NODE_TEXT_ENCODER_TEXT_INPUT, (void*)(system_prompt.c_str()),
    system_prompt.size () * sizeof(char), nullptr);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to set the embedding input data");
  }

  status = ctx.api.GenieNode_setData (image_encoder_node->handle,
    GENIE_NODE_IMAGE_ENCODER_IMAGE_INPUT, (void*)(img.data()),
    img.size () * sizeof (float), nullptr);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error ("Genie: Failed to set the embedding input data");
  }

  for (const auto& ci : custom_inputs) {
    std::ifstream file(ci.file, std::ios::binary | std::ios::ate);
    if (!file) {
      throw std::runtime_error("Genie: Failed to open custom input file: " + ci.file);
    }
    uint32_t file_size = file.tellg();
    std::shared_ptr<void> image_buffer (new int8_t[file_size], [](void* p)
      { delete[] static_cast<int8_t*> (p); });

    std::ifstream embedding_stream (ci.file, std::ifstream::binary);
    embedding_stream.read (static_cast<char*> (image_buffer.get ()), file_size);

    Node *target_Node;
    if (ci.node == "imageEncoder") {
      target_Node = image_encoder_node;
    } else if (ci.node == "lutEncoder") {
      target_Node = lut_encoder_node;
    } else if (ci.node == "textGenerator") {
      target_Node = text_generation_node;
    } else {
      throw std::invalid_argument ("Genie: Unknown node in custom_inputs: " + ci.node);
    }

    GenieNode_IOName_t io_enum = string_to_IO_Node(ci.input_type);

    status = ctx.api.GenieNode_setData(target_Node->handle, io_enum,
          (void*)image_buffer.get (), file_size, nullptr);
    if (GENIE_STATUS_SUCCESS != status) {
      throw std::runtime_error("Genie: Failed to set the embedding input data");
    }

    keep_alive.emplace_back(std::move(image_buffer));
  }

  GenieSampler_Handle_t sampler_handle;
  GenieSamplerConfig_Handle_t sampler_config_handle;
  std::string sampler_config = read_file_to_string_or_throw("sampler.json");

  if (temperature != 1 || top_p != 1 || presence_penalty != 0.0 ||
      frequency_penalty != 0.0) {
    status = ctx.api.GenieNode_getSampler(text_generation_node->handle,
      &sampler_handle);
    if (GENIE_STATUS_SUCCESS != status) {
      throw std::runtime_error("Genie: Failed to get sampler.");
    }
    status = ctx.api.GenieSamplerConfig_createFromJson(sampler_config.c_str(), &sampler_config_handle);
    if (GENIE_STATUS_SUCCESS != status) {
      throw std::runtime_error("Genie: Failed to create sampler config.");
    }
    if (temperature != 1) {
      status = ctx.api.GenieSamplerConfig_setParam(sampler_config_handle,
        "temp", std::to_string(temperature).c_str ());
      if (GENIE_STATUS_SUCCESS != status) {
        throw std::runtime_error("Genie: Failed to setParam temp");
      }
    }
    if (top_p != 1) {
      status = ctx.api.GenieSamplerConfig_setParam(sampler_config_handle,
        "top-p", std::to_string(top_p).c_str());
      if (GENIE_STATUS_SUCCESS != status) {
        throw std::runtime_error("Genie: Failed to setParam top-p");
      }
    }
    if (presence_penalty != 0.0) {
      status = ctx.api.GenieSamplerConfig_setParam(sampler_config_handle,
        "presence-penalty", std::to_string(presence_penalty).c_str());
      if (GENIE_STATUS_SUCCESS != status) {
        throw std::runtime_error("Genie: Failed to setParam presence-penalty");
      }
    }
    if (frequency_penalty != 0.0) {
      status = ctx.api.GenieSamplerConfig_setParam(sampler_config_handle,
        "frequency-penalty", std::to_string(frequency_penalty).c_str());
      if (GENIE_STATUS_SUCCESS != status) {
        throw std::runtime_error("Genie: Failed to setParam frequency-penalty");
      }
    }

    status = ctx.api.GenieSampler_applyConfig(sampler_handle, sampler_config_handle);
    if (GENIE_STATUS_SUCCESS != status) {
      throw std::runtime_error("Genie: Failed to apply sampler config.");
    }
    ctx.api.GenieSamplerConfig_free (sampler_config_handle);
  }

  std::string final_prompt =
      "<|vision_end|> " + question + " <|im_end|>\n<|im_start|>assistant";

  status =
    ctx.api.GenieNode_setData(lut_encoder_node->handle,
      GENIE_NODE_TEXT_ENCODER_TEXT_INPUT, (void*)(final_prompt.c_str()),
      final_prompt.size() * sizeof(char), nullptr);
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to set the text input data");
  }

  status = ctx.api.GeniePipeline_execute (pipeline.handle, out.get());
  if (GENIE_STATUS_SUCCESS != status) {
    throw std::runtime_error("Genie: Failed to execute");
  }

  ctx.api.GeniePipeline_reset(pipeline.handle);

  err.clear();
  return true;
}
}
//                  namespace genie

// ---------------------------
// GenieVLM internals
// ---------------------------

struct GenieVLM::EngineLogger {
    EngineLogger(VlmEngine engine, GenieLog_Level_t level, genie::Context& ctx)
      : engine_(engine), ctx(ctx) {

      file_path_ = std::string("log_") + to_string(engine_) + ".txt";
      stream_.open(file_path_, std::ios::out | std::ios::trunc);
      if (!stream_.is_open()) {
        throw std::runtime_error("Failed to open log file: " + file_path_);
      }

      Genie_Status_t status = ctx.api.GenieLog_create(nullptr,
        &EngineLogger::log_callback, level, &handle_);
      if ((status != GENIE_STATUS_SUCCESS) || (!handle_)) {
        throw std::runtime_error("Failed to create Genie log handle for " +
                                std::string(to_string(engine_)));
      }
      register_logger(handle_, this);
    }

    ~EngineLogger() {

      if (handle_) {
        unregister_logger(handle_);
        ctx.api.GenieLog_free(handle_);
        handle_ = nullptr;
      }
      if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
      }
    }

    GenieLog_Handle_t handle() const { return handle_; }

  private:
    static const char* level_string(GenieLog_Level_t level) {

      switch (level) {
        case GENIE_LOG_LEVEL_ERROR:   return "ERROR";
        case GENIE_LOG_LEVEL_WARN:    return "WARN";
        case GENIE_LOG_LEVEL_INFO:    return "INFO";
        case GENIE_LOG_LEVEL_VERBOSE: return "VERBOSE";
        default:                      return "UNKNOWN";
      }
    }

    static void log_callback(const GenieLog_Handle_t handle,
                            const char* fmt,
                            GenieLog_Level_t level,
                            uint64_t timestamp,
                            va_list args) {

      EngineLogger* logger = lookup_logger(handle);
      if (!logger) return;
      logger->write(fmt ? fmt : "", level, timestamp, args);
    }

    void write(const char* fmt,
              GenieLog_Level_t level,
              uint64_t timestamp,
              va_list args) {
      if (!stream_.is_open()) {
        return;
      }

      char buffer[2048];
      va_list args_copy;
      va_copy(args_copy, args);
  #if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wformat-nonliteral"
  #endif
      vsnprintf(buffer, sizeof(buffer), fmt, args_copy);
  #if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
  #endif
      va_end(args_copy);

      stream_ << "[" << timestamp << "] "
              << level_string(level) << " "
              << buffer << std::endl;
    }

    static void register_logger(GenieLog_Handle_t handle, EngineLogger* logger) {

      std::lock_guard<std::mutex> lk(registry_mutex());
      registry()[handle] = logger;
    }

    static void unregister_logger(GenieLog_Handle_t handle) {


      std::lock_guard<std::mutex> lk(registry_mutex());
      registry().erase(handle);
    }

    static EngineLogger* lookup_logger(GenieLog_Handle_t handle) {

      std::lock_guard<std::mutex> lk(registry_mutex());
      auto& reg = registry();
      auto it = reg.find(handle);
      return it == reg.end() ? nullptr : it->second;
    }

    static std::unordered_map<GenieLog_Handle_t, EngineLogger*>& registry() {

      static std::unordered_map<GenieLog_Handle_t, EngineLogger*> reg;
      return reg;
    }

    static std::mutex& registry_mutex() {

      static std::mutex m;
      return m;
    }

  private:
    VlmEngine engine_;
    std::string file_path_;
    std::ofstream stream_;
    GenieLog_Handle_t handle_{nullptr};
    genie::Context& ctx;
};

struct GenieVLM::EngineProfiler {
    explicit EngineProfiler(VlmEngine engine, genie::Context& ctx) :
    engine_(engine), ctx(ctx) {

      GenieProfileConfig_Handle_t cfg_handle = nullptr;

      const std::string cfg_path = trace_config_path(engine_);
      if (!cfg_path.empty()) {
        std::ifstream cfg_stream(cfg_path, std::ios::in | std::ios::binary);
        if (cfg_stream.is_open()) {
          std::ostringstream buffer;
          buffer << cfg_stream.rdbuf();
          const std::string cfg = buffer.str();
          if (!cfg.empty()) {
            Genie_Status_t cfg_status =
              ctx.api.GenieProfileConfig_createFromJson(cfg.c_str(), &cfg_handle);
            if ((cfg_status != GENIE_STATUS_SUCCESS) || (!cfg_handle)) {
              throw std::runtime_error("Failed to create profile config from " +
                cfg_path);
            }
          }
        }
      }

      Genie_Status_t status = ctx.api.GenieProfile_create(cfg_handle, &handle_);
      if ((status != GENIE_STATUS_SUCCESS) || (!handle_)) {
        throw std::runtime_error("Failed to create Genie profile handle for " +
                                std::string(to_string(engine_)));
      }
      if (cfg_handle) {
        ctx.api.GenieProfileConfig_free(cfg_handle);
      }

      file_path_ = std::string("profile_") + to_string(engine_) + ".json";
    }

    ~EngineProfiler() {
      dump_to_file();
      if (handle_) {
        ctx.api.GenieProfile_free(handle_);
        handle_ = nullptr;
      }
    }

    GenieProfile_Handle_t handle() const { return handle_; }

  private:
    static void alloc_callback(size_t size, const char** data) {
      char* buffer = static_cast<char*>(std::malloc(size));
      if (!buffer) {
        throw std::runtime_error("Failed to allocate memory for profile output");
      }
      *data = buffer;
    }

    void dump_to_file() {
      if (!handle_)
        return;

      const char* json_data = nullptr;
      Genie_Status_t status = ctx.api.GenieProfile_getJsonData(handle_,
        &EngineProfiler::alloc_callback, &json_data);
      if (status == GENIE_STATUS_SUCCESS && json_data) {
        std::ofstream out(file_path_, std::ios::out | std::ios::trunc);
        if (out.is_open()) {
          out << json_data;
          out.close();
        }
      }
      if (json_data) {
        std::free(const_cast<char*>(json_data));
      }
    }

    static std::string trace_config_path(VlmEngine engine) {

      switch (engine) {
        case VlmEngine::NPU0: return "trace_NPU_0.json";
        case VlmEngine::NPU1: return "trace_NPU_1.json";
        default: return "";
      }
    }

    VlmEngine engine_;
    std::string file_path_;
    GenieProfile_Handle_t handle_{nullptr};
    genie::Context& ctx;
};

struct GenieVLM::Instance {
    explicit Instance(VlmEngine e) : engine(e) {}

    VlmEngine engine;

    // Keep original instance config (needed for init phases)
    VlmInstanceConfig cfg;

    // Phase 1: loaded JSON configs in memory
    std::string pipeline_json;
    std::vector<std::string> node_jsons; // same order as cfg.nodes

    // Phase 2..4: Genie objects
    std::unique_ptr<genie::Pipeline> pipeline;
    std::vector<std::unique_ptr<genie::Node>> nodes;

    // Work queue
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::deque<VlmRequest> queue;

    // Worker
    std::thread worker;
    std::atomic<bool> running{false};
    std::thread config_thread;

    // Stats
    std::atomic<uint64_t> processed{0};
    std::atomic<uint64_t> failed{0};
};

GenieVLM::GenieVLM(GenieVLMConfig cfg, VlmCallback cb)
  : cfg_(std::move(cfg)), cb_(std::move(cb)) {

  if (!cb_) {
    throw std::runtime_error("GenieVLM: callback must not be null");
  }
  if (cfg_.instances.empty()) {
    throw std::runtime_error("GenieVLM: instances must not be empty");
  }
  start();
}

GenieVLM::~GenieVLM() {

  if (ctx->genie_handle) {
    dlclose (ctx->genie_handle);
    ctx->genie_handle = nullptr;
  }
  stop();
}

void GenieVLM::start() {

  // Start callback dispatcher first (so init failures can also be reported if you want)
  start_callback_dispatcher();
  ctx = genie::init_context();

  genie::load_library(*ctx);

  // Allocate Instance objects and copy per-instance config
  instances_.reserve(cfg_.instances.size());
  for (const auto& icfg : cfg_.instances) {
    auto inst = std::make_unique<Instance>(icfg.engine);
    inst->cfg = icfg;
    instances_.push_back(std::move(inst));
  }

  for (const auto& instp : instances_) {
    auto* inst = instp.get();
    instp->config_thread = std::thread([this, inst]() {
      setup_loggers(*inst);
      setup_profilers(*inst);
      load_all_configs(*inst);
      create_all_pipelines_and_nodes(*inst);
      connect_nodes(*inst);
      set_responce_callback(*inst);
    });
  }

  // Start per-instance workers after full graph is built
  for (size_t i = 0; i < instances_.size(); ++i) {

    auto* inst = instances_[i].get();
    inst->config_thread.join();
    inst->running = true;
    inst->worker = std::thread([this, inst, i]() {
      worker_loop(*inst, i);
    });
  }
}

void GenieVLM::stop() {

  if (stopped_.exchange(true)) return;

  // Stop workers
  for (auto& inst : instances_) {
    inst->running = false;
    inst->cv.notify_all();
  }
  for (auto& inst : instances_) {
    if (inst->worker.joinable()) inst->worker.join();
    if (inst->config_thread.joinable()) inst->config_thread.join();
  }

  // Stop callback dispatcher after workers (to drain remaining events if any)
  stop_callback_dispatcher();

  free_all_nodes_and_pipelines();

  instances_.clear();
  engine_loggers_.clear();
  engine_profilers_.clear();
}

void* GenieVLM::node_by_name(Instance &inst, std::string name) {

  for (auto& n : inst.nodes) {
    if (n && n->name == name) return n.get();
  }

  throw std::runtime_error("Failed to get node " + name);

  return nullptr;
}

void GenieVLM::load_all_configs(Instance &inst) {

  // Load ALL pipeline + node JSON configs for ALL instances first
  if (!inst.cfg.pipeline_config_path.empty()) {
    inst.pipeline_json =
      read_file_to_string_or_throw(inst.cfg.pipeline_config_path);
  } else {
    inst.pipeline_json = "";
  }

  inst.node_jsons.clear();
  inst.node_jsons.reserve(inst.cfg.nodes.size());
  for (const auto& ns : inst.cfg.nodes) {
    if (ns.config_path.empty()) {
      throw std::runtime_error("Node missing config_path (type=" + ns.type + ")");
    }
    inst.node_jsons.push_back(read_file_to_string_or_throw(ns.config_path));
  }
}

void GenieVLM::create_all_pipelines_and_nodes(Instance &inst) {

  // configs pipelines and nodes
  inst.pipeline = std::make_unique<genie::Pipeline>();
  genie::create_pipeline_config(*ctx, *inst.pipeline, inst.pipeline_json);
  if (auto log_handle = log_handle_for_engine(inst.engine)) {
    Genie_Status_t status =
      ctx->api.GeniePipelineConfig_bindLogger(inst.pipeline->config, log_handle);
    if (status != GENIE_STATUS_SUCCESS) {
      throw std::runtime_error("Genie: Failed to bind logger to pipeline config");
    }
  }
  if (auto profile_handle = profile_handle_for_engine(inst.engine)) {
    Genie_Status_t status = ctx->api.GeniePipelineConfig_bindProfiler
      (inst.pipeline->config, profile_handle);
    if (status != GENIE_STATUS_SUCCESS) {
      throw std::runtime_error("Genie: Failed to bind profiler to pipeline config");
    }
  }

  inst.nodes.clear();
  inst.nodes.reserve(inst.cfg.nodes.size());

  for (size_t ni = 0; ni < inst.cfg.nodes.size(); ++ni) {
    const auto& ns = inst.cfg.nodes[ni];
    const auto& json = inst.node_jsons[ni];

    auto node = std::make_unique<genie::Node>();
    genie::create_node_config(*ctx, *node, json);
    node->name = ns.name;
    if (auto log_handle = log_handle_for_engine(inst.engine)) {
      Genie_Status_t status =
        ctx->api.GenieNodeConfig_bindLogger(node->config, log_handle);
      if (status != GENIE_STATUS_SUCCESS) {
        throw std::runtime_error("Genie: Failed to bind logger to node config");
      }
    }
    if (auto profile_handle = profile_handle_for_engine(inst.engine)) {
      Genie_Status_t status =
        ctx->api.GenieNodeConfig_bindProfiler(node->config, profile_handle);
      if (status != GENIE_STATUS_SUCCESS) {
        throw std::runtime_error("Genie: Failed to bind profiler to node config");
      }
    }

    if (!node) {
      throw std::runtime_error("Failed to create node (type=" + ns.type +
        ", engine=" + std::string(to_string(inst.engine)) + ")");
    }
    inst.nodes.push_back(std::move(node));
  }

  // Create pipelines handles
  genie::create_pipeline(*ctx, *inst.pipeline);

  // Create nodes handles
  for (size_t idx = 0; idx < inst.nodes.size(); ++idx) {
    auto& node = inst.nodes[idx];
    genie::create_node(*ctx, *node);
  }

  // Add nodes
  // Attach in the order listed in cfg.nodes
  for (auto& node : inst.nodes) {
    genie::add_node(*ctx, *inst.pipeline, *node);
  }
}

void GenieVLM::connect_nodes(Instance &inst) {

  //"image_encoder" > "text_generator"
  genie::connect_node(*ctx, *inst.pipeline,
      GENIE_NODE_IMAGE_ENCODER_EMBEDDING_OUTPUT,
      (genie::Node*) node_by_name(inst, "image_encoder"),
      GENIE_NODE_TEXT_GENERATOR_EMBEDDING_INPUT,
      (genie::Node*) node_by_name(inst, "text_generator"));

  //"lut_encoder" > "text_generator"
  genie::connect_node(*ctx, *inst.pipeline,
      GENIE_NODE_TEXT_ENCODER_EMBEDDING_OUTPUT,
      (genie::Node*) node_by_name(inst, "lut_encoder"),
      GENIE_NODE_TEXT_GENERATOR_EMBEDDING_INPUT,
      (genie::Node*) node_by_name(inst, "text_generator"));
}

void GenieVLM::set_responce_callback(Instance &inst) {

  genie::set_responce_callback(*ctx,
      GENIE_NODE_TEXT_GENERATOR_TEXT_OUTPUT,
      (genie::Node*) node_by_name(inst, "text_generator"),
        [](const char* response_str,
          GenieNode_TextOutput_SentenceCode_t sentence_code,
          const void* user_data) -> Genie_Status_t {
          (void) sentence_code;

          VlmResult *res = static_cast<VlmResult*> (const_cast<void*>(user_data));

          if (!response_str) return GENIE_STATUS_ERROR_INVALID_ARGUMENT;

          if (response_str && res) {
            res->text += response_str;
          }

          return GENIE_STATUS_SUCCESS;
        }
      );
}

void GenieVLM::free_all_nodes_and_pipelines() {

  for (auto& instp : instances_) {
    auto& inst = *instp;

    // Attach in the order listed in cfg.nodes
    for (auto& node : inst.nodes) {
      genie::free_node(*ctx, *node);
    }

    genie::free_pipeline(*ctx, *inst.pipeline);
  }
}

std::vector<float> GenieVLM::preprocess(void *buffer) {

  float* in;
  std::vector<float> out;

  const size_t plane = static_cast<size_t> (MLVCONV_WIDTH * MLVCONV_HEIGHT);
  // L number of patches
  const size_t L = static_cast<size_t> (MLVCONV_WIDTH) / PATCH_SIZE *
    static_cast<size_t> (MLVCONV_HEIGHT) / PATCH_SIZE;
  // D floats per patch
  const size_t D = CHANNELS * TEMPORAL_GROUPING * PATCH_SIZE * PATCH_SIZE;

  in = reinterpret_cast<float*> (buffer);
  const float* G = in + plane;
  const float* B = G + plane;

  out.resize (L * D);

  const size_t P  = PATCH_SIZE;
  const size_t M  = MERGE_SIZE;
  const size_t Gw = static_cast<size_t> (MLVCONV_WIDTH) / P;  // patches per row
  const size_t BW = Gw / M;                           // blocks per row

  for (size_t i = 0; i < L; ++i) {
    for (size_t pix = 0; pix < P * P; ++pix) {
      size_t idx_src = ((((i / (M * M * BW)) * M + ((i / M) % M)) * P +
                          (pix / P))) * MLVCONV_WIDTH +
                          ((((i / (M * M)) % BW) * M + (i % M)) *
                          P + (pix % P));

      for (size_t c = 0; c < CHANNELS; ++c) {
        const float* src = (c == 0 ? in : (c == 1 ? G : B));

        for (size_t t = 0; t < TEMPORAL_GROUPING; ++t)
          out[i * D + (c * TEMPORAL_GROUPING * P * P) + pix +
            (t * P * P)] = src[idx_src];
      }
    }
  }

  return out;
}

uint64_t GenieVLM::submit(void *buffer,
                              std::string asistant_description,
                              std::string question,
                              float temperature,
                              float top_p,
                              float seed,
                              float presence_penalty,
                              float frequency_penalty,
                              void *user_data,
                              std::optional<std::chrono::milliseconds> timeout) {

  VlmRequest req;
  req.id = next_request_id_++;

  // Preprocess
  req.image = preprocess(buffer);
  req.asistant_description = asistant_description;
  req.question = question;

  req.temperature = std::move(temperature);
  req.top_p = std::move(top_p);
  req.seed = std::move(seed);
  req.presence_penalty = std::move(presence_penalty);
  req.frequency_penalty = std::move(frequency_penalty);
  req.user_data = user_data;

  if (timeout.has_value()) {
    req.deadline = std::chrono::steady_clock::now() + *timeout;
  }

  const size_t idx = pick_instance_index();
  if (!enqueue(idx, std::move(req))) {
    return 0;
  }
  return req.id;
}

size_t GenieVLM::instance_count() const {

  return instances_.size();
}

std::vector<GenieVLM::InstanceStats> GenieVLM::stats() const {

  std::vector<InstanceStats> out;
  out.reserve(instances_.size());
  for (const auto& inst : instances_) {
    std::lock_guard<std::mutex> lk(inst->mtx);
    out.push_back(InstanceStats{
      inst->engine,
      inst->queue.size(),
      inst->processed.load(),
      inst->failed.load()
    });
  }
  return out;
}

size_t GenieVLM::pick_instance_index() {

  const size_t n = instances_.size();
  if (cfg_.lb_mode == GenieVLMConfig::LBMode::RoundRobin) {
    return rr_.fetch_add(1) % n;
  }

  // LeastQueue
  size_t best = 0;
  size_t best_q = SIZE_MAX;

  for (size_t i = 0; i < n; ++i) {
    std::lock_guard<std::mutex> lk(instances_[i]->mtx);
    const size_t q = instances_[i]->queue.size();
    if (q < best_q) {
      best_q = q;
      best = i;
    }
  }

  return best;
}

bool GenieVLM::enqueue(size_t idx, VlmRequest req) {

  auto& inst = *instances_[idx];

  {
    std::lock_guard<std::mutex> lk(inst.mtx);

    if (cfg_.max_queue_per_instance > 0 &&
        inst.queue.size() >= cfg_.max_queue_per_instance) {
      if (cfg_.drop_oldest_on_overflow && !inst.queue.empty()) {
        inst.queue.pop_front();
      } else {
        return false; // reject
      }
    }

    inst.queue.push_back(std::move(req));
  }

  inst.cv.notify_one();
  return true;
}

void GenieVLM::worker_loop(Instance& inst, size_t instance_index) {

  (void)instance_index;

  while (inst.running) {
    VlmRequest req;

    {
      std::unique_lock<std::mutex> lk(inst.mtx);
      inst.cv.wait(lk, [&] { return !inst.running || !inst.queue.empty(); });

      if (!inst.running) break;
      if (inst.queue.empty()) continue;

      req = std::move(inst.queue.front());
    }

    // deadline check
    if (req.deadline.has_value() &&
        std::chrono::steady_clock::now() > *req.deadline) {
      inst.failed++;
      CallbackEvent ev;
      VlmResult out;
      ev.id = req.id;
      ev.ok = false;
      out.user_data = req.user_data;
      ev.result = std::move(out);
      ev.err = "timeout before processing";
      enqueue_callback(std::move(ev));
      continue;
    }

    auto out = std::make_shared<VlmResult>();
    std::string err;
    bool ok = false;

    try {
      genie::Node *text_generator_node =
        (genie::Node*) node_by_name(inst, "text_generator");
      genie::Node *lut_encoder_node =
        (genie::Node*) node_by_name(inst, "lut_encoder");
      genie::Node *image_encoder_node =
        (genie::Node*) node_by_name(inst, "image_encoder");

      ok = genie::run_vlm(&inst, *inst.pipeline, *ctx, text_generator_node,
        lut_encoder_node, image_encoder_node, req.image, req.asistant_description,
        req.question, req.temperature, req.top_p, req.seed, req.presence_penalty,
        req.frequency_penalty, req.user_data, out, err);

    } catch (const std::exception& ex) {
      ok = false;
      err = ex.what();
    } catch (...) {
      ok = false;
      err = "unknown exception";
    }

    {
      std::unique_lock<std::mutex> lk(inst.mtx);
      inst.queue.pop_front();
    }

    if (ok) inst.processed++;
    else    inst.failed++;

    CallbackEvent ev;
    ev.id = req.id;
    ev.ok = ok;
    ev.result = *out;
    ev.err = std::move(err);
    ev.result.user_data = req.user_data;

    enqueue_callback(std::move(ev));
  }
}

// ---------------------------
// Callback dispatcher
// ---------------------------
void GenieVLM::start_callback_dispatcher() {

  cb_running_ = true;
  cb_thread_ = std::thread([this]() { callback_loop(); });
}

void GenieVLM::stop_callback_dispatcher() {

  cb_running_ = false;
  cb_cv_.notify_all();
  if (cb_thread_.joinable()) cb_thread_.join();

  // optional: drop any remaining events
  std::lock_guard<std::mutex> lk(cb_mtx_);
  cb_queue_.clear();
}

bool GenieVLM::enqueue_callback(CallbackEvent ev) {

  // If configured to drop on overflow, do it. Otherwise block producers.
  std::unique_lock<std::mutex> lk(cb_mtx_);

  if (cfg_.max_callback_queue > 0) {
    if (cfg_.drop_callbacks_on_overflow) {
      if (cb_queue_.size() >= cfg_.max_callback_queue) {
        // drop newest (or choose drop oldest)
        return false;
      }
    } else {
      // block until there's space or shutdown
      cb_cv_.wait(lk, [&] {
        return !cb_running_ || cb_queue_.size() < cfg_.max_callback_queue;
      });
      if (!cb_running_) return false;
    }
  }

  cb_queue_.push_back(std::move(ev));
  lk.unlock();
  cb_cv_.notify_one();
  return true;
}

void GenieVLM::callback_loop() {

  while (cb_running_) {
    CallbackEvent ev;

    {
      std::unique_lock<std::mutex> lk(cb_mtx_);
      cb_cv_.wait(lk, [&] { return !cb_running_ || !cb_queue_.empty(); });

      if (!cb_running_ && cb_queue_.empty()) break;
      if (cb_queue_.empty()) continue;

      ev = std::move(cb_queue_.front());
      cb_queue_.pop_front();

      // wake blocked producers (when max_callback_queue is enforced)
      lk.unlock();
      cb_cv_.notify_all();
    }

    // Execute user callback OUTSIDE lock
    try {
      cb_(ev.id, ev.ok, ev.result, ev.err);
    } catch (...) {
      // swallow exceptions from user callback
    }
  }

  // Optional: drain remaining events on shutdown (if you want):
  // while(true) { ... }
}

void GenieVLM::setup_loggers(Instance &inst) {

  if (!cfg_.logging_enabled)
    return;

  engine_loggers_.clear();

  const VlmEngine engine = inst.engine;
  if (!needs_logger(engine))
    return;
  if (engine_loggers_.count(engine))
    return;

  engine_loggers_.emplace(
    engine,
    std::make_unique<EngineLogger>(engine, cfg_.log_level, *ctx));
}

GenieLog_Handle_t GenieVLM::log_handle_for_engine(VlmEngine engine) const {

  if (!cfg_.logging_enabled)
    return nullptr;

  auto it = engine_loggers_.find(engine);
  if (it == engine_loggers_.end() || !(it->second))
    return nullptr;

  return it->second->handle();
}

bool GenieVLM::needs_logger(VlmEngine engine) {

  return (engine == VlmEngine::NPU0) || (engine == VlmEngine::NPU1);
}

void GenieVLM::setup_profilers(Instance &inst) {

  if (!cfg_.profiling_enabled)
    return;

  engine_profilers_.clear();

  const VlmEngine engine = inst.engine;
  if (!needs_logger(engine))
    return;
  if (engine_profilers_.count(engine))
    return;

  engine_profilers_.emplace(
    engine,
    std::make_unique<EngineProfiler>(engine, *ctx));
}

GenieProfile_Handle_t GenieVLM::profile_handle_for_engine(VlmEngine engine) const {

  if (!cfg_.profiling_enabled)
    return nullptr;

  auto it = engine_profilers_.find(engine);
  if (it == engine_profilers_.end() || !(it->second))
    return nullptr;

  return it->second->handle();
}
