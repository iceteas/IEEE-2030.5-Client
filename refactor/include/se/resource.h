#ifndef SE_RESOURCE_H
#define SE_RESOURCE_H

#include "se/codec.h"
#include "se/http_client.h"
#include "se/val.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NODE_NEW = 0,
  NODE_FETCHING,
  NODE_READY,
  NODE_STALE,
  NODE_ERROR
} NodeState;

typedef struct ResourceNode {
  char *href;
  char *type_hint;
  Val *data;
  NodeState state;
  int http_status;
  time_t fetched_at;
  int poll_rate_sec;
  time_t poll_next;
  unsigned retry : 1;
  uint8_t retry_count;
  struct ResourceNode *parent;
  struct ResourceNode **children;
  int child_n;
  int child_cap;
} ResourceNode;

typedef struct ResourceClient {
  HttpClient *http;
  const CodecOps *codec;
  char *base_url; /* e.g. https://host:443  (no trailing slash required) */
} ResourceClient;

ResourceNode *resource_node_new(const char *href, const char *type_hint);
void resource_node_free(ResourceNode *node);
void resource_set_data(ResourceNode *node, Val *data);
ResourceNode *resource_find_child(ResourceNode *parent, const char *href);
ResourceNode *resource_add_child(ResourceNode *parent, const char *href,
                                 const char *type_hint);

int resource_expand(ResourceNode *node);
int resource_fetch(ResourceClient *client, ResourceNode *node);
int resource_fetch_deep(ResourceClient *client, ResourceNode *node, int max_depth);

ResourceClient *resource_client_new(HttpClient *http, const CodecOps *codec,
                                    const char *base_url);
void resource_client_free(ResourceClient *rc);

#ifdef __cplusplus
}
#endif

#endif
