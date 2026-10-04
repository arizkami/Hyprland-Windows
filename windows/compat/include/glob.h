#pragma once
/* POSIX glob() for Windows; implemented in windows/compat/Posix.cpp.
 * Supports '*', '?' and '[...]' (with '!'/'^' negation and ranges) in any path
 * component, '/' or '\' separators, GLOB_TILDE and sorted results. */
#include <stddef.h>

typedef struct {
    size_t gl_pathc; /* matched paths */
    char** gl_pathv; /* gl_pathc paths, then NULL */
    size_t gl_offs;  /* unused; always 0 */
} glob_t;

#define GLOB_ERR      (1 << 0)
#define GLOB_MARK     (1 << 1)
#define GLOB_NOSORT   (1 << 2)
#define GLOB_NOCHECK  (1 << 4)
#define GLOB_NOESCAPE (1 << 6)
#define GLOB_TILDE    (1 << 12)

#define GLOB_NOSPACE 1
#define GLOB_ABORTED 2
#define GLOB_NOMATCH 3

#ifdef __cplusplus
extern "C" {
#endif
int  glob(const char* pattern, int flags, int (*errfunc)(const char* path, int error), glob_t* result);
void globfree(glob_t* result);
#ifdef __cplusplus
}
#endif
