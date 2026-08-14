#include "core/recorder.h"

#include <QDateTime>
#include <QDir>

#include <chrono>
#include <cstdio>
#include <thread>

namespace core {

Recorder::Recorder(std::string camera_name, std::string output_dir,
                   int pre_sec, int post_sec)
    : camera_name_(std::move(camera_name)),
      output_dir_(std::move(output_dir)),
      pre_sec_(pre_sec),
      post_sec_(post_sec),
      max_pre_frames_(pre_sec * 30),
      max_post_frames_(post_sec * 30) {
    std::replace(camera_name_.begin(), camera_name_.end(), ' ', '_');
    QDir().mkpath(QString::fromStdString(output_dir_));
    writer_thread_ = std::thread(&Recorder::writerLoop, this);
}

Recorder::~Recorder() { stop(); }

void Recorder::writerLoop() {
    while (running_) {
        // pull one item
        cv::VideoWriter writer;
        cv::Mat frame;
        bool have = false;
        int cmd = -1;
        std::string close_path;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (!write_queue_.empty()) {
                auto& item = write_queue_.front();
                cmd = item.first;
                frame = item.second;
                write_queue_.pop_front();
                have = true;
            } else if (!close_queue_.empty()) {
                writer = close_queue_.front().second;
                close_path = close_paths_.front();
                close_queue_.pop_front();
                close_paths_.pop_front();
                have = true;
                cmd = 1;
            }
        }

        if (!have) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        if (cmd == 0) {
            if (writer_open_) {
                try { writer_.write(frame); } catch (...) {}
            }
        } else if (cmd == 1) {
            try { writer.release(); } catch (...) {}
        }
    }
}

void Recorder::startRecording(const cv::Mat& frame, double fps) {
    if (recording_.load()) return;

    std::string timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss").toStdString();
    std::string filename = camera_name_ + "_" + timestamp + ".mp4";
    std::string filepath = output_dir_ + "/" + filename;
    int h = frame.rows, w = frame.cols;

    // codec fallback selection (avoids Intel iHD / VAAPI decode errors)
    const char* codecs[] = {"avc1", "H264", "mp4v", "MJPG"};
    writer_open_ = false;
    for (const char* codec : codecs) {
        cv::VideoWriter trial;
        int fourcc = cv::VideoWriter::fourcc(codec[0], codec[1], codec[2], codec[3]);
        if (trial.open(filepath, fourcc, fps, cv::Size(w, h))) {
            writer_ = trial;
            writer_open_ = true;
            break;
        }
    }

    if (!writer_open_) {
        writer_ = cv::VideoWriter();
        return;
    }
    current_file_ = filepath;
    recording_ = true;
    post_frames_remaining_ = max_post_frames_;

    // flush pre-buffer into the writer
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (const auto& buf : pre_buffer_) {
            if (write_queue_.size() >= 300) break;
            write_queue_.emplace_back(0, buf);
        }
    }
}

void Recorder::stopRecording() {
    if (!recording_.load()) return;
    cv::VideoWriter writer_to_close = writer_;
    std::string path = current_file_;
    writer_ = cv::VideoWriter();
    writer_open_ = false;
    recording_ = false;
    paused_ = false;
    current_file_.clear();
    post_frames_remaining_ = 0;

    if (writer_to_close.isOpened()) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        close_queue_.emplace_back(1, writer_to_close);
        close_paths_.push_back(path);
    }
}

void Recorder::pause() { paused_ = true; }
void Recorder::resume() { paused_ = false; }

void Recorder::writeFrame(const cv::Mat& frame, bool motion_detected) {
    if (frame.empty()) return;

    if (static_cast<int>(pre_buffer_.size()) >= max_pre_frames_)
        pre_buffer_.pop_front();
    pre_buffer_.push_back(frame.clone());

    if (paused_.load()) return;

    if (motion_detected) {
        post_frames_remaining_ = max_post_frames_;
        if (!recording_.load()) startRecording(frame);
    }

    if (recording_.load() && writer_open_) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (write_queue_.size() < 300)
                write_queue_.emplace_back(0, frame.clone());
        }
        if (!motion_detected) {
            post_frames_remaining_--;
            if (post_frames_remaining_ <= 0) stopRecording();
        }
    }
}

void Recorder::stop() {
    stopRecording();
    running_ = false;
    if (writer_thread_.joinable()) writer_thread_.join();
}

std::string Recorder::currentFile() const { return current_file_; }

}  // namespace core
