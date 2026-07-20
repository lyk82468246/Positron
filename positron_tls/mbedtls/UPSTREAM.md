# Mbed TLS upstream

- Project: Mbed TLS
- Version: 2.16.12
- Tag: `mbedtls-2.16.12`
- Commit: `cf4667126010c665341f9e50ef691b7ef8294188`
- Source: https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-2.16.12
- License: Apache-2.0 OR GPL-2.0-or-later; Positron selects Apache-2.0

This is a legacy compatibility pin selected because later branches did not
compile under the current MSVC9/C89 port without further work. The historical
LTS label does not imply current upstream maintenance. Positron's verified
client path uses `MBEDTLS_SSL_VERIFY_REQUIRED` and calls
`mbedtls_ssl_set_hostname()`, but maintainers must still review current Mbed
TLS advisories and treat an upgrade as security work rather than feature work.

The complete upstream tag is vendored so a clone contains every source file
referenced by `positron_tls.vcproj`. An audit against the official tag found no
content changes; the pre-existing local copy differed only in line endings.

Positron's Windows Mobile configuration and platform adapter live outside the
upstream tree in `../mbedtls_config.h` and `../positron_tls.c`. No Mbed TLS
source file is patched for this build.
