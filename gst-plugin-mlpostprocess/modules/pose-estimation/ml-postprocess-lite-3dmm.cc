/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-lite-3dmm.h"

#include <cfloat>
#include <cmath>

#define DEFAULT_THRESHOLD 0.70

#define ALPHA_ID_SIZE          219
#define ALPHA_EXP_SIZE         39

// List of for the true index for each supported landmark.
std::vector<uint32_t> lmk_idx = {
    662, 660, 659, 669, 750, 700, 583, 560, 561, 608, 966, 712, 708, 707, 557,
    554, 880, 2278, 2275, 2276, 2284, 2360, 2314, 2203, 2181, 2180, 2227, 2553,
    2325, 2321, 2322, 2176, 2175, 1852, 1867, 1877, 1869, 1870, 1848, 1851,
    1846, 1842, 219, 218, 226, 216, 201, 191, 195, 198, 197, 148, 150, 299, 281,
    1796, 1935, 2580, 2003, 1974, 331, 138, 290, 993, 366, 333, 2532, 2498,
    2489, 2519, 3189, 2515, 2517, 2805, 0, 1615, 932, 900, 911, 945, 1229, 930,
    926, 0, 2073, 2104, 398, 470, 443, 1627, 2119, 487, 393, 2030, 2080, 448,
    2130, 506, 498, 2163, 540, 536, 2161, 534, 0, 256
};

static const char* moduleCaps = R"(
{
  "type": "pose-estimation",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 512], [1, 265],
      ]
    },
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, 265],
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(DEFAULT_THRESHOLD) {

}

Module::~Module() {

}

void Module::KeypointTransformCoordinates(Keypoint& keypoint,
                                          const Region& region) {

  keypoint.x = (keypoint.x - region.x) / region.width;
  keypoint.y = (keypoint.y - region.y) / region.height;
}

std::string Module::Caps() {

  return std::string(moduleCaps);
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (json_settings.empty()) {
    LOG (logger_, kError, "No json settings");
    return false;
  }

  auto root = JsonValue::Parse(json_settings);
  if (!root || root->GetType() != JsonType::Object) return false;

  threshold_ = root->GetNumber("confidence");
  threshold_ /= 100.0;
  LOG (logger_, kLog, "Threshold: %f", threshold_);

  std::map<std::string, std::string> settings_db;
  auto dbs = root->GetArray("databases");

  for (auto db : dbs) {
    settings_db.insert({
      db->GetString("name"),
      db->GetString("location")
    });
  }

  if (!LoadDatabases(settings_db))
    return false;

  return true;
}

std::vector<float> Module::LoadBinaryDatabase (const std::string filename,
                                               const uint32_t n_values) {

  std::vector<float> database;
  std::vector<float> contents;

  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file) {
    LOG (logger_, kError, "Failed to open %s", filename.c_str());
    return database;
  }

  std::streamsize size_in_bytes = file.tellg();
  file.seekg(0, std::ios::beg);

  size_t num_floats = size_in_bytes / sizeof(float);
  contents.resize(num_floats);

  if (!file.read(reinterpret_cast<char*>(contents.data()), size_in_bytes)) {
    LOG (logger_, kError, "Failed to read contents from %s", filename.c_str());
    return database;
  }

  database.resize(lmk_idx.size() * 3 * n_values);

  for (uint32_t idx = 0; idx < lmk_idx.size(); idx++) {
    for (uint32_t num = 0; num < n_values; num++) {
      database[(idx * 3 + 0) * n_values + num] =
          contents[(lmk_idx[idx] * 3 + 0) * n_values + num];

      database[(idx * 3 + 1) * n_values + num] =
          contents[(lmk_idx[idx] * 3 + 1) * n_values + num];

      database[(idx * 3 + 2) * n_values + num] =
          contents[(lmk_idx[idx] * 3 + 2) * n_values + num];
    }
  }

  return database;
}

bool Module::LoadDatabases (std::map<std::string, std::string> settings){

  if (settings.size() != 3) {
    LOG (logger_, kError, "Expecting 3 values in labels but got %ld",
        settings.size());
    return false;
  }

  std::vector<std::string> label_entries;

  for (uint32_t idx = 0; idx < settings.size(); idx++) {
    label_entries.push_back(labels_parser_.GetLabel(idx));
  }

  if (settings.find("mean-face") == settings.end()) {
    LOG (logger_, kError, "Missing entry for mean-face");
  }

  std::string location = settings["mean-face"];
  meanface_ = LoadBinaryDatabase(location, 1);

  if (meanface_.empty()) {
    LOG (logger_, kError, "Failed to load meanface");
    return false;
  }

  if (settings.find("shape-basis") == settings.end()) {
    LOG (logger_, kError, "Missing entry for shape-basis");
  }

  location = settings["shape-basis"];
  shapebasis_ = LoadBinaryDatabase (location, ALPHA_ID_SIZE);

  if (shapebasis_.empty()) {
    LOG (logger_, kError, "Failed to load shapebasis");
    return false;
  }

  if (settings.find("blend-shape") == settings.end()) {
    LOG (logger_, kError, "Missing entry for blend-shape");
    return false;
  }

  location = settings["blend-shape"];
  blendshape_ = LoadBinaryDatabase (location, ALPHA_EXP_SIZE);

  if (blendshape_.empty()) {
    LOG (logger_, kError, "Failed to load blendhsape");
    return false;
  }

  return true;
}

bool Module::MatrixMultiplication(Matrix3f& outmatrix,
                                  const Matrix3f& l_matrix,
                                  const Matrix3f& r_matrix) {

  for (size_t row = 0; row < 3; ++row) {
    for (size_t col = 0; col < 3; ++col) {
        float sum = 0.0f;

        for (size_t idx = 0; idx < 3; ++idx) {
            sum += l_matrix[row][idx] * r_matrix[idx][col];
        }

        outmatrix[row][col] = sum;
    }
  }

  return true;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(PoseEstimations)) {
    LOG (logger_, kError, "Unexpected output type!");
    return false;
  }

  // Get region
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  // Get video resolution
  Resolution& resolution =
      std::any_cast<Resolution&>(mlparams["input-tensor-dimensions"]);

  uint32_t source_width = resolution.width;
  uint32_t source_height = resolution.height;

  PoseEstimations& estimations =
    std::any_cast<PoseEstimations&>(output);

  uint vertices_idx = 0;

  if (tensors.size() == 2)
    vertices_idx = 1;

  const float *vertices = reinterpret_cast<const float *>(tensors[vertices_idx].data);

  uint32_t n_vertices = tensors[vertices_idx].dimensions[1];

  float confidence = vertices[n_vertices - 1];

  LOG (logger_, kLog, "Confidence [%f]", confidence);

  if (confidence < threshold_)
    return true;

  float tf = vertices[n_vertices - 2] * 150.0f + 450.0f;
  float ty = vertices[n_vertices - 3] * 60.0f;
  float tx = vertices[n_vertices - 4] * 60.0f;

  LOG (logger_, kLog, "Translation coordinates X[%f] Y[%f] F[%f]", tx, ty, tf);

  float roll = vertices[n_vertices - 5] * M_PI / 2;
  float yaw = vertices[n_vertices - 6] * M_PI / 2;
  float pitch = vertices[n_vertices - 7] * M_PI / 2 + M_PI;

  LOG (logger_, kDebug, "Roll[%f] Yaw[%f] Pitch[%f]", roll, yaw, pitch);

  roll_matrix_[0][0] = cos(-roll);
  roll_matrix_[0][1] = -sin(-roll);
  roll_matrix_[0][2] = 0.0;
  roll_matrix_[1][0] = sin(-roll);
  roll_matrix_[1][1] = cos(-roll);
  roll_matrix_[1][2] = 0.0;
  roll_matrix_[2][0] = 0.0;
  roll_matrix_[2][1] = 0.0;
  roll_matrix_[2][2] = 1.0;

  yaw_matrix_[0][0] = cos(-yaw);
  yaw_matrix_[0][1] = 0.0;
  yaw_matrix_[0][2] = sin(-yaw);
  yaw_matrix_[1][0] = 0.0;
  yaw_matrix_[1][1] = 1.0;
  yaw_matrix_[1][2] = 0.0;
  yaw_matrix_[2][0] = -sin(-yaw);
  yaw_matrix_[2][1] = 0.0;
  yaw_matrix_[2][2] = cos(-yaw);


  pitch_matrix_[0][0] = 1.0;
  pitch_matrix_[0][1] = 0.0;
  pitch_matrix_[0][2] = 0.0;
  pitch_matrix_[1][0] = 0.0;
  pitch_matrix_[1][1] = cos(-pitch);
  pitch_matrix_[1][2] = -sin(-pitch);
  pitch_matrix_[2][0] = 0.0;
  pitch_matrix_[2][1] = sin(-pitch);
  pitch_matrix_[2][2] = cos(-pitch);

  Matrix3f matrix;

  MatrixMultiplication (matrix, pitch_matrix_, roll_matrix_);
  MatrixMultiplication (roll_matrix_, yaw_matrix_, matrix);

  PoseEstimation entry;

  entry.confidence = confidence * 100.0;
  entry.keypoints.resize(lmk_idx.size() / 2);

  for (uint32_t idx = 0; idx < lmk_idx.size(); idx+=2) {
    Keypoint& kp = entry.keypoints[idx / 2];

    uint32_t id = idx * 3;

    float x = meanface_[id];
    float y = meanface_[id + 1];
    float z = meanface_[id + 2];

    for (uint32_t num = 0; num < ALPHA_ID_SIZE; num++) {
      float value = vertices[num] * 3.0f;

      id = idx * 3 * ALPHA_ID_SIZE + num;
      x += value * shapebasis_[id];

      id = (idx * 3 + 1) * ALPHA_ID_SIZE + num;
      y += value * shapebasis_[id];

      id = (idx * 3 + 2) * ALPHA_ID_SIZE + num;
      z += value * shapebasis_[id];
    }

    for (uint32_t num = 0; num < ALPHA_EXP_SIZE; num++) {
      float value = vertices[ALPHA_ID_SIZE + num] * 0.5f + 0.5f;

      id = idx * 3 * ALPHA_EXP_SIZE + num;
      x += value * blendshape_[id];

      id = (idx * 3 + 1) * ALPHA_EXP_SIZE + num;
      y += value * blendshape_[id];

      id = (idx * 3 + 2) * ALPHA_EXP_SIZE + num;
      z += value * blendshape_[id];
    }

    float tmp_x = (x * roll_matrix_[0][0]) +
        (y * roll_matrix_[0][1]) + (z * roll_matrix_[0][2]);
    float tmp_y = (x * roll_matrix_[1][0]) +
        (y * roll_matrix_[1][1]) + (z * roll_matrix_[1][2]);
    float tmp_z = (x * roll_matrix_[2][0]) +
        (y * roll_matrix_[2][1]) + (z * roll_matrix_[2][2]);

    x = tmp_x + tx;
    y = tmp_y + ty;
    z = tmp_z + 500.0F;

    kp.x = (x * tf / 500.0F) + (source_width / 2);
    kp.y = (y * tf / 500.0F) + (source_height / 2);

    KeypointTransformCoordinates(kp, region);

    kp.name = "unknown";
    kp.color = 0xFF0000FF;
    kp.confidence = confidence * 100.0;

    LOG (logger_, kDebug, "Keypoint: %u [%f x %f], confidence %f",
        idx / 2, kp.x, kp.y,
        kp.confidence);
  }

  entry.xtraparams.emplace(Dictionary{
    {"roll", roll},
    {"yaw", yaw},
    {"pitch", pitch}
  });

  estimations.push_back(entry);

  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
