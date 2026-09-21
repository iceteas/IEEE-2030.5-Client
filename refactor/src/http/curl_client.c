#include "se/http_client.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct HttpClient {
  CURL *easy;
  char *ca_path;
  char *cert_path;
  char *key_path;
  char *accept;
  long connect_timeout_sec;
  long transfer_timeout_sec;
  int verify_peer;
};

typedef struct Buffer {
  uint8_t *data;
  size_t len;
  size_t cap;
} Buffer;

static char *xstrdup(const char *s) {
  size_t n;
  char *o;
  if (!s) return NULL;
  n = strlen(s);
  o = malloc(n + 1);
  if (!o) return NULL;
  memcpy(o, s, n + 1);
  return o;
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  Buffer *b = userdata;
  size_t n = size * nmemb;
  if (b->len + n + 1 > b->cap) {
    size_t cap = b->cap ? b->cap * 2 : 4096;
    uint8_t *nd;
    while (cap < b->len + n + 1) cap *= 2;
    nd = realloc(b->data, cap);
    if (!nd) return 0;
    b->data = nd;
    b->cap = cap;
  }
  memcpy(b->data + b->len, ptr, n);
  b->len += n;
  b->data[b->len] = '\0';
  return n;
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
  HttpResult *r = userdata;
  size_t n = size * nitems;
  const char *k = "Content-Type:";
  size_t klen = strlen(k);
  if (n > klen && strncasecmp(buffer, k, klen) == 0) {
    char *p = buffer + klen;
    char *end;
    while (*p == ' ' || *p == '\t') p++;
    end = p;
    while (*end && *end != '\r' && *end != '\n' && *end != ';') end++;
    free(r->content_type);
    r->content_type = malloc((size_t)(end - p) + 1);
    if (r->content_type) {
      memcpy(r->content_type, p, (size_t)(end - p));
      r->content_type[end - p] = '\0';
    }
  }
  return n;
}

void http_result_clear(HttpResult *r) {
  if (!r) return;
  free(r->content_type);
  free(r->body);
  free(r->error);
  memset(r, 0, sizeof(*r));
}

HttpClient *http_client_new(const HttpClientConfig *cfg) {
  HttpClient *c;
  CURLcode rc;
  static int inited = 0;
  if (!inited) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    inited = 1;
  }
  c = calloc(1, sizeof(HttpClient));
  if (!c) return NULL;
  c->easy = curl_easy_init();
  if (!c->easy) {
    free(c);
    return NULL;
  }
  c->connect_timeout_sec = cfg && cfg->connect_timeout_sec ? cfg->connect_timeout_sec : 10;
  c->transfer_timeout_sec = cfg && cfg->transfer_timeout_sec ? cfg->transfer_timeout_sec : 60;
  c->verify_peer = cfg ? cfg->verify_peer : 1;
  if (cfg) {
    c->ca_path = xstrdup(cfg->ca_path);
    c->cert_path = xstrdup(cfg->cert_path);
    c->key_path = xstrdup(cfg->key_path);
    c->accept = xstrdup(cfg->accept ? cfg->accept : "application/sep+xml");
  } else {
    c->accept = xstrdup("application/sep+xml");
  }
  (void)rc;
  return c;
}

void http_client_free(HttpClient *c) {
  if (!c) return;
  if (c->easy) curl_easy_cleanup(c->easy);
  free(c->ca_path);
  free(c->cert_path);
  free(c->key_path);
  free(c->accept);
  free(c);
}

static int http_perform(HttpClient *c, const char *method, const char *url,
                        const char *content_type, const uint8_t *body, size_t len,
                        HttpResult *out) {
  Buffer buf = {0};
  struct curl_slist *headers = NULL;
  char accept_hdr[256];
  CURLcode rc;
  long status = 0;

  if (!c || !url || !out) return -1;
  memset(out, 0, sizeof(*out));

  curl_easy_reset(c->easy);
  curl_easy_setopt(c->easy, CURLOPT_URL, url);
  curl_easy_setopt(c->easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c->easy, CURLOPT_CONNECTTIMEOUT, c->connect_timeout_sec);
  curl_easy_setopt(c->easy, CURLOPT_TIMEOUT, c->transfer_timeout_sec);
  curl_easy_setopt(c->easy, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(c->easy, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(c->easy, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt(c->easy, CURLOPT_HEADERDATA, out);
  curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYPEER, c->verify_peer ? 1L : 0L);
  curl_easy_setopt(c->easy, CURLOPT_SSL_VERIFYHOST, c->verify_peer ? 2L : 0L);

  if (c->ca_path) curl_easy_setopt(c->easy, CURLOPT_CAINFO, c->ca_path);
  if (c->cert_path) curl_easy_setopt(c->easy, CURLOPT_SSLCERT, c->cert_path);
  if (c->key_path) curl_easy_setopt(c->easy, CURLOPT_SSLKEY, c->key_path);

  snprintf(accept_hdr, sizeof(accept_hdr), "Accept: %s",
           c->accept ? c->accept : "application/sep+xml");
  headers = curl_slist_append(headers, accept_hdr);

  if (method && strcmp(method, "GET") != 0) {
    curl_easy_setopt(c->easy, CURLOPT_CUSTOMREQUEST, method);
    if (body && len) {
      curl_easy_setopt(c->easy, CURLOPT_POSTFIELDS, body);
      curl_easy_setopt(c->easy, CURLOPT_POSTFIELDSIZE, (long)len);
    }
    if (content_type) {
      char ct[256];
      snprintf(ct, sizeof(ct), "Content-Type: %s", content_type);
      headers = curl_slist_append(headers, ct);
    }
  }

  curl_easy_setopt(c->easy, CURLOPT_HTTPHEADER, headers);
  rc = curl_easy_perform(c->easy);
  curl_slist_free_all(headers);

  if (rc != CURLE_OK) {
    out->error = xstrdup(curl_easy_strerror(rc));
    free(buf.data);
    return -1;
  }

  curl_easy_getinfo(c->easy, CURLINFO_RESPONSE_CODE, &status);
  out->status = status;
  out->body = buf.data;
  out->body_len = buf.len;
  return 0;
}

int http_get(HttpClient *c, const char *url, HttpResult *out) {
  return http_perform(c, "GET", url, NULL, NULL, 0, out);
}

int http_send(HttpClient *c, const char *method, const char *url,
              const char *content_type, const uint8_t *body, size_t len,
              HttpResult *out) {
  return http_perform(c, method ? method : "GET", url, content_type, body, len,
                      out);
}
