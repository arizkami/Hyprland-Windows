#pragma once
/* The libuuid subset Hyprland uses; implemented in windows/compat/Posix.cpp. */
typedef unsigned char uuid_t[16];

#ifdef __cplusplus
extern "C" {
#endif
/* Random (version 4, RFC 9562 variant) UUID from the system CSPRNG. */
void uuid_generate_random(uuid_t out);
#ifdef __cplusplus
}
#endif
