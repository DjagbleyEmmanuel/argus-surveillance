#include "core/detection_worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace core {

using namespace std::chrono_literals;

DetectionWorker::DetectionWorker(std::shared_ptr<CameraSource> camera,
                                 std::string worker_id, bool recording_enabled,
                                 std::shared_ptr<FaceRecognizer> face_recognizer,
                                 std::shared_ptr<ObjectDetector> object_detector)
    : camera_(std::move(camera)),
      worker_id_(std::move(worker_id)),
      recording_enabled_(recording_enabled),
      face_recognizer_(std::move(face_recognizer)),
      object_detector_(std::move(object_detector)) {}

DetectionWorker::~DetectionWorker() { stop(); }

void DetectionWorker::start() {
    if (running_.exchange(true)) return;
    stop_event_ = false;

    motion_detector_ = std::make_unique<MotionDetector>(motion_threshold_.load());
    problem_detector_ = std::make_unique<ProblemDetector>();
    if (human_enabled_) human_detector_ = std::make_unique<HumanDetector>(300);
    if (recording_enabled_)
        recorder_ = std::make_unique<Recorder>(camera_->name());

    main_thread_ = std::thread(&DetectionWorker::mainLoop, this);
    hog_thread_ = std::thread(&DetectionWorker::hogLoop, this);
    yolo_thread_ = std::thread(&DetectionWorker::yoloLoop, this);
    face_thread_ = std::thread(&DetectionWorker::faceLoop, this);
}

void DetectionWorker::stop() {
    if (!running_.load()) {
        // still join any lingering threads
        if (main_thread_.joinable()) main_thread_.join();
        if (hog_thread_.joinable()) hog_thread_.join();
        if (yolo_thread_.joinable()) yolo_thread_.join();
        if (face_thread_.joinable()) face_thread_.join();
        return;
    }
    running_ = false;
    stop_event_ = true;
    if (main_thread_.joinable()) main_thread_.join();
    if (hog_thread_.joinable()) hog_thread_.join();
    if (yolo_thread_.joinable()) yolo_thread_.join();
    if (face_thread_.joinable()) face_thread_.join();
    if (recorder_) recorder_->stop();
}

// ---------------------------------------------------------------------------
// main pipeline
// ---------------------------------------------------------------------------

void DetectionWorker::mainLoop() {
    int health_skip = 25;
    int frame_count = 0;
    fps_time_ = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();

    // scene-change gate state (main-loop thread only)
    cv::Mat gate_prev;
    int gate_active = kGateCooldownFrames;  // run detectors from startup
    bool prev_gate_on = true;

    while (running_ && !stop_event_) {
        cv::Mat frame;
        if (!camera_->readLatest(frame)) {
            std::this_thread::sleep_for(5ms);
            continue;
        }
        auto t0 = std::chrono::steady_clock::now();

        frame_count++;
        frame_count_++;
        double now = std::chrono::duration<double>(
                         std::chrono::steady_clock::now().time_since_epoch()).count();

        if (now - fps_time_ >= 1.0) {
            current_fps_ = frame_count_ / (now - fps_time_);
            frame_count_ = 0;
            fps_time_ = now;
            dropped_last_sec_ = static_cast<int>(camera_->frameDrops());
            camera_->resetFrameDrops();
        }

        // ---- scene-change gate -------------------------------------------
        // Cheap downscaled consecutive-frame diff. A static scene means there
        // is nothing new to detect, so the expensive detectors are skipped
        // (this is where most idle CPU goes). Cooldown keeps the gate open for
        // a few frames after each change so slow motion never blinks it.
        if (!frame.empty()) {
            try {
                const int gw = kGateRefWidth;
                const int gh = std::max(
                    1, static_cast<int>(static_cast<double>(frame.rows) * gw /
                                        std::max(frame.cols, 1)));
                cv::Mat small;
                cv::resize(frame, small, cv::Size(gw, gh), 0, 0,
                           cv::INTER_NEAREST);
                cv::cvtColor(small, small, cv::COLOR_BGR2GRAY);
                bool changed = gate_prev.empty();
                if (!changed && small.size() == gate_prev.size()) {
                    cv::Mat diff;
                    cv::absdiff(small, gate_prev, diff);
                    changed = cv::mean(diff)[0] > kGateDiffMean;
                }
                gate_prev = small;
                if (changed) gate_active = kGateCooldownFrames;
                else gate_active = std::max(0, gate_active - 1);
            } catch (...) {
                gate_active = kGateCooldownFrames;  // never gate off a bad frame
            }
        }
        const bool gate_on = gate_active > 0;
        if (gate_on && !prev_gate_on) {
            // Scene just became active: run heavy detection immediately.
            hog_skip_ = kHogInterval;
            yolo_skip_ = kYoloInterval;
            face_skip_ = kFaceInterval;
        }
        prev_gate_on = gate_on;

        std::vector<DetectedObject> motion_boxes;
        std::vector<Problem> problems;

        // motion (fast MOG2)
        if (motion_enabled_ && motion_detector_) {
            motion_detector_->detect(frame);
            motion_boxes = motion_detector_->motionBoxes();
        }

        // HOG human feed (async side thread)
        std::vector<DetectedObject> human_boxes;
        if (human_enabled_) {
            if (gate_on) {
                hog_skip_++;
                if (hog_skip_ >= kHogInterval) {
                    hog_skip_ = 0;
                    {
                        std::lock_guard<std::mutex> lock(hog_lock_);
                        hog_frame_ = frame;
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(hog_lock_);
                    human_boxes = human_boxes_;
                }
            } else {
                // static scene: drop stale overlays, stop submitting
                std::lock_guard<std::mutex> lock(hog_lock_);
                human_boxes_.clear();
            }
        }

        // YOLO object feed (async side thread)
        std::vector<DetectedObject> object_boxes;
        if (object_enabled_ && object_detector_ && object_detector_->isEnabled()) {
            if (gate_on) {
                yolo_skip_++;
                if (yolo_skip_ >= kYoloInterval) {
                    yolo_skip_ = 0;
                    {
                        std::lock_guard<std::mutex> lock(yolo_lock_);
                        yolo_frame_ = frame;
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(yolo_lock_);
                    object_boxes = object_boxes_;
                }
            } else {
                std::lock_guard<std::mutex> lock(yolo_lock_);
                object_boxes_.clear();
            }
        }

        // face feed (async side thread)
        std::vector<DetectedObject> face_boxes;
        if (face_enabled_ && face_recognizer_) {
            if (gate_on) {
                face_skip_++;
                if (face_skip_ >= kFaceInterval) {
                    face_skip_ = 0;
                    {
                        std::lock_guard<std::mutex> lock(face_lock_);
                        face_frame_ = frame;
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(face_lock_);
                    face_boxes = face_boxes_;
                }
            } else {
                std::lock_guard<std::mutex> lock(face_lock_);
                face_boxes_.clear();
            }
        }

        // health check
        if (problem_detector_ && frame_count % health_skip == 0)
            problems = problem_detector_->checkFrame(frame);

        // recording
        bool is_recording = false, is_paused = false;
        {
            std::lock_guard<std::mutex> lock(rec_mutex_);
            if (recording_enabled_ && recorder_) {
                bool any_event = !motion_boxes.empty() || !human_boxes.empty() ||
                                 !face_boxes.empty() || !object_boxes.empty();
                recorder_->writeFrame(frame, any_event);
                is_recording = recorder_->isRecording();
                is_paused = recorder_->isPaused();
            }
        }

        DetectionResult result;
        result.frame = nightEnhance(frame);  // shared handle (clone only when enhanced)
        result.motion_boxes = std::move(motion_boxes);
        result.human_boxes = std::move(human_boxes);
        result.face_boxes = std::move(face_boxes);
        result.object_boxes = std::move(object_boxes);
        result.problems = std::move(problems);
        result.fps = current_fps_;
        result.recording = is_recording;
        result.paused = is_paused;
        result.frame_count = frame_count;
        result.timestamp = now;
        result.process_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - t0).count();
        result.dropped_frames = dropped_last_sec_;
        result.gate_idle = !gate_on;

        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            result_ = std::move(result);  // overwrite oldest (ring of 1 latest)
        }
    }
}

// ---------------------------------------------------------------------------
// side threads
// ---------------------------------------------------------------------------

void DetectionWorker::hogLoop() {
    auto local_hog = std::make_unique<HumanDetector>(300);
    while (running_ && !stop_event_) {
        if (!human_enabled_) {
            std::this_thread::sleep_for(300ms);
            continue;
        }
        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lock(hog_lock_);
            if (!hog_frame_.empty()) {
                frame = hog_frame_.clone();
                hog_frame_.release();
            }
        }
        if (!frame.empty()) {
            local_hog->detect(frame);
            std::lock_guard<std::mutex> lock(hog_lock_);
            human_boxes_ = local_hog->humans();
        }
        std::this_thread::sleep_for(200ms);
    }
}

void DetectionWorker::yoloLoop() {
    while (running_ && !stop_event_) {
        if (!object_enabled_ || !object_detector_) {
            std::this_thread::sleep_for(400ms);
            continue;
        }
        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lock(yolo_lock_);
            if (!yolo_frame_.empty()) {
                frame = yolo_frame_.clone();
                yolo_frame_.release();
            }
        }
        if (!frame.empty()) {
            auto boxes = object_detector_->detect(frame);
            std::lock_guard<std::mutex> lock(yolo_lock_);
            object_boxes_ = std::move(boxes);
        }
        std::this_thread::sleep_for(250ms);
    }
}

void DetectionWorker::faceLoop() {
    int retest_pending = 0;
    while (running_ && !stop_event_) {
        if (!face_enabled_ || !face_recognizer_) {
            retest_pending = 0;
            std::this_thread::sleep_for(300ms);
            continue;
        }
        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lock(face_lock_);
            if (!face_frame_.empty()) {
                frame = face_frame_.clone();
                face_frame_.release();
            }
        }
        if (!frame.empty()) {
            if (retest_pending > 0) {
                --retest_pending;
            } else {
                auto boxes = face_recognizer_->recognizeFaces(frame, 320);
                {
                    std::lock_guard<std::mutex> lock(face_lock_);
                    face_boxes_ = std::move(boxes);
                }
                if (!face_boxes_.empty()) retest_pending = kFaceRetestGap;
            }
        }
        std::this_thread::sleep_for(120ms);
    }
}

cv::Mat DetectionWorker::nightEnhance(const cv::Mat& frame) {
    if (frame.empty()) return frame;

    int lm = light_mode_.load();
    if (lm == 0) {  // auto: brightness hysteresis on the raw frame
        try {
            cv::Mat gray;
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            double mean = cv::mean(gray)[0];
            if (mean < 40) auto_night_.store(true);
            else if (mean > 90) auto_night_.store(false);
        } catch (...) {
            auto_night_.store(false);
        }
    }
    bool night = (lm == 2) || night_vision_.load() ||
                 (lm == 0 && auto_night_.load());
    if (!night) return frame;

    try {
        cv::Mat out = frame.clone();  // never mutate the shared capture buffer

        // 1) pre-CLAHE denoise — CLAHE amplifies sensor noise in the dark, so
        //    a median filter first is the single biggest quality win.
        cv::Mat work = out;
        if (clahe_denoise_.load()) cv::medianBlur(out, work, 3);

        // 2) CLAHE on the LAB luma channel (preserves color).
        cv::Mat lab;
        cv::cvtColor(work, lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> planes;
        cv::split(lab, planes);
        // Pre-CLAHE luma mean: the gamma and desat thresholds must read the
        // un-enhanced luma (mirrors core/detection_worker.py exactly).
        const double luma_mean = cv::mean(planes[0])[0];
        const int tile = std::max(2, clahe_tile_.load());
        const double clip = std::max(0.5, clahe_clip_.load());
        if (clahe_.empty() || clahe_dirty_.exchange(false))
            clahe_ = cv::createCLAHE(clip, {tile, tile});
        clahe_->apply(planes[0], planes[0]);

        // 3) reactive gamma on the CLAHE'd luma, EMA-smoothed so auto-exposure
        //    bouncing never flickers frame-to-frame.
        if (clahe_gamma_.load()) {
            double g = std::min(1.8, std::max(0.35, 110.0 / std::max(luma_mean, 1.0)));
            gamma_smooth_ = gamma_smooth_ * 0.7 + g * 0.3;
            g = gamma_smooth_;
            uchar lut[256];
            for (int i = 0; i < 256; ++i)
                lut[i] = cv::saturate_cast<uchar>(
                    std::min(255.0, std::pow(i / 255.0, 1.0 / g) * 255.0));
            cv::LUT(planes[0], cv::Mat(1, 256, CV_8UC1, lut).clone(), planes[0]);
        } else {
            gamma_smooth_ = 1.0;
        }

        cv::merge(planes, lab);
        cv::Mat out2;
        cv::cvtColor(lab, out2, cv::COLOR_Lab2BGR);

        // 4) auto-desaturate in very low light — chroma noise reads much
        //    cleaner when nearly black; saturation scales with ambient luma.
        if (clahe_desat_.load() && luma_mean < 30.0) {
            const double s_factor = std::max(0.25, luma_mean / 30.0);
            cv::Mat hsv;
            cv::cvtColor(out2, hsv, cv::COLOR_BGR2HSV);
            std::vector<cv::Mat> hsv_planes;
            cv::split(hsv, hsv_planes);
            cv::Mat s32;
            hsv_planes[1].convertTo(s32, CV_32F);
            s32 = cv::min(s32 * s_factor, 255.0);
            s32.convertTo(hsv_planes[1], CV_8U);
            cv::merge(hsv_planes, hsv);
            cv::cvtColor(hsv, out2, cv::COLOR_HSV2BGR);
        }
        return out2;
    } catch (...) {
        return frame;
    }
}

void DetectionWorker::updateClahe(double clip, int tile, bool denoise,
                                  bool gamma, bool desat) {
    clahe_clip_.store(clip);
    clahe_tile_.store(tile);
    clahe_denoise_.store(denoise);
    clahe_gamma_.store(gamma);
    clahe_desat_.store(desat);
    clahe_dirty_.store(true);
}

std::optional<DetectionResult> DetectionWorker::getResult() {
    std::lock_guard<std::mutex> lock(result_mutex_);
    auto out = result_;
    result_.reset();
    return out;
}

void DetectionWorker::updateSettings(bool motion, bool human, bool face, bool objects,
                                     bool recording, int threshold) {
    motion_enabled_ = motion;
    human_enabled_ = human;
    face_enabled_ = face;
    object_enabled_ = objects;
    motion_threshold_ = threshold;
    if (motion_detector_) motion_detector_->setThreshold(threshold);

    if (recording != recording_enabled_) {
        std::lock_guard<std::mutex> lock(rec_mutex_);
        recording_enabled_ = recording;
        if (recording && !recorder_) {
            recorder_ = std::make_unique<Recorder>(camera_->name());
        } else if (!recording && recorder_) {
            recorder_->stop();
            recorder_.reset();
        }
    }
}

void DetectionWorker::updateNightVision(bool night_vision, const std::string& light_mode) {
    night_vision_ = night_vision;
    int m = 0;  // auto
    if (light_mode == "day") m = 1;
    else if (light_mode == "night") m = 2;
    light_mode_ = m;
}

void DetectionWorker::stopRecording() {
    std::lock_guard<std::mutex> lock(rec_mutex_);
    if (recorder_) {
        recorder_->stopRecording();
        recording_enabled_ = false;
    }
}

void DetectionWorker::togglePause() {
    std::lock_guard<std::mutex> lock(rec_mutex_);
    if (recorder_) {
        if (recorder_->isPaused())
            recorder_->resume();
        else
            recorder_->pause();
    }
}

bool DetectionWorker::isRecording() const {
    std::lock_guard<std::mutex> lock(rec_mutex_);
    return recorder_ && recorder_->isRecording();
}

bool DetectionWorker::isPaused() const {
    std::lock_guard<std::mutex> lock(rec_mutex_);
    return recorder_ && recorder_->isPaused();
}

}  // namespace core
