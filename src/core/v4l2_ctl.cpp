#include "core/v4l2_ctl.h"

#include <QByteArray>
#include <QProcess>
#include <QStringList>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <regex>

namespace core {
namespace v4l2ctl {

// Run v4l2-ctl; returns its exit code (-1 on timeout / failure to launch).
static int runV4l2Ctl(const QStringList& args, QString* out, QString* err,
                      int timeout_ms = 2000) {
    QProcess p;
    p.start(QStringLiteral("v4l2-ctl"), args);
    if (!p.waitForFinished(timeout_ms)) {
        p.kill();
        p.waitForFinished(500);
        return -1;
    }
    if (out) *out = QString::fromLocal8Bit(p.readAllStandardOutput());
    if (err) *err = QString::fromLocal8Bit(p.readAllStandardError());
    return p.exitCode();
}

bool listControls(const std::string& device,
                  std::map<std::string, ControlInfo>& out) {
    out.clear();
    QString so, se;
    if (runV4l2Ctl({QStringLiteral("-d"), QString::fromStdString(device),
                    QStringLiteral("-l")}, &so, &se) != 0) {
        return false;
    }
    const std::string text = (so + se).toStdString();
    // e.g. "     exposure_dynamic_framerate 0x009a0903 (bool)   : default=0 value=1"
    std::regex name_re(R"(\s*([a-zA-Z0-9_]+)\s+(0x[0-9a-fA-F]+)\s+\(([^)]+)\))");
    std::regex val_re(R"(value=(-?\d+))");
    std::smatch m;
    auto it = text.cbegin();
    while (std::regex_search(it, text.cend(), m, name_re)) {
        ControlInfo ci;
        ci.id = static_cast<int>(std::stoul(m[2].str(), nullptr, 16));
        ci.type = m[3].str();
        const auto& pre = m.prefix();
        const auto& post = m.suffix();
        // value may precede or follow the id within the line; scan the whole line
        const std::string line = pre.str() + m.str() + post.str();
        std::smatch vm;
        if (std::regex_search(line, vm, val_re)) {
            ci.value = std::stoi(vm[1].str());
        }
        out[m[1].str()] = ci;
        it = m[0].second;
    }
    return true;
}

bool controlSupported(const std::string& device, const std::string& name) {
    std::map<std::string, ControlInfo> ctrls;
    if (!listControls(device, ctrls)) return false;
    return ctrls.find(name) != ctrls.end();
}

std::optional<int> getControl(const std::string& device,
                              const std::string& name) {
    QString so, se;
    if (runV4l2Ctl({QStringLiteral("-d"), QString::fromStdString(device),
                    QStringLiteral("--get-ctrl"), QString::fromStdString(name)},
                   &so, &se) != 0) {
        return std::nullopt;
    }
    const QString combined = so + se;
    if (combined.contains(QStringLiteral("unknown control"), Qt::CaseInsensitive))
        return std::nullopt;
    const int pos = combined.lastIndexOf(QLatin1Char(':'));
    if (pos < 0) return std::nullopt;
    bool ok = false;
    const int v = combined.mid(pos + 1).trimmed().toInt(&ok);
    if (!ok) return std::nullopt;
    return std::optional<int>(v);
}

int setControl(const std::string& device, const std::string& name, int value) {
    QString so, se;
    const int rc = runV4l2Ctl(
        {QStringLiteral("-d"), QString::fromStdString(device),
         QStringLiteral("-c"),
         QString::fromStdString(name) + QLatin1Char('=') + QString::number(value)},
        &so, &se);
    if (rc == 0) return 0;

    // Fallback: raw VIDIOC_S_CTRL ioctl with our own fd — many UVC controls
    // are settable while streaming even when v4l2-ctl reports EBUSY.
    int id = -1;
    std::map<std::string, ControlInfo> ctrls;
    if (listControls(device, ctrls)) {
        auto it = ctrls.find(name);
        if (it != ctrls.end()) id = it->second.id;
    }
    if (id < 0) return -1;

    const int fd = ::open(device.c_str(), O_RDWR);
    if (fd < 0) return -1;
    struct v4l2_control ctl = {};
    ctl.id = static_cast<__u32>(id);
    ctl.value = value;
    const int irc = ::ioctl(fd, VIDIOC_S_CTRL, &ctl);
    const int saved_errno = errno;
    ::close(fd);
    if (irc == 0) return 0;
    if (saved_errno == EBUSY) return 1;
    if (saved_errno == EACCES || saved_errno == EPERM) return 2;
    return -1;
}

}  // namespace v4l2ctl
}  // namespace core
