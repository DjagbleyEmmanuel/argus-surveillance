#pragma once

#include <map>
#include <optional>
#include <string>

namespace core {

// Runtime V4L2 user-control helper (mirrors core/v4l2_ctrl.py).
//
// USB-converted laptop cameras enumerate as standard UVC devices and each
// model exposes a different subset of controls. The night-time control
// `exposure_dynamic_framerate` does NOT exist on every camera, so capability
// is probed per device at runtime and never assumed.
namespace v4l2ctl {

inline constexpr const char* kExposureDynamicFramerate =
    "exposure_dynamic_framerate";

struct ControlInfo {
    int id = 0;        // control id (0x009a0903...)
    std::string type;  // "bool", "int", "menu", ...
    int value = -1;    // current on-device value, if listed
};

// All controls via `v4l2-ctl -d DEV -l`. Returns false on device errors.
bool listControls(const std::string& device,
                  std::map<std::string, ControlInfo>& out);

// True only if the control actually exists on this device.
bool controlSupported(const std::string& device, const std::string& name);

// Real on-device value. std::nullopt if the control does not exist (or the
// device cannot be queried).
std::optional<int> getControl(const std::string& device,
                              const std::string& name);

// Apply the value. Primary path is v4l2-ctl; if it fails (EBUSY/EACCES while
// streaming) a direct VIDIOC_S_CTRL ioctl with our own fd is used so the
// control sticks while the camera is open.
//  Returns: 0 = ok, -1 = unsupported / device error, 1 = device busy,
//           2 = permission denied.
int setControl(const std::string& device, const std::string& name, int value);

}  // namespace v4l2ctl
}  // namespace core
