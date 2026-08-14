#include "core/face_recognizer.h"

#include "core/paths.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace core {

FaceRecognizer::FaceRecognizer(std::string faces_dir) : faces_dir_(std::move(faces_dir)) {
    QDir().mkpath(QString::fromStdString(faces_dir_));
    initCascade();
    buildRecognizer();
}

void FaceRecognizer::initCascade() {
    std::vector<QString> candidates = {
        QString::fromStdString(core::modelsDir() + "/haarcascade_frontalface_default.xml"),
        "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml",
    };
    for (const auto& p : candidates) {
        if (QFileInfo::exists(p)) {
            face_cascade_.load(p.toStdString());
            if (!face_cascade_.empty()) return;
        }
    }
    try {
        face_cascade_.load(cv::samples::findFile("haarcascade_frontalface_default.xml"));
    } catch (...) {}
}

void FaceRecognizer::buildRecognizer() {
    label_map_.clear();
    name_map_.clear();
    next_label_ = 0;
    trained_ = false;

    recognizer_ = cv::face::LBPHFaceRecognizer::create(1, 8, 8, 8);
    if (recognizer_.empty()) return;

    std::vector<cv::Mat> images;
    std::vector<int> labels;

    QDir base(QString::fromStdString(faces_dir_));
    for (const QFileInfo& person : base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        std::string person_name = person.fileName().toStdString();
        int lbl = next_label_;
        label_map_[lbl] = person_name;
        name_map_[person_name] = lbl;
        next_label_++;

        QDir person_dir(person.absoluteFilePath());
        for (const QFileInfo& f : person_dir.entryInfoList(QDir::Files)) {
            QString lower = f.fileName().toLower();
            if (!(lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")))
                continue;
            cv::Mat img = cv::imread(f.absoluteFilePath().toStdString(), cv::IMREAD_GRAYSCALE);
            if (img.empty()) continue;
            cv::resize(img, img, cv::Size(100, 100));
            cv::equalizeHist(img, img);
            images.push_back(img);
            labels.push_back(lbl);
        }
    }

    if (!images.empty()) {
        try {
            recognizer_->train(images, labels);
            trained_ = true;
        } catch (...) {}
    }
}

std::vector<DetectedObject> FaceRecognizer::recognizeFaces(const cv::Mat& frame, int target_width) {
    if (frame.empty() || face_cascade_.empty()) return {};

    int h = frame.rows, w = frame.cols;
    double scale_x = 1.0, scale_y = 1.0;
    cv::Mat small = frame;
    if (w > target_width) {
        scale_x = static_cast<double>(w) / target_width;
        int target_h = static_cast<int>(h / scale_x);
        cv::resize(frame, small, cv::Size(target_width, target_h), 0, 0, cv::INTER_NEAREST);
        scale_y = static_cast<double>(h) / target_h;
    }

    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    std::vector<cv::Rect> rects;
    try {
        std::lock_guard<std::mutex> lock(cascade_lock_);
        face_cascade_.detectMultiScale(gray, rects, 1.2, 5, cv::CASCADE_SCALE_IMAGE,
                                       cv::Size(40, 40));
    } catch (...) {
        return {};
    }

    std::vector<DetectedObject> results;
    for (size_t idx = 0; idx < rects.size(); ++idx) {
        cv::Rect r = rects[idx];
        cv::Mat crop = gray(r).clone();
        cv::resize(crop, crop, cv::Size(100, 100));
        cv::equalizeHist(crop, crop);

        int ox = static_cast<int>(r.x * scale_x);
        int oy = static_cast<int>(r.y * scale_y);
        int ow = static_cast<int>(r.width * scale_x);
        int oh = static_cast<int>(r.height * scale_y);

        std::string label;
        cv::Scalar color;
        float conf;

        if (trained_ && !recognizer_.empty()) {
            try {
                int lbl = -1;
                double dist = 0.0;
                recognizer_->predict(crop, lbl, dist);
                if (dist < LBPH_CONF_THRESHOLD) {
                    auto it = label_map_.find(lbl);
                    std::string person = (it != label_map_.end()) ? it->second : "Unknown";
                    label = "KNOWN: " + person;
                    std::transform(label.begin(), label.end(), label.begin(), ::toupper);
                    color = cv::Scalar(0, 255, 102);
                    conf = static_cast<float>(std::max(0.0, 1.0 - dist / 100.0));
                } else {
                    label = "UNKNOWN FACE";
                    color = cv::Scalar(0, 240, 255);
                    conf = 0.55f;
                }
            } catch (...) {
                label = "UNKNOWN FACE";
                color = cv::Scalar(0, 240, 255);
                conf = 0.55f;
            }
        } else {
            label = "FACE DETECTED";
            color = cv::Scalar(255, 200, 0);
            conf = 0.60f;
        }

        results.push_back({ox, oy, ow, oh, label, conf, color, static_cast<int>(300 + idx)});
    }
    return results;
}

bool FaceRecognizer::enrollFace(const std::string& name, const cv::Mat& face_bgr) {
    if (face_bgr.empty()) return false;
    std::string clean;
    for (char c : name) clean += (c == ' ') ? '_' : c;
    if (clean.empty()) return false;

    QDir person_dir(QString::fromStdString(faces_dir_ + "/" + clean));
    person_dir.mkpath(".");

    cv::Mat gray;
    cv::cvtColor(face_bgr, gray, cv::COLOR_BGR2GRAY);
    cv::resize(gray, gray, cv::Size(100, 100));
    cv::equalizeHist(gray, gray);

    int count = 0;
    for (const QFileInfo& f : person_dir.entryInfoList(QDir::Files)) {
        QString lower = f.fileName().toLower();
        if (lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")) count++;
    }
    long now = static_cast<long>(std::time(nullptr));
    std::string filepath = faces_dir_ + "/" + clean + "/" + clean + "_" +
                           std::to_string(count + 1) + "_" + std::to_string(now) + ".png";
    if (!cv::imwrite(filepath, gray)) return false;
    buildRecognizer();
    return true;
}

bool FaceRecognizer::enrollFromFrame(const std::string& name, const cv::Mat& frame_bgr,
                                     std::string& message) {
    cv::Mat frame = frame_bgr.clone();
    if (face_cascade_.empty()) {
        bool ok = enrollFace(name, frame);
        message = ok ? "Enrolled (no cascade — used full frame)."
                     : "Enrollment failed.";
        return ok;
    }

    cv::Mat gray, eq;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, eq);

    std::vector<cv::Rect> rects;
    try {
        std::lock_guard<std::mutex> lock(cascade_lock_);
        face_cascade_.detectMultiScale(eq, rects, 1.2, 5, cv::CASCADE_SCALE_IMAGE,
                                       cv::Size(60, 60));
    } catch (...) {
        bool ok = enrollFace(name, frame);
        message = ok ? "Face detect failed — full frame saved instead."
                     : "Enrollment failed.";
        return ok;
    }

    if (rects.empty()) {
        bool ok = enrollFace(name, frame_bgr);
        message = ok ? "No face detected — full frame saved.\nTry moving closer with good lighting."
                     : "Enrollment failed.";
        return ok;
    }

    cv::Rect largest = rects[0];
    for (const auto& r : rects)
        if (r.width * r.height > largest.width * largest.height) largest = r;
    cv::Mat crop = frame_bgr(largest);
    bool ok = enrollFace(name, crop);
    message = ok ? "Face enrolled successfully for '" + name + "'!"
                 : "Enrollment failed.";
    return ok;
}

bool FaceRecognizer::deletePerson(const std::string& name) {
    std::string clean;
    for (char c : name) clean += (c == ' ') ? '_' : c;
    QDir person_dir(QString::fromStdString(faces_dir_ + "/" + clean));
    if (person_dir.exists()) {
        if (!person_dir.removeRecursively()) return false;
        buildRecognizer();
        return true;
    }
    return false;
}

std::vector<std::string> FaceRecognizer::listEnrolled() {
    std::vector<std::string> out;
    QDir base(QString::fromStdString(faces_dir_));
    for (const QFileInfo& person : base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        out.push_back(person.fileName().toStdString());
    std::sort(out.begin(), out.end());
    return out;
}

int FaceRecognizer::sampleCount(const std::string& name) {
    std::string clean;
    for (char c : name) clean += (c == ' ') ? '_' : c;
    int count = 0;
    QDir person_dir(QString::fromStdString(faces_dir_ + "/" + clean));
    for (const QFileInfo& f : person_dir.entryInfoList(QDir::Files)) {
        QString lower = f.fileName().toLower();
        if (lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")) count++;
    }
    return count;
}

}  // namespace core
