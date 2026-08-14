#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QSpinBox>
#include <QTimer>

#include <functional>
#include <memory>

#include <opencv2/opencv.hpp>

#include "core/face_recognizer.h"

namespace ui {

// Local Face Recognition Database — LBPH enrollment.
// Mirrors ui/face_manager_dialog.py. frame_provider lets MainWindow feed the
// live preview + auto-capture while the dialog is open.
class FaceManagerDialog : public QDialog {
    Q_OBJECT
public:
    FaceManagerDialog(std::shared_ptr<core::FaceRecognizer> recognizer,
                      std::function<cv::Mat()> frame_provider,
                      QWidget* parent = nullptr);

private slots:
    void startAutoCapture();
    void autoCaptureTick();
    void enrollFromFile();
    void refreshList();

private:
    void buildUi();
    void refreshPreview();
    void stopAutoCapture(bool success);

    std::shared_ptr<core::FaceRecognizer> recognizer_;
    std::function<cv::Mat()> frame_provider_;

    int capture_count_ = 0;
    int capture_target_ = 5;
    bool capturing_ = false;

    QLabel* preview_lbl_ = nullptr;
    QLineEdit* name_input_ = nullptr;
    QSpinBox* samples_spin_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QPushButton* auto_cap_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr;
    QListWidget* face_list_ = nullptr;
    QTimer* capture_timer_ = nullptr;
};

}  // namespace ui
