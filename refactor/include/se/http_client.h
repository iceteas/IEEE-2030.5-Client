#ifndef SE_HTTP_CLIENT_H
#define SE_HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HttpClient HttpClient;

typedef struct HttpClientConfig {
  const char *ca_path;
  const char *cert_path;
  const char *key_path;
  long connect_timeout_sec;
  long transfer_timeout_sec;
  int verify_peer; /* 1 = verify, 0 = skip (tests only) */
  const char *accept;
} HttpClientConfig;

typedef struct HttpResult {
  long status;
  char *content_type;
  uint8_t *body;
  size_t body_len;
  char *error;
} HttpResult;

HttpClient *http_client_new(const HttpClientConfig *cfg);
void http_client_free(HttpClient *c);

int http_get(HttpClient *c, const char *url, HttpResult *out);
int http_send(HttpClient *c, const char *method, const char *url,
              const char *content_type, const uint8_t *body, size_t len,
              HttpResult *out);
void http_result_clear(HttpResult *r);

#ifdef __cplusplus
}
#endif

#endif
