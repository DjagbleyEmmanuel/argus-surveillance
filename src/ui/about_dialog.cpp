#include "ui/about_dialog.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("About Argus");
    setWindowIcon(QIcon(":/icons/app.png"));
    resize(460, 300);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto* title = new QLabel("◆ ARGUS");
    title->setObjectName("headerTitle");
    title->setStyleSheet("font-size: 22px; font-weight: 800; color: #00f0ff; letter-spacing: 3px;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* sub = new QLabel("TACTICAL VISION SUITE");
    sub->setStyleSheet("color: #f8fafc; font-size: 12px; font-weight: 700; letter-spacing: 2px;");
    sub->setAlignment(Qt::AlignCenter);
    layout->addWidget(sub);

    auto* version = new QLabel(QString("Version %1").arg(ARGUS_VERSION));
    version->setStyleSheet("color: #38bdf8; font-size: 12px; font-weight: 700;");
    version->setAlignment(Qt::AlignCenter);
    layout->addWidget(version);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: rgba(255,255,255,0.1);");
    layout->addWidget(sep);

    auto* desc = new QLabel(
        "Multi-camera AI surveillance suite.\n"
        "Motion (MOG2) · Human (HOG) · Face (LBPH) · YOLOv4 objects.\n"
        "Event-triggered recording, multi-monitor popout, night vision,\n"
        "and a full sci-fi HUD.");
    desc->setStyleSheet("color: #94a3b8; font-size: 11px;");
    desc->setAlignment(Qt::AlignCenter);
    layout->addWidget(desc);

    layout->addStretch();

    auto* close_btn = new QPushButton("Close");
    close_btn->setFixedWidth(120);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);

    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(close_btn);
    row->addStretch();
    layout->addLayout(row);
}

}  // namespace ui
