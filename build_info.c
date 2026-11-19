#include "build_info.h"

#include <stdio.h>

/* Supplied by the Makefile (-DBELYA_BUILD_SHA / -DBELYA_BUILD_DATE). The
 * fallbacks keep a tarball or hand-compiled build honest instead of failing. */
#ifndef BELYA_BUILD_SHA
#define BELYA_BUILD_SHA "unknown"
#endif

#ifndef BELYA_BUILD_DATE
#define BELYA_BUILD_DATE "unknown"
#endif

const char *belya_build_sha(void) { return BELYA_BUILD_SHA; }
const char *belya_build_date(void) { return BELYA_BUILD_DATE; }

const char *belya_version_string(void) {
    static char buf[96];
    snprintf(buf, sizeof(buf), "belya %s (%s)", BELYA_BUILD_SHA, BELYA_BUILD_DATE);
    return buf;
}
