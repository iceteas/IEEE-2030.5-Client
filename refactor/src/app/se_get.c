#include "se/codec.h"
#include "se/http_client.h"
#include "se/resource.h"
#include "se/val.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s <url> [ca.pem client.crt client.key]\n"
          "  GET a 2030.5 XML resource, print Val paths, expand links.\n",
          argv0);
}

int main(int argc, char **argv) {
  HttpClientConfig cfg = {0};
  HttpClient *http;
  ResourceClient *rc;
  ResourceNode *root;
  int i;

  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  cfg.accept = "application/sep+xml";
  cfg.verify_peer = 1;
  cfg.connect_timeout_sec = 10;
  cfg.transfer_timeout_sec = 60;
  if (argc >= 5) {
    cfg.ca_path = argv[2];
    cfg.cert_path = argv[3];
    cfg.key_path = argv[4];
  } else {
    cfg.verify_peer = 0; /* demo convenience without certs */
  }

  http = http_client_new(&cfg);
  rc = resource_client_new(http, codec_xml(), NULL);
  root = resource_node_new(argv[1], NULL);

  if (resource_fetch(rc, root) != 0) {
    fprintf(stderr, "fetch failed status=%d state=%d\n", root->http_status,
            root->state);
    resource_node_free(root);
    resource_client_free(rc);
    http_client_free(http);
    return 1;
  }

  printf("fetched %s status=%d children=%d\n", root->href, root->http_status,
         root->child_n);
  if (root->data && root->data->key)
    printf("root element: %s\n", root->data->key);

  for (i = 0; i < root->child_n; i++)
    printf("  child[%d]: %s\n", i, root->children[i]->href);

  /* sample field dump if EndDeviceList-like */
  if (val_get_str(root->data, "EndDevice/0/LFDI"))
    printf("EndDevice/0/LFDI=%s\n", val_get_str(root->data, "EndDevice/0/LFDI"));

  resource_node_free(root);
  resource_client_free(rc);
  http_client_free(http);
  return 0;
}
