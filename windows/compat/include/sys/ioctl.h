#pragma once
/* ioctl() only addresses Linux device nodes (DRM); fails with ENOTTY on Windows.
 * Implemented in windows/compat/Posix.cpp. */
#ifdef __cplusplus
extern "C" {
#endif
int ioctl(int fd, unsigned long request, ...);
#ifdef __cplusplus
}
#endif
