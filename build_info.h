#ifndef BELYA_BUILD_INFO_H
#define BELYA_BUILD_INFO_H

/* Provenance that travels with the binary. A SHA in a sidecar file describes what
 * someone intended to deploy; a SHA compiled in describes what is running. */
const char *belya_build_sha(void);
const char *belya_build_date(void);
const char *belya_version_string(void);   /* "belya <sha> (<date>)" */

#endif /* BELYA_BUILD_INFO_H */
