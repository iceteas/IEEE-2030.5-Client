#include "se/codec.h"
#include "se/der_schedule.h"
#include "se/poll.h"
#include "se/resource.h"
#include "se/val.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void test_der_phase(void) {
  Val *ctrl = val_obj("DERControl");
  DerIntervalView iv;
  CHECK(val_set_i64(ctrl, "interval/start", 1000) == 0);
  CHECK(val_set_i64(ctrl, "interval/duration", 100) == 0);
  CHECK(val_set_i64(ctrl, "EventStatus/currentStatus", 1) == 0);
  CHECK(der_interval_view(ctrl, &iv) == 0);
  CHECK(iv.start == 1000 && iv.duration == 100);
  CHECK(der_interval_end(&iv) == 1100);
  CHECK(der_phase(&iv, 999) == DER_PHASE_SCHEDULED);
  CHECK(der_phase(&iv, 1000) == DER_PHASE_ACTIVE);
  CHECK(der_phase(&iv, 1099) == DER_PHASE_ACTIVE);
  CHECK(der_phase(&iv, 1100) == DER_PHASE_ENDED);
  CHECK(der_event_status(ctrl) == 1);
  val_free(ctrl);
}

static void test_poll_schedule_meta(void) {
  ResourceNode *root = resource_node_new("/edev", "EndDeviceList");
  Val *data = val_obj("EndDeviceList");
  PollEngine pe;
  time_t now = 10000;

  CHECK(val_set_i64(data, "pollRate", 60) == 0);
  resource_set_data(root, data);
  resource_apply_meta(root);
  CHECK(root->poll_rate_sec == 60);
  resource_schedule_poll(root, now);
  CHECK(root->poll_next == now + 60);

  poll_engine_init(&pe, NULL, root);
  /* without client, tick should not crash; attempts 0 because fetch needs client */
  /* manually mark due */
  root->poll_next = now;
  root->state = NODE_READY;
  CHECK(poll_seconds_until_next(&pe, now) == 0);
  root->poll_next = now + 30;
  CHECK(poll_seconds_until_next(&pe, now) == 30);

  resource_node_free(root);
}

static void test_encode_put_body(void) {
  /* PUT path uses codec encode — verify response-like Val encodes */
  Val *resp = val_obj("DERControlResponse");
  uint8_t *out = NULL;
  size_t len = 0;
  char *err = NULL;
  CHECK(val_set_str(resp, "subject", "001122") == 0);
  CHECK(val_set_i64(resp, "status", 1) == 0);
  CHECK(codec_xml()->encode(resp, &out, &len, &err) == 0);
  CHECK(out != NULL && len > 0);
  CHECK(strstr((char *)out, "DERControlResponse") != NULL);
  CHECK(strstr((char *)out, "subject") != NULL);
  free(out);
  val_free(resp);
}

static int walk_count;
static void count_fn(ResourceNode *n, void *u) {
  (void)n;
  (void)u;
  walk_count++;
}

static void test_walk_and_inherit_poll(void) {
  ResourceNode *root = resource_node_new("/dcap", NULL);
  ResourceNode *child;
  root->poll_rate_sec = 120;
  child = resource_add_child(root, "/edev", "EndDeviceList");
  resource_apply_meta(child);
  CHECK(child->poll_rate_sec == 120);
  walk_count = 0;
  resource_walk(root, count_fn, NULL);
  CHECK(walk_count == 2);
  resource_node_free(root);
}

int main(void) {
  test_der_phase();
  test_poll_schedule_meta();
  test_encode_put_body();
  test_walk_and_inherit_poll();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("poll_der_test OK\n");
  return 0;
}
