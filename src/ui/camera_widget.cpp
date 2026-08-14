#include "ui/camera_widget.h"

#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

#include "core/detector.h"

namespace ui {

namespace {

// Rounded-header helper for the camera card top bar (keeps the paint area for
// the video canvas below it, matching the Python header widget).
QString toolButtonStyle(const QString& color, const QString& hover) {
    return QString(
        "QPushButton { background: rgba(30,41,59,0.7); border: 1px solid rgba(255,255,255,0.08);"
        "border-radius: 6px; color: %1; font-size: 11px; font-weight: 700; padding: 3px 6px; }"
        "QPushButton:hover { background: rgba(51,65,85,0.9); border-color: %1; color: %2; }"
        "QPushButton:pressed { background: rgba(15,23,42,0.9); }"
        "QPushButton:checked { background: rgba(56,189,248,0.25); border-color: #38bdf8; color: #ffffff; }")
        .arg(color, hover);
}

}  // namespace

// ===========================================================================
// VideoCanvas — paints the frame with all visual effects + overlays, and owns
// the zoom/pan physics + snapshot capture.
// ===========================================================================
class VideoCanvas : public QWidget {
public:
    explicit VideoCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(280, 180);
        setMouseTracking(true);
        snap_timer_ = new QTimer(this);
        snap_timer_->setSingleShot(true);
        snap_timer_->setInterval(1200);
        connect(snap_timer_, &QTimer::timeout, this, [this] { show_snap_overlay_ = false; update(); });
    }

    void updateFrame(core::DetectionResult result) {
        result_ = std::move(result);
        have_frame_ = !result_.frame.empty();
        // frozen: keep showing the last frame, don't repaint from new data
        if (frozen_) return;
        update();
    }

    bool hasFrame() const { return have_frame_; }

    cv::Mat latestRawFrame() const { return result_.frame; }

    void setShowNightVision(bool v) { show_night_vision_ = v; qInfo("NIGHT %s (mode=%s)", v ? "ON" : "OFF", light_mode_.c_str()); }
    bool nightVision() const { return show_night_vision_; }
    const std::string& lightMode() const { return light_mode_; }
    void setShowHud(bool v) { show_hud_ = v; }
    void setShowObjectLabels(bool v) { show_object_labels_ = v; }
    void setShowScanlines(bool v) { show_scanlines_ = v; }
    void setLighting(int b, int c, int s, int w) {
        brightness_ = b; contrast_ = c; saturation_ = s; warmth_ = w;
    }
    void setLightMode(const std::string& mode) { light_mode_ = mode; }

    void setFrozen(bool v) {
        frozen_ = v;
        if (v && have_frame_) frozen_mat_ = result_.frame.clone();
        else frozen_mat_.release();
        update();
    }
    bool frozen() const { return frozen_; }

    void takeSnapshot() {
        if (!have_frame_ || result_.frame.empty()) return;
        QDir().mkpath("snapshots");
        cv::Mat img = result_.frame.clone();
        int h = img.rows, w = img.cols;

        QString dt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString cam_clean = QString::fromStdString(camera_name_);
        cam_clean.replace(' ', '_');
        QString path = QString("snapshots/%1_%2.png").arg(cam_clean, ts);

        // bottom watermark bar
        cv::rectangle(img, {0, h - 30}, {w, h}, cv::Scalar(10, 10, 20), cv::FILLED);
        cv::addWeighted(img, 1.0, img, 0.0, 0, img);
        QString watermark = QString("ARGUS SNAPSHOT | %1 | %2")
                                .arg(QString::fromStdString(camera_name_).toUpper(), dt);
        cv::putText(img, watermark.toStdString(), {12, h - 9},
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(250, 204, 21), 1);

        if (cv::imwrite(path.toStdString(), img)) {
            show_snap_overlay_ = true;
            snap_timer_->start();
            update();
        }
    }

    void setZoomActive(bool v) {
        zoom_active_ = v;
        if (!v) {
            target_zoom_ = 1.0;
            target_pan_x_ = 0.0;
            target_pan_y_ = 0.0;
        }
        qInfo("ZOOM %s (target=%.2fx)", v ? "ON" : "OFF", v ? target_zoom_ : 1.0);
        setCursor(v ? Qt::CrossCursor : Qt::ArrowCursor);
    }
    bool zoomActive() const { return zoom_active_; }

    void setCameraName(const std::string& n) { camera_name_ = n; }
    void setStatusString(const std::string& s) { status_string_ = s; }

    std::function<void()> on_spotlight;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(8, 10, 18));

        if (!have_frame_) {
            p.setPen(QColor(148, 163, 184));
            p.drawText(rect(), Qt::AlignCenter,
                       QString("Connecting…\n%1").arg(QString::fromStdString(status_string_)));
            return;
        }

        cv::Mat display;
        if (frozen_ && !frozen_mat_.empty()) {
            display = frozen_mat_.clone();
        } else {
            display = result_.frame.clone();  // working copy for lighting/overlay
        }

        applyLighting(display);

        if (show_hud_) {
            for (const auto& b : result_.motion_boxes)
                core::MotionDetector::drawTacticalBox(display, b.x, b.y, b.w, b.h,
                                                      b.color, "MOTION", b.confidence);
            for (const auto& b : result_.human_boxes)
                core::MotionDetector::drawTacticalBox(display, b.x, b.y, b.w, b.h,
                                                      b.color, "TARGET-HUMAN", b.confidence);
            for (const auto& b : result_.face_boxes)
                core::MotionDetector::drawTacticalBox(display, b.x, b.y, b.w, b.h,
                                                      b.color, b.label, b.confidence);
        }
        if (show_object_labels_) {
            for (const auto& b : result_.object_boxes)
                core::MotionDetector::drawTacticalBox(display, b.x, b.y, b.w, b.h,
                                                      b.color, b.label, b.confidence);
        }

        if (show_scanlines_) {
            for (int y = 0; y < display.rows; y += 3)
                cv::line(display, {0, y}, {display.cols, y}, cv::Scalar(0, 0, 0, 40), 1);
        }

        // --- zoom / pan (eased, done in painter space for glitch-free sampling) ---
        if (zoom_active_ || std::abs(current_zoom_ - 1.0) > 0.01) {
            current_zoom_ += (target_zoom_ - current_zoom_) * 0.15;
            current_pan_x_ += (target_pan_x_ - current_pan_x_) * 0.12;
            current_pan_y_ += (target_pan_y_ - current_pan_y_) * 0.12;
        }

        // to QImage (share buffer; safe while display is local)
        int w = display.cols, h = display.rows;
        if (w <= 0 || h <= 0) return;
        QImage qimg(display.data, w, h, static_cast<int>(display.step),
                    QImage::Format_BGR888);

        // visible source rect: the sub-region of the frame shown at zoom level
        double z = std::max(1.0, current_zoom_);
        int cw = std::clamp(static_cast<int>(std::lround(w / z)), 1, w);
        int ch = std::clamp(static_cast<int>(std::lround(h / z)), 1, h);
        int cx = static_cast<int>(std::lround(
            std::clamp(current_pan_x_, 0.0, static_cast<double>(w - cw))));
        int cy = static_cast<int>(std::lround(
            std::clamp(current_pan_y_, 0.0, static_cast<double>(h - ch))));
        QRectF src(cx, cy, cw, ch);

        // keep aspect ratio, centered
        QRect target = rect();
        double scale = std::min(double(width()) / cw, double(height()) / ch);
        int tw = static_cast<int>(std::lround(cw * scale));
        int th = static_cast<int>(std::lround(ch * scale));
        target.setSize({tw, th});
        target.moveCenter(rect().center());
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(target, qimg, src);

        // --- HUD text overlay ---
        bool has_motion = !result_.motion_boxes.empty();
        bool has_humans = !result_.human_boxes.empty();
        bool has_faces = !result_.face_boxes.empty();
        bool has_objects = !result_.object_boxes.empty();
        bool has_problems = !result_.problems.empty();

        QColor status_color(56, 189, 248);
        if (has_humans || has_faces) status_color = QColor(74, 222, 128);
        else if (has_objects) status_color = QColor(251, 146, 60);
        else if (has_motion) status_color = QColor(251, 191, 36);
        else if (has_problems) status_color = QColor(244, 63, 94);

        p.setPen(QColor(0, 255, 204));
        p.drawText(10, 18, QString("FPS %1").arg(result_.fps, 0, 'f', 1));
        p.setPen(QColor(100, 116, 139));
        p.drawText(10, 32, QString("%1x%2").arg(w).arg(h));

        if (result_.recording) {
            p.setPen(QColor(244, 63, 94));
            p.setBrush(QColor(244, 63, 94));
            p.drawEllipse(rect().width() - 70, 14, 8, 8);
            p.setPen(QColor(244, 63, 94));
            p.drawText(rect().width() - 58, 22, "REC");
        }

        // --- overlays ---
        if (result_.paused) drawCenteredText(p, "⏸ PAUSED", QColor(251, 191, 36), 26, QColor(15, 23, 42, 220));
        if (frozen_) drawCenteredText(p, "❄ STREAM FROZEN", QColor(56, 189, 248), 20, QColor(56, 189, 248, 40));
        if (show_snap_overlay_) drawCenteredText(p, "📸 SNAPSHOT SAVED", QColor(250, 204, 21), 20, QColor(250, 204, 21, 50));

        p.setPen(status_color);
        p.drawEllipse(rect().width() - 24, 14, 10, 10);
    }

    void wheelEvent(QWheelEvent* event) override {
        if (!zoom_active_) { event->ignore(); return; }
        double delta = event->angleDelta().y();
        if (delta > 0) target_zoom_ = std::min(6.0, target_zoom_ * 1.06);
        else target_zoom_ = std::max(1.0, target_zoom_ / 1.06);
        qInfo("ZOOM wheel -> target %.2fx", target_zoom_);
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pan_start_ = event->pos();
            dragging_ = false;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (zoom_active_ && pan_start_ && event->buttons() & Qt::LeftButton) {
            dragging_ = true;
            if (have_frame_) {
                int w = result_.frame.cols, h = result_.frame.rows;
                double z = std::max(1.0, current_zoom_);
                int dx = event->pos().x() - pan_start_->x();
                int dy = event->pos().y() - pan_start_->y();
                double max_px = std::max(0.0, w - w / z);
                double max_py = std::max(0.0, h - h / z);
                target_pan_x_ = std::clamp(target_pan_x_ - dx * z * 0.15, 0.0, max_px);
                target_pan_y_ = std::clamp(target_pan_y_ - dy * z * 0.15, 0.0, max_py);
                pan_start_ = event->pos();
            }
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (!dragging_ && on_spotlight) on_spotlight();
            pan_start_.reset();
            dragging_ = false;
        }
    }

private:
    void drawCenteredText(QPainter& p, const QString& text, const QColor& fg,
                          int font_size, const QColor& bg) {
        QRect box(0, 0, width() - 80, 60);
        box.moveCenter(rect().center());
        if (bg.alpha() > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(box.adjusted(0, 0, 0, 0), 8, 8);
        }
        QFont f = p.font();
        f.setPointSize(font_size);
        f.setBold(true);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(box, Qt::AlignCenter, text);
    }

    void applyLighting(cv::Mat& display) {
        if (brightness_ != 0 || contrast_ != 100)
            cv::convertScaleAbs(display, display, contrast_ / 100.0, brightness_);
        if (saturation_ != 100) {
            cv::Mat hsv;
            cv::cvtColor(display, hsv, cv::COLOR_BGR2HSV);
            cv::Mat chan;
            cv::extractChannel(hsv, chan, 1);
            cv::multiply(chan, saturation_ / 100.0, chan);
            cv::insertChannel(chan, hsv, 1);
            cv::cvtColor(hsv, display, cv::COLOR_HSV2BGR);
        }
        if (warmth_ != 0) {
            int wv = warmth_;
            double r_gain = 1.0 + std::max(0, wv) / 200.0;
            double b_gain = 1.0 + std::max(0, -wv) / 200.0;
            cv::Mat lut(1, 256, CV_8UC3);
            for (int i = 0; i < 256; ++i) {
                lut.at<cv::Vec3b>(0, i) = cv::Vec3b(
                    static_cast<uchar>(std::min(255.0, i * b_gain)),
                    static_cast<uchar>(i),
                    static_cast<uchar>(std::min(255.0, i * r_gain)));
            }
            cv::LUT(display, lut, display);
        }
    }

    core::DetectionResult result_;
    bool have_frame_ = false;
    cv::Mat frozen_mat_;
    std::string camera_name_;
    std::string status_string_;

    bool show_night_vision_ = false;
    bool show_hud_ = true;
    bool show_object_labels_ = true;
    bool show_scanlines_ = false;
    std::string light_mode_ = "auto";

    int brightness_ = 0;
    int contrast_ = 100;
    int saturation_ = 100;
    int warmth_ = 0;

    bool frozen_ = false;
    bool zoom_active_ = false;
    double target_zoom_ = 1.0, current_zoom_ = 1.0;
    double target_pan_x_ = 0.0, target_pan_y_ = 0.0;
    double current_pan_x_ = 0.0, current_pan_y_ = 0.0;
    std::optional<QPoint> pan_start_;
    bool dragging_ = false;

    bool show_snap_overlay_ = false;
    QTimer* snap_timer_ = nullptr;
};

// ===========================================================================
// CameraWidget
// ===========================================================================

class CameraWidget::HeaderWidget : public QWidget {
public:
    QLabel* status_icon = nullptr;
    QLabel* name_label = nullptr;
    QLabel* rec_indicator = nullptr;
    QPushButton* snap_btn = nullptr;
    QPushButton* night_btn = nullptr;
    QPushButton* freeze_btn = nullptr;
    QPushButton* rec_btn = nullptr;
    QPushButton* hud_btn = nullptr;
    QPushButton* obj_btn = nullptr;
    QPushButton* scan_btn = nullptr;
    QPushButton* zoom_btn = nullptr;
    QPushButton* popout_btn = nullptr;
    QPushButton* spotlight_btn = nullptr;
    QPushButton* settings_btn = nullptr;
    QPushButton* remove_btn = nullptr;
    QPushButton* edf_btn = nullptr;
};

CameraWidget::CameraWidget(std::shared_ptr<core::CameraSource> camera, QWidget* parent)
    : QWidget(parent), camera_(std::move(camera)) {
    setMinimumSize(320, 220);
    setObjectName("cameraWidget");
    buildUi();

    // Periodic mirror of the real on-device control (never a cached guess).
    // Also re-probes support on unsupported models so a hotplugged camera that
    // now exposes the control picks it up.
    edf_timer_ = new QTimer(this);
    edf_timer_->setInterval(4000);
    connect(edf_timer_, &QTimer::timeout, this, &CameraWidget::refreshDynamicFramerate);
    edf_timer_->start();
    QTimer::singleShot(0, this, &CameraWidget::refreshDynamicFramerate);
}

void CameraWidget::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buildHeader();
    layout->addWidget(header_);

    buildSettingsPanel();
    layout->addWidget(settings_panel_);

    canvas_ = new VideoCanvas(this);
    canvas_->setObjectName("videoCanvas");
    canvas_->setCameraName(camera_->name());
    canvas_->setStatusString(camera_->statusString());
    canvas_->on_spotlight = [this] { emit spotlightRequested(); };
    layout->addWidget(canvas_, 1);

    buildFooter();
    layout->addWidget(footer_);
}

void CameraWidget::buildHeader() {
    header_ = new HeaderWidget;
    header_->setFixedHeight(38);
    header_->setStyleSheet(
        "background: rgba(15,23,42,0.85); border-top-left-radius: 10px; border-top-right-radius: 10px;"
        "border-bottom: 1px solid rgba(255,255,255,0.08);");
    auto* hdr = new QHBoxLayout(header_);
    hdr->setContentsMargins(10, 2, 8, 2);
    hdr->setSpacing(4);

    header_->status_icon = new QLabel("●");
    header_->status_icon->setStyleSheet("color: #00ffcc; font-size: 13px; background: transparent;");
    header_->status_icon->setFixedWidth(16);

    header_->name_label = new QLabel(QString::fromStdString(camera_->name()));
    header_->name_label->setStyleSheet(
        "color: #f8fafc; font-size: 13px; font-weight: 700; letter-spacing: 0.5px; background: transparent;");

    header_->rec_indicator = new QLabel("● REC");
    header_->rec_indicator->setStyleSheet(
        "color: #f43f5e; font-size: 10px; font-weight: 800; background: rgba(244,63,94,0.15);"
        "border: 1px solid rgba(244,63,94,0.4); border-radius: 6px; padding: 2px 6px;");
    header_->rec_indicator->setVisible(false);

    hdr->addWidget(header_->status_icon);
    hdr->addWidget(header_->name_label);
    hdr->addWidget(header_->rec_indicator);
    hdr->addStretch();

    auto add_tool = [&](QPushButton*& btn, const QString& text, const QString& tip,
                        const QString& color, const QString& hover) {
        btn = new QPushButton(text);
        btn->setToolTip(tip);
        btn->setStyleSheet(toolButtonStyle(color, hover));
        hdr->addWidget(btn);
    };

    add_tool(header_->snap_btn, "📸", "Capture HD Snapshot Watermark", "#facc15", "#fde047");
    add_tool(header_->night_btn, "🌙", "Toggle Digital Night Vision (CLAHE Enhancer)", "#c084fc", "#e879f9");
    add_tool(header_->freeze_btn, "❄", "Freeze/Unfreeze frame stream", "#38bdf8", "#7dd3fc");
    add_tool(header_->rec_btn, "REC", "Record on motion trigger", "#f43f5e", "#fda4af");
    add_tool(header_->hud_btn, "HUD", "Toggle Sci-Fi Tactical Reticles", "#38bdf8", "#7dd3fc");
    add_tool(header_->obj_btn, "OBJ", "Toggle YOLO Object Labels", "#fb923c", "#fdba74");
    add_tool(header_->scan_btn, "CRT", "Toggle CRT Scanlines", "#4ade80", "#86efac");
    add_tool(header_->zoom_btn, "🔍", "Toggle Smooth Pan / Zoom", "#38bdf8", "#7dd3fc");
    add_tool(header_->popout_btn, "⧉", "Detach stream to Multi-Monitor Window", "#38bdf8", "#7dd3fc");
    add_tool(header_->spotlight_btn, "⛶", "Full Application Screen (Spotlight)", "#38bdf8", "#7dd3fc");
    add_tool(header_->settings_btn, "⚙", "Resolution/FPS Settings", "#94a3b8", "#cbd5e1");
    // exposure_dynamic_framerate: per-camera night control. Hidden unless the
    // device actually exposes it (runtime-probed, never assumed). Value is
    // mirrored from the device on an interval.
    add_tool(header_->edf_btn, "DR", "Toggle dynamic framerate (exposure_dynamic_framerate)", "#34d399", "#6ee7b7");
    header_->edf_btn->setCheckable(true);
    header_->edf_btn->setVisible(false);
    connect(header_->edf_btn, &QPushButton::toggled, this,
            &CameraWidget::onDynamicFramerateToggled);
    add_tool(header_->remove_btn, "✕", "Remove camera module", "#f43f5e", "#fda4af");

    connect(header_->snap_btn, &QPushButton::clicked, this, [this] { canvas_->takeSnapshot(); });

    header_->night_btn->setCheckable(true);
    connect(header_->night_btn, &QPushButton::toggled, this,
            [this](bool v) { setShowNightVision(v); });

    header_->freeze_btn->setCheckable(true);
    connect(header_->freeze_btn, &QPushButton::toggled, this,
            [this](bool v) { canvas_->setFrozen(v); });

    header_->rec_btn->setCheckable(true);
    connect(header_->rec_btn, &QPushButton::toggled, this,
            [this](bool v) { emit recordingToggled(v); });

    header_->hud_btn->setCheckable(true);
    header_->hud_btn->setChecked(true);
    connect(header_->hud_btn, &QPushButton::toggled, this,
            [this](bool v) { canvas_->setShowHud(v); });

    header_->obj_btn->setCheckable(true);
    header_->obj_btn->setChecked(true);
    connect(header_->obj_btn, &QPushButton::toggled, this,
            [this](bool v) { canvas_->setShowObjectLabels(v); });

    header_->scan_btn->setCheckable(true);
    connect(header_->scan_btn, &QPushButton::toggled, this,
            [this](bool v) { canvas_->setShowScanlines(v); });

    header_->zoom_btn->setCheckable(true);
    connect(header_->zoom_btn, &QPushButton::toggled, this,
            [this](bool v) { canvas_->setZoomActive(v); });

    connect(header_->popout_btn, &QPushButton::clicked, this, [this] { emit popoutRequested(); });
    connect(header_->spotlight_btn, &QPushButton::clicked, this, [this] { emit spotlightRequested(); });
    connect(header_->settings_btn, &QPushButton::clicked, this, &CameraWidget::toggleSettingsPanel);
    connect(header_->remove_btn, &QPushButton::clicked, this, [this] { emit removeRequested(); });
}

void CameraWidget::buildSettingsPanel() {
    settings_panel_ = new QWidget;
    settings_panel_->setFixedHeight(0);
    settings_panel_->setStyleSheet(
        "background: rgba(15,23,42,0.95); border-bottom: 1px solid rgba(255,255,255,0.08);");
    auto* sl = new QHBoxLayout(settings_panel_);
    sl->setContentsMargins(8, 2, 8, 2);
    sl->setSpacing(6);

    auto* res_lbl = new QLabel("Res:");
    res_combo_ = new QComboBox;
    res_combo_->setFixedHeight(24);
    res_combo_->setMinimumWidth(90);

    auto* fps_lbl = new QLabel("FPS:");
    fps_combo_ = new QComboBox;
    fps_combo_->setFixedHeight(24);
    fps_combo_->setMinimumWidth(60);
    fps_combo_->addItems({"5", "10", "15", "20", "25", "30"});
    fps_combo_->setCurrentText("15");

    fmt_label_ = new QLabel("Fmt:");
    fmt_combo_ = new QComboBox;
    fmt_combo_->setFixedHeight(24);
    fmt_combo_->setMinimumWidth(110);
    fmt_combo_->setToolTip("Capture output format (USB webcams only)");

    sl->addWidget(res_lbl);
    sl->addWidget(res_combo_);
    sl->addWidget(fps_lbl);
    sl->addWidget(fps_combo_);
    sl->addWidget(fmt_label_);
    sl->addWidget(fmt_combo_);
    sl->addStretch();

    connect(res_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                auto r = selectedResolution();
                emit resolutionChanged(r.first, r.second);
            });
    connect(fps_combo_, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
            this, [this](const QString& t) {
                bool ok = false;
                int v = t.toInt(&ok);
                if (ok) emit fpsChanged(v);
            });
    connect(fmt_combo_, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
            this, [this](const QString& t) { emit pixelFormatChanged(t); });
}

void CameraWidget::buildFooter() {
    footer_ = new QWidget;
    footer_->setFixedHeight(24);
    footer_->setStyleSheet(
        "background: rgba(15,23,42,0.85); border-bottom-left-radius: 10px; border-bottom-right-radius: 10px;"
        "border-top: 1px solid rgba(255,255,255,0.08);");
    auto* ftr = new QHBoxLayout(footer_);
    ftr->setContentsMargins(10, 0, 10, 0);
    ftr->setSpacing(6);

    footer_fps_ = new QLabel("0.0 FPS");
    footer_fps_->setObjectName("camFooterFps");
    footer_fps_->setStyleSheet("color: #4ade80; font-size: 11px; font-weight: 700; background: transparent;");
    auto* info = new QLabel("");
    info->setObjectName("camFooterInfo");
    info->setStyleSheet("color: #fbbf24; font-size: 10px; font-weight: 700; background: transparent;");
    auto* res = new QLabel("");
    res->setObjectName("camFooterRes");
    res->setStyleSheet("color: #64748b; font-size: 10px; background: transparent;");

    ftr->addWidget(footer_fps_);
    ftr->addStretch();
    ftr->addWidget(info);
    ftr->addStretch();
    ftr->addWidget(res);
}

void CameraWidget::toggleSettingsPanel() {
    settings_open_ = !settings_open_;
    settings_panel_->setFixedHeight(settings_open_ ? 28 : 0);
}

void CameraWidget::refreshDynamicFramerate() {
    if (!camera_->edfSupported()) {
        // Hotplug re-detect: an unsupported model may gain the control.
        camera_->refreshDynamicFramerate(false);
        if (!camera_->edfSupported()) return;
    }
    const int val = camera_->edfValue();
    if (val < 0) return;
    edf_supported_ = true;
    edf_value_ = val;
    header_->edf_btn->setVisible(true);
    QSignalBlocker blocker(header_->edf_btn);
    header_->edf_btn->setChecked(val == 1);
    header_->edf_btn->setText(val == 1 ? "DR●" : "DR");
}

void CameraWidget::onDynamicFramerateToggled(bool checked) {
    const int want = checked ? 1 : 0;
    const int rc = camera_->setDynamicFramerate(want);
    header_->edf_btn->setText(checked ? "DR●" : "DR");
    if (rc == 0) {
        edf_value_ = want;
        emit dynamicFramerateToggled(want);
        return;
    }
    // Never fail silently: revert the button and explain why.
    QSignalBlocker blocker(header_->edf_btn);
    header_->edf_btn->setChecked(!checked);
    header_->edf_btn->setText(!checked ? "DR●" : "DR");
    QString why;
    switch (rc) {
        case 1: why = QStringLiteral("the device is busy — try again"); break;
        case 2: why = QStringLiteral("permission denied on the video device"); break;
        default: why = QStringLiteral("control not supported on this camera"); break;
    }
    QMessageBox::warning(this, QStringLiteral("Dynamic Framerate"),
        QStringLiteral("Could not set exposure_dynamic_framerate=%1 on %2.\n%3")
            .arg(want).arg(QString::fromStdString(camera_->name())).arg(why));
}

void CameraWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    canvas_->update();
}

void CameraWidget::updateFrame(core::DetectionResult result) {
    if (footer_fps_)
        footer_fps_->setText(QString("%1 FPS").arg(result.fps, 0, 'f', 1));
    recording_ = result.recording;
    canvas_->updateFrame(std::move(result));
    setRecordingState(recording_);
}

void CameraWidget::setRecordingState(bool rec) {
    header_->rec_indicator->setVisible(rec);
    header_->rec_btn->blockSignals(true);
    header_->rec_btn->setChecked(rec);
    header_->rec_btn->blockSignals(false);
}

void CameraWidget::setPausedState(bool paused) { canvas_->update(); }

void CameraWidget::setShowNightVision(bool v) {
    canvas_->setShowNightVision(v);
    header_->night_btn->setChecked(v);
    if (worker_) worker_->updateNightVision(v, canvas_->lightMode());
}
void CameraWidget::setShowHud(bool v) { canvas_->setShowHud(v); header_->hud_btn->setChecked(v); }
void CameraWidget::setShowObjectLabels(bool v) { canvas_->setShowObjectLabels(v); header_->obj_btn->setChecked(v); }
void CameraWidget::setShowScanlines(bool v) { canvas_->setShowScanlines(v); header_->scan_btn->setChecked(v); }
void CameraWidget::setLighting(int b, int c, int s, int w) { canvas_->setLighting(b, c, s, w); }
void CameraWidget::setLightMode(const std::string& m) {
    canvas_->setLightMode(m);
    if (worker_) worker_->updateNightVision(canvas_->nightVision(), m);
}
void CameraWidget::setDetectionWorker(std::shared_ptr<core::DetectionWorker> worker) {
    worker_ = std::move(worker);
    if (worker_) worker_->updateNightVision(canvas_->nightVision(), canvas_->lightMode());
}

void CameraWidget::setResolutions(const std::vector<std::pair<int, int>>& sizes) {
    res_combo_->blockSignals(true);
    res_combo_->clear();
    for (const auto& r : sizes)
        res_combo_->addItem(QString("%1x%2").arg(r.first).arg(r.second),
                            QVariant::fromValue<qint64>((qint64(r.first) << 32) | qint64(r.second)));
    res_combo_->blockSignals(false);
    if (res_combo_->count() > 0) res_combo_->setCurrentIndex(0);
}

std::pair<int, int> CameraWidget::selectedResolution() const {
    qint64 v = res_combo_->currentData().toLongLong();
    if (v > 0) {
        int w = static_cast<int>(v >> 32);
        int h = static_cast<int>(v & 0xFFFFFFFFLL);
        if (w > 0 && h > 0) return {w, h};
    }
    return {640, 480};
}

void CameraWidget::selectResolution(int width, int height) {
    qint64 target = (qint64(width) << 32) | qint64(height);
    res_combo_->blockSignals(true);
    for (int i = 0; i < res_combo_->count(); ++i) {
        if (res_combo_->itemData(i).toLongLong() == target) {
            res_combo_->setCurrentIndex(i);
            break;
        }
    }
    res_combo_->blockSignals(false);
}

void CameraWidget::setPixelFormats(bool is_usb, const std::string& current) {
    fmt_combo_->blockSignals(true);
    fmt_combo_->clear();
    if (is_usb) {
        for (const auto& [label, code] : core::PixelFormats()) {
            fmt_combo_->addItem(QString::fromStdString(label),
                                QString::fromStdString(code));
            if (label == current) fmt_combo_->setCurrentText(QString::fromStdString(label));
        }
        if (fmt_combo_->currentText().isEmpty())
            fmt_combo_->setCurrentText("Auto (Default)");
    }
    fmt_combo_->setEnabled(is_usb);
    fmt_combo_->setVisible(is_usb);
    fmt_label_->setVisible(is_usb);
    fmt_combo_->blockSignals(false);
}

void CameraWidget::setCurrentPixelFormat(const std::string& fmt) {
    fmt_combo_->blockSignals(true);
    if (!fmt.empty())
        fmt_combo_->setCurrentText(QString::fromStdString(fmt));
    fmt_combo_->blockSignals(false);
}

void CameraWidget::setFps(int fps) {
    fps_combo_->blockSignals(true);
    QString t = QString::number(fps);
    if (fps_combo_->findText(t) < 0) {
        fps_combo_->addItem(t);
        fps_combo_->model()->sort(0);
    }
    fps_combo_->setCurrentText(t);
    fps_combo_->blockSignals(false);
}

int CameraWidget::selectedFps() const {
    bool ok = false;
    int v = fps_combo_->currentText().toInt(&ok);
    return ok ? v : 15;
}

void CameraWidget::takeSnapshot() { canvas_->takeSnapshot(); }

cv::Mat CameraWidget::latestFrame() const {
    if (!canvas_ || !canvas_->hasFrame()) return cv::Mat();
    return canvas_->latestRawFrame();
}

}  // namespace ui
