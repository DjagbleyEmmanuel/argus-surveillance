#include "core/settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QStandardPaths>

namespace core {

std::string SettingsStore::configPath() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return (dir + "/argus/settings.json").toStdString();
}

Settings SettingsStore::load() {
    Settings s;
    QString path = QString::fromStdString(configPath());
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return s;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return s;
    QJsonObject root = doc.object();

    s.theme = root.value("theme").toString(s.theme.c_str()).toStdString();
    s.grid_index = root.value("grid_index").toInt(s.grid_index);
    s.poll_rate = root.value("poll_rate").toInt(s.poll_rate);
    s.motion_threshold = root.value("motion_threshold").toInt(s.motion_threshold);

    QJsonObject modules = root.value("modules").toObject();
    s.module_motion = modules.value("motion").toBool(s.module_motion);
    s.module_human = modules.value("human").toBool(s.module_human);
    s.module_face = modules.value("face").toBool(s.module_face);
    s.module_objects = modules.value("objects").toBool(s.module_objects);
    s.module_recording = modules.value("recording").toBool(s.module_recording);

    QJsonObject yolo = root.value("yolo").toObject();
    s.yolo_conf = yolo.value("conf").toDouble(s.yolo_conf);
    s.yolo_nms = yolo.value("nms").toDouble(s.yolo_nms);
    s.yolo_input_size = yolo.value("input_size").toInt(s.yolo_input_size);
    s.yolo_min_size_percent = yolo.value("min_size_percent").toDouble(s.yolo_min_size_percent);
    s.yolo_dark_gate = yolo.value("dark_gate").toDouble(s.yolo_dark_gate);

    QJsonObject visuals = root.value("visuals").toObject();
    s.visual_hud = visuals.value("hud").toBool(s.visual_hud);
    s.visual_night = visuals.value("night").toBool(s.visual_night);
    s.visual_object_labels = visuals.value("object_labels").toBool(s.visual_object_labels);
    s.visual_scanlines = visuals.value("scanlines").toBool(s.visual_scanlines);

    QJsonObject lighting = root.value("lighting").toObject();
    s.light_mode = lighting.value("light_mode").toString(QString::fromStdString(s.light_mode)).toStdString();
    s.brightness = lighting.value("brightness").toInt(s.brightness);
    s.contrast = lighting.value("contrast").toInt(s.contrast);
    s.saturation = lighting.value("saturation").toInt(s.saturation);
    s.warmth = lighting.value("warmth").toInt(s.warmth);

    QJsonObject clahe = root.value("clahe").toObject();
    s.clahe_clip = clahe.value("clip").toDouble(s.clahe_clip);
    s.clahe_tile = clahe.value("tile").toInt(s.clahe_tile);
    s.clahe_denoise = clahe.value("denoise").toBool(s.clahe_denoise);
    s.clahe_gamma = clahe.value("gamma").toBool(s.clahe_gamma);
    s.clahe_desat = clahe.value("desat").toBool(s.clahe_desat);

    for (const auto& v : root.value("cameras").toArray()) {
        QJsonObject c = v.toObject();
        CameraConfig cfg;
        cfg.name = c.value("name").toString().toStdString();
        cfg.type = c.value("type").toString(QStringLiteral("usb")).toStdString();
        cfg.url = c.value("url").toString().toStdString();
        cfg.pixel_format = c.value("pixel_format").toString().toStdString();
        if (c.contains("resolution")) {
            auto res = c.value("resolution").toArray();
            if (res.size() >= 2) {
                cfg.resolution_w = res[0].toInt();
                cfg.resolution_h = res[1].toInt();
            }
        }
        cfg.fps = c.value("fps").toDouble();
        cfg.usb_sys_name = c.value("usb_sys_name").toString().toStdString();
        cfg.usb_bus_path = c.value("usb_bus_path").toString().toStdString();
        cfg.edf_value = c.value("edf_value").toInt(-1);
        s.cameras.push_back(std::move(cfg));
    }
    return s;
}

void SettingsStore::save(const Settings& s) {
    QJsonObject root;
    root.insert("theme", QString::fromStdString(s.theme));
    root.insert("grid_index", s.grid_index);
    root.insert("poll_rate", s.poll_rate);
    root.insert("motion_threshold", s.motion_threshold);

    QJsonObject modules;
    modules.insert("motion", s.module_motion);
    modules.insert("human", s.module_human);
    modules.insert("face", s.module_face);
    modules.insert("objects", s.module_objects);
    modules.insert("recording", s.module_recording);
    root.insert("modules", modules);

    QJsonObject yolo;
    yolo.insert("conf", s.yolo_conf);
    yolo.insert("nms", s.yolo_nms);
    yolo.insert("input_size", s.yolo_input_size);
    yolo.insert("min_size_percent", s.yolo_min_size_percent);
    yolo.insert("dark_gate", s.yolo_dark_gate);
    root.insert("yolo", yolo);

    QJsonObject visuals;
    visuals.insert("hud", s.visual_hud);
    visuals.insert("night", s.visual_night);
    visuals.insert("object_labels", s.visual_object_labels);
    visuals.insert("scanlines", s.visual_scanlines);
    root.insert("visuals", visuals);

    QJsonObject lighting;
    lighting.insert("light_mode", QString::fromStdString(s.light_mode));
    lighting.insert("brightness", s.brightness);
    lighting.insert("contrast", s.contrast);
    lighting.insert("saturation", s.saturation);
    lighting.insert("warmth", s.warmth);
    root.insert("lighting", lighting);

    QJsonObject clahe;
    clahe.insert("clip", s.clahe_clip);
    clahe.insert("tile", s.clahe_tile);
    clahe.insert("denoise", s.clahe_denoise);
    clahe.insert("gamma", s.clahe_gamma);
    clahe.insert("desat", s.clahe_desat);
    root.insert("clahe", clahe);

    QJsonArray cameras;
    for (const auto& c : s.cameras) {
        QJsonObject co;
        co.insert("name", QString::fromStdString(c.name));
        co.insert("type", QString::fromStdString(c.type));
        co.insert("url", QString::fromStdString(c.url));
        co.insert("pixel_format", QString::fromStdString(c.pixel_format));
        QJsonArray res;
        res.append(c.resolution_w);
        res.append(c.resolution_h);
        co.insert("resolution", res);
        co.insert("fps", c.fps);
        co.insert("usb_sys_name", QString::fromStdString(c.usb_sys_name));
        co.insert("usb_bus_path", QString::fromStdString(c.usb_bus_path));
        co.insert("edf_value", c.edf_value);
        cameras.append(co);
    }
    root.insert("cameras", cameras);

    QString dir = QString::fromStdString(configPath());
    dir = dir.mid(0, dir.lastIndexOf('/'));
    QDir().mkpath(dir);
    QFile f(QString::fromStdString(configPath()));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

}  // namespace core
