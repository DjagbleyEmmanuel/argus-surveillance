#pragma once

#include <opencv2/opencv.hpp>

#include <mutex>
#include <string>
#include <vector>

#include "core/detector.h"

namespace core {

// YOLOv4 (-tiny) object detector via cv::dnn. 80 COCO classes.
//
// Port notes vs. Python:
//  - cv::dnn::Net is NOT thread-safe -> one std::mutex serialises inference.
//  - Temporal stability filter (hits>=2, misses<=2) suppresses flicker.
//  - dark_gate skips frames that are too dark to avoid hallucinated detections.
//  - CUDA selected at load if cv::cuda reports a device; verified with a tiny
//    forward pass; falls back to OpenVINO then OpenCV CPU.
class ObjectDetector {
public:
    ObjectDetector(std::string models_dir = "assets/models",
                   float conf_threshold = 0.50f, float nms_threshold = 0.45f,
                   int input_size = 288, float min_size_percent = 0.02f,
                   float dark_gate = 20.0f);

    // Loads bundled yolov4.cfg/.weights/coco.names (or a custom set).
    bool loadModel(const std::string& cfg_path = "", const std::string& weights_path = "",
                   const std::string& names_path = "");
    void releaseNet();  // frees the net (CUDA VRAM) before a replacement loads

    std::vector<DetectedObject> detect(const cv::Mat& frame);

    // ---- tuning ----
    void setConfThreshold(float v) { conf_threshold_ = v; }
    void setNmsThreshold(float v) { nms_threshold_ = v; }
    void setInputSize(int v) { input_size_ = v; }
    void setMinSizePercent(float v) { min_size_percent_ = v; }
    void setDarkGate(float v) { dark_gate_ = v; }

    float confThreshold() const { return conf_threshold_; }
    float nmsThreshold() const { return nms_threshold_; }
    int inputSize() const { return input_size_; }
    float minSizePercent() const { return min_size_percent_; }
    float darkGate() const { return dark_gate_; }

    bool isEnabled() const { return enabled_ && !net_.empty(); }
    bool isBundled() const { return is_bundled_; }
    const std::string& modelName() const { return model_name_; }
    const std::vector<std::string>& classes() const { return classes_; }
    std::string backendName() const { return backend_name_; }

private:
    bool verifyBackend();  // tiny forward pass to confirm CUDA works
    std::vector<DetectedObject> filterStable(std::vector<DetectedObject>&& raw);

    cv::dnn::Net net_;
    std::mutex infer_lock_;  // serialises inference across camera threads

    std::vector<std::string> classes_;
    std::vector<std::string> output_layers_;

    std::string models_dir_;
    std::string model_name_ = "yolov4-tiny";
    std::string backend_name_ = "CPU";

    float conf_threshold_;
    float nms_threshold_;
    int input_size_;
    float min_size_percent_;
    float dark_gate_;

    bool enabled_ = false;
    bool is_bundled_ = true;

    struct StableEntry {
        DetectedObject obj;
        std::string label;
        int hits = 0, misses = 0;
    };
    std::vector<StableEntry> stable_;
};

}  // namespace core
