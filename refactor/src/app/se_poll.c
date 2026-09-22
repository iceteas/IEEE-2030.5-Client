#include "se/codec.h"
#include "se/http_client.h"
#include "se/poll.h"
#include "se/resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s <url> [poll_seconds] [ca.pem client.crt client.key]\n"
          "  GET once, then poll the root node at its pollRate (or poll_seconds).\n",
          argv0);
}

int main(int argc, char **argv) {
  HttpClientConfig cfg = {0};
  HttpClient *http;
  ResourceClient *rc;
  ResourceNode *root;
  PollEngine pe;
  int force_poll = 0;
  int argi = 2;

  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }
  if (argc >= 3 && argv[2][0] >= '0' && argv[2][0] <= '9') {
    force_poll = atoi(argv[2]);
    argi = 3;
  }

  cfg.accept = "application/sep+xml";
  cfg.verify_peer = 1;
  cfg.connect_timeout_sec = 10;
  cfg.transfer_timeout_sec = 60;
  if (argc >= argi + 3) {
    cfg.ca_path = argv[argi];
    cfg.cert_path = argv[argi + 1];
    cfg.key_path = argv[argi + 2];
  } else {
    cfg.verify_peer = 0;
  }

  http = http_client_new(&cfg);
  rc = resource_client_new(http, codec_xml(), NULL);
  root = resource_node_new(argv[1], NULL);
  poll_engine_init(&pe, rc, root);

  if (resource_fetch(rc, root) != 0) {
    fprintf(stderr, "initial fetch failed status=%d\n", root->http_status);
    resource_node_free(root);
    resource_client_free(rc);
    http_client_free(http);
    return 1;
  }
  if (force_poll > 0) {
    root->poll_rate_sec = force_poll;
    resource_schedule_poll(root, time(NULL));
  }
  printf("ready children=%d poll_rate=%d next_in=%ds\n", root->child_n,
         root->poll_rate_sec, poll_seconds_until_next(&pe, time(NULL)));

  for (;;) {
    time_t now = time(NULL);
    int wait = poll_seconds_until_next(&pe, now);
    if (wait < 0) {
      printf("nothing scheduled; exiting\n");
      break;
    }
    if (wait > 0) sleep((unsigned)wait);
    now = time(NULL);
    printf("tick attempts=%d\n", poll_tick(&pe, now));
  }

  resource_node_free(root);
  resource_client_free(rc);
  http_client_free(http);
  return 0;
}
