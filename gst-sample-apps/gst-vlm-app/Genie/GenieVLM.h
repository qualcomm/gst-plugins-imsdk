/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <cstdarg>

#define GENIE_STATUS_SUCCESS                   0
#define GENIE_STATUS_WARNING_ABORTED           1
#define GENIE_STATUS_WARNING_BOUND_HANDLE      2
#define GENIE_STATUS_WARNING_PAUSED            3
#define GENIE_STATUS_WARNING_CONTEXT_EXCEEDED  4
#define GENIE_STATUS_ERROR_GENERAL             -1
#define GENIE_STATUS_ERROR_INVALID_ARGUMENT    -2
#define GENIE_STATUS_ERROR_MEM_ALLOC           -3
#define GENIE_STATUS_ERROR_INVALID_CONFIG      -4
#define GENIE_STATUS_ERROR_INVALID_HANDLE      -5
#define GENIE_STATUS_ERROR_QUERY_FAILED        -6
#define GENIE_STATUS_ERROR_JSON_FORMAT         -7
#define GENIE_STATUS_ERROR_JSON_SCHEMA         -8
#define GENIE_STATUS_ERROR_JSON_VALUE          -9
#define GENIE_STATUS_ERROR_GENERATE_FAILED     -10
#define GENIE_STATUS_ERROR_GET_HANDLE_FAILED   -11
#define GENIE_STATUS_ERROR_APPLY_CONFIG_FAILED -12
#define GENIE_STATUS_ERROR_SET_PARAMS_FAILED   -13
#define GENIE_STATUS_ERROR_BOUND_HANDLE        -14

typedef enum {
  GENIE_LOG_LEVEL_ERROR   = 1,
  GENIE_LOG_LEVEL_WARN    = 2,
  GENIE_LOG_LEVEL_INFO    = 3,
  GENIE_LOG_LEVEL_VERBOSE = 4,
} GenieLog_Level_t;

typedef enum {
  /// The string is the entire query/response.
  GENIE_NODE_SENTENCE_COMPLETE = 0,
  /// The string is the beginning of the query/response.
  GENIE_NODE_SENTENCE_BEGIN = 1,
  /// The string is a part of the query/response and not the beginning or end.
  GENIE_NODE_SENTENCE_CONTINUE = 2,
  /// The string is the end of the query/response.
  GENIE_NODE_SENTENCE_END = 3,
  /// The query has been aborted.
  GENIE_NODE_SENTENCE_ABORT = 4,
  /// Rewind the KV cache as per prefix query match before processing the query
  GENIE_NODE_SENTENCE_REWIND = 5,
} GenieNode_TextOutput_SentenceCode_t;

typedef enum {
  GENIE_NODE_TEXT_GENERATOR_TEXT_INPUT            = 0,
  GENIE_NODE_TEXT_GENERATOR_EMBEDDING_INPUT       = 1,
  GENIE_NODE_TEXT_GENERATOR_TEXT_OUTPUT           = 2,
  GENIE_NODE_TEXT_ENCODER_TEXT_INPUT              = 100,
  GENIE_NODE_TEXT_ENCODER_EMBEDDING_OUTPUT        = 101,
  GENIE_NODE_IMAGE_ENCODER_IMAGE_INPUT            = 200,
  GENIE_NODE_IMAGE_ENCODER_EMBEDDING_OUTPUT       = 201,
  GENIE_NODE_IMAGE_ENCODER_IMAGE_POS_SIN          = 202,
  GENIE_NODE_IMAGE_ENCODER_IMAGE_POS_COS          = 203,
  GENIE_NODE_IMAGE_ENCODER_IMAGE_FULL_ATTN_MASK   = 204,
  GENIE_NODE_IMAGE_ENCODER_IMAGE_WINDOW_ATTN_MASK = 205,

  // model specific
  GENIE_NODE_IMAGE_ENCODER_PRETILE_EMBEDDING_INPUT   = 206,  // Llama3.2-11B
  GENIE_NODE_IMAGE_ENCODER_POSTTILE_EMBEDDING_INPUT  = 207,  // Llama3.2-11B
  GENIE_NODE_IMAGE_ENCODER_GATED_POS_EMBEDDING_INPUT = 208,  // Llama3.2-11B
} GenieNode_IOName_t;

enum class VlmEngine {
  NPU0,
  NPU1,
  GPU
};

// Forward declaration
namespace genie {
  struct Context;
}

struct VlmEngineHash {
  size_t operator()(VlmEngine e) const noexcept {
    return static_cast<size_t>(e);
  }
};

struct CustomInput {
  std::string node;
  std::string input_type;
  std::string file;
};

typedef const struct _GeniePipelineConfig_Handle_t* GeniePipelineConfig_Handle_t;
typedef const struct _GeniePipeline_Handle_t* GeniePipeline_Handle_t;
typedef const struct _GenieLog_Handle_t* GenieLog_Handle_t;
typedef const struct _GenieProfile_Handle_t* GenieProfile_Handle_t;
typedef const struct _GenieNodeConfig_Handle_t* GenieNodeConfig_Handle_t;
typedef const struct _GenieNode_Handle_t* GenieNode_Handle_t;
typedef const struct _GenieSampler_Handle_t* GenieSampler_Handle_t;
typedef const struct _GenieLogConfig_Handle_t* GenieLogConfig_Handle_t;
typedef const struct _GenieSamplerConfig_Handle_t* GenieSamplerConfig_Handle_t;
typedef void (*Genie_AllocCallback_t)(
  const size_t size, const char** allocatedData);
typedef const struct _GenieProfileConfig_Handle_t* GenieProfileConfig_Handle_t;
typedef int32_t Genie_Status_t;

typedef void (*GenieLog_Callback_t)(const GenieLog_Handle_t handle,
                                    const char* fmt,
                                    GenieLog_Level_t level,
                                    uint64_t timestamp,
                                    va_list args);

typedef Genie_Status_t (*GenieNode_TextOutput_Callback_t)(
    const char* response,
    const GenieNode_TextOutput_SentenceCode_t sentenceCode,
    const void* userData);

const char* to_string(VlmEngine e);

struct VlmResult {
  std::string text;
  void *user_data;
  // extend with JSON, tokens, latency, etc.
};

using VlmCallback = std::function<void(
  uint64_t request_id,
  bool ok,
  const VlmResult& result,
  const std::string& err
)>;

struct VlmNodeSpec {
  // e.g. "ImageInput", "Tokenizer", "VLM", "Decoder", etc.
  std::string type;
  // Optional: name/id inside pipeline graph
  std::string name;
  // Path to node config.json
  std::string config_path;
};

struct VlmInstanceConfig {
  VlmEngine engine = VlmEngine::NPU0;
  int device_index = 0;

  // Path to pipeline config.json for this instance
  std::string pipeline_config_path;

  // Nodes (each node has its own config.json)
  std::vector<VlmNodeSpec> nodes;
};

struct GenieVLMConfig {
  std::vector<VlmInstanceConfig> instances;

  enum class LBMode { RoundRobin, LeastQueue } lb_mode = LBMode::RoundRobin;

  size_t max_queue_per_instance = 64;
  bool drop_oldest_on_overflow = false;

  bool strict_fifo_per_instance = true;

  // Callback dispatcher queue settings (optional)
  size_t max_callback_queue = 4096;
  bool drop_callbacks_on_overflow = false; // otherwise blocks producers (workers)

  bool logging_enabled = false;
  GenieLog_Level_t log_level = GENIE_LOG_LEVEL_VERBOSE;
  bool profiling_enabled = false;
};

class GenieVLM {
public:
  explicit GenieVLM(GenieVLMConfig cfg, VlmCallback cb);
  ~GenieVLM();

  GenieVLM(const GenieVLM&) = delete;
  GenieVLM& operator=(const GenieVLM&) = delete;

  // Submit a job. Returns request_id, or 0 if rejected (backpressure).
  uint64_t submit(void *buffer,
                  std::string asistant_description,
                  std::string question,
                  float temperature,
                  float top_p,
                  float seed,
                  float presence_penalty,
                  float frequency_penalty,
                  void *user_data,
                  std::optional<std::chrono::milliseconds> timeout = std::nullopt);

  size_t instance_count() const;

  struct InstanceStats {
    VlmEngine engine;
    size_t queue_size;
    uint64_t processed;
    uint64_t failed;
  };

  std::vector<InstanceStats> stats() const;

private:
  struct VlmRequest {
    uint64_t id = 0;
    std::vector<float> image;
    std::string asistant_description;
    std::string question;
    float temperature;
    float top_p;
    float seed;
    float presence_penalty;
    float frequency_penalty;
    void *user_data;
    std::optional<std::chrono::steady_clock::time_point> deadline;
  };

  struct CallbackEvent {
    uint64_t id = 0;
    bool ok = false;
    VlmResult result;
    std::string err;
  };

  struct Instance;
  struct EngineLogger;
  struct EngineProfiler;

  std::vector<float> preprocess(void *buffer);

  void start();
  void stop();

  void* node_by_name(Instance &inst, std::string name);

  // Init phases (global ordering across all instances)
  void load_all_configs(Instance &inst);
  void create_all_pipelines_and_nodes(Instance &inst);
  void connect_nodes(Instance &inst);
  void set_responce_callback(Instance &inst);
  void free_all_nodes_and_pipelines();

  // Worker and load balancing
  size_t pick_instance_index();
  bool enqueue(size_t idx, VlmRequest req);
  void worker_loop(Instance& inst, size_t instance_index);

  // Callback dispatcher
  void start_callback_dispatcher();
  void stop_callback_dispatcher();
  bool enqueue_callback(CallbackEvent ev);
  void callback_loop();
  void setup_loggers(Instance &inst);
  GenieLog_Handle_t log_handle_for_engine(VlmEngine engine) const;
  static bool needs_logger(VlmEngine engine);
  void setup_profilers(Instance &inst);
  GenieProfile_Handle_t profile_handle_for_engine(VlmEngine engine) const;

private:
  GenieVLMConfig cfg_;
  VlmCallback cb_;
  std::unique_ptr<genie::Context> ctx;

  std::vector<std::unique_ptr<Instance>> instances_;

  // Callback dispatcher state
  std::thread cb_thread_;
  std::atomic<bool> cb_running_{false};
  std::mutex cb_mtx_;
  std::condition_variable cb_cv_;
  std::deque<CallbackEvent> cb_queue_;

  std::atomic<uint64_t> next_request_id_{1};
  std::atomic<size_t> rr_{0};
  std::atomic<bool> stopped_{false};

  std::unordered_map<VlmEngine, std::unique_ptr<EngineLogger>,
    VlmEngineHash> engine_loggers_;

  std::unordered_map<VlmEngine, std::unique_ptr<EngineProfiler>,
    VlmEngineHash> engine_profilers_;
};
