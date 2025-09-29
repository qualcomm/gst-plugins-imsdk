/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-tensor.h"

#include <cstring>

static const char* moduleCaps = R"(
{
  "type": "tensor",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 63],
        [1, 1],
        [1, 1],
        [1, 63]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb) {

}

Module::~Module() {

}

uint32_t Module::GetTensorTypeSize (const TensorType type) {
  uint32_t tensor_type_size = 0;

  switch (type) {
    case kInt8:
      tensor_type_size = sizeof(int8_t);
      break;
    case kUint8:
      tensor_type_size = sizeof(uint8_t);
      break;
    case kInt32:
      tensor_type_size = sizeof(int32_t);
      break;
    case kUint32:
      tensor_type_size = sizeof(uint32_t);
      break;
    case kFloat16:
      tensor_type_size = sizeof(float) / 2;
      break;
    case kFloat32:
      tensor_type_size = sizeof(float);
      break;
    default:
      LOG (logger_, kError, "Invalid tensor type!");
      break;
    }

  return tensor_type_size;
}

bool Module::ValidateTensorSize (const Tensor& l_tensor,
                                 const Tensor& r_tensor) {
  uint32_t l_size = 1, r_size = 1;

  for (const uint32_t dim : l_tensor.dimensions)
    l_size *= dim;

  for (const uint32_t dim : r_tensor.dimensions)
    r_size *= dim;

  return l_size == r_size;
}

std::string Module::Caps() {
  return std::string(moduleCaps);
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(Tensors)) {
    LOG (logger_, kError, "Unexpected type passed!");
    return false;
  }

  if (tensors.size() != 4) {
    LOG (logger_, kError, "Postprocess input tensors must be 4! "
        "4 != %lu", tensors.size());
    return false;
  }

  if (tensors[0].dimensions[1] != tensors[3].dimensions[1]) {
    LOG (logger_, kError, "Second dimensions of first and third tensor must be "
        "equal: %u != %u", tensors[0].dimensions[1], tensors[3].dimensions[1]);
    return false;
  }

  Tensors& output_tensors = std::any_cast<Tensors&>(output);

  if (output_tensors.size() != 3) {
    LOG (logger_, kError, "Postprocess must output 3 tensors! "
        "3 != %lu", output_tensors.size());
    return false;
  }

  bool success = ValidateTensorSize (tensors[0], output_tensors[0]);
  success &= ValidateTensorSize (tensors[2], output_tensors[1]);
  success &= ValidateTensorSize (tensors[3], output_tensors[2]);

  if (!success) {
    LOG (logger_, kError, "Input and Output tensors size missmatch!");
    return false;
  }

  uint32_t num_coordinates = output_tensors[0].dimensions[2];
  uint32_t num_keypoints = output_tensors[0].dimensions[1];
  LOG (logger_, kLog, "Coordinates per point: %u "
      " Number of keypoints: %u", num_coordinates, num_keypoints);

  // coordinates in image coordinates
  const float* coordinates = reinterpret_cast<const float*>(tensors[0].data);
  // Indicate left or right hand.
  // If value is closer to 0 -> left hand. If value is closer to 1 -> right hand.
  const float* handedness = reinterpret_cast<const float*>(tensors[2].data);
  // coordinates in world coordinates
  const float* world_coordiantes =
      reinterpret_cast<const float*>(tensors[3].data);

  uint32_t tensor_type_size = GetTensorTypeSize (output_tensors[0].type);
  memcpy (output_tensors[0].data, coordinates,
      num_keypoints * num_coordinates * tensor_type_size);

  memcpy (output_tensors[1].data, handedness, tensor_type_size);

  memcpy (output_tensors[2].data, world_coordiantes,
      num_keypoints * num_coordinates * tensor_type_size);

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
