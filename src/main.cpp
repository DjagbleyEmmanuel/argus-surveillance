#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QTimer>

#include <opencv2/core.hpp>

#include <algorithm>
#include <thread>

#include "ui/main_window.h"

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    QApplication app(argc, argv);
    app.setApplicationName("Argus");
    app.setOrganizationName("Argus");
    app.setWindowIcon(QIcon(":/icons/app.png"));

    // Cap OpenCV's internal thread pool so N camera feeds don't oversubscribe.
    unsigned cores = std::thread::hardware_concurrency();
    int n = static_cast<int>(std::max(1u, std::min(4u, cores / 2)));
    cv::setNumThreads(n);
    qInfo("OpenCV thread pool limited to %d threads.", n);

    QFont font("Segoe UI", 10);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    ui::MainWindow window;
    window.show();

    if (app.arguments().contains("--selftest-sliders")) {
        QTimer::singleShot(2500, [&] {
            int tested = window.selftestYoloSliders();
            qInfo("SELFTEST: exercised %d YOLO sliders, no crash.", tested);
            window.close();
            app.quit();
        });
    }

    if (app.arguments().contains("--selftest-canvas")) {
        QTimer::singleShot(3500, [&] {
            int rc = window.selftestCanvas();
            qInfo("SELFTEST-CANVAS: rc=%d", rc);
            window.close();
            app.quit();
        });
    }

    return app.exec();
}
