/* Force-included (-include) when building the X server for RISC OS with
 * GCCSDK 4.7.4 / UnixLib. Fills small gaps in UnixLib's headers. */
#ifndef RISCOS_COMPAT_H
#define RISCOS_COMPAT_H

#include <fcntl.h>
#include <signal.h>

/* RISC OS has no symbolic links, so refusing to follow one is a no-op. */
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

/* UnixLib has siginfo_t but none of the si_code values. Only used by the
 * server to word its crash message; pick a value UnixLib never sets so
 * faults are not misreported as signals sent by a user. */
#ifndef SI_USER
#define SI_USER 0x7fff0001
#endif

#endif
