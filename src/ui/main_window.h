#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QTabWidget>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/camera.h"
#include "core/detection_worker.h"
#include "core/face_recognizer.h"
#include "core/object_detector.h"
#include "core/settings.h"
#include "ui/add_camera_dialog.h"
#include "ui/camera_widget.h"
#include "ui/detached_camera_window.h"

namespace ui {

// Main application window. Mirrors ui/main_window.py:
//  - header (theme / layout / gallery / face-db / about / add)
//  - grid view + side drawer with two tabs: CONTROLS + ALERT LOG
//  - full control panel: detection modules, YOLO tuning, HUD visuals, lighting,
//    master recording, sensitivity, poll rate, GPU label, cam list, stop-all
//  - spotlight mode + floating slide-out drawer handle
//  - per-camera widget wiring (popout / spotlight / recording / res/fps/fmt)
//  - persistent settings (theme, modules, yolo, visuals, lighting, cameras)
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // regression self-test (drives the YOLO tuning sliders offscreen)
    int selftestYoloSliders();
    // regression self-test (exercises zoom/night/wheel/fps on live camera cards)
    int selftestCanvas();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onAddCamera();
    void onUsbScanFinished(std::vector<core::UsbCamera> cams);
    void pollCamera(const QString& cid);
    void onLightingChange(int value);
    void onLightModeChanged(int index);

private:
    void setupUi();
    void detectCameras();  // async USB scan
    void loadSavedSettings();
    void saveSettings();

    QWidget* buildControlPanel();
    QWidget* buildEventLogPanel();
    QScrollArea* wrapInScroll(QWidget* w);

    void addCamera(const core::CameraConfig& cfg, bool silent = false);
    void removeCamera(const QString& cid);
    void refreshGrid();
    void refreshCamList();
    void refreshStatus();
    void createWidget(const QString& cid, const core::CameraConfig& cfg = {});
    void applyLightingTo(CameraWidget* w);
    void applyClaheToAll();
    void detachCamera(CameraWidget* widget);
    void reattachCamera(CameraWidget* widget);
    void toggleSpotlight(CameraWidget* widget);

    // wiring helpers
    void setGlobalModule(const QString& key, bool enabled);
    void refreshYoloLabel();
    void addEventLog(const QString& text, const QString& color_hex);

    // widgets (header)
    QComboBox* theme_combo_ = nullptr;
    QComboBox* grid_combo_ = nullptr;
    QLabel* gpu_label_ = nullptr;

    // drawer
    QTabWidget* drawer_tabs_ = nullptr;
    QWidget* drawer_ = nullptr;
    QPushButton* drawer_handle_ = nullptr;
    QSplitter* body_splitter_ = nullptr;

    // detection modules
    QCheckBox* motion_cb_ = nullptr;
    QCheckBox* human_cb_ = nullptr;
    QCheckBox* face_cb_ = nullptr;
    QCheckBox* object_cb_ = nullptr;
    QCheckBox* record_cb_ = nullptr;

    // yolo tuning
    QLabel* yolo_model_label_ = nullptr;
    QPushButton* yolo_reset_btn_ = nullptr;
    QSlider* yolo_conf_slider_ = nullptr;
    QSlider* yolo_nms_slider_ = nullptr;
    QComboBox* yolo_size_combo_ = nullptr;
    QSlider* yolo_min_slider_ = nullptr;
    QSlider* yolo_dark_slider_ = nullptr;

    // visuals
    QCheckBox* hud_cb_ = nullptr;
    QCheckBox* night_cb_ = nullptr;
    QCheckBox* obj_cb_ = nullptr;
    QCheckBox* scan_cb_ = nullptr;

    // lighting
    QComboBox* light_mode_combo_ = nullptr;
    QSlider* light_brightness_ = nullptr;
    QSlider* light_contrast_ = nullptr;
    QSlider* light_saturation_ = nullptr;
    QSlider* light_warmth_ = nullptr;
    int light_brightness_val_ = 0;
    int light_contrast_val_ = 100;
    int light_saturation_val_ = 100;
    int light_warmth_val_ = 0;

    // Task 2 CLAHE night-vision tuning
    QSlider* clahe_clip_slider_ = nullptr;
    QSlider* clahe_tile_slider_ = nullptr;
    QCheckBox* clahe_denoise_cb_ = nullptr;
    QCheckBox* clahe_gamma_cb_ = nullptr;
    QCheckBox* clahe_desat_cb_ = nullptr;

    // recording / performance
    QPushButton* global_pause_btn_ = nullptr;
    QSlider* sens_slider_ = nullptr;
    QComboBox* poll_combo_ = nullptr;

    // cam list + event log + status
    QVBoxLayout* cam_list_layout_ = nullptr;
    QListWidget* event_list_ = nullptr;

    std::vector<core::UsbCamera> usb_cameras_;
    UsbScanWorker* usb_scan_thread_ = nullptr;

    QMap<QString, std::shared_ptr<core::CameraSource>> cameras_;
    QMap<QString, std::shared_ptr<core::DetectionWorker>> workers_;
    QMap<QString, CameraWidget*> widgets_;
    QMap<QString, DetachedCameraWindow*> detached_;
    QMap<QString, QTimer*> timers_;

    std::shared_ptr<core::ObjectDetector> object_detector_;
    std::shared_ptr<core::FaceRecognizer> face_recognizer_;

    QString current_theme_ = "Cyberpunk Neon";
    int grid_cols_ = 2;
    int polls_per_sec_ = 30;
    QString spotlight_cid_;

    // event-log dedup
    struct EventStamp { QString text; qint64 when; };
    std::vector<EventStamp> recent_events_;

    QWidget* grid_host_ = nullptr;
    QGridLayout* grid_layout_ = nullptr;
};

}  // namespace ui
