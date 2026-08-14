#include "ui/detached_camera_window.h"

#include <QCloseEvent>
#include <QVBoxLayout>

#include "ui/camera_widget.h"

namespace ui {

DetachedCameraWindow::DetachedCameraWindow(CameraWidget* camera_widget,
                                           QObject* main_window)
    : camera_widget_(camera_widget), main_window_(main_window) {
    // No Qt parent: a transient window (QDialog with a parent) minimizes along
    // with the main window. An independent top-level stays visible.
    setWindowFlags(Qt::Window);
    setWindowTitle(QString("Multi-Monitor Stream - %1")
                       .arg(QString::fromStdString(camera_widget_->camera()->name())));
    resize(800, 500);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(camera_widget_);
}

void DetachedCameraWindow::closeEvent(QCloseEvent* event) {
    emit closed();
    event->accept();
}

}  // namespace ui
