#pragma once
// Force-included (/FI) into every upstream source compiled for Windows (C and C++).
// Supplies the POSIX vocabulary types shared headers use in declarations. It must
// stay declaration-only: behavior differences belong in #ifdef _WIN32 code paths.
#ifdef _WIN32

#include <stdint.h>
#include <sys/types.h>

typedef int      pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef intptr_t ssize_t;
typedef unsigned int uint; // glibc <sys/types.h>
typedef unsigned int mode_t;

// GCC alternate keywords used by C headers shared with Linux (libdrm).
#define __inline__   inline
#define __volatile__ volatile

#if defined(_MSC_VER) && !defined(__clang__)
// C headers shared with Linux (libinput) annotate prototypes with GCC attributes
// (printf formats, deprecation). MSVC has no equivalent syntax; drop them.
#define __attribute__(x)
#endif

#ifdef __cplusplus
#include "Posix.hpp"
#include <type_traits>
// libwayland's wl_container_of uses GCC's __typeof__, which MSVC lacks.
#define __typeof__(expr) std::remove_reference_t<decltype(expr)>
#endif

#endif
