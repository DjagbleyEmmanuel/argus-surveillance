#pragma once

#include <QDialog>

namespace ui {

class CameraWidget;

// Independent top-level window for dragging a feed to a secondary monitor.
// Port notes vs. Python: created with NO Qt parent + explicit Qt::Window so the
// OS treats it as its own top-level window — minimizing the main window does
// NOT minimize this one.
class DetachedCameraWindow : public QDialog {
    Q_OBJECT
public:
    explicit DetachedCameraWindow(CameraWidget* camera_widget,
                                  QObject* main_window = nullptr);

    CameraWidget* cameraWidget() const { return camera_widget_; }

protected:
    void closeEvent(QCloseEvent* event) override;

signals:
    void closed();

private:
    CameraWidget* camera_widget_;
    QObject* main_window_;
};

}  // namespace ui
