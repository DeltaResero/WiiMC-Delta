/****************************************************************************
 * WiiMC
 * Tantric 2009-2012
 *
 * http.h
 *
 * HTTP operations
 ***************************************************************************/

#ifndef _HTTP_H_
#define _HTTP_H_

u32 http_request (char *url, FILE *hfile, char *buffer, u32 maxsize, bool silent);

// Fetches url into buffer, accepting gzip, which arrives unpacked. With a body
// it sends a POST of JSON. headers adds whole header lines, each ending in
// \r\n. A gzip reply bigger than maxsize unpacked is cut at maxsize. Returns
// the size stored, or 0 on failure.
u32 http_fetch (const char *url, const char *body, const char *headers, char *buffer, u32 maxsize);

#endif
