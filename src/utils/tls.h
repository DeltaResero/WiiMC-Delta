/****************************************************************************
 * WiiMC
 *
 * tls.h
 *
 * TLS client connections for WiiMC's own HTTP code, using mbedTLS over a
 * connected non-blocking libogc socket.
 ***************************************************************************/

#ifndef _WIIMC_TLS_H_
#define _WIIMC_TLS_H_

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wiimc_tls wiimc_tls;

/* Handshake on socket s and verify the server's certificate for host
 * against cacert.pem in WiiMC's folder. NULL on any failure, including a
 * missing CA bundle. The socket stays open either way. */
wiimc_tls *tls_connect(s32 s, const char *host, u32 timeout_ms);

/* Like net_read()/net_write(): bytes transferred, -EAGAIN when the call
 * would block, 0 when the server closed the connection, another negative
 * value on errors. */
s32 tls_read(wiimc_tls *t, void *buf, u32 len);
s32 tls_write(wiimc_tls *t, const void *buf, u32 len);

/* Send close_notify and free the connection; the socket is not closed. */
void tls_close(wiimc_tls *t);

#ifdef __cplusplus
}
#endif

#endif
