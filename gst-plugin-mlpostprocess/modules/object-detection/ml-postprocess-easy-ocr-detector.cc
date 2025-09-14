/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-postprocess-easy-ocr-detector.h"

#include <cmath>
#define INTERSECTION_THRESHOLD 0.05

static const char* moduleCaps = R"(
{
  "type": "object-detection",
  "tensors": [
    {
      "format": ["FLOAT32"],
      "dimensions": [
        [1, [8, 480], [8, 480], [1, 5]],
        [1, [1,32], [8, 480], [8, 480]]
      ]
    }
  ]
}
)";

Module::Module(LogCallback cb)
    : logger_(cb),
      threshold_(INTERSECTION_THRESHOLD),
      detector_args_{} {
}

Module::~Module() {

}

std::string Module::Caps() {

  return std::string(moduleCaps);
}

static inline cv::Mat CreateMatView(const std::vector<float>& score_map,
                                    uint32_t width, uint32_t height) {

  return cv::Mat(height, width, CV_32F, const_cast<float*>(score_map.data()));
}

// Reorder 4 points to clockwise starting from top-left (min x+y)
static inline void orderBoxClockwise(cv::Point2f pts[4]) {

  // Compute sums and index of top-left (min sum)
  float sums[4];
  for (int i = 0; i < 4; ++i)
    sums[i] = pts[i].x + pts[i].y;

  int startidx = 0;
  for (int i = 1; i < 4; ++i)
    if (sums[i] < sums[startidx])
      startidx = i;

  // Rotate so that top-left is first
  std::array<cv::Point2f, 4> tmp;
  for (int i = 0; i < 4; ++i)
    tmp[(i + (4 - startidx)) % 4] = pts[i];

  for (int i = 0; i < 4; ++i)
    pts[i] = tmp[i];
}

void GetDetBoxesCore(const std::vector<float>& text_score_map,
                     const std::vector<float>& link_score_map,
                     uint32_t width, uint32_t height,float text_threshold,
                     float link_threshold,float low_text,
                     std::vector<std::array<cv::Point2f, 4>>& det,
                     cv::Mat& labels) {

  cv::Mat text = CreateMatView(text_score_map, width, height);
  cv::Mat link = CreateMatView(link_score_map, width, height);

  cv::Mat textScore, linkScore;
  cv::threshold(text, textScore, low_text, 1.0, cv::THRESH_BINARY);
  cv::threshold(link, linkScore, link_threshold, 1.0, cv::THRESH_BINARY);

  cv::Mat sumFloat = textScore + linkScore;
  cv::Mat combinedU8;
  cv::compare(sumFloat, 0.0, combinedU8, cv::CMP_GT);

  // Connected components with stats on combined mask
  cv::Mat stats, centroids;
  int nLabels = cv::connectedComponentsWithStats(combinedU8, labels, stats,
                                                 centroids, 4, CV_32S);
  det.clear();
  det.reserve(std::max(0, nLabels - 1));

  for (int k = 1; k < nLabels; ++k) {
    int area = stats.at<int>(k, cv::CC_STAT_AREA);
    if (area < 10) continue;

    // Build mask for this component: (labels == k)
    cv::Mat compMask = (labels == k);

    // Max(text) over component; if < text_threshold, skip
    double maxTextVal = 0.0;
    cv::minMaxLoc(text, nullptr, &maxTextVal, nullptr, nullptr, compMask);
    if (maxTextVal < static_cast<double>(text_threshold))
        continue;

    // segmap: uint8 mask 0/255 for this component
    cv::Mat segmap(height, width, CV_8U, cv::Scalar(0));
    segmap.setTo(255, compMask);

    // Remove link-only area: segmap[link_score==1 & text_score==0] = 0
    cv::Mat textZeroMask;
    cv::compare(textScore, 0.0, textZeroMask, cv::CMP_EQ);
    cv::Mat linkOneMask;
    cv::compare(linkScore, 1.0, linkOneMask, cv::CMP_EQ);
    cv::Mat removeMask = (linkOneMask & textZeroMask);
    segmap.setTo(0, removeMask);

    // Expand region: compute niter
    int x = stats.at<int>(k, cv::CC_STAT_LEFT);
    int y = stats.at<int>(k, cv::CC_STAT_TOP);
    int w = stats.at<int>(k, cv::CC_STAT_WIDTH);
    int h = stats.at<int>(k, cv::CC_STAT_HEIGHT);

    int niter = 0;
    if (w > 0 && h > 0) {
      float ratio = std::sqrt(static_cast<float>(area) * static_cast<float>(std::min(w, h)) /
                             (static_cast<float>(w) * static_cast<float>(h)));
      niter = static_cast<int>(ratio * 2.0f);
    }

    int sx = std::max(0, x - niter);
    int sy = std::max(0, y - niter);
    int ex = std::min(static_cast<int>(width),  x + w + niter);
    int ey = std::min(static_cast<int>(height), y + h + niter);

    int ksize = 2 * niter + 1;
    if (ksize < 1) ksize = 1;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(ksize, ksize));

    cv::Rect roi(sx, sy, ex - sx, ey - sy);
    if (roi.width > 0 && roi.height > 0) {
        cv::Mat segROI = segmap(roi);
        cv::dilate(segROI, segROI, kernel);
    }
    // Gather non-zero points in segmap
    std::vector<cv::Point> nz;
    cv::findNonZero(segmap, nz);
    if (nz.empty())
      continue;

    // Min-area rectangle
    cv::RotatedRect rrect = cv::minAreaRect(nz);
    cv::Point2f boxPts[4];
    rrect.points(boxPts);

    // Align diamond-shape if needed: check aspect ratio of the rotated rect
    float d01 = std::hypot(boxPts[0].x - boxPts[1].x, boxPts[0].y - boxPts[1].y);
    float d12 = std::hypot(boxPts[1].x - boxPts[2].x, boxPts[1].y - boxPts[2].y);
    float longer = std::max(d01, d12);
    float shorter = std::min(d01, d12);
    float box_ratio = longer / (shorter + 1e-5f);

    if (std::fabs(1.0f - box_ratio) <= 0.1f) {
      // Replace with axis-aligned bounding box from nz points
      int l = width, r = 0, t = height, b = 0;
      for (const auto& p : nz) {
        l = std::min(l, p.x);
        r = std::max(r, p.x);
        t = std::min(t, p.y);
        b = std::max(b, p.y);
      }
      boxPts[0] = cv::Point2f(static_cast<float>(l), static_cast<float>(t));
      boxPts[1] = cv::Point2f(static_cast<float>(r), static_cast<float>(t));
      boxPts[2] = cv::Point2f(static_cast<float>(r), static_cast<float>(b));
      boxPts[3] = cv::Point2f(static_cast<float>(l), static_cast<float>(b));
    }
    // Make clockwise order with top-left first
    orderBoxClockwise(boxPts);

    std::array<cv::Point2f, 4> box = { boxPts[0], boxPts[1], boxPts[2], boxPts[3] };
    det.push_back(box);
  }
  return;
}

bool Module::Configure(const std::string& labels_file,
                       const std::string& json_settings) {

  if (!labels_parser_.LoadFromFile(labels_file)) {
    LOG (logger_, kError, "Failed to parse labels");
    return false;
  }

  if (!json_settings.empty()) {
    auto root = JsonValue::Parse(json_settings);
    if (!root || root->GetType() != JsonType::Object) return false;

    threshold_ = root->GetNumber("confidence");
    threshold_ /= 100.0;
    LOG (logger_, kLog, "Threshold: %f", threshold_);

    detector_args_.text_threshold = static_cast<float>(root->GetNumber("text_threshold"));
    LOG (logger_, kLog, "Text threshold: %f", detector_args_.text_threshold);

    detector_args_.link_threshold = static_cast<float>(root->GetNumber("link_threshold"));
    LOG (logger_, kLog, "Link threshold: %f", detector_args_.link_threshold);

    detector_args_.low_text = static_cast<float>(root->GetNumber("low_text"));
    LOG (logger_, kLog, "Low text: %f", detector_args_.low_text);
  }

  return true;
}


float Module::IntersectionScore(ObjectDetection &l_box, ObjectDetection &r_box) {

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  float width = fmin (l_box.right, r_box.right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= fmax (l_box.left, r_box.left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  float height = fmin (l_box.bottom, r_box.bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= fmax (l_box.top, r_box.top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F)
    return 0.0F;

  // Calculate intersection area.
  float intersection = width * height;

  // Calculate the area of the 2 objects.
  float l_area = (l_box.right - l_box.left) * (l_box.bottom - l_box.top);
  float r_area = (r_box.right - r_box.left) * (r_box.bottom - r_box.top);

  // Intersection over Union score.
  return intersection / (l_area + r_area - intersection);
}


static inline void UnionInto(ObjectDetection& a, const ObjectDetection& b) {

  a.left   = std::min(a.left,   b.left);
  a.top    = std::min(a.top,    b.top);
  a.right  = std::max(a.right,  b.right);
  a.bottom = std::max(a.bottom, b.bottom);
}


void Module::MergeOverlappingBoxes(ObjectDetection &l_box,
                                   ObjectDetections &boxes) {

  for (uint32_t idx = 0; idx < boxes.size();) {
    ObjectDetection r_box = boxes[idx];

    // If labels do not match, continue with next list entry.
    if (l_box.name != r_box.name) {
      idx++;
      continue;
    }

    double score = IntersectionScore(l_box, r_box);

    // If the score is below the threshold, continue with next list entry.
    if (score >= INTERSECTION_THRESHOLD) {
      UnionInto (l_box, r_box);
      boxes.erase(boxes.begin() + idx);
    } else {
      idx++;
    }
  }
}

void Module::GetDetBoxes(const std::vector<float>& text_score_map,
                         const std::vector<float>& link_score_map, uint32_t width,
                         uint32_t height, const float text_threshold,
                         const float link_threshold,const float low_text,
                         std::vector<Poly>& boxes) {

  std::vector<std::array<cv::Point2f, 4>> det;
  cv::Mat labels;

  GetDetBoxesCore (text_score_map, link_score_map, width, height, text_threshold,
                    link_threshold, low_text, det, labels);

  boxes.clear();
  boxes.reserve(det.size());

  for (const auto& b : det) {
    Poly pb = { b[0], b[1], b[2], b[3] };
    boxes.push_back(pb);
  }
}

void Module::TransformDimensions(ObjectDetection &box,
                                 const Region& region) {

  box.top = (box.top - region.y) / region.height;
  box.bottom = (box.bottom - region.y) / region.height;
  box.left = (box.left - region.x) / region.width;
  box.right = (box.right - region.x) / region.width;
}

std::vector<ObjectDetection> Module::PolygonsToBoxes(const std::vector<Poly>& boxes) {

  std::vector<ObjectDetection> detections;
  detections.reserve(boxes.size());
  for (const auto& poly : boxes) {
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();

    for (const auto& pt : poly) {
      if (pt.x < minX) minX = pt.x;
      if (pt.y < minY) minY = pt.y;
      if (pt.x > maxX) maxX = pt.x;
      if (pt.y > maxY) maxY = pt.y;
    }

    ObjectDetection det;
    det.name = labels_parser_.GetLabel(0);
    det.color = labels_parser_.GetColor(0);
    det.left = minX;
    det.right = maxX;
    det.top = minY;
    det.bottom = maxY;
    detections.push_back(det);
  }
  return detections;
}

bool Module::Process(const Tensors& tensors, Dictionary& mlparams,
                     std::any& output) {

  if (output.type() != typeid(ObjectDetections)) {
    LOG (logger_, kError, "Unexpected output type!");
    return false;
  }

  if (tensors.empty() || tensors[0].dimensions.size() < 4) {
    LOG(logger_, kError, "Invalid tensor dimensions!");
    return false;
  }

  std::vector<Poly> boxes;

  ObjectDetections& detections =
      std::any_cast<ObjectDetections&>(output);

  // Copy info
  Region& region =
      std::any_cast<Region&>(mlparams["input-tensor-region"]);

  uint32_t n_rows = tensors[0].dimensions[1];
  uint32_t n_cols = tensors[0].dimensions[2];
  uint32_t channels = tensors[0].dimensions[3];
  uint32_t grid_size = n_rows * n_cols;
  const float *output_tensor = reinterpret_cast<const float *>(tensors[0].data);
  std::vector<float> text_score_map(grid_size);
  std::vector<float> link_score_map(grid_size);
  std::vector<ObjectDetection> entries;

  for (uint32_t h = 0; h < n_rows; ++h) {
    for (uint32_t w = 0; w < n_cols; ++w) {
      int base_idx = (h * n_cols + w) * channels;
      text_score_map[h * n_cols + w] = output_tensor[base_idx + 0];
      link_score_map[h * n_cols + w] = output_tensor[base_idx + 1];
    }
  }

  GetDetBoxes(text_score_map, link_score_map, n_cols, n_rows,
              detector_args_.text_threshold, detector_args_.link_threshold,
              detector_args_.low_text, boxes);
  const float stride = 2.0f;

  for (auto& poly : boxes) {
    for (auto& p : poly) {
      p.x *= stride;
      p.y *= stride;
    }
  }

  entries = PolygonsToBoxes(boxes);
  for (auto& det : entries) {
    TransformDimensions(det, region);
    MergeOverlappingBoxes(det, detections);
    LOG(logger_, kTrace, "Label: %s Box[%f, %f, %f, %f]", det.name.c_str(),
        det.top, det.left, det.bottom, det.right);
    detections.push_back(det);
  }
  return true;
}

IModule* NewModule(LogCallback logger) {
  return new Module(logger);
}
