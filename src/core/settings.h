#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <mutex>
#include <string>
#include <vector>

namespace core {

// Persistent JSON settings at ~/.config/argus/settings.json.
// Mirrors core/settings.py (deep-merge over defaults, never throws on save).
struct CameraConfig {
    std::string name;
    std::string type;         // "usb" | "rtsp" | "http"
    std::string url;
    std::string pixel_format;
    int resolution_w = 0, resolution_h = 0;
    double fps = 0.0;
    std::string usb_sys_name;
    std::string usb_bus_path;
};

struct Settings {
    std::string theme = "Cyberpunk Neon";
    int grid_index = 1;
    int poll_rate = 30;
    int motion_threshold = 5000;

    bool module_motion = true;
    bool module_human = true;
    bool module_face = true;
    bool module_objects = true;
    bool module_recording = false;

    double yolo_conf = 0.50;
    double yolo_nms = 0.45;
    int yolo_input_size = 288;
    double yolo_min_size_percent = 0.02;
    double yolo_dark_gate = 20.0;

    bool visual_hud = true;
    bool visual_night = false;
    bool visual_object_labels = true;
    bool visual_scanlines = false;

    std::string light_mode = "auto";
    int brightness = 0;
    int contrast = 100;
    int saturation = 100;
    int warmth = 0;

    std::vector<CameraConfig> cameras;
};

class SettingsStore {
public:
    static std::string configPath();
    Settings load();
    void save(const Settings& s);
};

}  // namespace core
