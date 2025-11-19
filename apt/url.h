#ifndef _URL_H
#define _URL_H_ 1

#ifdef __cplusplus
extern "C" {
#endif /* C++ */

#include <string.h>

typedef enum url_schema {
  PROTO_HTTP = 1,
  PROTO_HTTPS,
} url_proto;

typedef struct url_sview {
  unsigned int begin, end;
} url_str_view;

static inline void url_strcpy(char *dst, const char *src, const url_str_view *view) {
  unsigned int len = view->end - view->begin;
  strncpy(dst, src + view->begin, len);
}

struct url_parse {
  url_proto proto;
  url_str_view host;
  url_str_view path;
  url_str_view ofile;
  unsigned short port;
  void (*url_error)(const char *fmt, ...);
};

/**
 * Set `result->url_error`, and parse the field
 */
int urlparse(const char *url, struct url_parse *result);

#ifdef __cplusplus
}
#endif /* C++ */

#endif
