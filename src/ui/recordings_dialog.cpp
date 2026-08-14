#include "ui/recordings_dialog.h"

#include "ui/media_player_dialog.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProcess>
#include <QSize>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {

namespace {

struct MediaEntry {
    QString name;
    QString path;
    qint64 size = 0;
    qint64 mtime = 0;
};

QList<MediaEntry> listMedia(const QString& dir, const QStringList& extensions) {
    QList<MediaEntry> out;
    QDir d(dir);
    if (!d.exists()) return out;
    const auto infos = d.entryInfoList(QDir::Files, QDir::Name);
    for (const auto& fi : infos) {
        QString ext = fi.suffix().toLower();
        if (extensions.contains(ext)) {
            out.append({fi.fileName(), fi.absoluteFilePath(), fi.size(),
                        fi.lastModified().toSecsSinceEpoch()});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const MediaEntry& a, const MediaEntry& b) { return a.mtime > b.mtime; });
    return out;
}

QString formatSize(qint64 bytes) {
    if (bytes >= 100 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
}

}  // namespace

RecordingsDialog::RecordingsDialog(QString output_dir, QString snapshot_dir, QWidget* parent)
    : QDialog(parent), output_dir_(std::move(output_dir)), snapshot_dir_(std::move(snapshot_dir)) {
    setWindowTitle("Media & Snapshot Gallery");
    resize(920, 580);
    setMinimumSize(850, 480);
    buildUi();
    refreshClips();
    refreshSnapshots();
}

void RecordingsDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto* hdr_box = new QGroupBox("Storage Overview & Destination");
    auto* hb = new QHBoxLayout(hdr_box);
    summary_label_ = new QLabel("Loading media stats...");
    summary_label_->setStyleSheet("color: #38bdf8; font-weight: 700; font-size: 12px;");
    hb->addWidget(summary_label_);
    hb->addStretch();

    auto* change_btn = new QPushButton("📁 Change Storage Folder");
    change_btn->setObjectName("primaryBtn");
    connect(change_btn, &QPushButton::clicked, this, &RecordingsDialog::chooseStorageDirectory);

    auto* refresh_btn = new QPushButton("🔄 Refresh");
    connect(refresh_btn, &QPushButton::clicked, this, &RecordingsDialog::refreshAll);

    hb->addWidget(change_btn);
    hb->addWidget(refresh_btn);
    layout->addWidget(hdr_box);

    tabs_ = new QTabWidget;

    auto* video_tab = new QWidget;
    auto* v_layout = new QVBoxLayout(video_tab);
    v_layout->setContentsMargins(0, 8, 0, 0);
    video_list_ = new QListWidget;
    v_layout->addWidget(video_list_);
    tabs_->addTab(video_tab, "🎬 VIDEO CLIPS");

    auto* snap_tab = new QWidget;
    auto* s_layout = new QVBoxLayout(snap_tab);
    s_layout->setContentsMargins(0, 8, 0, 0);
    snap_list_ = new QListWidget;
    s_layout->addWidget(snap_list_);
    tabs_->addTab(snap_tab, "📸 HD SNAPSHOTS");

    layout->addWidget(tabs_, 1);

    auto* ftr = new QHBoxLayout;
    auto* open_folder_btn = new QPushButton("📁 Open Current Folder");
    connect(open_folder_btn, &QPushButton::clicked, this, &RecordingsDialog::openFolder);
    auto* close_btn = new QPushButton("Close");
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    ftr->addWidget(open_folder_btn);
    ftr->addStretch();
    ftr->addWidget(close_btn);
    layout->addLayout(ftr);
}

void RecordingsDialog::refreshClips() {
    video_list_->clear();
    QDir().mkpath(output_dir_);

    const QStringList exts = {"mp4", "avi", "mkv"};
    for (const auto& e : listMedia(output_dir_, exts)) {
        auto* card = new QWidget;
        card->setFixedHeight(54);
        card->setStyleSheet(
            "QWidget { background: #0e172a; border: 1px solid rgba(255,255,255,0.08);"
            "border-radius: 8px; }");
        auto* cl = new QHBoxLayout(card);
        cl->setContentsMargins(14, 4, 14, 4);
        cl->setSpacing(12);

        auto* name_lbl = new QLabel("🎥  " + e.name);
        name_lbl->setStyleSheet(
            "font-size: 13px; font-weight: 700; color: #f8fafc; background: transparent; border: none;");
        cl->addWidget(name_lbl);
        cl->addStretch();

        QString dt = QDateTime::fromSecsSinceEpoch(e.mtime).toString("yyyy-MM-dd HH:mm:ss");
        auto* meta = new QLabel(QString("%1  •  %2").arg(dt, formatSize(e.size)));
        meta->setStyleSheet(
            "color: #38bdf8; font-size: 11px; font-weight: 600; background: rgba(56,189,248,0.1);"
            "border: 1px solid rgba(56,189,248,0.25); border-radius: 6px; padding: 4px 10px;");
        cl->addWidget(meta);

        auto* play_btn = new QPushButton("🔍 INSPECT CLIP");
        play_btn->setObjectName("primaryBtn");
        play_btn->setFixedHeight(34);
        play_btn->setMinimumWidth(125);
        connect(play_btn, &QPushButton::clicked, this, [this, e] { playFile(e.path); });

        auto* del_btn = new QPushButton("🗑 DELETE");
        del_btn->setObjectName("dangerBtn");
        del_btn->setFixedHeight(34);
        del_btn->setMinimumWidth(85);
        connect(del_btn, &QPushButton::clicked, this, [this, e] { deleteFile(e.path, false); });

        cl->addWidget(play_btn);
        cl->addWidget(del_btn);

        auto* item = new QListWidgetItem(video_list_);
        item->setSizeHint(QSize(0, 60));
        video_list_->addItem(item);
        video_list_->setItemWidget(item, card);
    }
    updateSummary();
}

void RecordingsDialog::refreshSnapshots() {
    snap_list_->clear();
    QDir().mkpath(snapshot_dir_);

    const QStringList exts = {"png", "jpg", "jpeg"};
    for (const auto& e : listMedia(snapshot_dir_, exts)) {
        auto* card = new QWidget;
        card->setFixedHeight(54);
        card->setStyleSheet(
            "QWidget { background: #0e172a; border: 1px solid rgba(255,255,255,0.08);"
            "border-radius: 8px; }");
        auto* cl = new QHBoxLayout(card);
        cl->setContentsMargins(14, 4, 14, 4);
        cl->setSpacing(12);

        auto* name_lbl = new QLabel("📸  " + e.name);
        name_lbl->setStyleSheet(
            "font-size: 13px; font-weight: 700; color: #f8fafc; background: transparent; border: none;");
        cl->addWidget(name_lbl);
        cl->addStretch();

        QString dt = QDateTime::fromSecsSinceEpoch(e.mtime).toString("yyyy-MM-dd HH:mm:ss");
        auto* meta = new QLabel(QString("%1  •  %2").arg(dt, formatSize(e.size)));
        meta->setStyleSheet(
            "color: #4ade80; font-size: 11px; font-weight: 600; background: rgba(74,222,128,0.1);"
            "border: 1px solid rgba(74,222,128,0.25); border-radius: 6px; padding: 4px 10px;");
        cl->addWidget(meta);

        auto* view_btn = new QPushButton("👁 VIEW IMAGE");
        view_btn->setObjectName("primaryBtn");
        view_btn->setFixedHeight(34);
        view_btn->setMinimumWidth(110);
        connect(view_btn, &QPushButton::clicked, this, [this, e] { playFile(e.path); });

        auto* del_btn = new QPushButton("🗑 DELETE");
        del_btn->setObjectName("dangerBtn");
        del_btn->setFixedHeight(34);
        del_btn->setMinimumWidth(85);
        connect(del_btn, &QPushButton::clicked, this, [this, e] { deleteFile(e.path, true); });

        cl->addWidget(view_btn);
        cl->addWidget(del_btn);

        auto* item = new QListWidgetItem(snap_list_);
        item->setSizeHint(QSize(0, 60));
        snap_list_->addItem(item);
        snap_list_->setItemWidget(item, card);
    }
    updateSummary();
}

void RecordingsDialog::updateSummary() {
    qint64 total = 0;
    for (const auto& d : {output_dir_, snapshot_dir_}) {
        QDir dir(d);
        if (!dir.exists()) continue;
        for (const auto& fi : dir.entryInfoList(QDir::Files))
            total += fi.size();
    }
    summary_label_->setText(
        QString("Folder: %1  |  Videos: %2  |  Snapshots: %3  |  Storage: %4 MB")
            .arg(QFileInfo(output_dir_).fileName())
            .arg(video_list_->count())
            .arg(snap_list_->count())
            .arg(total / (1024.0 * 1024.0), 0, 'f', 1));
}

void RecordingsDialog::refreshAll() {
    refreshClips();
    refreshSnapshots();
}

void RecordingsDialog::playFile(const QString& path) {
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, "Error", "File does not exist.");
        return;
    }
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov") {
        MediaPlayerDialog dlg(path, this);
        dlg.exec();
    } else {
        QProcess::startDetached("xdg-open", {path});
    }
}

void RecordingsDialog::deleteFile(const QString& path, bool snapshot) {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, snapshot ? "Delete Snapshot" : "Delete Video Clip",
        QString("Permanently delete '%1'?").arg(QFileInfo(path).fileName()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    if (QFile::remove(path)) {
        if (snapshot) refreshSnapshots();
        else refreshClips();
    } else {
        QMessageBox::warning(this, "Error", "Failed to delete file.");
    }
}

void RecordingsDialog::openFolder() {
    QString dir = (tabs_->currentIndex() == 1) ? snapshot_dir_ : output_dir_;
    QProcess::startDetached("xdg-open", {QDir(dir).absolutePath()});
}

void RecordingsDialog::chooseStorageDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Storage Folder", output_dir_);
    if (dir.isEmpty()) return;
    output_dir_ = dir;
    snapshot_dir_ = dir + "/snapshots";
    QDir().mkpath(output_dir_);
    QDir().mkpath(snapshot_dir_);
    refreshAll();
    QMessageBox::information(this, "Storage Updated",
                             QString("Storage path set to:\n%1").arg(dir));
}

}  // namespace ui
