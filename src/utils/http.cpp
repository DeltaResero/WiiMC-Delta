/****************************************************************************
 * WiiMC
 * Tantric 2009-2012
 *
 * http.cpp
 *
 * HTTP operations
 * Written by dhewg/bushing, modified by Tantric
 ***************************************************************************/

#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ogcsys.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <zlib.h>

#include "menu.h"
#include "http.h"
#include "wiimc.h"

//#define http_malloc malloc
//#define http_free free
//#define http_memalign memalign

#include "mem2_manager.h"
#include "tls.h"
#define http_malloc(x) mem2_malloc(x, MEM2_OTHER)
#define http_free(x) mem2_free(x, MEM2_OTHER)
#define http_memalign(x,y) mem2_memalign(x,y, MEM2_OTHER)

#define MAX_SIZE (1024*1024*15)

extern "C" {
#include "stream/url.h"
}

#define TCP_CONNECT_TIMEOUT     13  // 13 secs to make a connection
#define TCP_SEND_SIZE           (32 * 1024)
#define TCP_RECV_SIZE           (32 * 1024)
#define TCP_BLOCK_RECV_TIMEOUT  15000 // 15 secs to receive
#define TCP_BLOCK_SEND_TIMEOUT  15000 // 15 secs to send
#define TCP_BLOCK_SIZE          2048
#define HTTP_TIMEOUT            35000 // 35 secs to get an http response
#define IOS_O_NONBLOCK          0x04
#define TLS_HANDSHAKE_TIMEOUT   15000 // 15 secs for the TLS handshake

// a connected socket, with TLS on top for https
typedef struct
{
    s32 s;
    wiimc_tls *tls;
} http_conn;

static s32 conn_read(const http_conn *c, void *buf, u32 len)
{
    return c->tls ? tls_read(c->tls, buf, len) : net_read(c->s, buf, len);
}

static s32 conn_write(const http_conn *c, const void *buf, u32 len)
{
    return c->tls ? tls_write(c->tls, buf, len) : net_write(c->s, buf, len);
}

static void conn_close(http_conn *c)
{
    tls_close(c->tls);
    c->tls = NULL;
    net_close(c->s);
}

static int _httoi(const char *value)
{
    struct CHexMap
    {
        char chr;
        int value;
    };
    const int HexMapL = 16;
    CHexMap HexMap[HexMapL] =
    {
        {'0', 0}, {'1', 1},
        {'2', 2}, {'3', 3},
        {'4', 4}, {'5', 5},
        {'6', 6}, {'7', 7},
        {'8', 8}, {'9', 9},
        {'A', 10}, {'B', 11},
        {'C', 12}, {'D', 13},
        {'E', 14}, {'F', 15}
    };

    char *s = (char *)value;
    int result = 0;
    if (*s == '0' && toupper(*(s + 1)) == 'X') s += 2;
    bool firsttime = true;
    while (*s != '\0')
    {
        bool found = false;
        for (int i = 0; i < HexMapL; i++)
        {
            if (toupper(*s) == HexMap[i].chr)
            {
                if (!firsttime) result <<= 4;
                result |= HexMap[i].value;
                found = true;
                break;
            }
        }
        if (!found) break;
        s++;
        firsttime = false;
    }

    return result;
}

static s32 tcp_socket(void)
{
    s32 s, res;

    s = net_socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0)
        return s;

    // Switch off Nagle with TCP_NODELAY
    u32 nodelay = 1;
    net_setsockopt(s,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof(nodelay));

    res = net_fcntl(s, F_GETFL, 0);
    if (res < 0)
    {
        net_close(s);
        return res;
    }

    res = net_fcntl(s, F_SETFL, res | IOS_O_NONBLOCK);
    if (res < 0)
    {
        net_close(s);
        return res;
    }

    return s;
}

static s32 tcp_connect(char *host, const u16 port)
{
    struct hostent *hp;
    struct sockaddr_in sa;
    struct in_addr val;
    s32 s, res;
    u64 t1;

    s = tcp_socket();
    if (s < 0)
        return s;

    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family= PF_INET;
    sa.sin_len = sizeof(struct sockaddr_in);
    sa.sin_port= htons(port);

    if(strlen(host) < 16 && inet_aton(host, &val))
    {
        sa.sin_addr.s_addr = val.s_addr;
    }
    else
    {
        hp = net_gethostbyname (host);
        if (!hp || !(hp->h_addrtype == PF_INET))
        {
            // errno can be zero or positive here, which the caller would
            // take for a socket and then close someone else's
            net_close(s);
            return -1;
        }

        memcpy((char *) &sa.sin_addr, hp->h_addr_list[0], hp->h_length);
    }

    t1=ticks_to_secs(gettime());
    do
    {
        res = net_connect(s,(struct sockaddr*) &sa, sizeof (sa));
        if(ticks_to_secs(gettime())-t1 > TCP_CONNECT_TIMEOUT) break;
        usleep(500);
    } while(res != -EISCONN);

    if(res != -EISCONN)
    {
        net_close(s);
        return -1;
    }

    return s;
}

static int tcp_readln(const http_conn *s, char *buf, const u16 max_length)
{
    s32 res = -1;
    s32 ret;
    u64 start_time = gettime();
    u16 c = 0;

    while (c < max_length)
    {
        if (ticks_to_millisecs(diff_ticks(start_time, gettime())) > HTTP_TIMEOUT)
            break;

        ret = conn_read(s, &buf[c], 1);

        if (ret == -EAGAIN)
        {
            usleep(20 * 1000);
            continue;
        }

        if (ret <= 0)
            break;

        if (c > 0 && buf[c - 1] == '\r' && buf[c] == '\n')
        {
            res = 0;
            buf[c-1] = 0;
            break;
        }
        c++;
        start_time = gettime();
        usleep(100);
    }
    return res;
}

static u32 tcp_read(const http_conn *s, u8 *buffer, const u32 length)
{
    char *p;
    u32 left, block, received, step=0;
    u64 t;
    s32 res;

    p = (char *)buffer;
    left = length;
    received = 0;

    t = gettime();
    while (left)
    {
        if (ticks_to_millisecs(diff_ticks(t, gettime()))
                > TCP_BLOCK_RECV_TIMEOUT)
        {
            break;
        }

        block = left;
        if (block > TCP_RECV_SIZE)
            block = TCP_RECV_SIZE;

        res = conn_read(s, p, block);

        if (res == -EAGAIN)
        {
            usleep(20 * 1000);
            continue;
        }

        if(res<=0) break;

        received += res;
        left -= res;
        p += res;
        usleep(1000);

        if ((received / TCP_BLOCK_SIZE) > step)
        {
            t = gettime ();
            step++;
        }
    }
    return received;
}

static u32 tcp_write(const http_conn *s, const u8 *buffer, const u32 length)
{
    const u8 *p;
    u32 left, block, sent, step=0;
    s64 t;
    s32 res;

    p = buffer;
    left = length;
    sent = 0;

    t = gettime();
    while (left)
    {
        if (ticks_to_millisecs(diff_ticks(t, gettime()))
                > TCP_BLOCK_SEND_TIMEOUT)
        {
            break;
        }

        block = left;
        if (block > TCP_SEND_SIZE)
            block = TCP_SEND_SIZE;

        res = conn_write(s, p, block);

        if ((res == 0) || (res == -56) || (res == -EAGAIN))
        {
            usleep(20 * 1000);
            continue;
        }

        if (res < 0)
            break;

        sent += res;
        left -= res;
        p += res;
        usleep(100);

        if ((sent / TCP_BLOCK_SIZE) > step)
        {
            t = gettime ();
            step++;
        }
    }

    return left == 0;
}

// the value of a header line when its name matches, ignoring case
static const char *header_value(const char *line, const char *name)
{
    size_t n = strlen(name);

    if (strncasecmp(line, name, n) != 0 || line[n] != ':')
        return NULL;

    line += n + 1;
    while (*line == ' ' || *line == '\t')
        line++;
    return line;
}

// reads a whole body, chunked or not, into dst; stops at cap
static u32 read_body(const http_conn *s, bool chunked, u32 content_length, char *dst, u32 cap)
{
    if (!chunked)
        return tcp_read(s, (u8 *)dst, content_length < cap ? content_length : cap);

    char line[64];
    u32 size = 0;

    while (tcp_readln(s, line, sizeof(line) - 1) == 0)
    {
        u32 len = _httoi(line);
        if (len == 0 || size + len > cap)
            break;

        u32 got = tcp_read(s, (u8 *)dst + size, len);
        size += got;

        if (got != len || tcp_readln(s, line, sizeof(line) - 1) != 0) // chunk boundary
            break;
    }
    return size;
}

// unpacks a gzip body into buffer, keeping only what fits in maxsize
static u32 gunzip(const char *in, u32 insize, char *buffer, u32 maxsize)
{
    z_stream z;
    memset(&z, 0, sizeof(z));

    if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK)
        return 0;

    z.next_in = (Bytef *)in;
    z.avail_in = insize;
    z.next_out = (Bytef *)buffer;
    z.avail_out = maxsize;

    int ret = inflate(&z, Z_FINISH);
    u32 size = maxsize - z.avail_out;
    inflateEnd(&z);

    // a full buffer, or a reply cut short, still gives the part unpacked
    if (ret != Z_STREAM_END && ret != Z_BUF_ERROR)
        return 0;
    return size;
}

#define MAX_GZIP_SIZE (1024*1024)

/****************************************************************************
 * http_request
 * Retrieves the specified URL, and stores it in the specified file or buffer.
 * With a body it sends a POST of JSON. headers adds whole header lines, each
 * ending in \r\n. With gzip it accepts a gzip reply, unpacked into the buffer.
 ***************************************************************************/
static u32 http_request(const char *url, FILE *hfile, char *buffer, u32 maxsize, bool silent, int total_redirects,
    const char *body, const char *headers, bool gzip)
{
    u32 res = 0;
    char *http_host=NULL;
    char *http_path=NULL;
    u16 http_port;
    bool https;
    URL_t *tmpurl;

    u32 http_status = 404;
    u32 sizeread = 0, content_length = 0;

    int linecount;

    if(maxsize > MAX_SIZE)
        return 0;

    if (url == NULL || (hfile == NULL && buffer == NULL))
        return 0;

    tmpurl = url_new((char *)url);

    if (tmpurl == NULL)
        return 0;

    if(strlen(tmpurl->hostname) >= 1024 || strlen(tmpurl->file) >= 2048)
    {
        url_free(tmpurl);
        return 0;
    }
    http_host = (char*) malloc(1024);
    http_path = (char*) malloc(2048);

    strcpy(http_host, tmpurl->hostname);
    strcpy(http_path, tmpurl->file);
    http_port = tmpurl->port;
    https = tmpurl->protocol && strcasecmp(tmpurl->protocol, "https") == 0;

    if(http_port == 0)
        http_port = https ? 443 : 80;

    url_free(tmpurl);

    http_conn conn = { -1, NULL };
    http_conn *s = &conn;

    conn.s = tcp_connect(http_host, http_port);

    if (conn.s < 0)
    {
        free(http_path);
        free(http_host);
        return 0;
    }

    if (https)
    {
        // the server's certificate must check out against WiiMC's CA bundle
        conn.tls = tls_connect(conn.s, http_host, TLS_HANDSHAKE_TIMEOUT);
        if (!conn.tls)
        {
            net_close(conn.s);
            free(http_path);
            free(http_host);
            return 0;
        }
    }

    size_t bodylen = body ? strlen(body) : 0;
    size_t reqsize = strlen(http_path) + strlen(http_host) + (headers ? strlen(headers) : 0) + 512;
    char *request = (char*) malloc(reqsize);
    char *r = request;
    char *end = request + reqsize;

    r += snprintf(r, end - r, "%s %s HTTP/1.1\r\n", body ? "POST" : "GET", http_path);
    r += snprintf(r, end - r, "Host: %s\r\n", http_host);
    if (!headers || !strstr(headers, "User-Agent:"))
        r += snprintf(r, end - r, "User-Agent: %s/%s (IOS%d)\r\n", APPNAME, APPVERSION, IOS_GetVersion());
    if (gzip)
        r += snprintf(r, end - r, "Accept-Encoding: gzip\r\n");
    if (body)
        r += snprintf(r, end - r, "Content-Type: application/json\r\nContent-Length: %u\r\n", (unsigned)bodylen);
    if (headers)
        r += snprintf(r, end - r, "%s", headers);
    r += snprintf(r, end - r, "Connection: close\r\nCache-Control: no-cache\r\n\r\n");

    res = tcp_write(s, (u8 *) request, strlen(request));

    if (res && body)
        res = tcp_write(s, (const u8 *) body, bodylen);

    free(request);
    free(http_path);
    free(http_host);

    if(res == 0) // can't send request to server
    {
        conn_close(s);
        return 0;
    }

    char *line = NULL;
    char *redirect = NULL;
    const char *v;
    bool chunked = false;
    bool gzipped = false;
    redirect = (char*) malloc(2048);
    redirect[0] = 0;
    line = (char*) malloc(2079);
    for (linecount = 0; linecount < 64; linecount++)
    {
        memset(line,0,2079);
        if (tcp_readln(s, line, 2078) != 0)
        {
            http_status = 404;
            break;
        }
        if (strlen(line) < 1)
            break;

        sscanf(line, "HTTP/1.%*u %u", &http_status);

        if ((v = header_value(line, "Content-Length")))
            content_length = strtoul(v, NULL, 10);
        else if ((v = header_value(line, "Location")))
            snprintf(redirect, 2048, "%s", v);
        else if ((v = header_value(line, "Transfer-Encoding")))
            chunked = strncasecmp(v, "chunked", 7) == 0;
        else if ((v = header_value(line, "Content-Encoding")))
            gzipped = strncasecmp(v, "gzip", 4) == 0;
    }

    if (http_status != 200)
    {
        free(line);
        conn_close(s);

        if((http_status == 301 || http_status == 302) && redirect[0] != 0 && total_redirects < 5)  //only 5 redirects allowed
        {
            u32 ret = http_request(redirect, hfile, buffer, maxsize, silent, total_redirects+1, body, headers, gzip);
            free(redirect);
            return ret;
        }
        free(redirect);
        return 0;
    }
    free(redirect);

    // gzip was asked for and sent: read it all, then unpack into the buffer
    if (gzipped && gzip && buffer != NULL)
    {
        free(line);

        char *z = (char *) malloc(MAX_GZIP_SIZE);
        if (!z)
        {
            conn_close(s);
            return 0;
        }

        if(!silent)
            ShowAction("Downloading...");

        // anything past MAX_GZIP_SIZE is left unread
        u32 zsize = read_body(s, chunked, chunked ? 0 : content_length, z, MAX_GZIP_SIZE);
        conn_close(s);

        sizeread = zsize ? gunzip(z, zsize, buffer, maxsize) : 0;
        free(z);

        if(!silent)
            CancelAction();
        return sizeread;
    }

    // length unknown - just read as much as we can
    if(content_length == 0)
    {
        content_length = maxsize;
    }
    else if (content_length > maxsize)
    {
        free(line);
        conn_close(s);
        return 0;
    }

    if (buffer != NULL)
    {
        free(line);

        if(!silent)
            ShowAction("Downloading...");

        sizeread = read_body(s, chunked, content_length, buffer, maxsize);

        if(chunked)
            content_length = sizeread;

        if(!silent)
            CancelAction();
    }
    else
    {
        // read into file
        u32 bufSize = (1024 * 32);
        u32 bytesLeft = content_length;
        u32 readSize;
        free(line);

        if(!silent)
            ShowProgress("Downloading...", 0, content_length);
        u8 * fbuffer = (u8 *) http_malloc(bufSize);
        if(fbuffer)
        {
            while (bytesLeft > 0)
            {
                if (bytesLeft < bufSize)
                    readSize = bytesLeft;
                else
                    readSize = bufSize;

                res = tcp_read(s, fbuffer, readSize);
                if (!res)
                    break;

                sizeread += res;
                bytesLeft -= res;

                res = fwrite(fbuffer, 1, res, hfile);
                if (!res)
                    break;

                if(!silent)
                    ShowProgress("Downloading...", (content_length - bytesLeft), content_length);
            }
            http_free(fbuffer);
        }
        if(!silent)
            CancelAction();
    }

    conn_close(s);

    if (content_length < maxsize && sizeread != content_length)
    {
        return 0;
    }

    return sizeread;
}

u32 http_request(char *url, FILE *hfile, char *buffer, u32 maxsize, bool silent)
{
    return http_request(url, hfile, buffer, maxsize, silent, 0, NULL, NULL, false);
}

u32 http_fetch(const char *url, const char *body, const char *headers, char *buffer, u32 maxsize)
{
    return http_request(url, NULL, buffer, maxsize, true, 0, body, headers, true);
}
