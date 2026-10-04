#pragma once
/* Memory mapping of descriptors is only used for buffers and keymaps shared with
 * Wayland clients, which Windows does not have. mmap() therefore fails with ENOSYS;
 * implemented in windows/compat/Posix.cpp. */
#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void*)-1)

#ifdef __cplusplus
extern "C" {
#endif
void* mmap(void* address, size_t length, int protection, int flags, int fd, long long offset);
int   munmap(void* address, size_t length);
#ifdef __cplusplus
}
#endif
