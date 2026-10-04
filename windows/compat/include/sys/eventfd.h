#pragma once
/* eventfd() pairs with DRM sync objects (no DRM on Windows); fails with ENOSYS.
 * Implemented in windows/compat/Posix.cpp. */
#include <stdint.h>

#define EFD_SEMAPHORE 0x1
#define EFD_CLOEXEC   0x80000
#define EFD_NONBLOCK  0x800

typedef uint64_t eventfd_t;

#ifdef __cplusplus
extern "C" {
#endif
int eventfd(unsigned int initval, int flags);
#ifdef __cplusplus
}
#endif
