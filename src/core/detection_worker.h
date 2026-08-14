#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include "core/camera.h"
#include "core/detector.h"
#include "core/face_recognizer.h"
#include "core/object_detector.h"
#include "core/recorder.h"

namespace core {

// One frame of pipeline output, delivered to the GUI poll loop. cv::Mat is a
// shared handle (refcounted) — no per-frame deep copies across the boundary.
struct DetectionResult {
    cv::Mat frame;  // shared, cheap
    std::vector<DetectedObject> motion_boxes;
    std::vector<DetectedObject> human_boxes;
    std::vector<DetectedObject> face_boxes;
    std::vector<DetectedObject> object_boxes;
    std::vector<Problem> problems;
    double fps = 0.0;
    bool recording = false;
    bool paused = false;
    int frame_count = 0;
    double timestamp = 0.0;
};

// Per-camera pipeline. Threads mirror Python detection_worker.py:
//   main  : read_latest -> motion (MOG2) -> health -> pack result -> queue(2)
//   hog   : human detection every 15 frames
//   yolo  : object detection every 20 frames (CUDA)
//   face  : face recognition every 8 frames
class DetectionWorker {
public:
    DetectionWorker(std::shared_ptr<CameraSource> camera, std::string worker_id = "0",
                    bool recording_enabled = false,
                    std::shared_ptr<FaceRecognizer> face_recognizer = nullptr,
                    std::shared_ptr<ObjectDetector> object_detector = nullptr);
    ~DetectionWorker();

    void start();
    void stop();

    // Polled by the GUI thread; returns the latest result (nullopt if none).
    std::optional<DetectionResult> getResult();

    std::shared_ptr<CameraSource> camera() const { return camera_; }
    const std::string& workerId() const { return worker_id_; }

    // ---- toggles / tuning ----
    void updateSettings(bool motion, bool human, bool face, bool objects,
                        bool recording, int threshold);

    // Night-vision hint (GUI pushes light mode + toggle; worker applies CLAHE
    // off the GUI thread so paint stays cheap). mode: "auto" | "day" | "night".
    void updateNightVision(bool night_vision, const std::string& light_mode);
    bool nightVisionActive() const { return light_mode_.load() != 1 && (light_mode_.load() == 2 || night_vision_.load() || (light_mode_.load() == 0 && auto_night_.load())); }

    // CLAHE tunables (GUI sliders/checkboxes; applied on the worker thread).
    void updateClahe(double clip, int tile, bool denoise, bool gamma, bool desat);

    // ---- recording control (thread-safe) ----
    void stopRecording();
    void togglePause();
    bool isRecording() const;
    bool isPaused() const;

    // state snapshot (thread-safe)
    bool motionEnabled() const { return motion_enabled_; }
    // (remaining public getters used by MainWindow)

private:
    void mainLoop();
    void hogLoop();
    void yoloLoop();
    void faceLoop();
    cv::Mat nightEnhance(const cv::Mat& frame);

    std::shared_ptr<CameraSource> camera_;
    std::string worker_id_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_event_{false};
    std::thread main_thread_, hog_thread_, yolo_thread_, face_thread_;

    // tuning (written by GUI, read by threads)
    std::atomic<bool> motion_enabled_{true};
    std::atomic<bool> human_enabled_{true};
    std::atomic<bool> face_enabled_{true};
    std::atomic<bool> object_enabled_{true};
    std::atomic<bool> recording_enabled_{false};
    std::atomic<int> motion_threshold_{5000};

    // night vision (light mode: 0=auto, 1=day, 2=night)
    std::atomic<int> light_mode_{0};
    std::atomic<bool> night_vision_{false};
    std::atomic<bool> auto_night_{false};
    // Task 2 CLAHE tuning (written by GUI, read by the main-loop thread)
    std::atomic<double> clahe_clip_{3.5};
    std::atomic<int> clahe_tile_{8};
    std::atomic<bool> clahe_denoise_{true};
    std::atomic<bool> clahe_gamma_{true};
    std::atomic<bool> clahe_desat_{true};
    std::atomic<bool> clahe_dirty_{true};
    cv::Ptr<cv::CLAHE> clahe_;  // main-loop thread only
    double gamma_smooth_ = 1.0;  // main-loop thread only

    // results ring (max 2)
    std::mutex result_mutex_;
    std::optional<DetectionResult> result_;

    // recorder guard (created/toggled by GUI, used by main loop)
    mutable std::mutex rec_mutex_;

    // shared detectors
    std::unique_ptr<MotionDetector> motion_detector_;
    std::unique_ptr<ProblemDetector> problem_detector_;
    std::unique_ptr<HumanDetector> human_detector_;
    std::unique_ptr<Recorder> recorder_;
    std::shared_ptr<FaceRecognizer> face_recognizer_;
    std::shared_ptr<ObjectDetector> object_detector_;

    // frame handoff + latest boxes (mutex protected)
    std::mutex hog_lock_, yolo_lock_, face_lock_;
    cv::Mat hog_frame_, yolo_frame_, face_frame_;
    std::vector<DetectedObject> human_boxes_, object_boxes_, face_boxes_;

    int hog_skip_ = 0, yolo_skip_ = 0, face_skip_ = 0;
    static constexpr int kHogInterval = 15;
    static constexpr int kYoloInterval = 20;
    static constexpr int kFaceInterval = 8;
    // after a detection finds faces, skip this many submissions before retesting
    // (faces move slowly; throttling the expensive cascade avoids CPU-stall spikes)
    static constexpr int kFaceRetestGap = 2;

    int frame_count_ = 0;
    double fps_time_ = 0.0, current_fps_ = 0.0;
};

}  // namespace core
