#pragma once
/* POSIX uname() for Windows; implemented in windows/compat/Posix.cpp. */
#define _UTSNAME_LENGTH 65

struct utsname {
    char sysname[_UTSNAME_LENGTH];  /* "Windows" */
    char nodename[_UTSNAME_LENGTH]; /* computer name */
    char release[_UTSNAME_LENGTH];  /* major.minor.build */
    char version[_UTSNAME_LENGTH];  /* update build revision */
    char machine[_UTSNAME_LENGTH];  /* x86_64, aarch64 */
};

#ifdef __cplusplus
extern "C" {
#endif
int uname(struct utsname* name);
#ifdef __cplusplus
}
#endif
