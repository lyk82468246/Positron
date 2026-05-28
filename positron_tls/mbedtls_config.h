/*
 * Positron mbedTLS configuration for Windows CE 5.2 / WM6 Pro (ARMV4I).
 * Target: mbedTLS 2.28.x LTS. TLS 1.2 client only.
 *
 * Activated via preprocessor define:
 *     MBEDTLS_CONFIG_FILE="mbedtls_config.h"
 */

#ifndef POSITRON_MBEDTLS_CONFIG_H
#define POSITRON_MBEDTLS_CONFIG_H

/* ---- Platform ---------------------------------------------------------- */
/* WinCE has malloc/free in CRT; let mbedTLS use them directly.            */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

/* WinCE has no /dev/urandom and CryptoAPI varies by device.               */
/* We register a custom hardware-poll callback in positron_tls.c.          */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* Disable time. We use MBEDTLS_SSL_VERIFY_NONE in Phase 1, so cert        */
/* expiry checks are skipped. Avoids any time_t portability surprises.    */
/* (MBEDTLS_HAVE_TIME deliberately NOT defined.)                           */

/* No file system. Certs are compiled in (Phase 2).                        */
/* (MBEDTLS_FS_IO deliberately NOT defined.)                               */

/* No built-in net layer. We supply our own Winsock2 BIO callbacks.        */
/* (MBEDTLS_NET_C deliberately NOT defined.)                               */

/* No threading. Each TLS handle is used from a single thread for now.    */
/* (MBEDTLS_THREADING_C deliberately NOT defined.)                         */

/* ---- RNG --------------------------------------------------------------- */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* ---- Symmetric --------------------------------------------------------- */
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_GCM_C

/* ---- Hashes ------------------------------------------------------------ */
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C       /* TLS 1.2 PRF for legacy ciphersuites    */
#define MBEDTLS_SHA1_C      /* X.509, legacy fingerprints              */
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C    /* provides SHA-384 too                    */

/* ---- Big-num and asymmetric ------------------------------------------- */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

/* ---- Elliptic curves --------------------------------------------------- */
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM

/* ---- X.509 (parsing only; verification disabled in Phase 1) ----------- */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* ---- TLS --------------------------------------------------------------- */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION   /* SNI required by api.anthropic.com */

/* ---- Key exchanges ----------------------------------------------------- */
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/* ---- Error strings (small, helpful for diagnostics) ------------------- */
#define MBEDTLS_ERROR_C

/* Validate this config against module dependencies. */
#include "mbedtls/check_config.h"

#endif /* POSITRON_MBEDTLS_CONFIG_H */
