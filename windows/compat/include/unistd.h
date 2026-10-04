#pragma once
/* The POSIX descriptor subset the MSVC CRT already provides (io.h/process.h), under
 * POSIX names. Process creation (fork/exec) is deliberately absent: callers port it. */
#include <io.h>
#include <process.h>
#include <stdio.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#ifndef F_OK
#define F_OK 0
#define W_OK 2
#define R_OK 4
#define X_OK 0 /* Windows has no execute bit; access() treats it as existence */
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* pipe() as _pipe() with a binary, non-inherited pair. */
int pipe(int fds[2]);
#ifdef __cplusplus
}
#endif
