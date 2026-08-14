#pragma once

#include <opencv2/opencv.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace core {

// Event-triggered H.264 clip recorder.
//
// Port notes vs. Python core/recorder.py:
//  - pre_sec*30 frame pre-buffer (deque, dropped when no motion)
//  - post_sec*30 tail frames after motion stops
//  - dedicated writer thread + bounded queue (max 300)
//  - codec fallback: avc1 -> H264 -> mp4v -> MJPG
class Recorder {
public:
    Recorder(std::string camera_name, std::string output_dir = "recordings",
             int pre_sec = 2, int post_sec = 3);
    ~Recorder();

    void writeFrame(const cv::Mat& frame, bool motion_detected = false);
    void stopRecording();
    void pause();
    void resume();
    void stop();  // full shutdown (join writer thread)

    bool isRecording() const { return recording_.load(); }
    bool isPaused() const { return paused_.load(); }
    std::string currentFile() const;

private:
    void writerLoop();
    void startRecording(const cv::Mat& frame, double fps = 15.0);

    std::string camera_name_;
    std::string output_dir_;
    int pre_sec_, post_sec_;

    std::mutex lock_;
    cv::VideoWriter writer_;
    bool writer_open_ = false;
    std::string current_file_;
    std::atomic<bool> recording_{false};
    std::atomic<bool> paused_{false};

    std::deque<cv::Mat> pre_buffer_;
    int max_pre_frames_, max_post_frames_;
    int post_frames_remaining_ = 0;

    std::deque<std::pair<int, cv::Mat>> write_queue_;  // {cmd, frame}; cmd 0=write 1=close
    std::deque<std::pair<int, cv::VideoWriter>> close_queue_;
    std::deque<std::string> close_paths_;
    std::mutex queue_mutex_;
    std::atomic<bool> running_{true};
    std::thread writer_thread_;
};

}  // namespace core
