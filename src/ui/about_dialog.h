#pragma once

#include <QDialog>

namespace ui {

// Simple app info dialog. Mirrors ui/about_dialog.py.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

}  // namespace ui
