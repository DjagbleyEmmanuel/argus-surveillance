#include "core/detector.h"

#include "core/paths.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace core {

// ---------------------------------------------------------------------------
// MotionDetector
// ---------------------------------------------------------------------------

void MotionDetector::detect(const cv::Mat& frame) {
    motion_boxes_.clear();
    motion_detected_ = false;
    if (frame.empty()) return;

    frame_count_++;
    int h = frame.rows, w = frame.cols;

    double scale = 1.0;
    double inv_scale = 1.0;
    cv::Mat proc_frame = frame;
    if (w > process_width_) {
        scale = static_cast<double>(process_width_) / w;
        int small_h = static_cast<int>(h * scale);
        cv::resize(frame, proc_frame, cv::Size(process_width_, small_h), 0, 0, cv::INTER_NEAREST);
        inv_scale = 1.0 / scale;
    }

    cv::Mat gray;
    cv::cvtColor(proc_frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(15, 15), 0);

    cv::Mat fg_mask;
    bg_subtractor_->apply(gray, fg_mask);

    if (frame_count_ < kLearnFrames) return;  // still learning background

    cv::threshold(fg_mask, fg_mask, 200, 255, cv::THRESH_BINARY);
    cv::morphologyEx(fg_mask, fg_mask, cv::MORPH_OPEN, kernel_);
    cv::morphologyEx(fg_mask, fg_mask, cv::MORPH_CLOSE, kernel_);
    cv::dilate(fg_mask, fg_mask, kernel_, cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(fg_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double scaled_min_area = min_area_ * (1.0 / (inv_scale * inv_scale));
    double now = std::chrono::duration<double>(
                     std::chrono::steady_clock::now().time_since_epoch()).count();

    int idx = 0;
    for (const auto& cnt : contours) {
        double area = cv::contourArea(cnt);
        if (area < scaled_min_area) continue;

        cv::Rect r = cv::boundingRect(cnt);
        int ox = static_cast<int>(r.x * inv_scale);
        int oy = static_cast<int>(r.y * inv_scale);
        int ow = static_cast<int>(r.width * inv_scale);
        int oh = static_cast<int>(r.height * inv_scale);

        int cx = ox + ow / 2, cy = oy + oh / 2;
        motion_trails_.emplace_back(cx, cy, now);

        float conf = static_cast<float>(std::min(area / (scaled_min_area * 5.0), 1.0));

        motion_boxes_.push_back({ox, oy, ow, oh, "motion", conf,
                                 cv::Scalar(0, 240, 255), idx + 1});
        motion_detected_ = true;
        idx++;
    }

    // prune trails older than 2.5s
    motion_trails_.erase(
        std::remove_if(motion_trails_.begin(), motion_trails_.end(),
                       [&](const auto& t) { return (now - std::get<2>(t)) >= 2.5; }),
        motion_trails_.end());
}

void MotionDetector::drawTacticalBox(cv::Mat& frame, int x, int y, int w, int h,
                                     const cv::Scalar& color, const std::string& label,
                                     float conf) {
    if (w <= 0 || h <= 0) return;

    int line_len = std::min(18, std::max(1, w / 4));
    line_len = std::min(line_len, std::max(1, h / 4));
    const int thickness = 2;

    cv::line(frame, {x, y}, {x + line_len, y}, color, thickness);
    cv::line(frame, {x, y}, {x, y + line_len}, color, thickness);
    cv::line(frame, {x + w, y}, {x + w - line_len, y}, color, thickness);
    cv::line(frame, {x + w, y}, {x + w, y + line_len}, color, thickness);
    cv::line(frame, {x, y + h}, {x + line_len, y + h}, color, thickness);
    cv::line(frame, {x, y + h}, {x, y + h - line_len}, color, thickness);
    cv::line(frame, {x + w, y + h}, {x + w - line_len, y + h}, color, thickness);
    cv::line(frame, {x + w, y + h}, {x + w, y + h - line_len}, color, thickness);

    // center crosshair
    int cx = x + w / 2, cy = y + h / 2;
    cv::line(frame, {cx - 4, cy}, {cx + 4, cy}, color, 1);
    cv::line(frame, {cx, cy - 4}, {cx, cy + 4}, color, 1);

    std::string tag;
    if (conf > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[%s] LOCK %d%%", label.c_str(),
                      static_cast<int>(conf * 100));
        tag = buf;
    } else {
        tag = "[" + label + "]";
    }

    int baseline = 0;
    cv::Size text_size = cv::getTextSize(tag, cv::FONT_HERSHEY_SIMPLEX, 0.4, 1, &baseline);
    int th = text_size.height, tw = text_size.width;

    cv::Mat overlay = frame.clone();
    cv::rectangle(overlay, {x, y - th - 6}, {x + tw + 8, y}, cv::Scalar(10, 10, 20), cv::FILLED);
    cv::addWeighted(overlay, 0.6, frame, 0.4, 0, frame);
    cv::rectangle(frame, {x, y - th - 6}, {x + tw + 8, y}, color, 1);
    cv::putText(frame, tag, {x + 4, y - 4}, cv::FONT_HERSHEY_SIMPLEX, 0.4, color, 1);
}

// ---------------------------------------------------------------------------
// HumanDetector
// ---------------------------------------------------------------------------

HumanDetector::HumanDetector(int target_width) : target_width_(target_width) {
    hog_.setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
    hog_ok_ = true;
    // cascade fallback (optional)
    try {
        cascade_ok_ = cascade_.load(core::modelsDir() + "/haarcascade_fullbody.xml");
    } catch (...) {
        cascade_ok_ = false;
    }
}

void HumanDetector::detect(const cv::Mat& frame) {
    humans_.clear();
    if (frame.empty()) return;

    int h = frame.rows, w = frame.cols;
    double scale_x = 1.0, scale_y = 1.0;
    cv::Mat small = frame;
    if (w > target_width_) {
        scale_x = static_cast<double>(w) / target_width_;
        int target_h = static_cast<int>(h / scale_x);
        cv::resize(frame, small, cv::Size(target_width_, target_h), 0, 0, cv::INTER_NEAREST);
        scale_y = static_cast<double>(h) / target_h;
    }

    if (hog_ok_) {
        std::vector<cv::Rect> rects;
        std::vector<double> weights;
        hog_.detectMultiScale(small, rects, weights, 0, cv::Size(12, 12), cv::Size(8, 8), 1.1, 2);
        int idx = 0;
        for (size_t i = 0; i < rects.size(); ++i) {
            if (weights[i] < 0.5) continue;
            cv::Rect r = rects[i];
            if (r.width < 40 || r.height < 80) continue;
            humans_.push_back({static_cast<int>(r.x * scale_x),
                               static_cast<int>(r.y * scale_y),
                               static_cast<int>(r.width * scale_x),
                               static_cast<int>(r.height * scale_y),
                               "human", static_cast<float>(weights[i]),
                               cv::Scalar(0, 255, 102), idx + 100});
            idx++;
        }
        return;
    }

    if (cascade_ok_) {
        cv::Mat gray;
        cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Rect> rects;
        cascade_.detectMultiScale(gray, rects, 1.1, 3, 0, cv::Size(30, 60));
        int idx = 0;
        for (const auto& r : rects) {
            humans_.push_back({static_cast<int>(r.x * scale_x),
                               static_cast<int>(r.y * scale_y),
                               static_cast<int>(r.width * scale_x),
                               static_cast<int>(r.height * scale_y),
                               "human", 0.75f, cv::Scalar(0, 255, 102), idx + 200});
            idx++;
        }
    }
}

// ---------------------------------------------------------------------------
// ProblemDetector
// ---------------------------------------------------------------------------

std::vector<Problem> ProblemDetector::checkFrame(const cv::Mat& frame) {
    std::vector<Problem> problems;
    if (frame.empty()) {
        problems.push_back({"no_frame", "error", "No stream signal"});
        return problems;
    }

    cv::Mat small = frame;
    if (frame.cols > 320) {
        int nh = static_cast<int>(frame.rows * 160.0 / frame.cols);
        cv::resize(frame, small, cv::Size(160, nh), 0, 0, cv::INTER_NEAREST);
    }

    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    double mean_brightness = cv::mean(gray)[0];

    if (mean_brightness < 12)
        problems.push_back({"too_dark", "warning", "Feed Too Dark"});
    else if (mean_brightness > 245)
        problems.push_back({"too_bright", "warning", "Feed Overexposed"});

    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);
    cv::Scalar m, stddev;
    cv::meanStdDev(lap, m, stddev);
    double blur = stddev[0] * stddev[0];  // variance of Laplacian
    if (blur < 8.0)
        problems.push_back({"blurry", "warning", "Lens Out of Focus"});

    return problems;
}

}  // namespace core
