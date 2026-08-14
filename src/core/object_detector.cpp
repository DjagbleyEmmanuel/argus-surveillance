#include "core/object_detector.h"

#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace core {

namespace {

// Deterministic per-class color, like the Python helper.
cv::Scalar classColor(int cid) {
    unsigned h = cid * 2654435761u;
    cv::Scalar c;
    c[0] = 60 + (h & 0xFF) % 160;
    c[1] = 60 + ((h >> 8) & 0xFF) % 160;
    c[2] = 60 + ((h >> 16) & 0xFF) % 160;
    return c;
}

float boxIoU(const cv::Rect& a, const cv::Rect& b) {
    cv::Rect inter = a & b;
    float inter_area = static_cast<float>(inter.area());
    float union_area = static_cast<float>(a.area() + b.area()) - inter_area;
    return union_area > 0 ? inter_area / union_area : 0.0f;
}

}  // namespace

ObjectDetector::ObjectDetector(std::string models_dir, float conf_threshold,
                               float nms_threshold, int input_size,
                               float min_size_percent, float dark_gate)
    : models_dir_(std::move(models_dir)),
      conf_threshold_(conf_threshold),
      nms_threshold_(nms_threshold),
      input_size_(input_size),
      min_size_percent_(min_size_percent),
      dark_gate_(dark_gate) {
    loadModel();
}

bool ObjectDetector::loadModel(const std::string& cfg_path,
                               const std::string& weights_path,
                               const std::string& names_path) {
    releaseNet();
    enabled_ = false;
    is_bundled_ = cfg_path.empty() && weights_path.empty() && names_path.empty();

    std::string cfg = cfg_path.empty() ? models_dir_ + "/yolov4-tiny.cfg" : cfg_path;
    std::string weights = weights_path.empty() ? models_dir_ + "/yolov4-tiny.weights" : weights_path;
    std::string names = names_path.empty() ? models_dir_ + "/coco.names" : names_path;

    model_name_ = QFileInfo(QString::fromStdString(weights)).completeBaseName().toStdString();

    if (!QFileInfo::exists(QString::fromStdString(cfg)) ||
        !QFileInfo::exists(QString::fromStdString(weights)) ||
        !QFileInfo::exists(QString::fromStdString(names))) {
        return false;
    }

    // class names
    classes_.clear();
    {
        std::ifstream f(names);
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty()) classes_.push_back(line);
        }
    }

    try {
        net_ = cv::dnn::readNetFromDarknet(cfg, weights);
        if (net_.empty()) {
            net_ = cv::dnn::readNet(weights, cfg);
        }
        if (net_.empty()) return false;
        // output layer names
        std::vector<cv::String> layer_names = net_.getLayerNames();
        std::vector<int> unconnected = net_.getUnconnectedOutLayers();
        output_layers_.clear();
        for (int u : unconnected) {
            if (u - 1 >= 0 && u - 1 < static_cast<int>(layer_names.size()))
                output_layers_.push_back(layer_names[u - 1]);
        }
        if (output_layers_.empty()) return false;

        // CUDA -> OpenVINO -> CPU
        bool backend_set = false;
        int cuda_count = 0;
        try { cuda_count = cv::cuda::getCudaEnabledDeviceCount(); } catch (...) { cuda_count = 0; }

        if (cuda_count > 0) {
            try {
                net_.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                net_.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                if (verifyBackend()) {
                    backend_name_ = "CUDA GPU";
                    backend_set = true;
                }
            } catch (...) {}
        }
        if (!backend_set) {
            try {
                net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                if (verifyBackend()) {
                    backend_name_ = "CPU";
                    backend_set = true;
                }
            } catch (...) {}
        }
        if (!backend_set) return false;

        enabled_ = true;
        qInfo("YOLO model loaded: %s (%s)", model_name_.c_str(), backend_name_.c_str());
        return true;
    } catch (...) {
        net_ = cv::dnn::Net();
        return false;
    }
}

void ObjectDetector::releaseNet() {
    std::lock_guard<std::mutex> lock(infer_lock_);
    net_ = cv::dnn::Net();  // drops references; OpenCV frees GPU buffers
    enabled_ = false;
    stable_.clear();
}

bool ObjectDetector::verifyBackend() {
    try {
        cv::Mat probe(1, 3, CV_32F, cv::Scalar(0));
        cv::Mat input(1, 3 * input_size_ * input_size_, CV_32F, cv::Scalar(0));
        // use 64x64 like Python to keep the probe cheap
        cv::Mat blob(1, 3 * 64 * 64, CV_32F, cv::Scalar(0));
        blob = blob.reshape(1, {1, 3, 64, 64});
        std::lock_guard<std::mutex> lock(infer_lock_);
        net_.setInput(blob);
        std::vector<cv::Mat> outs;
        net_.forward(outs, output_layers_[0]);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<DetectedObject> ObjectDetector::detect(const cv::Mat& frame) {
    if (!isEnabled() || frame.empty()) return {};

    const int h = frame.rows, w = frame.cols;

    // skip very dark frames (anti-hallucination)
    try {
        cv::Mat gray_small;
        cv::cvtColor(frame, gray_small, cv::COLOR_BGR2GRAY);
        if (cv::mean(gray_small)[0] < dark_gate_) return {};
    } catch (...) {}

    cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0 / 255.0,
                                          cv::Size(input_size_, input_size_),
                                          cv::Scalar(), true, false);

    std::vector<cv::Mat> outputs;
    try {
        std::lock_guard<std::mutex> lock(infer_lock_);
        net_.setInput(blob);
        net_.forward(outputs, output_layers_);
    } catch (...) {
        return {};
    }

    int min_w = std::max(2, static_cast<int>(w * min_size_percent_));
    int min_h = std::max(2, static_cast<int>(h * min_size_percent_));
    double min_area = w * h * (min_size_percent_ * min_size_percent_);

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    for (const cv::Mat& output : outputs) {
        for (int row = 0; row < output.rows; ++row) {
            const float* det = output.ptr<float>(row);
            int num_classes = output.cols - 5;
            int class_id = 0;
            float best = -1.0f;
            for (int c = 0; c < num_classes; ++c) {
                if (det[5 + c] > best) { best = det[5 + c]; class_id = c; }
            }
            float conf = best;
            if (!std::isfinite(conf) || conf < conf_threshold_) continue;

            float cx = det[0] * w, cy = det[1] * h;
            float bw = det[2] * w, bh = det[3] * h;
            if (!std::isfinite(cx) || !std::isfinite(cy) ||
                !std::isfinite(bw) || !std::isfinite(bh)) continue;
            if (bw < min_w || bh < min_h || (bw * bh) < min_area) continue;

            int x = static_cast<int>(cx - bw / 2);
            int y = static_cast<int>(cy - bh / 2);
            int bw_i = static_cast<int>(bw);
            int bh_i = static_cast<int>(bh);
            if (bw_i <= 0 || bh_i <= 0) continue;

            boxes.emplace_back(x, y, bw_i, bh_i);
            confidences.push_back(conf);
            class_ids.push_back(class_id);
        }
    }

    if (boxes.empty()) return {};

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_threshold_, nms_threshold_, indices);
    if (indices.empty()) return {};

    std::vector<DetectedObject> raw;
    for (int i : indices) {
        cv::Rect r = boxes[i];
        int cid = class_ids[i];
        std::string name = (cid >= 0 && cid < static_cast<int>(classes_.size()))
                               ? classes_[cid] : "UNIDENTIFIED OBJECT";
        std::string label = name.empty() ? "UNIDENTIFIED OBJECT" : name;
        std::transform(label.begin(), label.end(), label.begin(), ::toupper);
        cv::Scalar color = (cid >= 0 && cid < static_cast<int>(classes_.size()))
                               ? classColor(cid) : cv::Scalar(180, 180, 180);
        raw.push_back({std::max(0, r.x), std::max(0, r.y), r.width, r.height,
                       label, confidences[i], color, 500 + i});
    }

    return filterStable(std::move(raw));
}

std::vector<DetectedObject> ObjectDetector::filterStable(std::vector<DetectedObject>&& raw) {
    const float iou_thresh = 0.30f;
    std::vector<bool> matched(stable_.size(), false);

    for (const auto& cand : raw) {
        int best_idx = -1;
        float best_iou = 0.0f;
        cv::Rect cand_rect(cand.x, cand.y, cand.w, cand.h);
        for (size_t si = 0; si < stable_.size(); ++si) {
            if (matched[si]) continue;
            if (stable_[si].label != cand.label) continue;
            cv::Rect sr(stable_[si].obj.x, stable_[si].obj.y,
                        stable_[si].obj.w, stable_[si].obj.h);
            float iou = boxIoU(cand_rect, sr);
            if (iou > best_iou) { best_iou = iou; best_idx = static_cast<int>(si); }
        }

        if (best_idx >= 0 && best_iou >= iou_thresh) {
            matched[best_idx] = true;
            StableEntry& st = stable_[best_idx];
            st.hits++;
            st.misses = 0;
            DetectedObject& obj = st.obj;
            const float a = 0.5f;
            obj.x = static_cast<int>(a * cand.x + (1 - a) * obj.x);
            obj.y = static_cast<int>(a * cand.y + (1 - a) * obj.y);
            obj.w = static_cast<int>(a * cand.w + (1 - a) * obj.w);
            obj.h = static_cast<int>(a * cand.h + (1 - a) * obj.h);
            obj.confidence = std::max(obj.confidence, cand.confidence);
        } else {
            stable_.push_back({cand, cand.label, 1, 0});
            matched.push_back(false);
        }
    }

    for (size_t si = 0; si < stable_.size(); ++si) {
        if (!matched[si]) stable_[si].misses++;
    }
    stable_.erase(std::remove_if(stable_.begin(), stable_.end(),
                                 [](const StableEntry& e) { return e.misses > 2; }),
                  stable_.end());

    std::vector<DetectedObject> out;
    for (const auto& st : stable_) {
        if (st.hits >= 2) out.push_back(st.obj);
    }
    return out;
}

}  // namespace core
