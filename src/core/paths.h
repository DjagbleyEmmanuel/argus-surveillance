#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <cstdlib>
#include <string>

namespace core {

// Runtime models directory. Search order:
//   1. $ARGUS_ASSETS_DIR environment override
//   2. ARGUS_ASSETS_DIR compile definition (installed .deb location)
//   3. <appdir>/../share/argus/models   (installed .deb)
//   4. <appdir>/assets/models            (dev run from the build tree)
//   5. ./assets/models                   (legacy: run from repo/build cwd)
//   6. /usr/share/argus/models           (last resort)
// All but the last are only accepted if the directory actually exists.
inline std::string modelsDir() {
    const char* env = std::getenv("ARGUS_ASSETS_DIR");
    if (env && *env && QFileInfo::exists(QString::fromLocal8Bit(env)))
        return std::string(env);

#ifdef ARGUS_ASSETS_DIR
    if (QFileInfo::exists(QStringLiteral(ARGUS_ASSETS_DIR)))
        return std::string(ARGUS_ASSETS_DIR);
#endif

    const QString appdir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appdir + "/../share/argus/models",
        appdir + "/assets/models",
        QStringLiteral("assets/models"),
        QStringLiteral("/usr/share/argus/models"),
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) return c.toStdString();
    }
    return "assets/models";
}

}  // namespace core
