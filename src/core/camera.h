#pragma once

#include <opencv2/opencv.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace core {

enum class CameraType { USB, RTSP, HTTP };
enum class CameraStatus { UNKNOWN, CONNECTING, ONLINE, OFFLINE, ERROR };

// Selectable V4L2 capture output formats for USB cameras.
// Map "UI label" -> FourCC code ("" => auto/default).
inline const std::vector<std::pair<std::string, std::string>>& PixelFormats() {
    static const std::vector<std::pair<std::string, std::string>> fmts = {
        {"Auto (Default)", ""},
        {"Motion-JPEG (MPJG)", "MJPG"},
        {"YUYV 4:2:2 (YUYV)", "YUYV"},
        {"RGB3", "RGB3"},
        {"BGR3", "BGR3"},
        {"NV12", "NV12"},
        {"I420 (YU12)", "I420"},
        {"Grayscale (GREY)", "GREY"},
    };
    return fmts;
}

// A capture output format a USB camera ACTUALLY supports (from
// v4l2-ctl --list-formats-ext), with its valid frame sizes.
struct CameraFormat {
    std::string label;                       // UI label
    std::string fourcc;                      // V4L2 FOURCC code ("" for Auto)
    std::vector<std::pair<int, int>> sizes;  // supported frame sizes
};

// Stable per-physical-device identity (survives /dev/videoN index changes).
struct UsbIdentity {
    int index = -1;
    std::string sys_name;
    std::string bus_path;  // basename under /dev/v4l/by-path
};

struct UsbCamera {
    int index = -1;
    std::string label;  // e.g. "Integrated_Webcam_HD (video4)"
};

// std::thread-based capture pump. Owns the cv::VideoCapture handle and pushes
// the latest frame onto a tiny ring; readers use readLatest() which drains it.
//
// Port notes vs. Python:
//  - Two-step open: create VideoCapture, set CAP_PROP_OPEN_TIMEOUT_MSEC and
//    CAP_PROP_READ_TIMEOUT_MSEC BEFORE open() so dead nodes fail fast.
//  - Reconnect with exponential backoff (2s -> 30s), like the Python loop.
//  - Sequential v4l2-ctl child-probe in detectUsbCameras(); never from the GUI.
class CameraSource {
public:
    CameraSource(std::string name, CameraType type, std::string url,
                 std::string camera_id = "", std::string pixel_format = "");
    ~CameraSource();

    CameraSource(const CameraSource&) = delete;
    CameraSource& operator=(const CameraSource&) = delete;

    bool open();          // two-step open w/ timeouts + pixel-format + verify read
    void start();         // spawn capture thread
    void stop();          // signal + join + release

    // Drains the latest frame (drops older ones). Returns false if none fresh.
    bool readLatest(cv::Mat& out);

    bool setResolution(int width, int height);
    bool setFps(double fps);
    bool setPixelFormat(const std::string& pixel_format);  // re-applies FOURCC

    // --- Non-blocking reconfigure API -----------------------------------
    // setResolution/setFps/setPixelFormat above touch the V4L2 device and can
    // block for seconds (a FOURCC change re-opens the device; a resolution
    // change renegotiates it). They must never run on the GUI thread, so the
    // methods below hand the request to the capture-loop thread, which is the
    // only thread that owns cap_. The GUI stays responsive.
    void setResolutionAsync(int width, int height);
    void setFpsAsync(double fps);
    void requestPixelFormat(const std::string& pixel_format);
    void applyPending();  // drained by captureLoop()

    // Candidate resolutions for the widget's Res combo (current first).
    std::vector<std::pair<int, int>> enumerateResolutions() const;

    // Formats the device ACTUALLY supports (label + fourcc + valid sizes),
    // queried from v4l2-ctl --list-formats-ext. The UI must only offer these —
    // the device silently falls back to its default for unsupported FOURCCs.
    static std::vector<CameraFormat> enumerateFormats(int index);
    std::vector<CameraFormat> enumerateFormats() const;  // uses this device

    // ---- introspection ----
    bool isOnline() const;   // ONLINE and frame received < 5s ago
    CameraStatus status() const { return status_.load(); }
    std::string statusString() const;
    double fps() const { return fps_.load(); }
    std::pair<int, int> resolution() const { return resolution_; }
    double uptime() const;
    // Frames dropped by the bounded capture ring (consumer couldn't keep up).
    long frameDrops() const { return frame_drops_.load(); }
    void resetFrameDrops() { frame_drops_.store(0); }

    // ---- accessors ----
    const std::string& name() const { return name_; }
    const std::string& cameraId() const { return camera_id_; }
    const std::string& sourceUrl() const { return source_url_; }
    CameraType sourceType() const { return type_; }
    const std::string& pixelFormat() const { return pixel_format_; }
    const std::string& usbSysName() const { return usb_sys_name_; }
    const std::string& usbBusPath() const { return usb_bus_path_; }

    // ---- exposure_dynamic_framerate (per-camera night frame-rate control) ----
    // Capability is probed at runtime per model (never assumed to exist) and
    // the value is always read from the device, never a cached guess.
    std::string devicePath() const;               // "/dev/videoN" or ""
    bool edfSupported() const { return edf_supported_.load(); }
    int edfValue() const { return edf_value_.load(); }  // -1 if unknown
    // Apply the control now; returns 0=ok, -1=unsupported/error, 1=busy,
    // 2=permission denied. Remembered as the persisted preference.
    int setDynamicFramerate(int value);
    // Persisted preference (0/1, -1 = none); re-applied on every open/reconnect.
    void setDynamicFrameratePref(int value) { edf_pref_.store(value); }
    int dynamicFrameratePref() const { return edf_pref_.load(); }
    // Query device support + real value; optionally re-apply the pref.
    void refreshDynamicFramerate(bool apply_pref);

    // ---- static discovery / identity (thread-safe, sequential probing) ----
    static std::vector<int> usbCaptureIndices();
    static UsbIdentity getUsbIdentity(int idx);
    // Find the CURRENT index for a saved identity (bus path first, then name).
    static int resolveUsbIndex(const std::string& bus_path,
                               const std::string& sys_name, int fallback);
    // Sequential v4l2-ctl --stream probe. Call from a worker thread.
    static std::vector<UsbCamera> detectUsbCameras();

    // Returns None-equivalent when url is not USB/RTSP/HTTP.
    static std::optional<CameraType> detectType(const std::string& source_url);

private:
    void captureLoop();
    bool reconnect();
    void applyPixelFormat();  // applies pixel_format_ + verifies; cap_mutex_ held

    std::string name_;
    CameraType type_;
    std::string source_url_;
    std::string camera_id_;
    std::string pixel_format_;

    // identity (filled at open time for USB)
    std::string usb_sys_name_;
    std::string usb_bus_path_;

    // exposure_dynamic_framerate state (thread-safe; -1 = none/unknown)
    std::atomic<bool> edf_supported_{false};
    std::atomic<int> edf_value_{-1};
    std::atomic<int> edf_pref_{-1};

    std::unique_ptr<cv::VideoCapture> cap_;
    mutable std::mutex cap_mutex_;

    // reconfigure request queue (drained by captureLoop on its own thread)
    std::mutex cmd_mutex_;
    std::queue<std::function<void()>> cmd_queue_;
    bool need_reopen_ = false;  // pixel-format change => device reopen

    std::atomic<CameraStatus> status_{CameraStatus::UNKNOWN};
    std::atomic<double> fps_{0.0};
    std::pair<int, int> resolution_{0, 0};
    std::atomic<double> last_frame_time_{0.0};
    std::atomic<long> frame_drops_{0};
    double reconnect_delay_ = 2.0;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_event_{false};
    std::thread thread_;

    // latest-frame ring (max ~2), guarded
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<cv::Mat> frame_queue_;
    int frame_count_ = 0;
    double fps_start_time_ = 0.0;
};

}  // namespace core
