/****************************************************************************
 * WiiMC
 *
 * tls.c
 *
 * TLS client connections for WiiMC's own HTTP code, using mbedTLS over a
 * connected non-blocking libogc socket. Certificates are always verified,
 * against the CA bundle cacert.pem in WiiMC's folder; there is no
 * unverified fallback.
 ***************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <network.h>
#include <ogc/mutex.h>
#include <ogc/lwp_watchdog.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h> /* error codes only; MBEDTLS_NET_C is off */
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "tls.h"

extern char appPath[];

struct wiimc_tls {
    s32 sock;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
};

/* The CA bundle is parsed once and then only read, by every connection:
 * WiiMC's own and, through the wrappers below, FFmpeg's. */
static mbedtls_x509_crt ca_chain;
static int ca_state; /* 0 not loaded yet, 1 loaded, -1 failed */
static int ca_error;
static mutex_t ca_mutex = LWP_MUTEX_NULL;

int __real_mbedtls_x509_crt_parse_file(mbedtls_x509_crt *chain, const char *path);
int __wrap_mbedtls_x509_crt_parse_file(mbedtls_x509_crt *chain, const char *path);
void __real_mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *conf, mbedtls_x509_crt *ca,
    mbedtls_x509_crl *crl);
void __wrap_mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *conf, mbedtls_x509_crt *ca,
    mbedtls_x509_crl *crl);

static void ca_path(char *path, size_t size)
{
    snprintf(path, size, "%s/cacert.pem", appPath);
}

static int load_ca_chain(void)
{
    char path[1024];
    int ret;

    if (ca_mutex == LWP_MUTEX_NULL)
        LWP_MutexInit(&ca_mutex, false);
    LWP_MutexLock(ca_mutex);
    if (ca_state == 0) {
        ca_path(path, sizeof(path));
        mbedtls_x509_crt_init(&ca_chain);
        ret = __real_mbedtls_x509_crt_parse_file(&ca_chain, path);
        // a positive result counts certificates that failed to parse
        if (ret < 0 || ca_chain.version == 0) {
            printf("tls: no usable CA bundle at %s (%d), https is not available\n", path, ret);
            mbedtls_x509_crt_free(&ca_chain);
            ca_error = ret < 0 ? ret : MBEDTLS_ERR_X509_INVALID_FORMAT;
            ca_state = -1;
        } else {
            ca_state = 1;
        }
    }
    LWP_MutexUnlock(ca_mutex);
    return ca_state == 1;
}

/* The DOL is linked with --wrap for these two. FFmpeg's tls_mbedtls.c
 * parses its CA file into a chain of its own for every connection; when
 * that file is WiiMC's bundle, its chain is left empty and the shared one
 * is configured in its place. Other files are parsed as before. */
int __wrap_mbedtls_x509_crt_parse_file(mbedtls_x509_crt *chain, const char *path)
{
    char ours[1024];

    ca_path(ours, sizeof(ours));
    if (strcmp(path, ours) == 0 && chain->version == 0)
        return load_ca_chain() ? 0 : ca_error;

    return __real_mbedtls_x509_crt_parse_file(chain, path);
}

void __wrap_mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *conf, mbedtls_x509_crt *ca,
    mbedtls_x509_crl *crl)
{
    if (ca && ca != &ca_chain && ca->version == 0 && ca_state == 1)
        ca = &ca_chain;
    __real_mbedtls_ssl_conf_ca_chain(conf, ca, crl);
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    s32 ret = net_send(*(s32 *)ctx, buf, len, 0);

    if (ret == -EAGAIN)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (ret < 0)
        return MBEDTLS_ERR_NET_SEND_FAILED;
    return ret;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    s32 ret = net_recv(*(s32 *)ctx, buf, len, 0);

    if (ret == -EAGAIN)
        return MBEDTLS_ERR_SSL_WANT_READ;
    if (ret < 0)
        return MBEDTLS_ERR_NET_RECV_FAILED;
    return ret;
}

static void tls_free(wiimc_tls *t)
{
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    free(t);
}

wiimc_tls *tls_connect(s32 s, const char *host, u32 timeout_ms)
{
    static const char pers[] = "wiimc http";
    wiimc_tls *t;
    char err[100];
    u64 start;
    int ret;

    if (!load_ca_chain())
        return NULL;

    t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    t->sock = s;
    mbedtls_entropy_init(&t->entropy);
    mbedtls_ctr_drbg_init(&t->drbg);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_ssl_init(&t->ssl);

    if ((ret = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy,
            (const unsigned char *)pers, sizeof(pers) - 1)) != 0 ||
        (ret = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
        goto fail;

    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&t->conf, &ca_chain, NULL);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);

    if ((ret = mbedtls_ssl_setup(&t->ssl, &t->conf)) != 0 ||
        (ret = mbedtls_ssl_set_hostname(&t->ssl, host)) != 0)
        goto fail;
    mbedtls_ssl_set_bio(&t->ssl, &t->sock, bio_send, bio_recv, NULL);

    start = gettime();
    while ((ret = mbedtls_ssl_handshake(&t->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
            goto fail;
        if (ticks_to_millisecs(diff_ticks(start, gettime())) > timeout_ms) {
            printf("tls: handshake with %s timed out\n", host);
            tls_free(t);
            return NULL;
        }
        usleep(10 * 1000);
    }
    return t;

fail:
    mbedtls_strerror(ret, err, sizeof(err));
    printf("tls: connection to %s failed: %s (-0x%04x)\n", host, err, (unsigned)-ret);
    if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
        printf("tls: certificate check flags 0x%x\n", (unsigned)mbedtls_ssl_get_verify_result(&t->ssl));
    tls_free(t);
    return NULL;
}

s32 tls_read(wiimc_tls *t, void *buf, u32 len)
{
    int ret = mbedtls_ssl_read(&t->ssl, buf, len);

    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        return -EAGAIN;
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;
    return ret;
}

s32 tls_write(wiimc_tls *t, const void *buf, u32 len)
{
    int ret = mbedtls_ssl_write(&t->ssl, buf, len);

    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        return -EAGAIN;
    return ret;
}

void tls_close(wiimc_tls *t)
{
    if (!t)
        return;
    mbedtls_ssl_close_notify(&t->ssl);
    tls_free(t);
}
