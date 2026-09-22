#include "se/resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

ResourceNode *resource_node_new(const char *href, const char *type_hint) {
  ResourceNode *n = calloc(1, sizeof(ResourceNode));
  if (!n) return NULL;
  n->href = xstrdup(href);
  n->type_hint = type_hint ? xstrdup(type_hint) : NULL;
  n->state = NODE_NEW;
  return n;
}

void resource_node_free(ResourceNode *node) {
  int i;
  if (!node) return;
  for (i = 0; i < node->child_n; i++) resource_node_free(node->children[i]);
  free(node->children);
  val_free(node->data);
  free(node->href);
  free(node->type_hint);
  free(node);
}

void resource_set_data(ResourceNode *node, Val *data) {
  if (!node) return;
  if (node->data) val_free(node->data);
  node->data = data;
}

ResourceNode *resource_find_child(ResourceNode *parent, const char *href) {
  int i;
  if (!parent || !href) return NULL;
  for (i = 0; i < parent->child_n; i++) {
    if (parent->children[i]->href && strcmp(parent->children[i]->href, href) == 0)
      return parent->children[i];
  }
  return NULL;
}

ResourceNode *resource_add_child(ResourceNode *parent, const char *href,
                                 const char *type_hint) {
  ResourceNode *c;
  ResourceNode **ni;
  if (!parent || !href) return NULL;
  c = resource_find_child(parent, href);
  if (c) return c;
  c = resource_node_new(href, type_hint);
  if (!c) return NULL;
  c->parent = parent;
  if (parent->child_n + 1 > parent->child_cap) {
    int cap = parent->child_cap ? parent->child_cap * 2 : 4;
    ni = realloc(parent->children, (size_t)cap * sizeof(ResourceNode *));
    if (!ni) {
      resource_node_free(c);
      return NULL;
    }
    parent->children = ni;
    parent->child_cap = cap;
  }
  parent->children[parent->child_n++] = c;
  return c;
}

static const char *href_of_val(const Val *v) {
  const char *h;
  if (!v) return NULL;
  h = val_get_str(v, "@href");
  if (h) return h;
  return val_get_str(v, "href");
}

static void expand_val_item(ResourceNode *parent, Val *item, const char *type_hint) {
  const char *href;
  if (!item) return;
  href = href_of_val(item);
  if (!href) return;
  resource_add_child(parent, href, type_hint ? type_hint : item->key);
}

int resource_expand(ResourceNode *node) {
  int i, j;
  if (!node || !node->data) return 0;
  if (node->data->kind != VAL_OBJ) return 0;

  for (i = 0; i < node->data->u.kids.n; i++) {
    Val *child = node->data->u.kids.items[i];
    size_t klen;
    if (!child || !child->key) continue;
    klen = strlen(child->key);

    /* *Link fields */
    if (klen > 4 && strcmp(child->key + klen - 4, "Link") == 0) {
      const char *href = href_of_val(child);
      if (href) resource_add_child(node, href, child->key);
      continue;
    }

    if (child->kind == VAL_ARR) {
      for (j = 0; j < child->u.kids.n; j++)
        expand_val_item(node, child->u.kids.items[j], child->key);
      continue;
    }

    if (child->kind == VAL_OBJ && href_of_val(child))
      expand_val_item(node, child, child->key);
  }
  return 0;
}

static char *join_url(const char *base, const char *href) {
  size_t bl, hl;
  char *out;
  if (!href) return NULL;
  if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0)
    return xstrdup(href);
  if (!base) return xstrdup(href);
  bl = strlen(base);
  hl = strlen(href);
  out = malloc(bl + hl + 2);
  if (!out) return NULL;
  if (href[0] == '/') {
    /* base may be https://host:port or https://host:port/path — use origin only
       for absolute-path href. Simplest: if base has scheme, strip path. */
    const char *p = strstr(base, "://");
    const char *slash;
    size_t origin_len;
    if (p) {
      slash = strchr(p + 3, '/');
      origin_len = slash ? (size_t)(slash - base) : bl;
      memcpy(out, base, origin_len);
      memcpy(out + origin_len, href, hl + 1);
      return out;
    }
  }
  if (bl > 0 && base[bl - 1] == '/')
    snprintf(out, bl + hl + 2, "%s%s", base, href[0] == '/' ? href + 1 : href);
  else
    snprintf(out, bl + hl + 2, "%s/%s", base, href[0] == '/' ? href + 1 : href);
  return out;
}

ResourceClient *resource_client_new(HttpClient *http, const CodecOps *codec,
                                    const char *base_url) {
  ResourceClient *rc = calloc(1, sizeof(ResourceClient));
  if (!rc) return NULL;
  rc->http = http;
  rc->codec = codec ? codec : codec_xml();
  rc->base_url = base_url ? xstrdup(base_url) : NULL;
  return rc;
}

void resource_client_free(ResourceClient *rc) {
  if (!rc) return;
  free(rc->base_url);
  free(rc);
}

int resource_fetch(ResourceClient *client, ResourceNode *node) {
  HttpResult res;
  Val *val;
  char *url;
  char *err = NULL;
  const CodecOps *codec;

  if (!client || !node || !client->http) return -1;
  codec = client->codec ? client->codec : codec_xml();
  url = join_url(client->base_url, node->href);
  if (!url) return -1;

  node->state = NODE_FETCHING;
  memset(&res, 0, sizeof(res));
  if (http_get(client->http, url, &res) != 0) {
    node->state = NODE_ERROR;
    node->retry = 1;
    if (node->retry_count < 255) node->retry_count++;
    free(url);
    http_result_clear(&res);
    return -1;
  }
  free(url);

  node->http_status = (int)res.status;
  if (res.status < 200 || res.status >= 300 || !res.body) {
    node->state = NODE_ERROR;
    node->retry = 1;
    if (node->retry_count < 255) node->retry_count++;
    http_result_clear(&res);
    return -1;
  }

  val = codec->decode(res.body, res.body_len, &err);
  free(err);
  http_result_clear(&res);
  if (!val) {
    node->state = NODE_ERROR;
    node->retry = 1;
    if (node->retry_count < 255) node->retry_count++;
    return -1;
  }

  resource_set_data(node, val);
  node->state = NODE_READY;
  node->retry = 0;
  node->retry_count = 0;
  node->fetched_at = time(NULL);
  resource_expand(node);
  resource_apply_meta(node);
  if (node->poll_rate_sec > 0)
    resource_schedule_poll(node, node->fetched_at);
  return 0;
}

int resource_fetch_deep(ResourceClient *client, ResourceNode *node, int max_depth) {
  int i;
  if (!node || max_depth < 0) return -1;
  if (resource_fetch(client, node) != 0) return -1;
  if (max_depth == 0) return 0;
  for (i = 0; i < node->child_n; i++) {
    resource_fetch_deep(client, node->children[i], max_depth - 1);
  }
  return 0;
}

int resource_fetch_missing(ResourceClient *client, ResourceNode *node) {
  int i, n = 0;
  if (!node) return 0;
  if (node->state == NODE_NEW || (node->state == NODE_ERROR && node->retry)) {
    if (resource_fetch(client, node) == 0) n++;
  }
  for (i = 0; i < node->child_n; i++)
    n += resource_fetch_missing(client, node->children[i]);
  return n;
}

void resource_apply_meta(ResourceNode *node) {
  int64_t pr = -1;
  if (!node) return;
  if (node->data) {
    pr = val_get_i64(node->data, "pollRate", -1);
    if (pr < 0) pr = val_get_i64(node->data, "@pollRate", -1);
  }
  if (pr > 0) {
    node->poll_rate_sec = (int)pr;
  } else if (node->poll_rate_sec <= 0 && node->parent &&
             node->parent->poll_rate_sec > 0) {
    node->poll_rate_sec = node->parent->poll_rate_sec;
  }
}

void resource_schedule_poll(ResourceNode *node, time_t now) {
  if (!node || node->poll_rate_sec <= 0) return;
  node->poll_next = now + node->poll_rate_sec;
}

void resource_walk(ResourceNode *node, ResourceWalkFn fn, void *userdata) {
  int i;
  if (!node || !fn) return;
  fn(node, userdata);
  for (i = 0; i < node->child_n; i++)
    resource_walk(node->children[i], fn, userdata);
}

static int resource_send(ResourceClient *client, ResourceNode *node, Val *body,
                         const char *method) {
  HttpResult res;
  uint8_t *bytes = NULL;
  size_t len = 0;
  char *err = NULL;
  char *url;
  const CodecOps *codec;
  Val *payload;
  int rc;

  if (!client || !node || !client->http) return -1;
  codec = client->codec ? client->codec : codec_xml();
  payload = body ? body : node->data;
  if (!payload) return -1;
  if (codec->encode(payload, &bytes, &len, &err) != 0) {
    free(err);
    return -1;
  }
  url = join_url(client->base_url, node->href);
  if (!url) {
    free(bytes);
    return -1;
  }
  memset(&res, 0, sizeof(res));
  rc = http_send(client->http, method, url, codec->mime, bytes, len, &res);
  free(url);
  free(bytes);
  if (rc != 0) {
    http_result_clear(&res);
    return -1;
  }
  node->http_status = (int)res.status;
  http_result_clear(&res);
  if (node->http_status < 200 || node->http_status >= 300) return -1;
  return 0;
}

int resource_put(ResourceClient *client, ResourceNode *node, Val *body) {
  return resource_send(client, node, body, "PUT");
}

int resource_post(ResourceClient *client, ResourceNode *node, Val *body) {
  return resource_send(client, node, body, "POST");
}
