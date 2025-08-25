/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <any>
#include <unordered_map>
#include <optional>
#include <functional>
#include <cstdarg>

/** The C interface function that a submodule must expose:
 *
 * IModule* NewModule(LogCallback logger) {
 *   return new Module(logger);
 * }
 */
#define ML_POST_PROCESS_MODULE_NEW_FUNC "NewModule"

/** LOG
 *
 * Definitions of the macro for logging.
 */
#define LOG(logger, level, fmt, ...) \
  if (logger) { \
    char buff[1024]; \
    snprintf(buff, sizeof(buff), fmt, ##__VA_ARGS__); \
    logger(level, buff); \
  }

/** LogCallback
 *
 * Definitions of the callback function for logging.
 */
using LogCallback =
    std::function<void(uint32_t level, const char* msg)>;

/** Log level
 *
 * Definitions of possible log levels.
 */
enum LogLevel : uint32_t {
  kError,
  kWarning,
  kInfo,
  kDebug,
  kTrace,
  kLog,
};

/** VideoFormat
 *
 * Definitions of possible RGB color formats.
 */
enum VideoFormat : uint32_t {
  kGRAY8,

  kRGB888,
  kBGR888,

  kARGB8888,
  kXRGB8888,

  kABGR8888,
  kXBGR8888,

  kRGBA8888,
  kRGBX8888,

  kBGRA8888,
  kBGRX8888,
};

/** Plane
 * @data: Pointer to the plane data
 * @offset: offset of the plane
 * @stride: stride of the plane
 * @size: Size of the plane in bytes.
 *
 * Encapsulates information for a image plane.
 */
struct Plane {
  uint8_t* data;
  uint32_t offset;
  uint32_t stride;
  size_t   size;

  Plane()
      : data(nullptr), offset(0), stride(0), size(0) {};

  Plane(uint8_t* data, uint32_t offset, uint32_t stride, size_t size)
      : data(data), offset(offset), stride(stride), size(size) {};
};

// Variable vector of video plane structures for an image.
typedef std::vector<Plane> Planes;

/** VideoFrame:
 * @width: Width in pixels.
 * @height: Height in pixels.
 * @bits: The number of bits used to pack data items.
 * @n_components: The number of components in the video format.
 * @format: Color format.
 * @planes: Plane specific information.
 *
 * Encapsulates image data along with information describing its properties.
 */
struct VideoFrame {
  uint32_t    width;
  uint32_t    height;
  uint32_t    bits;
  uint32_t    n_components;
  VideoFormat format;
  Planes      planes;

  VideoFrame()
      : width(0), height(0), bits(0), n_components(0),
        format(VideoFormat::kGRAY8), planes(0) {};

  VideoFrame(uint32_t width, uint32_t height, uint32_t bits, uint32_t n_components,
      VideoFormat format, Planes& planes)
      : width(width), height(height), bits(bits), n_components(n_components),
        format(format), planes(planes) {};
};

/** Region:
 * @x: X axis coordinate of the top-left corner on pixels.
 * @y: Y axis coordinate of the top-left corner on pixels.
 * @width: Width in pixels
 * @height: Height in pixels
 *
 * Encapsulates image data along with information describing its properties.
 */
struct Region {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;

  Region()
      : x(0), y(0), width(0), height(0) {};

  Region(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
      : x(x), y(y),width(width), height(height) {};
};

/** Resolution:
 * @width: Width in pixels
 * @height: Height in pixels
 *
 * Encapsulates image size with information describing its properties.
 */
struct Resolution {
  uint32_t width;
  uint32_t height;

  Resolution()
      : width(0), height(0) {};

  Resolution(uint32_t width, uint32_t height)
      : width(width), height(height) {};
};

/** TensorType
 *
 * Definitions of possible tensor types.
 */
enum TensorType : uint32_t {
  kInt8,
  kUint8,
  kInt32,
  kUint32,
  kFloat16,
  kFloat32,
};

/** Tensor:
 * @type: Type of the tensor.
 * @name: Tensor name.
 * @dimensions: Vector with the tensor dimensions.
 * @data: Pointer to the mapped tensor data.
 *
 * Encapsulates tensor data along with information describing its properties.
 */
struct Tensor {
  TensorType            type;
  std::string           name;
  std::vector<uint32_t> dimensions;
  void*                 data;

  Tensor()
      : type(TensorType::kUint8), dimensions(0), data(nullptr) {};

  Tensor(TensorType type, std::string name,
         std::vector<uint32_t>& dimensions, void* data)
      : type(type), name(name), dimensions(dimensions), data(data) {};
};

// Variable vector of tensor structures.
typedef std::vector<Tensor> Tensors;

// Map between a parameter and its value: <parameter name, value>
typedef std::unordered_map<std::string, std::any> Dictionary;

/** TextGeneration:
 * @contents: The generated text.
 * @confidence: Percentage certainty that the prediction is accurate.
 * @color: Optional color that is associated with this prediction.
 * @xtraparams: Optional additional parameters in #Dictionary which the user
 *              can export from the submodule and be passed downstream.
 *
 * Information describing prediction result from text generation models.
 * All fields are mandatory and need to be filled by the submodule.
 */
struct TextGeneration {
  std::string               contents;
  float                     confidence;

  std::optional<uint32_t>   color;
  std::optional<Dictionary> xtraparams;

  TextGeneration()
      : contents(), confidence(0) {};

  TextGeneration(std::string& contents, float confidence)
      : contents(contents), confidence(confidence) {};
};

// Variable vector of text generation structures.
typedef std::vector<TextGeneration> TextGenerations;

// Variable vector of Detection Entries.
// Information describing a group of prediction results from the same tensor batch.
typedef std::vector<TextGenerations> TextPrediction;

/** AudioClassification:
 * @name: Name of the class prediction.
 * @confidence: Percentage certainty that the prediction is accurate.
 * @color: Optional color that is associated with this prediction.
 * @xtraparams: Optional additional parameters in #Dictionary which the user
 *              can export from the submodule and be passed downstream.
 *
 * Information describing prediction result from classification models.
 * All fields are mandatory and need to be filled by the submodule.
 */
struct AudioClassification {
  std::string               name;
  float                     confidence;

  std::optional<uint32_t>   color;
  std::optional<Dictionary> xtraparams;

  AudioClassification()
      : name(), confidence(0) {};

  AudioClassification(std::string name, float confidence)
      : name(name), confidence(confidence) {};
};

// Variable vector of classification structures.
typedef std::vector<AudioClassification> AudioClassifications;

// Variable vector of Detection Entries.
// Information describing a group of prediction results from the same tensor batch.
typedef std::vector<AudioClassifications> AudioClassPrediction;

/** ImageClassification:
 * @name: Name of the class prediction.
 * @confidence: Percentage certainty that the prediction is accurate.
 * @color: Optional color that is associated with this prediction.
 * @xtraparams: Optional additional parameters in #Dictionary which the user
 *              can export from the submodule and be passed downstream.
 *
 * Information describing prediction result from classification models.
 * All fields are mandatory and need to be filled by the submodule.
 */
struct ImageClassification {
  std::string               name;
  float                     confidence;

  std::optional<uint32_t>   color;
  std::optional<Dictionary> xtraparams;

  ImageClassification()
      : name(), confidence(0) {};

  ImageClassification(std::string name, float confidence)
      : name(name), confidence(confidence) {};
};

// Variable vector of classification structures.
typedef std::vector<ImageClassification> ImageClassifications;

// Variable vector of Detection Entries.
// Information describing a group of prediction results from the same tensor batch.
typedef std::vector<ImageClassifications> ImageClassPrediction;

/** Keypoint:
 * @name: Name of the keypoint,
 * @x: X axis coordinate of the keypoint.
 * @y: Y axis coordinate of the keypoint.
 * @confidence: The confidence for the keypoint.
 * @color: Optional color that is associated with this keypoint.
 *
 * Information describing keypoint location and confidence score.
 *
 * The fields x and y must be set in (0.0 to 1.0) relative coordinate system.
 */
struct Keypoint {
  std::string             name;
  float                   x;
  float                   y;
  float                   confidence;

  std::optional<uint32_t> color;

  Keypoint()
      : name(), x(0), y(0), confidence(0.0) {};

  Keypoint(std::string name, float x, float y, float confidence)
      : name(name), x(x), y(y), confidence(confidence) {};
};

// Variable vector of keypoint structures.
typedef std::vector<Keypoint> Keypoints;

/** KeypointLink:
 * @l_kp: The first keypoint in the #Keypoints vector.
 * @r_kp: The second keypoint in the #Keypoints vector.
 * @color: Optional color that is associated with this keypoint link.
 *
 * Information describing a link between two keypoints.
 */
struct KeypointLink {
  Keypoint                l_kp;
  Keypoint                r_kp;

  std::optional<uint32_t> color;

  KeypointLink(Keypoint l_kp, Keypoint r_kp)
      : l_kp(l_kp), r_kp(r_kp) {};
};

// Variable vector of keypoint link structures.
typedef std::vector<KeypointLink> KeypointLinks;

/**
 * PoseEstimation:
 * @name: Name of the prediction.
 * @confidence: The overall confidence for the estimated pose.
 * @keypoints: Variable vector of #Keypoint.
 * @links: Optional variable vector of #KeypointLink.
 * @xtraparams: Optional additional parameters in #Dictionary which the user
 *              can export from the submodule and be passed downstream.
 *
 * Information describing prediction result from pose estimation models.
 * All fields are mandatory and need to be filled by the submodule.
 */
struct PoseEstimation {
  std::string                  name;
  float                        confidence;
  Keypoints                    keypoints;

  std::optional<KeypointLinks> links;
  std::optional<Dictionary>    xtraparams;

  PoseEstimation()
      : name(), confidence(0), keypoints() {};

  PoseEstimation(std::string name, float confidence, Keypoints& keypoints)
      : name(name), confidence(confidence), keypoints(keypoints) {};
};

// Variable vector of pose estimation structures.
typedef std::vector<PoseEstimation> PoseEstimations;

// Variable vector of Detection Entries.
// Information describing a group of prediction results from the same tensor batch.
typedef std::vector<PoseEstimations> PosePrediction;

/** ObjectDetection:
 * @name: Name of the prediction.
 * @confidence: Percentage certainty that the prediction is accurate.
 * @left: X axis coordinate of upper-left corner.
 * @top: Y axis coordinate of upper-left corner.
 * @right: X axis coordinate of lower-right corner.
 * @bottom: Y axis coordinate of lower-right corner.
 * @color: Optional color that is associated with this prediction.
 * @landmarks: Optional variable vector of #Keypoint.
 * @xtraparams: Optional additional parameters in #Dictionary which the user
 *              can export from the submodule and be passed downstream.
 *
 * Information describing prediction result from object detection models.
 * All fields are mandatory and need to be filled by the submodule.
 *
 * The fields top, left, bottom and right must be set in (0.0 to 1.0) relative
 * coordinate system.
 */
struct ObjectDetection {
  std::string               name;
  float                     confidence;
  float                     left;
  float                     top;
  float                     right;
  float                     bottom;

  std::optional<uint32_t>   color;
  std::optional<Keypoints>  landmarks;
  std::optional<Dictionary> xtraparams;

  ObjectDetection()
      : name(), confidence(0), left(0), top(0), right(0), bottom(0) {};

  ObjectDetection(std::string name, float confidence, float left, float top,
                      float right, float bottom)
      : name(name),
        confidence(confidence),
        left(left),
        top(top),
        right(right),
        bottom(bottom) {};
};

// Variable vector of object detection structures.
typedef std::vector<ObjectDetection> ObjectDetections;

// Variable vector of Detection Entries.
// Information describing a group of prediction results from the same tensor batch.
typedef std::vector<ObjectDetections> DetectionPrediction;

/** IModule
 *
 * ML post-processing module interface.
 **/
class IModule {
 public:
  virtual ~IModule() {};

  /** Caps.
   *
   * Retrieve the capabilities supported by this module.
   * Contains a JSON string with all suppoted types and dimensions
   * by the post processing module.
   *
   * return: JSON string to the module capabilities.
   */
  virtual std::string Caps() = 0;

  /** Configure
   * @labels_file: Labels file path for post-processing.
   * @json_settings: Module specific settings.
   *
   * Configure the module with a set of settings.
   *
   * return: true on success or false on failure
   */
  virtual bool Configure(const std::string& labels_file,
                         const std::string& json_settings) = 0;

  /** Process
   * @tensors: Tensor objects that need processing.
   * @mlparams: Additional parameters that may be needed for the processing
   *            of the tensors. May not be applicable to all submodules.
   *    Image Classification:
   *    Audio Classification:
   *    Tensor Generation:
   *        - None. No additoional parameters are needed.
   *    Object Detection:
   *    Pose Estimation:
   *    Image Segmentation:
   *    Super Resolution:
   *        - 'input-tensor-dimensions': Resolution
   *          Dimensions of the model input tensor, from which the passed
   *          #Tensors for processing, were produced.
   *        - 'input-tensor-region': Region
   *          Position and dimensions of the rectangle in the input tensor
   *          that was filled with data.
   *    Text Generation:
   *        - 'input-context-index': uint32_t
   *          Optional index from in the input tensor from which the text
   *          context tokens begin. Used to offset the indices for extraction
   *          based models.
   *        - 'input-context-tokens': std::vector<std::string>
   *          Optional list with tokenized words of the text context given
   *          as input to the model and required for extraction based models.
   * @output: Module specific output:
   *    Image Classification: Variable vector of ClassPrediction.
   *    Audio Classification: Variable vector of AudioClassPrediction.
   *    Object Detection: Variable vector of DetectionPrediction.
   *    Pose Estimation: Variable vector of PosePrediction.
   *    Image Segmentation: Image mask represented by VideoFrame.
   *    Super Resolution: Scaled image represented by VideoFrame.
   *    Text Generation: Variable vector of TextPrediction.
   *    Tensor Generation: Tensor output represented by Tensors.
   *
   * Process incoming buffer containing result tensors and converts that
   * information into a plugin specific output.
   *
   * return: true on success or false on failure
   */
  virtual bool Process(const Tensors& tensors, Dictionary& mlparams,
                       std::any& output) = 0;
};

/* NewModule
 *
 * Main API for loading an instance of Post processing module.
 *
 * return: Pointer to new Post processing module instance.
 **/
extern "C" IModule* NewModule(LogCallback logger);
