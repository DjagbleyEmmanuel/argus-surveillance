#include "ui/main_window.h"

#include "core/paths.h"
#include "ui/about_dialog.h"
#include "ui/face_manager_dialog.h"
#include "ui/recordings_dialog.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <thread>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QString("Argus Tactical Vision Suite  —  v%1").arg(ARGUS_VERSION));
    setWindowIcon(QIcon(":/icons/app.png"));
    setMinimumSize(1280, 720);
    resize(1600, 950);

    object_detector_ = std::make_shared<core::ObjectDetector>(core::modelsDir());
    face_recognizer_ = std::make_shared<core::FaceRecognizer>("faces");

    setupUi();
    detectCameras();
    loadSavedSettings();

    connect(this, &MainWindow::cameraOpened, this, &MainWindow::onCameraOpened);
    connect(this, &MainWindow::cameraOpenFailed, this, &MainWindow::onCameraOpenFailed);
}

// ===========================================================================
// UI construction
// ===========================================================================

void MainWindow::setupUi() {
    auto* central = new QWidget;
    setCentralWidget(central);
    auto* main_layout = new QVBoxLayout(central);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // --- header ---
    auto* header = new QWidget;
    header->setFixedHeight(48);
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(16, 4, 16, 4);

    auto* title = new QLabel("◆ ARGUS TACTICAL");
    title->setObjectName("headerTitle");
    h->addWidget(title);
    h->addStretch();

    gpu_label_ = new QLabel("⚡ GPU OFF");
    gpu_label_->setStyleSheet("font-size: 11px; font-weight: 700; padding: 0 8px;");
    h->addWidget(gpu_label_);

    h->addWidget(new QLabel("Theme:"));
    theme_combo_ = new QComboBox;
    for (const auto& name : themeNames()) theme_combo_->addItem(name);
    theme_combo_->setCurrentText(current_theme_);
    connect(theme_combo_, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
            this, [this](const QString& name) {
                current_theme_ = name;
                setStyleSheet(buildStylesheet(name));
                saveSettings();
            });

    h->addWidget(new QLabel("Layout:"));
    grid_combo_ = new QComboBox;
    grid_combo_->addItems({"1x1 Spotlight", "2x2 Grid", "3x3 Grid"});
    grid_combo_->setCurrentIndex(1);
    connect(grid_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                grid_cols_ = (idx == 0) ? 1 : (idx == 1 ? 2 : 3);
                spotlight_cid_.clear();
                refreshGrid();
            });

    auto* recordings_btn = new QPushButton("🎬 Gallery");
    recordings_btn->setObjectName("primaryBtn");
    connect(recordings_btn, &QPushButton::clicked, this, [this] {
        RecordingsDialog dlg("recordings", "snapshots", this);
        dlg.exec();
        refreshStatus();
    });

    auto* add_btn = new QPushButton("⊕ Add Camera");
    add_btn->setObjectName("addBtn");
    connect(add_btn, &QPushButton::clicked, this, &MainWindow::onAddCamera);

    auto* face_db_btn = new QPushButton("👤 Face DB");
    connect(face_db_btn, &QPushButton::clicked, this, [this] {
        FaceManagerDialog dlg(face_recognizer_, [this]() -> cv::Mat {
            for (auto* w : widgets_) {
                cv::Mat f = w->latestFrame();
                if (!f.empty()) return f;
            }
            return cv::Mat();
        }, this);
        dlg.exec();
    });

    auto* about_btn = new QPushButton("ℹ About");
    connect(about_btn, &QPushButton::clicked, this, [this] {
        AboutDialog dlg(this);
        dlg.exec();
    });

    h->addWidget(theme_combo_);
    h->addWidget(grid_combo_);
    h->addWidget(recordings_btn);
    h->addWidget(add_btn);
    h->addWidget(face_db_btn);
    h->addWidget(about_btn);
    main_layout->addWidget(header);

    // --- body splitter: grid + drawer ---
    body_splitter_ = new QSplitter(Qt::Horizontal);
    body_splitter_->setHandleWidth(2);

    grid_host_ = new QWidget;
    auto* grid_margin = new QVBoxLayout(grid_host_);
    grid_margin->setContentsMargins(4, 4, 4, 4);
    grid_margin->setSpacing(4);
    grid_layout_ = new QGridLayout;
    grid_layout_->setSpacing(4);
    grid_margin->addLayout(grid_layout_);
    grid_margin->addStretch();
    body_splitter_->addWidget(grid_host_);

    // drawer with tabs
    drawer_ = new QWidget;
    drawer_->setMinimumWidth(300);
    drawer_tabs_ = new QTabWidget(drawer_);
    drawer_tabs_->addTab(wrapInScroll(buildControlPanel()), "🎛 CONTROLS");
    drawer_tabs_->addTab(wrapInScroll(buildEventLogPanel()), "🔔 ALERT LOG");

    auto* dl = new QVBoxLayout(drawer_);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->addWidget(drawer_tabs_);
    body_splitter_->addWidget(drawer_);
    body_splitter_->setSizes({1200, 300});
    main_layout->addWidget(body_splitter_, 1);

    // floating slide-out drawer handle (for spotlight mode)
    drawer_handle_ = new QPushButton("◀ CONTROLS", central);
    drawer_handle_->setFixedHeight(32);
    drawer_handle_->setFixedWidth(95);
    drawer_handle_->setStyleSheet(
        "QPushButton { background: rgba(56,189,248,0.2); color: #38bdf8;"
        "border: 1px solid rgba(56,189,248,0.5);"
        "border-top-left-radius: 8px; border-bottom-left-radius: 8px;"
        "font-weight: 700; font-size: 10px; }"
        "QPushButton:hover { background: rgba(56,189,248,0.4); color: #ffffff; }");
    drawer_handle_->setVisible(false);
    connect(drawer_handle_, &QPushButton::clicked, this, [this] {
        bool vis = drawer_->isVisible();
        drawer_->setVisible(!vis);
        drawer_handle_->setText(!vis ? "▶ CONTROLS" : "◀ CONTROLS");
    });
    drawer_handle_->raise();

    statusBar()->showMessage("Ready.");
    auto* cam_count = new QLabel("Cameras: 0/0 Online");
    auto* storage_count = new QLabel("Disk: 0 MB");
    auto* rec_count = new QLabel("● Idle");
    rec_count->setStyleSheet("color: #f43f5e; font-size: 11px; font-weight: 700;");
    cam_count->setObjectName("camCountLabel");
    storage_count->setObjectName("storageCountLabel");
    rec_count->setObjectName("recCountLabel");
    statusBar()->addPermanentWidget(cam_count);
    statusBar()->addPermanentWidget(storage_count);
    statusBar()->addPermanentWidget(rec_count);

    setStyleSheet(buildStylesheet(current_theme_));
}

QWidget* MainWindow::buildControlPanel() {
    auto* panel = new QWidget;
    auto* l = new QVBoxLayout(panel);
    l->setContentsMargins(10, 10, 10, 10);
    l->setSpacing(8);

    // --- Detection Modules ---
    auto* det = new QGroupBox("Detection Modules");
    auto* d = new QVBoxLayout(det);
    motion_cb_ = new QCheckBox("Motion Detection");
    motion_cb_->setChecked(true);
    connect(motion_cb_, &QCheckBox::toggled, this, [this](bool v) { setGlobalModule("motion", v); });
    human_cb_ = new QCheckBox("Human Recognition");
    human_cb_->setChecked(true);
    connect(human_cb_, &QCheckBox::toggled, this, [this](bool v) { setGlobalModule("human", v); });
    face_cb_ = new QCheckBox("Face Recognition");
    face_cb_->setChecked(true);
    connect(face_cb_, &QCheckBox::toggled, this, [this](bool v) { setGlobalModule("face", v); });
    object_cb_ = new QCheckBox("🎯 YOLOv4 Object Detection");
    object_cb_->setChecked(true);
    connect(object_cb_, &QCheckBox::toggled, this, [this](bool v) { setGlobalModule("objects", v); });
    record_cb_ = new QCheckBox("Auto-Record on Event");
    record_cb_->setChecked(false);
    connect(record_cb_, &QCheckBox::toggled, this, [this](bool v) {
        setGlobalModule("recording", v);
        refreshStatus();
        saveSettings();
    });
    d->addWidget(motion_cb_);
    d->addWidget(human_cb_);
    d->addWidget(face_cb_);
    d->addWidget(object_cb_);
    d->addWidget(record_cb_);
    l->addWidget(det);

    // --- YOLO Tuning ---
    auto* yolo_grp = new QGroupBox("🎯 YOLO Tuning & Model");
    auto* yl = new QVBoxLayout(yolo_grp);
    yl->setSpacing(6);
    yolo_model_label_ = new QLabel("");
    yolo_model_label_->setWordWrap(true);
    yolo_model_label_->setStyleSheet("font-size: 11px; color: #94a3b8;");
    yl->addWidget(yolo_model_label_);

    auto* model_btns = new QHBoxLayout;
    auto* yolo_custom_btn = new QPushButton("Load Custom Weights…");
    connect(yolo_custom_btn, &QPushButton::clicked, this, [this] {
        QString weights = QFileDialog::getOpenFileName(
            this, "Select Custom YOLO Weights (.weights)",
            QDir::homePath(), "Darknet Weights (*.weights)");
        if (weights.isEmpty()) return;
        QString base = weights;
        if (base.endsWith(".weights")) base.chop(8);
        QString cfg = base + ".cfg";
        if (!QFileInfo::exists(cfg)) {
            cfg = QFileDialog::getOpenFileName(
                this, "Select matching .cfg architecture file",
                QFileInfo(weights).absolutePath(), "Darknet Config (*.cfg)");
            if (cfg.isEmpty()) return;
        }
        QString names = base + ".names";
        if (!QFileInfo::exists(names)) names = "";
        bool ok = object_detector_->loadModel(cfg.toStdString(), weights.toStdString(),
                                              names.toStdString());
        if (!ok) {
            QMessageBox::warning(this, "YOLO Model Failed",
                                 "Failed to load model. The bundled YOLOv4-tiny model is still active.");
            object_detector_->loadModel();
        }
        refreshYoloLabel();
    });
    yolo_reset_btn_ = new QPushButton("Reset Bundled");
    yolo_reset_btn_->setEnabled(false);
    connect(yolo_reset_btn_, &QPushButton::clicked, this, [this] {
        object_detector_->loadModel();
        object_cb_->setChecked(true);
        refreshYoloLabel();
    });
    model_btns->addWidget(yolo_custom_btn);
    model_btns->addWidget(yolo_reset_btn_);
    yl->addLayout(model_btns);

    auto add_yolo_slider = [&](const QString& label, const QString& name,
                               QSlider*& slider, QLabel*& val_lbl, int lo, int hi,
                               int val, const std::function<QString(int)>& fmt) {
        yl->addWidget(new QLabel(label));
        slider = new QSlider(Qt::Horizontal);
        slider->setObjectName(name);
        slider->setRange(lo, hi);
        slider->setValue(val);
        val_lbl = new QLabel(fmt(val));
        val_lbl->setStyleSheet("font-size: 11px;");
        QLabel* value_label = val_lbl;
        connect(slider, &QSlider::valueChanged, this, [this, fmt, value_label](int v) {
            value_label->setText(fmt(v));
            refreshYoloLabel();
            saveSettings();
        });
        yl->addWidget(slider);
        yl->addWidget(val_lbl);
    };
    QLabel* yolo_conf_val = nullptr;
    add_yolo_slider("Confidence Threshold:", "yolo_conf",
                    yolo_conf_slider_, yolo_conf_val, 5, 95, 50,
                    [](int v) { return QString("%1%").arg(v); });
    connect(yolo_conf_slider_, &QSlider::valueChanged, this, [this](int v) {
        object_detector_->setConfThreshold(v / 100.0f);
    });
    QLabel* yolo_nms_val = nullptr;
    add_yolo_slider("NMS Overlap:", "yolo_nms",
                    yolo_nms_slider_, yolo_nms_val, 10, 90, 45,
                    [](int v) { return QString("%1%").arg(v); });
    connect(yolo_nms_slider_, &QSlider::valueChanged, this, [this](int v) {
        object_detector_->setNmsThreshold(v / 100.0f);
    });

    yl->addWidget(new QLabel("Inference Input Size:"));
    yolo_size_combo_ = new QComboBox;
    yolo_size_combo_->addItems({"160", "192", "224", "256", "288", "320", "384", "416"});
    yolo_size_combo_->setCurrentText("288");
    connect(yolo_size_combo_, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
            this, [this](const QString& t) {
                bool ok = false;
                int v = t.toInt(&ok);
                if (ok) object_detector_->setInputSize(v);
                refreshYoloLabel();
                saveSettings();
            });
    yl->addWidget(yolo_size_combo_);

    QLabel* yolo_min_val = nullptr;
    add_yolo_slider("Minimum Object Size (% of frame):", "yolo_min",
                    yolo_min_slider_, yolo_min_val, 2, 100, 2,
                    [](int v) { return QString("%1%").arg(v); });
    connect(yolo_min_slider_, &QSlider::valueChanged, this, [this](int v) {
        object_detector_->setMinSizePercent(v / 100.0f);
    });
    QLabel* yolo_dark_val = nullptr;
    add_yolo_slider("Dark-Frame Noise Gate:", "yolo_dark",
                    yolo_dark_slider_, yolo_dark_val, 0, 80, 20,
                    [](int v) { return QString::number(v); });
    connect(yolo_dark_slider_, &QSlider::valueChanged, this, [this](int v) {
        object_detector_->setDarkGate(v);
    });
    l->addWidget(yolo_grp);
    refreshYoloLabel();

    // --- HUD & Visuals ---
    auto* hud_grp = new QGroupBox("Sci-Fi HUD & Visuals");
    auto* h_box = new QVBoxLayout(hud_grp);
    hud_cb_ = new QCheckBox("Target Reticles & HUD");
    hud_cb_->setChecked(true);
    connect(hud_cb_, &QCheckBox::toggled, this, [this](bool v) {
        for (auto* w : widgets_) w->setShowHud(v);
    });
    night_cb_ = new QCheckBox("🌙 Digital Night Vision (CLAHE)");
    connect(night_cb_, &QCheckBox::toggled, this, [this](bool v) {
        for (auto* w : widgets_) w->setShowNightVision(v);
    });
    obj_cb_ = new QCheckBox("🎯 YOLO Object Labels");
    obj_cb_->setChecked(true);
    connect(obj_cb_, &QCheckBox::toggled, this, [this](bool v) {
        for (auto* w : widgets_) w->setShowObjectLabels(v);
    });
    scan_cb_ = new QCheckBox("CRT Scanline Filter");
    connect(scan_cb_, &QCheckBox::toggled, this, [this](bool v) {
        for (auto* w : widgets_) w->setShowScanlines(v);
    });
    h_box->addWidget(hud_cb_);
    h_box->addWidget(night_cb_);
    h_box->addWidget(obj_cb_);
    h_box->addWidget(scan_cb_);
    l->addWidget(hud_grp);

    // --- Lighting & Color ---
    auto* light_grp = new QGroupBox("Lighting & Color");
    auto* lg = new QVBoxLayout(light_grp);
    auto* mode_row = new QHBoxLayout;
    mode_row->addWidget(new QLabel("Light Mode:"));
    light_mode_combo_ = new QComboBox;
    light_mode_combo_->addItem("Auto", "auto");
    light_mode_combo_->addItem("Day", "day");
    light_mode_combo_->addItem("Night", "night");
    connect(light_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLightModeChanged);
    mode_row->addWidget(light_mode_combo_);
    lg->addLayout(mode_row);

    auto add_light_row = [&](const QString& label, QSlider*& slider, int lo, int hi, int val) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label));
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(lo, hi);
        slider->setValue(val);
        connect(slider, &QSlider::valueChanged, this, &MainWindow::onLightingChange);
        row->addWidget(slider);
        lg->addLayout(row);
    };
    add_light_row("Brightness:", light_brightness_, -100, 100, 0);
    add_light_row("Contrast:", light_contrast_, 50, 180, 100);
    add_light_row("Saturation:", light_saturation_, 0, 200, 100);
    add_light_row("Warmth:", light_warmth_, -100, 100, 0);
    l->addWidget(light_grp);

    // --- Night Vision CLAHE (Task 2) ---
    auto* clahe_grp = new QGroupBox("🌙 Night Vision CLAHE");
    auto* clh = new QVBoxLayout(clahe_grp);
    clh->setSpacing(6);

    auto add_clahe_row = [&](const QString& label, QSlider*& slider,
                             QLabel*& val_lbl, int lo, int hi, int val,
                             const QString& suffix) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(label));
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(lo, hi);
        slider->setValue(val);
        val_lbl = new QLabel(QString("%1%2").arg(val).arg(suffix));
        val_lbl->setStyleSheet("font-size: 11px; min-width: 40px;");
        row->addWidget(slider);
        row->addWidget(val_lbl);
        clh->addLayout(row);
    };
    QLabel* clip_val = nullptr;
    add_clahe_row("CLAHE Clip:", clahe_clip_slider_, clip_val, 10, 80, 35, " (3.5)");
    connect(clahe_clip_slider_, &QSlider::valueChanged, this,
            [this, clip_val](int v) {
                clip_val->setText(QString("%1 (%2)").arg(v).arg(v / 10.0, 0, 'f', 1));
                applyClaheToAll();
                saveSettings();
            });
    QLabel* tile_val = nullptr;
    add_clahe_row("Tile Size:", clahe_tile_slider_, tile_val, 2, 16, 8, "");
    connect(clahe_tile_slider_, &QSlider::valueChanged, this,
            [this, tile_val](int v) {
                tile_val->setText(QString::number(v));
                applyClaheToAll();
                saveSettings();
            });

    auto add_clahe_cb = [&](const QString& label, QCheckBox*& cb, bool checked) {
        cb = new QCheckBox(label);
        cb->setChecked(checked);
        clh->addWidget(cb);
    };
    add_clahe_cb("Pre-CLAHE Denoise", clahe_denoise_cb_, true);
    add_clahe_cb("Reactive Gamma Lift", clahe_gamma_cb_, true);
    add_clahe_cb("Auto-Desaturate in Low Light", clahe_desat_cb_, true);
    for (auto* cb : {clahe_denoise_cb_, clahe_gamma_cb_, clahe_desat_cb_}) {
        connect(cb, &QCheckBox::toggled, this, [this](bool) {
            applyClaheToAll();
            saveSettings();
        });
    }
    l->addWidget(clahe_grp);

    // --- Master Recording Controls ---
    auto* rec_grp = new QGroupBox("Master Recording Controls");
    auto* rc = new QVBoxLayout(rec_grp);
    auto* global_stop_btn = new QPushButton("⏹ Stop All Recordings");
    global_stop_btn->setObjectName("dangerBtn");
    connect(global_stop_btn, &QPushButton::clicked, this, [this] {
        int stopped = 0;
        for (auto& [cid, w] : workers_.toStdMap()) {
            if (w->isRecording()) { w->stopRecording(); ++stopped; }
        }
        refreshStatus();
    });
    rc->addWidget(global_stop_btn);

    global_pause_btn_ = new QPushButton("⏸ Pause All Streams");
    connect(global_pause_btn_, &QPushButton::clicked, this, [this] {
        bool any_paused = false;
        for (auto& [cid, w] : workers_.toStdMap()) any_paused = any_paused || w->isPaused();
        for (auto& [cid, w] : workers_.toStdMap()) w->togglePause();
        global_pause_btn_->setText(any_paused ? "⏸ Pause All Streams" : "⏸ Resume Streams");
        refreshStatus();
    });
    rc->addWidget(global_pause_btn_);
    l->addWidget(rec_grp);

    // --- Detection Sensitivity ---
    auto* sens_grp = new QGroupBox("Detection Sensitivity");
    auto* sg = new QVBoxLayout(sens_grp);
    sg->addWidget(new QLabel("Motion Threshold:"));
    sens_slider_ = new QSlider(Qt::Horizontal);
    sens_slider_->setRange(500, 25000);
    sens_slider_->setValue(5000);
    auto* sens_val = new QLabel("5000 px");
    sens_val->setStyleSheet("font-size: 11px;");
    connect(sens_slider_, &QSlider::valueChanged, this, [this, sens_val](int v) {
        sens_val->setText(QString("%1 px").arg(v));
        for (auto& [cid, w] : workers_.toStdMap()) w->updateSettings(
            motion_cb_->isChecked(), human_cb_->isChecked(), face_cb_->isChecked(),
            object_cb_->isChecked(), record_cb_->isChecked(), v);
    });
    sg->addWidget(sens_slider_);
    sg->addWidget(sens_val);
    l->addWidget(sens_grp);

    // --- Refresh & Performance ---
    auto* perf_grp = new QGroupBox("Refresh & Performance");
    auto* pg = new QVBoxLayout(perf_grp);
    pg->addWidget(new QLabel("Target Poll Rate (FPS):"));
    poll_combo_ = new QComboBox;
    poll_combo_->addItems({"15", "30", "60"});
    poll_combo_->setCurrentText("30");
    connect(poll_combo_, QOverload<const QString&>::of(&QComboBox::currentTextChanged),
            this, [this](const QString& t) {
                bool ok = false;
                int v = t.toInt(&ok);
                if (!ok) return;
                polls_per_sec_ = v;
                for (auto& [cid, timer] : timers_.toStdMap()) timer->setInterval(1000 / v);
                saveSettings();
            });
    pg->addWidget(poll_combo_);

    auto* gpu_cb = new QCheckBox("Hardware Acceleration");
    gpu_cb->setChecked(true);
    gpu_cb->setEnabled(false);
    pg->addWidget(gpu_cb);
    l->addWidget(perf_grp);

    // --- Active Camera Feeds ---
    auto* cam_grp = new QGroupBox("Active Camera Feeds");
    auto* cl = new QVBoxLayout(cam_grp);
    auto* cam_list_host = new QWidget;
    cam_list_layout_ = new QVBoxLayout(cam_list_host);
    cam_list_layout_->setContentsMargins(0, 0, 0, 0);
    cam_list_layout_->setSpacing(2);
    auto* cam_scroll = new QScrollArea;
    cam_scroll->setWidgetResizable(true);
    cam_scroll->setWidget(cam_list_host);
    cam_scroll->setMinimumHeight(100);
    cam_scroll->setMaximumHeight(160);
    cam_scroll->setStyleSheet("QScrollArea { border: none; }");
    cl->addWidget(cam_scroll);
    l->addWidget(cam_grp);

    l->addStretch();

    auto* stop_btn = new QPushButton("⏻ Stop & Clear All");
    stop_btn->setObjectName("dangerBtn");
    connect(stop_btn, &QPushButton::clicked, this, [this] {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Confirm Stop All",
            "Stop all camera feeds and terminate active workers?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        for (auto& [cid, t] : timers_.toStdMap()) { t->stop(); t->deleteLater(); }
        timers_.clear();
        for (auto& [cid, cam] : cameras_.toStdMap()) cam->stop();
        for (auto& [cid, w] : workers_.toStdMap()) w->stop();
        for (auto& [cid, win] : detached_.toStdMap()) win->close();
        detached_.clear();
        for (auto& [cid, w] : widgets_.toStdMap()) w->deleteLater();
        cameras_.clear();
        workers_.clear();
        widgets_.clear();
        spotlight_cid_.clear();
        refreshGrid();
        refreshCamList();
        refreshStatus();
        saveSettings();
    });
    l->addWidget(stop_btn);

    return panel;
}

QWidget* MainWindow::buildEventLogPanel() {
    auto* panel = new QWidget;
    auto* l = new QVBoxLayout(panel);
    l->setContentsMargins(8, 8, 8, 8);
    l->setSpacing(8);

    auto* hdr = new QHBoxLayout;
    hdr->setContentsMargins(2, 2, 2, 4);
    auto* lbl = new QLabel("Live Security Triggers");
    lbl->setStyleSheet("font-weight: 700; font-size: 12px; color: #f8fafc;");
    auto* clear_btn = new QPushButton("🗑 Clear Log");
    clear_btn->setObjectName("dangerBtn");
    clear_btn->setStyleSheet(
        "QPushButton#dangerBtn { padding: 4px 10px; font-size: 11px; font-weight: 600; border-radius: 6px; }");
    connect(clear_btn, &QPushButton::clicked, this, [this] {
        event_list_->clear();
        recent_events_.clear();
    });
    hdr->addWidget(lbl);
    hdr->addStretch();
    hdr->addWidget(clear_btn);
    l->addLayout(hdr);

    event_list_ = new QListWidget;
    event_list_->setMinimumHeight(240);
    l->addWidget(event_list_, 1);
    return panel;
}

QScrollArea* MainWindow::wrapInScroll(QWidget* w) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(w);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    return scroll;
}

// ===========================================================================
// camera lifecycle
// ===========================================================================

void MainWindow::detectCameras() {
    usb_cameras_.clear();
    usb_scan_thread_ = new UsbScanWorker(this);
    connect(usb_scan_thread_, &UsbScanWorker::finishedScan,
            this, &MainWindow::onUsbScanFinished);
    usb_scan_thread_->start();
}

void MainWindow::onUsbScanFinished(std::vector<core::UsbCamera> cams) {
    usb_cameras_ = std::move(cams);
    if (usb_scan_thread_) {
        usb_scan_thread_->deleteLater();
        usb_scan_thread_ = nullptr;
    }
}

void MainWindow::onAddCamera() {
    std::set<int> active;
    for (const auto& [cid, cam] : cameras_.toStdMap()) {
        if (cam->sourceType() == core::CameraType::USB) {
            try { active.insert(std::stoi(cam->sourceUrl())); } catch (...) {}
        }
    }
    AddCameraDialog dialog(usb_cameras_, active, this);
    if (dialog.exec() == QDialog::Accepted) {
        auto res = dialog.getResult();
        if (res) {
            core::CameraConfig cfg;
            cfg.name = res->name;
            cfg.type = res->type;
            cfg.url = res->url;
            cfg.pixel_format = res->pixel_format;
            addCamera(cfg, false);
        }
    }
}

void MainWindow::addCamera(const core::CameraConfig& cfg, bool silent) {
    core::CameraType type = core::CameraType::USB;
    if (cfg.type == "rtsp") type = core::CameraType::RTSP;
    else if (cfg.type == "http") type = core::CameraType::HTTP;

    if (type == core::CameraType::USB) {
        int idx = -1;
        try { idx = std::stoi(cfg.url); } catch (...) {}
        if (idx >= 0) {
            for (const auto& [cid, cam] : cameras_.toStdMap()) {
                if (cam->sourceType() != core::CameraType::USB) continue;
                bool same = cam->sourceUrl() == cfg.url;
                if (cam->usbSysName().empty() == false || cam->usbBusPath().empty() == false) {
                    auto ident = core::CameraSource::getUsbIdentity(idx);
                    same = same || cam->usbSysName() == ident.sys_name;
                }
                if (same) {
                    if (silent) return;
                    QMessageBox::warning(this, "Camera In Use",
                        QString("'%1' is already streaming.\n\nRe-connecting it would break the existing feed.")
                            .arg(QString::fromStdString(cfg.name)));
                    return;
                }
            }
        }
    }

    std::string cid_str = "cam_" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    QString cid = QString::fromStdString(cid_str);

    auto camera = std::make_shared<core::CameraSource>(cfg.name, type, cfg.url, cid_str, cfg.pixel_format);
    // Restore per-camera night-control pref BEFORE open so it is re-applied.
    camera->setDynamicFrameratePref(cfg.edf_value);
    // Open + start on a background thread. camera->open() (and OpenCV's V4L2
    // device open with its timeouts/first-frame read) can block for seconds, so
    // doing it on the GUI thread would freeze the whole app. The worker/widget
    // are created only once the device is actually online.
    pending_cameras_.insert(cid, camera);
    pending_cfgs_.insert(cid, cfg);
    // Open on a detached worker thread; start() (which spawns the capture
    // thread) is done on the main thread once the device is confirmed online.
    std::thread([this, camera, cfg, cid, silent]() {
        bool ok = camera->open();
        if (ok)
            emit cameraOpened(cid, silent);
        else
            emit cameraOpenFailed(cid, silent);
    }).detach();
    refreshStatus();
}

void MainWindow::onCameraOpened(QString cid, bool silent) {
    auto camera = pending_cameras_.take(cid);
    auto cfg = pending_cfgs_.take(cid);
    if (!camera || cfg.url.empty()) return;

    if (cfg.resolution_w > 0 && cfg.resolution_h > 0)
        camera->setResolutionAsync(cfg.resolution_w, cfg.resolution_h);
    if (cfg.fps > 0.0)
        camera->setFpsAsync(cfg.fps);
    camera->start();
    cameras_.insert(cid, camera);

    auto worker = std::make_shared<core::DetectionWorker>(camera, camera->cameraId(),
                                                          record_cb_->isChecked(),
                                                          face_recognizer_,
                                                          object_detector_);
    worker->updateSettings(motion_cb_->isChecked(), human_cb_->isChecked(),
                           face_cb_->isChecked(), object_cb_->isChecked(),
                           record_cb_->isChecked(), sens_slider_->value());
    worker->start();
    workers_.insert(cid, worker);
    applyClaheToAll();

    createWidget(cid, cfg);

    auto* timer = new QTimer(this);
    timer->setInterval(1000 / polls_per_sec_);
    connect(timer, &QTimer::timeout, this, [this, cid] { pollCamera(cid); });
    timer->start();
    timers_.insert(cid, timer);

    refreshGrid();
    refreshCamList();
    refreshStatus();
    saveSettings();
}

void MainWindow::onCameraOpenFailed(QString cid, bool silent) {
    pending_cameras_.remove(cid);
    pending_cfgs_.remove(cid);
    refreshStatus();
    if (!silent) {
        QMessageBox::warning(this, "Connection Error",
            QString("Failed to connect to camera stream."));
    }
}

void MainWindow::createWidget(const QString& cid, const core::CameraConfig& cfg) {
    auto camera = cameras_.value(cid);
    auto* widget = new CameraWidget(camera);
    connect(widget, &CameraWidget::removeRequested, this, [this, cid] { removeCamera(cid); });
    connect(widget, &CameraWidget::spotlightRequested, this, [this, widget] { toggleSpotlight(widget); });
    connect(widget, &CameraWidget::popoutRequested, this, [this, widget] { detachCamera(widget); });
    connect(widget, &CameraWidget::recordingToggled, this, [this, cid](bool v) {
        if (auto w = workers_.value(cid)) w->updateSettings(
            motion_cb_->isChecked(), human_cb_->isChecked(), face_cb_->isChecked(),
            object_cb_->isChecked(), v, sens_slider_->value());
        refreshStatus();
        saveSettings();
    });
    connect(widget, &CameraWidget::stopRecordingRequested, this, [this, cid] {
        if (auto w = workers_.value(cid)) w->stopRecording();
        refreshStatus();
    });
    connect(widget, &CameraWidget::pauseRecordingRequested, this, [this, cid] {
        if (auto w = workers_.value(cid)) w->togglePause();
        refreshStatus();
    });
    connect(widget, &CameraWidget::resolutionChanged, this, [this, cid](int w, int h) {
        // Queue the V4L2 renegotiation to the capture thread so the GUI
        // stays responsive.
        if (auto cam = cameras_.value(cid)) cam->setResolutionAsync(w, h);
        saveSettings();
    });
    connect(widget, &CameraWidget::fpsChanged, this, [this, cid](int fps) {
        if (auto cam = cameras_.value(cid)) cam->setFpsAsync(fps);
        saveSettings();
    });
    connect(widget, &CameraWidget::pixelFormatChanged, this, [this, cid](const QString& fmt) {
        // FOURCC change reopens the device on the capture thread.
        if (auto cam = cameras_.value(cid)) cam->requestPixelFormat(fmt.toStdString());
        saveSettings();
    });
    connect(widget, &CameraWidget::dynamicFramerateToggled, this, [this](int) {
        saveSettings();
    });

    widget->setShowHud(hud_cb_->isChecked());
    widget->setShowNightVision(night_cb_->isChecked());
    widget->setShowObjectLabels(obj_cb_->isChecked());
    widget->setShowScanlines(scan_cb_->isChecked());
    applyLightingTo(widget);
    widget->setLightMode(light_mode_combo_->currentData().toString().toStdString());
    if (auto worker = workers_.value(cid)) widget->setDetectionWorker(worker);

    widget->setResolutions(camera->enumerateResolutions());
    widget->setPixelFormats(camera->sourceType() == core::CameraType::USB,
                            camera->pixelFormat());

    // restore saved per-camera resolution / fps
    if (cfg.resolution_w > 0 && cfg.resolution_h > 0)
        widget->selectResolution(cfg.resolution_w, cfg.resolution_h);
    if (cfg.fps > 0.0)
        widget->setFps(static_cast<int>(std::round(cfg.fps)));

    widgets_.insert(cid, widget);
}

void MainWindow::pollCamera(const QString& cid) {
    auto worker = workers_.value(cid);
    auto widget = widgets_.value(cid);
    if (!worker || !widget) return;
    auto result = worker->getResult();
    if (!result) return;

    QString now_str = QDateTime::currentDateTime().toString("HH:mm:ss");
    const auto& cam_name = QString::fromStdString(worker->camera()->name());

    if (!result->face_boxes.empty()) {
        const auto& label = QString::fromStdString(result->face_boxes[0].label);
        QString color = label.contains("KNOWN") ? "#4ade80" : "#38bdf8";
        addEventLog(QString("[%1] 👤 FACE: %2 — %3").arg(now_str, label, cam_name), color);
    } else if (!result->human_boxes.empty()) {
        addEventLog(QString("[%1] 👤 HUMAN TARGET: %2").arg(now_str, cam_name), "#4ade80");
    } else if (!result->object_boxes.empty()) {
        std::set<QString> labels;
        for (size_t i = 0; i < result->object_boxes.size() && i < 3; ++i)
            labels.insert(QString::fromStdString(result->object_boxes[i].label));
        QStringList list;
        for (const auto& s : labels) list << s;
        addEventLog(QString("[%1] 🎯 OBJECTS: %2 — %3").arg(now_str, list.join(", "), cam_name), "#fb923c");
    } else if (result->motion_boxes.size() > 2) {
        addEventLog(QString("[%1] ⚡ MOTION: %2").arg(now_str, cam_name), "#fbbf24");
    }

    widget->updateFrame(std::move(*result));
}

void MainWindow::addEventLog(const QString& text, const QString& color_hex) {
    qint64 now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    if (!recent_events_.empty() && recent_events_.back().text == text &&
        (now - recent_events_.back().when) < 3000)
        return;
    recent_events_.push_back({text, now});
    if (static_cast<int>(recent_events_.size()) > 50) recent_events_.erase(recent_events_.begin());

    auto* item = new QListWidgetItem(text);
    item->setForeground(QColor(color_hex));
    event_list_->insertItem(0, item);
    while (event_list_->count() > 80) event_list_->takeItem(event_list_->count() - 1);
}

void MainWindow::removeCamera(const QString& cid) {
    if (!cameras_.contains(cid)) return;
    if (auto* t = timers_.take(cid)) { t->stop(); t->deleteLater(); }
    if (auto c = cameras_.value(cid)) c->stop();
    if (auto w = workers_.take(cid)) w->stop();
    if (auto* win = detached_.take(cid)) win->close();
    if (auto* w = widgets_.take(cid)) w->deleteLater();
    cameras_.remove(cid);
    if (spotlight_cid_ == cid) spotlight_cid_.clear();
    refreshGrid();
    refreshCamList();
    refreshStatus();
    saveSettings();
}

// ===========================================================================
// detach / spotlight / grid
// ===========================================================================

void MainWindow::detachCamera(CameraWidget* widget) {
    QString cid = QString::fromStdString(widget->camera()->cameraId());
    if (detached_.contains(cid)) return;
    auto* win = new DetachedCameraWindow(widget, this);
    connect(win, &DetachedCameraWindow::closed, this, [this, widget, cid] {
        if (detached_.take(cid)) refreshGrid();
    });
    detached_.insert(cid, win);
    win->show();
    refreshGrid();
}

void MainWindow::reattachCamera(CameraWidget* widget) {
    QString cid = QString::fromStdString(widget->camera()->cameraId());
    if (detached_.take(cid)) refreshGrid();
}

void MainWindow::toggleSpotlight(CameraWidget* widget) {
    QString cid = QString::fromStdString(widget->camera()->cameraId());
    spotlight_cid_ = (spotlight_cid_ == cid) ? QString() : cid;
    refreshGrid();
}

void MainWindow::refreshGrid() {
    // clear
    while (grid_layout_->count()) {
        auto* item = grid_layout_->takeAt(0);
        if (item->widget()) item->widget()->setParent(nullptr);
    }

    std::vector<CameraWidget*> attached;
    for (const auto& cid : widgets_.keys()) {
        if (!detached_.contains(cid)) attached.push_back(widgets_.value(cid));
    }

    if (attached.empty()) {
        auto* hint = new QLabel("No active camera feeds.\nClick ⊕ Add Camera to connect a feed.");
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet("font-size: 16px; font-weight: 800; padding: 80px; background: transparent;");
        grid_layout_->addWidget(hint, 0, 0);
        drawer_handle_->setVisible(false);
        return;
    }

    // spotlight mode
    if (!spotlight_cid_.isEmpty() && widgets_.contains(spotlight_cid_) &&
        !detached_.contains(spotlight_cid_)) {
        grid_layout_->addWidget(widgets_.value(spotlight_cid_), 0, 0);
        body_splitter_->setSizes({1600, 0});
        drawer_->setVisible(false);
        drawer_handle_->setVisible(true);
        drawer_handle_->setText("▶ CONTROLS");
        return;
    }

    drawer_handle_->setVisible(false);
    drawer_->setVisible(true);
    body_splitter_->setSizes({1200, 300});

    int cols = std::clamp(grid_cols_, 1, 3);
    for (int i = 0; i < static_cast<int>(attached.size()); ++i) {
        grid_layout_->addWidget(attached[i], i / cols, i % cols);
    }
}

void MainWindow::refreshCamList() {
    while (cam_list_layout_->count()) {
        auto* item = cam_list_layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
    }
    for (const auto& [cid, cam] : cameras_.toStdMap()) {
        bool ok = cam->isOnline();
        QString color = ok ? "#38bdf8" : "#f43f5e";
        QString detached = detached_.contains(cid) ? " ⧉" : "";
        auto* lbl = new QLabel(QString("%1 %2%3").arg(ok ? "●" : "○")
                                       .arg(QString::fromStdString(cam->name()), detached));
        lbl->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 700; padding: 2px 4px;").arg(color));
        cam_list_layout_->addWidget(lbl);
    }
}

void MainWindow::refreshStatus() {
    int online = 0;
    for (const auto& [cid, cam] : cameras_.toStdMap()) if (cam->isOnline()) ++online;
    int total = cameras_.size();
    int recording = 0;
    for (const auto& [cid, w] : workers_.toStdMap()) if (w->isRecording()) ++recording;

    qint64 total_bytes = 0;
    QDir dir("recordings");
    if (dir.exists()) {
        for (const auto& fi : dir.entryInfoList(QDir::Files)) total_bytes += fi.size();
    }
    double mb = total_bytes / (1024.0 * 1024.0);

    auto* cam_count = statusBar()->findChild<QLabel*>("camCountLabel");
    auto* storage_count = statusBar()->findChild<QLabel*>("storageCountLabel");
    auto* rec_count = statusBar()->findChild<QLabel*>("recCountLabel");
    if (cam_count) cam_count->setText(QString("Cameras: %1/%2 Online").arg(online).arg(total));
    if (storage_count) storage_count->setText(QString("Disk: %1 MB").arg(mb, 0, 'f', 1));
    if (rec_count) rec_count->setText(recording > 0 ? QString("● %1 Recording").arg(recording) : "● Idle");
}

void MainWindow::setGlobalModule(const QString& key, bool enabled) {
    for (auto& [cid, w] : workers_.toStdMap()) {
        if (key == "motion") w->updateSettings(
            enabled, human_cb_->isChecked(), face_cb_->isChecked(), object_cb_->isChecked(),
            record_cb_->isChecked(), sens_slider_->value());
        else if (key == "human") w->updateSettings(
            motion_cb_->isChecked(), enabled, face_cb_->isChecked(), object_cb_->isChecked(),
            record_cb_->isChecked(), sens_slider_->value());
        else if (key == "face") w->updateSettings(
            motion_cb_->isChecked(), human_cb_->isChecked(), enabled, object_cb_->isChecked(),
            record_cb_->isChecked(), sens_slider_->value());
        else if (key == "objects") w->updateSettings(
            motion_cb_->isChecked(), human_cb_->isChecked(), face_cb_->isChecked(), enabled,
            record_cb_->isChecked(), sens_slider_->value());
        else if (key == "recording") w->updateSettings(
            motion_cb_->isChecked(), human_cb_->isChecked(), face_cb_->isChecked(),
            object_cb_->isChecked(), enabled, sens_slider_->value());
    }
    refreshStatus();
    saveSettings();
}

void MainWindow::refreshYoloLabel() {
    if (!object_detector_) return;
    bool enabled = object_detector_->isEnabled();
    if (enabled) {
        yolo_model_label_->setText(
            QString("Model: %1\nBackend: %2\nConf %3 · NMS %4 · %5px · %6% min")
                .arg(QString::fromStdString(object_detector_->modelName()),
                     QString::fromStdString(object_detector_->backendName()))
                .arg(object_detector_->confThreshold(), 0, 'f', 2)
                .arg(object_detector_->nmsThreshold(), 0, 'f', 2)
                .arg(object_detector_->inputSize())
                .arg(object_detector_->minSizePercent() * 100.0, 0, 'f', 0));
        yolo_model_label_->setStyleSheet("font-size: 11px; color: #4ade80;");
    } else {
        yolo_model_label_->setText("YOLO model not loaded");
        yolo_model_label_->setStyleSheet("font-size: 11px; color: #f87171;");
    }
    yolo_reset_btn_->setEnabled(!object_detector_->isBundled());
    object_cb_->setToolTip(enabled ? "YOLO model loaded ✔"
                                   : "YOLO model not found — place yolov4.cfg/.weights in assets/models/");
}

int MainWindow::selftestYoloSliders() {
    const QStringList names = {"yolo_conf", "yolo_nms", "yolo_min", "yolo_dark"};
    int tested = 0;
    for (const QString& name : names) {
        const auto sliders = findChildren<QSlider*>(name);
        if (sliders.isEmpty()) continue;
        QSlider* s = sliders.first();
        const int span = qMax(1, s->maximum() - s->minimum());
        for (int v = s->minimum(); v <= s->maximum(); v += qMax(1, span / 8)) {
            s->setValue(v);              // fires valueChanged -> connect lambdas
            QCoreApplication::processEvents();
        }
        s->setValue(s->maximum());
        QCoreApplication::processEvents();
        ++tested;
    }
    return tested;
}

int MainWindow::selftestCanvas() {
    const auto widgets = widgets_.values();
    if (widgets.isEmpty()) {
        qInfo("SELFTEST-CANVAS: no camera widgets to exercise.");
        return 1;
    }
    for (auto* w : widgets) {
        QPushButton* zoom = nullptr;
        QPushButton* night = nullptr;
        const auto btns = w->findChildren<QPushButton*>();
        for (auto* b : btns) {
            if (b->toolTip().contains("Smooth Pan / Zoom")) zoom = b;
            if (b->toolTip().contains("Digital Night Vision")) night = b;
        }
        if (!zoom || !night) {
            qInfo("SELFTEST-CANVAS: FAIL header buttons not found.");
            return 1;
        }

        zoom->click();                       // ZOOM ON
        QCoreApplication::processEvents();

        QWidget* canvas = w->findChild<QWidget*>("videoCanvas");
        if (!canvas) {
            qInfo("SELFTEST-CANVAS: FAIL video canvas not found.");
            return 1;
        }

        QWheelEvent in(QPointF(50, 50), QPointF(50, 50), QPoint(0, 0), QPoint(0, 120),
                       Qt::NoButton, Qt::NoModifier, Qt::ScrollBegin, false);
        QCoreApplication::sendEvent(canvas, &in);
        QCoreApplication::processEvents();
        QWheelEvent out(QPointF(50, 50), QPointF(50, 50), QPoint(0, 0), QPoint(0, -120),
                        Qt::NoButton, Qt::NoModifier, Qt::ScrollEnd, false);
        QCoreApplication::sendEvent(canvas, &out);
        QCoreApplication::processEvents();

        night->click();                      // toggle off
        QCoreApplication::processEvents();
        night->click();                      // toggle back on
        QCoreApplication::processEvents();

        zoom->click();                       // ZOOM OFF (resets target to 1.0)
        QCoreApplication::processEvents();

        if (auto* fps = w->findChild<QLabel*>("camFooterFps")) {
            qInfo("SELFTEST-CANVAS: footer fps label = \"%s\"",
                  qPrintable(fps->text()));
        }
    }
    qInfo("SELFTEST-CANVAS: exercised %d camera widget(s) (zoom+wheel+night), no crash.",
          (int)widgets.size());
    return 0;
}

// ===========================================================================
// lighting + persistence
// ===========================================================================

void MainWindow::applyLightingTo(CameraWidget* w) {
    w->setLighting(light_brightness_val_, light_contrast_val_,
                   light_saturation_val_, light_warmth_val_);
}

void MainWindow::applyClaheToAll() {
    const double clip = clahe_clip_slider_->value() / 10.0;
    const int tile = clahe_tile_slider_->value();
    const bool denoise = clahe_denoise_cb_->isChecked();
    const bool gamma = clahe_gamma_cb_->isChecked();
    const bool desat = clahe_desat_cb_->isChecked();
    for (auto& [cid, w] : workers_.toStdMap())
        w->updateClahe(clip, tile, denoise, gamma, desat);
}

void MainWindow::onLightingChange(int) {
    light_brightness_val_ = light_brightness_->value();
    light_contrast_val_ = light_contrast_->value();
    light_saturation_val_ = light_saturation_->value();
    light_warmth_val_ = light_warmth_->value();
    for (auto* w : widgets_) applyLightingTo(w);
    saveSettings();
}

void MainWindow::onLightModeChanged(int) {
    QString mode = light_mode_combo_->currentData().toString();
    for (auto* w : widgets_) w->setLightMode(mode.toStdString());
    saveSettings();
}

void MainWindow::loadSavedSettings() {
    core::SettingsStore store;
    core::Settings s = store.load();

    if (!s.theme.empty()) {
        current_theme_ = QString::fromStdString(s.theme);
        theme_combo_->setCurrentText(current_theme_);
        setStyleSheet(buildStylesheet(current_theme_));
    }

    grid_combo_->setCurrentIndex(std::clamp(s.grid_index, 0, 2));
    grid_cols_ = (s.grid_index == 0) ? 1 : (s.grid_index == 1 ? 2 : 3);

    poll_combo_->setCurrentText(QString::number(s.poll_rate));
    polls_per_sec_ = s.poll_rate;

    sens_slider_->setValue(s.motion_threshold);

    motion_cb_->setChecked(s.module_motion);
    human_cb_->setChecked(s.module_human);
    face_cb_->setChecked(s.module_face);
    object_cb_->setChecked(s.module_objects);
    record_cb_->setChecked(s.module_recording);

    yolo_conf_slider_->setValue(static_cast<int>(std::round(s.yolo_conf * 100)));
    yolo_nms_slider_->setValue(static_cast<int>(std::round(s.yolo_nms * 100)));
    yolo_size_combo_->setCurrentText(QString::number(s.yolo_input_size));
    yolo_min_slider_->setValue(static_cast<int>(std::round(s.yolo_min_size_percent * 100)));
    yolo_dark_slider_->setValue(static_cast<int>(std::round(s.yolo_dark_gate)));
    object_detector_->setConfThreshold(static_cast<float>(s.yolo_conf));
    object_detector_->setNmsThreshold(static_cast<float>(s.yolo_nms));
    object_detector_->setInputSize(s.yolo_input_size);
    object_detector_->setMinSizePercent(static_cast<float>(s.yolo_min_size_percent));
    object_detector_->setDarkGate(static_cast<float>(s.yolo_dark_gate));

    hud_cb_->setChecked(s.visual_hud);
    night_cb_->setChecked(s.visual_night);
    obj_cb_->setChecked(s.visual_object_labels);
    scan_cb_->setChecked(s.visual_scanlines);

    int idx = light_mode_combo_->findData(QString::fromStdString(s.light_mode));
    if (idx >= 0) light_mode_combo_->setCurrentIndex(idx);
    light_brightness_->setValue(s.brightness);
    light_contrast_->setValue(s.contrast);
    light_saturation_->setValue(s.saturation);
    light_warmth_->setValue(s.warmth);
    onLightingChange(0);
    onLightModeChanged(0);

    // CLAHE night-vision tuning (mirrors core/settings.py "clahe")
    clahe_clip_slider_->setValue(static_cast<int>(std::round(s.clahe_clip * 10)));
    clahe_tile_slider_->setValue(s.clahe_tile);
    clahe_denoise_cb_->setChecked(s.clahe_denoise);
    clahe_gamma_cb_->setChecked(s.clahe_gamma);
    clahe_desat_cb_->setChecked(s.clahe_desat);
    applyClaheToAll();
    refreshYoloLabel();

    // GPU label
    std::string backend = object_detector_->backendName();
    if (backend != "CPU") {
        gpu_label_->setText(QString("⚡ AI: %1").arg(QString::fromStdString(backend)));
        gpu_label_->setStyleSheet("font-size: 11px; font-weight: 700; padding: 0 8px; color: #4ade80;");
    } else {
        gpu_label_->setText("⚡ AI: CPU");
        gpu_label_->setStyleSheet("font-size: 11px; font-weight: 700; padding: 0 8px; color: #fbbf24;");
    }

    for (const auto& cfg : s.cameras) {
        if (cfg.type == "usb") {
            try {
                int url = std::stoi(cfg.url);
                core::CameraConfig resolved = cfg;
                resolved.url = std::to_string(
                    core::CameraSource::resolveUsbIndex(cfg.usb_bus_path,
                                                        cfg.usb_sys_name, url));
                addCamera(resolved, true);
            } catch (...) {
                addCamera(cfg, true);
            }
        } else {
            addCamera(cfg, true);
        }
    }
}

void MainWindow::saveSettings() {
    core::SettingsStore store;
    core::Settings s;

    s.theme = current_theme_.toStdString();
    s.motion_threshold = sens_slider_->value();
    s.module_motion = motion_cb_->isChecked();
    s.module_human = human_cb_->isChecked();
    s.module_face = face_cb_->isChecked();
    s.module_objects = object_cb_->isChecked();
    s.module_recording = record_cb_->isChecked();

    s.yolo_conf = object_detector_->confThreshold();
    s.yolo_nms = object_detector_->nmsThreshold();
    s.yolo_input_size = object_detector_->inputSize();
    s.yolo_min_size_percent = object_detector_->minSizePercent();
    s.yolo_dark_gate = object_detector_->darkGate();

    s.visual_hud = hud_cb_->isChecked();
    s.visual_night = night_cb_->isChecked();
    s.visual_object_labels = obj_cb_->isChecked();
    s.visual_scanlines = scan_cb_->isChecked();

    s.light_mode = light_mode_combo_->currentData().toString().toStdString();
    s.brightness = light_brightness_val_;
    s.contrast = light_contrast_val_;
    s.saturation = light_saturation_val_;
    s.warmth = light_warmth_val_;

    s.clahe_clip = clahe_clip_slider_->value() / 10.0;
    s.clahe_tile = clahe_tile_slider_->value();
    s.clahe_denoise = clahe_denoise_cb_->isChecked();
    s.clahe_gamma = clahe_gamma_cb_->isChecked();
    s.clahe_desat = clahe_desat_cb_->isChecked();

    s.poll_rate = polls_per_sec_;
    s.grid_index = grid_combo_->currentIndex();

    for (const auto& [cid, cam] : cameras_.toStdMap()) {
        core::CameraConfig c;
        c.name = cam->name();
        c.type = (cam->sourceType() == core::CameraType::RTSP) ? "rtsp"
                 : (cam->sourceType() == core::CameraType::HTTP) ? "http" : "usb";
        c.url = cam->sourceUrl();
        c.pixel_format = cam->pixelFormat();
        auto res = cam->resolution();
        c.resolution_w = res.first;
        c.resolution_h = res.second;
        c.fps = cam->fps();
        c.usb_sys_name = cam->usbSysName();
        c.usb_bus_path = cam->usbBusPath();
        c.edf_value = cam->dynamicFrameratePref();
        s.cameras.push_back(std::move(c));
    }

    store.save(s);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    for (auto& [cid, win] : detached_.toStdMap()) win->close();
    detached_.clear();
    for (auto& [cid, t] : timers_.toStdMap()) t->stop();
    for (auto& [cid, cam] : cameras_.toStdMap()) cam->stop();
    for (auto& [cid, w] : workers_.toStdMap()) w->stop();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (drawer_handle_) {
        drawer_handle_->move(width() - drawer_handle_->width() - 12, 54);
        drawer_handle_->raise();
    }
}

}  // namespace ui
