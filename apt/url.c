#include "url.h"

#include <assert.h>
#include <string.h>

static const char *schemes[] = {
    0,
    "http",
    "https",
};

#define NSCHEME (sizeof(schemes) / sizeof(schemes[0]))

int urlparse(const char *url, struct url_parse *result) {
  const char *sep = strstr(url, "://");
  if (!sep) {
    result->url_error("Invalid URL: missing scheme\n");
    return 1;
  }

  /* Parse scheme. */
  result->proto = 0;
  result->port = 0;
  const int scheme_len = sep - url;
  for (int i = 1; i < NSCHEME; i++) {
    if (strncmp(schemes[i], url, scheme_len) == 0) {
      result->proto = i;
      break;
    }
  }
  if (result->proto == 0) {
    result->url_error("unknown scheme\n");
    return 1;
  }

  /* Parse host name. */
  const char *host = sep + 3;
  const char *hend = host;
  result->host.begin = host - url;
  for (; *hend; hend++) {
    if (*hend == '/') {
      result->host.end = hend - url;
      break;
    }
    if (*hend == ':') {
      /* Parse port number. */
      result->host.end = hend - url;
      unsigned long port = 0;
      hend++;
      while (*hend != '/') {
        int digit = *hend;
        if (digit > '9' || digit < '0') {
          result->url_error("invalid digit in port\n");
          return 1;
        }

        port *= 10;
        port += (digit - '0');

        if (port > __UINT16_MAX__) {
          result->url_error("port number too large\n");
          return 1;
        }
        hend++;
      }

      result->port = port;
      break;
    }
  }
  if (!result->port) {
    switch (result->proto) {
    case (PROTO_HTTP): {
      result->port = 80;
      break;
    }
    case (PROTO_HTTPS): {
      result->port = 443;
      break;
    }
    }
  }

  /* Parse resource path. */
  if (!(*hend)) {
    result->host.end = hend - url;
    url_str_view empty = {
        .begin = 0,
        .end = 0,
    };
    result->ofile = result->path = empty;
  } else {
    assert(*hend == '/');
    url_str_view view = {
        .begin = hend - url,
        .end = strlen(url),
    };

    result->path = view;

    char *last = strrchr(hend, '/');
    assert(last);
    view.begin = last - url + 1;
    result->ofile = view;
  }

  return 0;
}
