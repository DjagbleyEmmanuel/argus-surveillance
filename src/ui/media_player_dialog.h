#pragma once

#include <QDialog>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QVideoWidget>

namespace ui {

// Playback dialog: video clips via QMediaPlayer/QVideoWidget, images via QLabel.
// Mirrors ui/media_player_dialog.py.
class MediaPlayerDialog : public QDialog {
    Q_OBJECT
public:
    explicit MediaPlayerDialog(const QString& file_path, QWidget* parent = nullptr);

private slots:
    void onPositionChanged(qint64 pos);
    void onDurationChanged(qint64 duration);

private:
    QString file_path_;
    bool is_video_ = false;

    QVideoWidget* video_widget_ = nullptr;
    QLabel* image_label_ = nullptr;

    QMediaPlayer* player_ = nullptr;
    QPushButton* play_btn_ = nullptr;
    QSlider* seek_slider_ = nullptr;
    QLabel* time_label_ = nullptr;
};

}  // namespace ui
