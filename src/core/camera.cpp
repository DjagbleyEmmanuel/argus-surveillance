#include "core/camera.h"

#include <QFileInfo>
#include <QDir>
#include <QProcess>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <thread>

namespace core {

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static std::string fourccOf(const std::string& label) {
    for (const auto& [lbl, code] : PixelFormats()) {
        if (lbl == label) return code;
    }
    return "";
}

static std::optional<CameraType> typeOf(const std::string& url) {
    if (url.rfind("rtsp://", 0) == 0 || url.rfind("rtsps://", 0) == 0)
        return CameraType::RTSP;
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
        return CameraType::HTTP;
    try {
        (void)std::stoi(url);
        return CameraType::USB;
    } catch (...) {
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// CameraSource
// ---------------------------------------------------------------------------

CameraSource::CameraSource(std::string name, CameraType type, std::string url,
                           std::string camera_id, std::string pixel_format)
    : name_(std::move(name)),
      type_(type),
      source_url_(std::move(url)),
      camera_id_(camera_id.empty() ? "cam_" + std::to_string(reinterpret_cast<uintptr_t>(this))
                                   : std::move(camera_id)),
      pixel_format_(std::move(pixel_format)) {}

CameraSource::~CameraSource() { stop(); }

std::optional<CameraType> CameraSource::detectType(const std::string& source_url) {
    return typeOf(source_url);
}

bool CameraSource::open() {
    std::lock_guard<std::mutex> lock(cap_mutex_);
    try {
        cap_ = std::make_unique<cv::VideoCapture>();

        if (type_ == CameraType::USB) {
            int idx = std::stoi(source_url_);
            // Stable identity BEFORE opening, so restore can find the device.
            UsbIdentity ident = getUsbIdentity(idx);
            usb_sys_name_ = ident.sys_name;
            usb_bus_path_ = ident.bus_path;

            // Two-step open: timeouts must be set before open() to be honored.
            cap_->set(cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 2500);
            cap_->set(cv::CAP_PROP_READ_TIMEOUT_MSEC, 2500);
            cap_->open(idx, cv::CAP_V4L2);
        } else {
            cap_->open(source_url_);
        }

        if (!cap_->isOpened()) {
            status_ = CameraStatus::ERROR;
            return false;
        }

        if (type_ == CameraType::USB && !pixel_format_.empty()) {
            std::string code = fourccOf(pixel_format_);
            if (code.size() == 4)
                cap_->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc(code[0], code[1], code[2], code[3]));
        }

        cv::Mat frame;
        if (!cap_->read(frame) || frame.empty()) {
            status_ = CameraStatus::ERROR;
            return false;
        }

        int w = static_cast<int>(cap_->get(cv::CAP_PROP_FRAME_WIDTH));
        int h = static_cast<int>(cap_->get(cv::CAP_PROP_FRAME_HEIGHT));
        resolution_ = (w > 0 && h > 0) ? std::make_pair(w, h)
                                       : std::make_pair(frame.cols, frame.rows);
        status_ = CameraStatus::ONLINE;
        last_frame_time_ = std::chrono::steady_clock::now().time_since_epoch().count() / 1e9;
        return true;
    } catch (const std::exception& e) {
        status_ = CameraStatus::ERROR;
        return false;
    }
}

bool CameraSource::reconnect() {
    // Re-open without the double-lock (open() takes cap_mutex_).
    try {
        std::unique_lock<std::mutex> lock(cap_mutex_);
        cap_ = std::make_unique<cv::VideoCapture>();
        if (type_ == CameraType::USB) {
            int idx = std::stoi(source_url_);
            cap_->set(cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 2500);
            cap_->set(cv::CAP_PROP_READ_TIMEOUT_MSEC, 2500);
            cap_->open(idx, cv::CAP_V4L2);
        } else {
            cap_->open(source_url_);
        }
        bool ok = cap_->isOpened();
        if (ok) status_ = CameraStatus::ONLINE;
        return ok;
    } catch (...) {
        return false;
    }
}

void CameraSource::captureLoop() {
    int fail_count = 0;
    auto last_wall = std::chrono::steady_clock::now();
    int frames = 0;

    while (running_ && !stop_event_) {
        bool is_opened = false;
        {
            std::lock_guard<std::mutex> lock(cap_mutex_);
            is_opened = cap_ && cap_->isOpened();
        }

        if (!is_opened) {
            status_ = CameraStatus::OFFLINE;
            if (stop_event_.load()) break;
            // reconnect backoff
            for (int i = 0; i < static_cast<int>(reconnect_delay_ * 10) && running_ && !stop_event_; ++i)
                std::this_thread::sleep_for(100ms);
            if (reconnect()) reconnect_delay_ = 2.0;
            fail_count = 0;
            continue;
        }

        cv::Mat frame;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(cap_mutex_);
            if (cap_ && cap_->isOpened()) {
                try {
                    ok = cap_->read(frame);
                } catch (...) {
                    ok = false;
                }
            }
        }

        if (!ok || frame.empty()) {
            fail_count++;
            if (fail_count > 5) {
                status_ = CameraStatus::OFFLINE;
                reconnect_delay_ = std::min(reconnect_delay_ * 1.5, 30.0);
                for (int i = 0; i < static_cast<int>(reconnect_delay_ * 10) && running_ && !stop_event_; ++i)
                    std::this_thread::sleep_for(100ms);
                if (reconnect()) reconnect_delay_ = 2.0;
                fail_count = 0;
            } else {
                std::this_thread::sleep_for(10ms);
            }
            continue;
        }

        fail_count = 0;
        reconnect_delay_ = 2.0;
        frame_count_++;
        frames++;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - last_wall).count() >= 1.5) {
            fps_ = frames / std::chrono::duration<double>(now - last_wall).count();
            frames = 0;
            last_wall = now;
        }

        last_frame_time_ = std::chrono::duration<double>(now.time_since_epoch()).count();
        status_ = CameraStatus::ONLINE;

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (frame_queue_.size() >= 2) frame_queue_.pop_front();
            frame_queue_.push_back(frame);
        }
        queue_cv_.notify_all();
    }
}

void CameraSource::start() {
    std::lock_guard<std::mutex> lock(cap_mutex_);
    if (running_) return;
    running_ = true;
    stop_event_ = false;
    thread_ = std::thread(&CameraSource::captureLoop, this);
}

void CameraSource::stop() {
    running_ = false;
    stop_event_ = true;
    queue_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(cap_mutex_);
        if (cap_) {
            cap_->release();
            cap_.reset();
        }
        status_ = CameraStatus::OFFLINE;
    }
}

bool CameraSource::readLatest(cv::Mat& out) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (frame_queue_.empty()) return false;
    out = frame_queue_.back();
    frame_queue_.clear();
    return !out.empty();
}

bool CameraSource::setResolution(int width, int height) {
    std::lock_guard<std::mutex> lock(cap_mutex_);
    if (!cap_ || !cap_->isOpened()) return false;
    cap_->set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap_->set(cv::CAP_PROP_FRAME_HEIGHT, height);
    std::this_thread::sleep_for(50ms);
    int rw = static_cast<int>(cap_->get(cv::CAP_PROP_FRAME_WIDTH));
    int rh = static_cast<int>(cap_->get(cv::CAP_PROP_FRAME_HEIGHT));
    if (rw > 0 && rh > 0) {
        resolution_ = {rw, rh};
        return true;
    }
    return false;
}

bool CameraSource::setFps(double fps) {
    std::lock_guard<std::mutex> lock(cap_mutex_);
    if (!cap_ || !cap_->isOpened()) return false;
    cap_->set(cv::CAP_PROP_FPS, fps);
    return true;
}

bool CameraSource::setPixelFormat(const std::string& pixel_format) {
    std::lock_guard<std::mutex> lock(cap_mutex_);
    pixel_format_ = pixel_format;
    if (!cap_ || !cap_->isOpened()) return true;  // applied on next open
    std::string code = fourccOf(pixel_format);
    if (code.size() != 4) return true;
    return cap_->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc(code[0], code[1], code[2], code[3]));
}

bool CameraSource::isOnline() const {
    if (status_ != CameraStatus::ONLINE) return false;
    double now = std::chrono::duration<double>(
                     std::chrono::steady_clock::now().time_since_epoch()).count();
    return (now - last_frame_time_.load()) < 5.0;
}

double CameraSource::uptime() const {
    if (!isOnline()) return 0.0;
    double now = std::chrono::duration<double>(
                     std::chrono::steady_clock::now().time_since_epoch()).count();
    return now - last_frame_time_.load();
}

std::string CameraSource::statusString() const {
    switch (status_) {
        case CameraStatus::UNKNOWN:   return "unknown";
        case CameraStatus::CONNECTING:return "connecting";
        case CameraStatus::ONLINE:    return "online";
        case CameraStatus::OFFLINE:   return "offline";
        case CameraStatus::ERROR:     return "error";
    }
    return "unknown";
}

std::vector<std::pair<int, int>> CameraSource::enumerateResolutions() const {
    // Mirrors Python: fixed candidate list, current resolution first.
    std::vector<std::pair<int, int>> sizes = {
        {160, 120}, {320, 240}, {640, 480}, {800, 600},
        {1024, 768}, {1280, 720}, {1280, 1024}, {1920, 1080}};
    auto cur = resolution_;
    if (cur.first > 0 && cur.second > 0) {
        auto it = std::find(sizes.begin(), sizes.end(), cur);
        if (it != sizes.end()) sizes.erase(it);
        sizes.insert(sizes.begin(), cur);
    }
    return sizes;
}

// ---------------------------------------------------------------------------
// static discovery
// ---------------------------------------------------------------------------

std::vector<int> CameraSource::usbCaptureIndices() {
    std::vector<int> probe_devs;
    QProcess proc;
    proc.start("v4l2-ctl", {"--list-devices"});
    if (proc.waitForFinished(5000)) {
        QString current_caps;
        for (const QString& line : QString::fromUtf8(proc.readAllStandardOutput()).split('\n')) {
            if (!line.isEmpty() && !line.startsWith(' ') && !line.startsWith('\t'))
                current_caps = line;
            QString stripped = line.trimmed();
            if (stripped.startsWith("/dev/video")) {
                bool is_metadata = current_caps.toLower().contains("metadata") ||
                                   current_caps.toLower().contains("ir");
                bool ok = false;
                int idx = stripped.mid(10).toInt(&ok);
                if (ok && !is_metadata && idx % 2 == 0)
                    probe_devs.push_back(idx);
            }
        }
    }
    if (probe_devs.empty()) return {0, 1, 2};
    return probe_devs;
}

UsbIdentity CameraSource::getUsbIdentity(int idx) {
    UsbIdentity info;
    info.index = idx;
    std::ifstream name_file("/sys/class/video4linux/video" + std::to_string(idx) + "/name");
    if (name_file) {
        std::string name;
        std::getline(name_file, name);
        if (!name.empty()) info.sys_name = name;
    }

    QDir by_path("/dev/v4l/by-path");
    for (const QFileInfo& entry : by_path.entryInfoList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
        if (entry.symLinkTarget() == QString("/dev/video%1").arg(idx)) {
            info.bus_path = entry.fileName().toStdString();
            break;
        }
    }
    return info;
}

int CameraSource::resolveUsbIndex(const std::string& bus_path,
                                  const std::string& sys_name, int fallback) {
    if (!bus_path.empty()) {
        QString target = QFileInfo("/dev/v4l/by-path/" + QString::fromStdString(bus_path)).symLinkTarget();
        if (target.startsWith("/dev/video")) {
            bool ok = false;
            int idx = target.mid(10).toInt(&ok);
            if (ok) return idx;
        }
    }
    if (!sys_name.empty()) {
        for (int idx : usbCaptureIndices()) {
            if (getUsbIdentity(idx).sys_name == sys_name) return idx;
        }
    }
    return fallback;
}

std::vector<UsbCamera> CameraSource::detectUsbCameras() {
    // Sequential v4l2-ctl child-probe with a hard 2.5s timeout. MUST stay
    // sequential — streaming multiple devices at once wedges the USB controller.
    //   rc=0 + clean stderr           -> streamed a frame -> include
    //   rc=0 + "busy" stderr          -> held by another app -> include
    //   rc=0 + "timeout" stderr       -> dead node that failed fast -> exclude
    //   killed by timeout (hang)      -> dead node -> exclude
    std::vector<int> found;
    for (int idx : usbCaptureIndices()) {
        QProcess probe;
        probe.start("v4l2-ctl", {"-d", QString("/dev/video%1").arg(idx),
                                 "--stream-count=1", "--stream-mmap"});
        if (!probe.waitForFinished(2500)) {
            probe.kill();
            continue;
        }
        if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0)
            continue;
        if (QString::fromUtf8(probe.readAllStandardError()).toLower().contains("timeout"))
            continue;
        found.push_back(idx);
    }

    std::vector<UsbCamera> result;
    std::sort(found.begin(), found.end());
    for (int idx : found) {
        UsbIdentity ident = getUsbIdentity(idx);
        std::string name = ident.sys_name.empty() ? "USB Camera " + std::to_string(idx)
                                                  : ident.sys_name;
        result.push_back({idx, name + " (video" + std::to_string(idx) + ")"});
    }
    return result;
}

}  // namespace core
