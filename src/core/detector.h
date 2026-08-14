#pragma once

#include <opencv2/opencv.hpp>

#include <string>
#include <tuple>
#include <vector>

namespace core {

// Mirrors Python's DetectedObject dataclass.
struct DetectedObject {
    int x = 0, y = 0, w = 0, h = 0;
    std::string label = "object";
    float confidence = 0.0f;
    cv::Scalar color = cv::Scalar(0, 255, 0);
    int track_id = 0;
};

struct Problem {
    std::string type;
    std::string severity;   // "error" | "warning"
    std::string message;
};

// MOG2 background subtraction + morphological cleanup + contour extraction.
// Runs in the worker main loop (fast, lightweight). Mirrors core/detector.py.
class MotionDetector {
public:
    explicit MotionDetector(int threshold = 5000, int min_area = 400,
                            int process_width = 480)
        : threshold_(threshold), min_area_(min_area), process_width_(process_width) {
        bg_subtractor_ = cv::createBackgroundSubtractorMOG2(300, 25, false);
        kernel_ = cv::getStructuringElement(cv::MORPH_ELLIPSE, {5, 5});
    }

    // Returns motion_boxes (scaled back to original frame coords).
    void detect(const cv::Mat& frame);

    const std::vector<DetectedObject>& motionBoxes() const { return motion_boxes_; }
    bool motionDetected() const { return motion_detected_; }
    void setThreshold(int t) { threshold_ = t; }

    // Sci-Fi corner-bracket reticle + crosshair + label tag (draws in place).
    static void drawTacticalBox(cv::Mat& frame, int x, int y, int w, int h,
                                const cv::Scalar& color, const std::string& label,
                                float conf = 0.0f);

private:
    int threshold_;
    int min_area_;
    int process_width_;
    int frame_count_ = 0;
    static constexpr int kLearnFrames = 20;

    cv::Ptr<cv::BackgroundSubtractorMOG2> bg_subtractor_;
    cv::Mat kernel_;
    bool motion_detected_ = false;
    std::vector<DetectedObject> motion_boxes_;
    std::vector<std::tuple<int, int, double>> motion_trails_;
};

// HOG people detector w/ Haar cascade fallback. Runs on a dedicated side-thread.
class HumanDetector {
public:
    explicit HumanDetector(int target_width = 300);

    void detect(const cv::Mat& frame);
    const std::vector<DetectedObject>& humans() const { return humans_; }

private:
    int target_width_;
    cv::HOGDescriptor hog_;
    bool hog_ok_ = false;
    cv::CascadeClassifier cascade_;
    bool cascade_ok_ = false;
    std::vector<DetectedObject> humans_;
};

// Frame health checks (brightness / focus). Runs every N frames.
class ProblemDetector {
public:
    std::vector<Problem> checkFrame(const cv::Mat& frame);
};

}  // namespace core
