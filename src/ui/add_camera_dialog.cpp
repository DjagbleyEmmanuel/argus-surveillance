#include "ui/add_camera_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ui {

AddCameraDialog::AddCameraDialog(std::vector<core::UsbCamera> usb_cameras,
                                 std::set<int> exclude_indices, QWidget* parent)
    : QDialog(parent),
      usb_cameras_(std::move(usb_cameras)),
      exclude_indices_(std::move(exclude_indices)) {
    setWindowTitle("Connect Surveillance Camera Feed");
    setFixedSize(520, 560);

    auto* tabs = new QTabWidget(this);

    // ---- USB tab ----
    auto* usb_tab = new QWidget;
    auto* uv = new QVBoxLayout(usb_tab);
    auto* header = new QHBoxLayout;
    header->addWidget(new QLabel("📹 Detected USB Cameras:"));
    refresh_btn_ = new QPushButton("🔄 Refresh");
    connect(refresh_btn_, &QPushButton::clicked, this, &AddCameraDialog::onRefreshUsb);
    header->addStretch();
    header->addWidget(refresh_btn_);
    uv->addLayout(header);

    usb_list_ = new QListWidget;
    populateList(usb_cameras_);
    connect(usb_list_, &QListWidget::currentRowChanged, this, &AddCameraDialog::onUsbSelected);
    uv->addWidget(usb_list_);
    tabs->addTab(usb_tab, "📹 STANDARD FEED");

    // ---- manual tab ----
    auto* manual_tab = new QWidget;
    auto* form = new QFormLayout(manual_tab);
    type_combo_ = new QComboBox;
    type_combo_->addItem("USB", "usb");
    type_combo_->addItem("RTSP", "rtsp");
    type_combo_->addItem("HTTP", "http");
    name_input_ = new QLineEdit;
    url_input_ = new QLineEdit;
    pixel_combo_ = new QComboBox;
    for (const auto& [label, code] : core::PixelFormats())
        pixel_combo_->addItem(QString::fromStdString(label), QString::fromStdString(code));
    form->addRow("Type:", type_combo_);
    form->addRow("Name:", name_input_);
    form->addRow("URL / Index:", url_input_);
    form->addRow("Pixel Format:", pixel_combo_);
    tabs->addTab(manual_tab, "⚙ MANUAL CONFIG");

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        std::string url = url_input_->text().toStdString();
        std::string name = name_input_->text().toStdString();
        std::string type = type_combo_->currentData().toString().toStdString();
        if (type == "usb" && name.empty())
            name = url.empty() ? "USB Camera" : "USB Camera " + url;
        if (url.empty()) {
            return;  // keep dialog open on empty URL
        }
        result_ = AddCameraDialog::Result{
            name, type, url, pixel_combo_->currentText().toStdString()};
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void AddCameraDialog::populateList(const std::vector<core::UsbCamera>& cams) {
    usb_list_->clear();
    for (const auto& c : cams) {
        if (exclude_indices_.count(c.index)) continue;
        auto* item = new QListWidgetItem(QString::fromStdString("📹 " + c.label));
        item->setData(Qt::UserRole, c.index);
        usb_list_->addItem(item);
    }
}

void AddCameraDialog::onUsbSelected(int row) {
    auto* item = usb_list_->item(row);
    if (!item) return;
    bool ok = false;
    int idx = item->data(Qt::UserRole).toInt(&ok);
    if (!ok) return;
    type_combo_->setCurrentIndex(0);  // USB
    url_input_->setText(QString::number(idx));
    // label already contains the stable device name
    for (const auto& c : usb_cameras_) {
        if (c.index == idx) {
            name_input_->setText(QString::fromStdString(c.label));
            break;
        }
    }
}

void AddCameraDialog::onRefreshUsb() {
    if (scan_thread_ && scan_thread_->isRunning()) return;
    refresh_btn_->setEnabled(false);
    refresh_btn_->setText("Scanning…");
    scan_thread_ = new UsbScanWorker(this);
    connect(scan_thread_, &UsbScanWorker::finishedScan,
            this, &AddCameraDialog::onScanFinished);
    scan_thread_->start();
}

void AddCameraDialog::onScanFinished(std::vector<core::UsbCamera> cams) {
    refresh_btn_->setEnabled(true);
    refresh_btn_->setText("🔄 Refresh");
    scan_thread_->deleteLater();
    scan_thread_ = nullptr;

    int cur = usb_list_->currentRow();
    usb_cameras_ = std::move(cams);
    populateList(usb_cameras_);
    if (cur >= 0 && cur < usb_list_->count())
        usb_list_->setCurrentRow(cur);
}

}  // namespace ui
