#include "ui/face_manager_dialog.h"

#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

#include <opencv2/imgcodecs.hpp>

namespace ui {

FaceManagerDialog::FaceManagerDialog(std::shared_ptr<core::FaceRecognizer> recognizer,
                                     std::function<cv::Mat()> frame_provider,
                                     QWidget* parent)
    : QDialog(parent), recognizer_(std::move(recognizer)),
      frame_provider_(std::move(frame_provider)) {
    setWindowTitle("👤 Local Face Recognition Database — LBPH Enrollment");
    resize(620, 580);
    buildUi();
    refreshList();
}

void FaceManagerDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* title = new QLabel("👤  OFFLINE FACE ENROLLMENT DATABASE  (LBPH Recogniser)");
    title->setStyleSheet("color: #4ade80; font-size: 14px; font-weight: 800; letter-spacing: 1px;");
    layout->addWidget(title);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(255,255,255,0.08);");
    layout->addWidget(sep);

    auto* preview_row = new QHBoxLayout;

    preview_lbl_ = new QLabel;
    preview_lbl_->setFixedSize(160, 120);
    preview_lbl_->setAlignment(Qt::AlignCenter);
    preview_lbl_->setStyleSheet(
        "background: #020208; border: 1px solid rgba(74,222,128,0.3); border-radius: 6px;");
    preview_lbl_->setText("📷 Feed preview");
    preview_row->addWidget(preview_lbl_);

    auto* form_box = new QGroupBox("ENROLL NEW PERSON");
    auto* fl = new QVBoxLayout(form_box);

    auto* name_row = new QHBoxLayout;
    name_row->addWidget(new QLabel("Name:"));
    name_input_ = new QLineEdit;
    name_input_->setPlaceholderText("e.g. John, Sarah …");
    name_row->addWidget(name_input_);
    fl->addLayout(name_row);

    auto* samples_row = new QHBoxLayout;
    samples_row->addWidget(new QLabel("Auto-capture samples:"));
    samples_spin_ = new QSpinBox;
    samples_spin_->setRange(1, 20);
    samples_spin_->setValue(5);
    samples_spin_->setToolTip("More samples = better accuracy. 5 is recommended for LBPH.");
    samples_row->addWidget(samples_spin_);
    samples_row->addStretch();
    fl->addLayout(samples_row);

    progress_bar_ = new QProgressBar;
    progress_bar_->setRange(0, 5);
    progress_bar_->setValue(0);
    progress_bar_->setFormat("Samples: %v / %m");
    progress_bar_->setVisible(false);
    fl->addWidget(progress_bar_);

    auto* btn_row = new QHBoxLayout;
    auto_cap_btn_ = new QPushButton("📷 Auto-Capture from Feed");
    auto_cap_btn_->setObjectName("primaryBtn");
    auto_cap_btn_->setToolTip(
        "Automatically captures multiple samples from the live feed for better accuracy.");
    connect(auto_cap_btn_, &QPushButton::clicked, this, &FaceManagerDialog::startAutoCapture);

    auto* file_btn = new QPushButton("📁 Load Image File");
    file_btn->setToolTip("Import a face photo from disk.");
    connect(file_btn, &QPushButton::clicked, this, &FaceManagerDialog::enrollFromFile);

    btn_row->addWidget(auto_cap_btn_);
    btn_row->addWidget(file_btn);
    fl->addLayout(btn_row);

    status_lbl_ = new QLabel("Ready — enter a name and click Auto-Capture.");
    status_lbl_->setStyleSheet("color: #94a3b8; font-size: 11px;");
    fl->addWidget(status_lbl_);

    preview_row->addWidget(form_box);
    layout->addLayout(preview_row);

    auto* list_box = new QGroupBox("ENROLLED PERSON PROFILES");
    auto* ll = new QVBoxLayout(list_box);
    face_list_ = new QListWidget;
    face_list_->setMinimumHeight(200);
    ll->addWidget(face_list_);
    layout->addWidget(list_box, 1);

    auto* ftr = new QHBoxLayout;
    ftr->addStretch();
    auto* close_btn = new QPushButton("Close");
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    ftr->addWidget(close_btn);
    layout->addLayout(ftr);

    capture_timer_ = new QTimer(this);
    capture_timer_->setInterval(400);
    connect(capture_timer_, &QTimer::timeout, this, &FaceManagerDialog::autoCaptureTick);

    refreshPreview();
}

void FaceManagerDialog::refreshPreview() {
    if (!frame_provider_) return;
    cv::Mat frame = frame_provider_();
    if (frame.empty()) return;
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    QImage qimg(rgb.data, rgb.cols, rgb.rows,
                static_cast<int>(rgb.step), QImage::Format_RGB888);
    QPixmap pix = QPixmap::fromImage(qimg).scaled(
        160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    preview_lbl_->setPixmap(pix);
}

void FaceManagerDialog::startAutoCapture() {
    QString name = name_input_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Please enter a person's name first.");
        return;
    }
    if (!frame_provider_ || frame_provider_().empty()) {
        QMessageBox::warning(this, "No Feed",
                             "No active camera frame available.\n"
                             "Use 'Load Image File' to enroll from a photo instead.");
        return;
    }
    capture_count_ = 0;
    capture_target_ = samples_spin_->value();
    capturing_ = true;
    progress_bar_->setMaximum(capture_target_);
    progress_bar_->setValue(0);
    progress_bar_->setVisible(true);
    auto_cap_btn_->setEnabled(false);
    status_lbl_->setText(QString("Capturing 0 / %1 samples…").arg(capture_target_));
    capture_timer_->start();
}

void FaceManagerDialog::autoCaptureTick() {
    if (!capturing_ || !frame_provider_) {
        stopAutoCapture(false);
        return;
    }
    cv::Mat frame = frame_provider_();
    if (frame.empty()) {
        stopAutoCapture(false);
        return;
    }
    QString name = name_input_->text().trimmed();
    std::string msg;
    bool ok = recognizer_->enrollFromFrame(name.toStdString(), frame, msg);
    if (ok) {
        capture_count_++;
        progress_bar_->setValue(capture_count_);
        status_lbl_->setText(QString("Captured %1 / %2 samples…")
                                 .arg(capture_count_).arg(capture_target_));
        refreshPreview();
    }
    if (capture_count_ >= capture_target_) stopAutoCapture(true);
}

void FaceManagerDialog::stopAutoCapture(bool success) {
    capture_timer_->stop();
    capturing_ = false;
    auto_cap_btn_->setEnabled(true);
    if (success) {
        QString name = name_input_->text().trimmed();
        status_lbl_->setText(
            QString("✅ Enrolled %1 samples for '%2'. LBPH model retrained.")
                .arg(capture_count_).arg(name));
        status_lbl_->setStyleSheet("color: #4ade80; font-size: 11px;");
        name_input_->clear();
        refreshList();
    } else {
        status_lbl_->setText("⚠ Capture stopped.");
        status_lbl_->setStyleSheet("color: #fbbf24; font-size: 11px;");
    }
}

void FaceManagerDialog::enrollFromFile() {
    QString name = name_input_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Please enter a person's name first.");
        return;
    }
    QString fpath = QFileDialog::getOpenFileName(
        this, "Select Face Image", "", "Images (*.png *.jpg *.jpeg)");
    if (fpath.isEmpty()) return;
    cv::Mat img = cv::imread(fpath.toStdString());
    if (img.empty()) {
        QMessageBox::warning(this, "Error", "Could not read image file.");
        return;
    }
    std::string msg;
    bool ok = recognizer_->enrollFromFrame(name.toStdString(), img, msg);
    if (ok) {
        QMessageBox::information(this, "Enrolled", QString::fromStdString(msg));
        name_input_->clear();
        status_lbl_->setStyleSheet("color: #4ade80; font-size: 11px;");
        status_lbl_->setText(QString("✅ %1").arg(QString::fromStdString(msg)));
        refreshList();
    } else {
        QMessageBox::warning(this, "Error", "Failed to save face image.");
    }
}

void FaceManagerDialog::refreshList() {
    face_list_->clear();
    auto enrolled = recognizer_->listEnrolled();
    if (enrolled.empty()) {
        auto* item = new QListWidgetItem("No enrolled profiles. Enroll a person above.");
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(Qt::gray);
        face_list_->addItem(item);
        return;
    }
    for (const std::string& name_str : enrolled) {
        QString name = QString::fromStdString(name_str);
        int count = recognizer_->sampleCount(name_str);

        auto* card = new QWidget;
        card->setFixedHeight(44);
        auto* cl = new QHBoxLayout(card);
        cl->setContentsMargins(8, 2, 8, 2);

        auto* lbl = new QLabel("👤  " + name.toUpper());
        lbl->setStyleSheet("font-size: 13px; font-weight: 700; color: #f8fafc; background: transparent; border: none;");
        cl->addWidget(lbl);

        auto* sample_lbl = new QLabel(QString("%1 sample%2").arg(count).arg(count != 1 ? "s" : ""));
        sample_lbl->setStyleSheet(
            "font-size: 11px; color: #4ade80; background: rgba(74,222,128,0.1);"
            "border: 1px solid rgba(74,222,128,0.3); border-radius: 5px; padding: 2px 8px;");
        cl->addWidget(sample_lbl);
        cl->addStretch();

        auto* add_btn = new QPushButton("➕ Add Sample");
        add_btn->setFixedHeight(26);
        add_btn->setStyleSheet(
            "QPushButton { padding: 2px 8px; font-size: 10px; font-weight: 700; "
            "background: rgba(56,189,248,0.15); border: 1px solid rgba(56,189,248,0.4); "
            "border-radius: 5px; color: #38bdf8; }"
            "QPushButton:hover { background: rgba(56,189,248,0.35); }");
        connect(add_btn, &QPushButton::clicked, this, [this, name] {
            if (!frame_provider_ || frame_provider_().empty()) {
                QMessageBox::warning(this, "No Feed",
                                     "No active camera frame available. Open the dialog from a live feed.");
                return;
            }
            QString clean = name;
            name_input_->setText(clean.replace("_", " "));
            samples_spin_->setValue(3);
            startAutoCapture();
        });
        cl->addWidget(add_btn);

        auto* del_btn = new QPushButton("🗑 Delete");
        del_btn->setFixedHeight(26);
        del_btn->setObjectName("dangerBtn");
        connect(del_btn, &QPushButton::clicked, this, [this, name_str, name] {
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "Delete Profile",
                QString("Permanently delete ALL samples for '%1'?").arg(name),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                recognizer_->deletePerson(name_str);
                refreshList();
                status_lbl_->setStyleSheet("color: #f43f5e; font-size: 11px;");
                status_lbl_->setText(QString("🗑 Deleted profile: %1").arg(name));
            }
        });
        cl->addWidget(del_btn);

        auto* item = new QListWidgetItem(face_list_);
        item->setSizeHint(QSize(0, 48));
        face_list_->addItem(item);
        face_list_->setItemWidget(item, card);
    }
}

}  // namespace ui
