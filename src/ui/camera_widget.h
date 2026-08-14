#pragma once

#include <QComboBox>
#include <QLabel>
#include <QWidget>

#include <memory>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "core/camera.h"
#include "core/detection_worker.h"

namespace ui {

class VideoCanvas;

// One camera feed card: header (tool buttons) + collapsible Res/FPS/Fmt panel +
// video canvas + footer status. Mirrors ui/camera_widget.py.
class CameraWidget : public QWidget {
    Q_OBJECT
public:
    explicit CameraWidget(std::shared_ptr<core::CameraSource> camera,
                          QWidget* parent = nullptr);

    std::shared_ptr<core::CameraSource> camera() const { return camera_; }

    // Slots (GUI thread, from the poll loop)
    void updateFrame(core::DetectionResult result);

    // ---- visual toggles (global controls drive these) ----
    void setShowNightVision(bool v);
    void setShowHud(bool v);
    void setShowObjectLabels(bool v);
    void setShowScanlines(bool v);

    // Night-vision CLAHE is applied on the worker thread; this wires the worker
    // so per-widget toggles/modes reach it (keeps the GUI paint path cheap).
    void setDetectionWorker(std::shared_ptr<core::DetectionWorker> worker);

    // ---- lighting & light mode ----
    void setLighting(int brightness, int contrast, int saturation, int warmth);
    void setLightMode(const std::string& mode);

    // ---- resolution / fps / pixel-format combos ----
    void setResolutions(const std::vector<std::pair<int, int>>& sizes);
    std::pair<int, int> selectedResolution() const;
    void selectResolution(int width, int height);
    void setPixelFormats(bool is_usb, const std::string& current);
    void setCurrentPixelFormat(const std::string& fmt);
    void setFps(int fps);
    int selectedFps() const;

    // ---- recording / pause state (from worker results) ----
    void setRecordingState(bool rec);
    void setPausedState(bool paused);

    // ---- actions ----
    void takeSnapshot();

    // Latest raw frame (shared handle, cheap). Empty if no frame yet.
    cv::Mat latestFrame() const;

signals:
    void removeRequested();
    void spotlightRequested();
    void popoutRequested();
    void recordingToggled(bool);
    void stopRecordingRequested();
    void pauseRecordingRequested();
    void resolutionChanged(int width, int height);
    void fpsChanged(int fps);
    void pixelFormatChanged(const QString& fmt);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void buildHeader();
    void buildSettingsPanel();
    void buildFooter();
    void toggleSettingsPanel();

    std::shared_ptr<core::CameraSource> camera_;
    std::shared_ptr<core::DetectionWorker> worker_;
    VideoCanvas* canvas_ = nullptr;

    // header widgets
    class HeaderWidget;
    HeaderWidget* header_ = nullptr;
    QWidget* settings_panel_ = nullptr;
    QWidget* footer_ = nullptr;
    QComboBox* res_combo_ = nullptr;
    QComboBox* fps_combo_ = nullptr;
    QComboBox* fmt_combo_ = nullptr;
    QLabel* fmt_label_ = nullptr;
    QLabel* footer_fps_ = nullptr;

    bool settings_open_ = false;
    bool recording_ = false;
};

}  // namespace ui
