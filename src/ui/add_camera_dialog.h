#pragma once

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QThread>

#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "core/camera.h"

namespace ui {

// Background USB re-scan so the GUI never blocks on sequential v4l2-ctl probes.
class UsbScanWorker : public QThread {
    Q_OBJECT
public:
    explicit UsbScanWorker(QObject* parent = nullptr) : QThread(parent) {}
    ~UsbScanWorker() override {
        requestInterruption();
        wait();
    }

signals:
    void finishedScan(std::vector<core::UsbCamera>);

protected:
    void run() override {
        try {
            emit finishedScan(core::CameraSource::detectUsbCameras());
        } catch (...) {
            emit finishedScan({});
        }
    }
};

// "Connect Surveillance Camera Feed" dialog.
// Port notes vs. Python: the USB list excludes indices already streaming
// (excludeIndices), and selection stores the index on the item so a skipped
// row can never yield a stale name.
class AddCameraDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddCameraDialog(std::vector<core::UsbCamera> usb_cameras = {},
                             std::set<int> exclude_indices = {},
                             QWidget* parent = nullptr);

    struct Result {
        std::string name;
        std::string type;   // "usb" | "rtsp" | "http"
        std::string url;
        std::string pixel_format;
    };
    std::optional<Result> getResult() const { return result_; }

private slots:
    void onUsbSelected(int row);
    void onRefreshUsb();
    void onScanFinished(std::vector<core::UsbCamera> cams);

private:
    void populateList(const std::vector<core::UsbCamera>& cams);

    std::vector<core::UsbCamera> usb_cameras_;
    std::set<int> exclude_indices_;
    std::optional<Result> result_;

    QListWidget* usb_list_ = nullptr;
    QLineEdit* url_input_ = nullptr;
    QLineEdit* name_input_ = nullptr;
    QComboBox* type_combo_ = nullptr;
    QComboBox* pixel_combo_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    UsbScanWorker* scan_thread_ = nullptr;
};

}  // namespace ui
