#pragma once

#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QWidget>

#include <string>

namespace ui {

// Media & Snapshot gallery. Mirrors ui/recordings_dialog.py.
class RecordingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RecordingsDialog(QString output_dir = "recordings",
                              QString snapshot_dir = "snapshots",
                              QWidget* parent = nullptr);

private slots:
    void refreshClips();
    void refreshSnapshots();
    void refreshAll();

private:
    void buildUi();
    void updateSummary();
    void playFile(const QString& path);
    void deleteFile(const QString& path, bool snapshot);
    void openFolder();
    void chooseStorageDirectory();

    QString output_dir_;
    QString snapshot_dir_;

    QLabel* summary_label_ = nullptr;
    QListWidget* video_list_ = nullptr;
    QListWidget* snap_list_ = nullptr;
    QTabWidget* tabs_ = nullptr;
};

}  // namespace ui
