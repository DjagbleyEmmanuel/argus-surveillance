#pragma once

#include <opencv2/opencv.hpp>
#ifdef HAVE_OPENCV_FACE
#include <opencv2/face.hpp>
#endif

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/detector.h"

namespace core {

// LBPH face recognition + Haar detection. Runs on a dedicated side-thread.
// Mirrors core/face_recognizer.py (label map, 100x100 LBPH, confidence gate).
class FaceRecognizer {
public:
    static constexpr int LBPH_CONF_THRESHOLD = 65;

    explicit FaceRecognizer(std::string faces_dir = "faces");

    // Detects faces (Haar) and recognises them (LBPH). target_width mirrors the
    // Python downscale for speed.
    std::vector<DetectedObject> recognizeFaces(const cv::Mat& frame, int target_width = 320);

    bool enrollFace(const std::string& name, const cv::Mat& face_bgr);
    bool enrollFromFrame(const std::string& name, const cv::Mat& frame_bgr,
                         std::string& message);
    bool deletePerson(const std::string& name);
    std::vector<std::string> listEnrolled();
    int sampleCount(const std::string& name);

private:
    void buildRecognizer();  // train LBPH from faces/ dir
    void initCascade();

    std::string faces_dir_;
    cv::CascadeClassifier face_cascade_;
    cv::Ptr<cv::face::LBPHFaceRecognizer> recognizer_;

    std::mutex cascade_lock_;
    std::map<int, std::string> label_map_;
    std::map<std::string, int> name_map_;
    int next_label_ = 0;
    bool trained_ = false;
};

}  // namespace core
