#include "ui/media_player_dialog.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

#include <QTime>

namespace ui {

MediaPlayerDialog::MediaPlayerDialog(const QString& file_path, QWidget* parent)
    : QDialog(parent), file_path_(file_path) {
    setWindowTitle(QFileInfo(file_path).fileName());
    resize(900, 620);
    setMinimumSize(560, 420);

    QFileInfo info(file_path);
    QString ext = info.suffix().toLower();
    is_video_ = (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    if (is_video_) {
        video_widget_ = new QVideoWidget;
        video_widget_->setStyleSheet("background: #020208;");
        layout->addWidget(video_widget_, 1);

        player_ = new QMediaPlayer(this);
        auto* audio = new QAudioOutput(this);
        audio->setVolume(0.5);
        player_->setAudioOutput(audio);
        player_->setVideoOutput(video_widget_);
        player_->setSource(QUrl::fromLocalFile(file_path));

        auto* controls = new QHBoxLayout;
        play_btn_ = new QPushButton("⏸");
        play_btn_->setFixedWidth(56);
        seek_slider_ = new QSlider(Qt::Horizontal);
        time_label_ = new QLabel("00:00 / 00:00");
        time_label_->setStyleSheet("color: #94a3b8; font-size: 11px; font-weight: 700;");

        connect(play_btn_, &QPushButton::clicked, this, [this] {
            if (player_->playbackState() == QMediaPlayer::PlayingState)
                player_->pause();
            else
                player_->play();
        });
        connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
            play_btn_->setText(s == QMediaPlayer::PlayingState ? "⏸" : "▶");
        });
        connect(player_, &QMediaPlayer::positionChanged, this, &MediaPlayerDialog::onPositionChanged);
        connect(player_, &QMediaPlayer::durationChanged, this, &MediaPlayerDialog::onDurationChanged);
        connect(seek_slider_, &QSlider::sliderMoved, player_, &QMediaPlayer::setPosition);

        controls->addWidget(play_btn_);
        controls->addWidget(seek_slider_, 1);
        controls->addWidget(time_label_);
        layout->addLayout(controls);

        player_->play();
    } else {
        image_label_ = new QLabel;
        image_label_->setAlignment(Qt::AlignCenter);
        image_label_->setStyleSheet("background: #020208;");
        QPixmap pix(file_path);
        if (!pix.isNull())
            image_label_->setPixmap(pix.scaled(860, 560, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
            image_label_->setText("Could not load image.");
        layout->addWidget(image_label_, 1);

        auto* close_btn = new QPushButton("Close");
        close_btn->setFixedWidth(100);
        auto* row = new QHBoxLayout;
        row->addStretch();
        row->addWidget(close_btn);
        connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
        layout->addLayout(row);
    }
}

void MediaPlayerDialog::onPositionChanged(qint64 pos) {
    if (!seek_slider_ || !player_) return;
    qint64 dur = player_->duration();
    if (dur > 0) seek_slider_->setMaximum(static_cast<int>(dur));
    seek_slider_->blockSignals(true);
    seek_slider_->setValue(static_cast<int>(pos));
    seek_slider_->blockSignals(false);

    auto fmt = [](qint64 ms) {
        return QTime(0, 0, 0, 0).addMSecs(static_cast<int>(ms)).toString("mm:ss");
    };
    time_label_->setText(QString("%1 / %2").arg(fmt(pos), fmt(dur)));
}

void MediaPlayerDialog::onDurationChanged(qint64 duration) {
    if (seek_slider_) seek_slider_->setMaximum(static_cast<int>(duration));
}

}  // namespace ui
