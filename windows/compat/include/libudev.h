#pragma once
/* Opaque udev types for headers that mention them (libinput.h). Windows has no udev;
 * device discovery comes from the aquamarine Win32 backend. */
#ifdef __cplusplus
extern "C" {
#endif
struct udev;
struct udev_device;
struct udev_monitor;
struct udev_enumerate;
#ifdef __cplusplus
}
#endif
