#include "se/codec.h"

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

#define CHECK_STREQ(a, b)                                                      \
  do {                                                                         \
    const char *_a = (a);                                                      \
    const char *_b = (b);                                                      \
    if (!_a || !_b || strcmp(_a, _b) != 0) {                                   \
      fprintf(stderr, "FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__,        \
              _a ? _a : "(null)", _b ? _b : "(null)");                         \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static const char *sample =
    "<?xml version=\"1.0\"?>\n"
    "<EndDeviceList href=\"/edev\" all=\"2\">\n"
    "  <EndDevice href=\"/edev/1\">\n"
    "    <LFDI>000102</LFDI>\n"
    "    <SFDI>12345678901</SFDI>\n"
    "  </EndDevice>\n"
    "  <EndDevice href=\"/edev/2\">\n"
    "    <LFDI>aabbcc</LFDI>\n"
    "  </EndDevice>\n"
    "</EndDeviceList>\n";

static void test_decode(void) {
  char *err = NULL;
  Val *v = codec_xml()->decode((const uint8_t *)sample, strlen(sample), &err);
  CHECK(v != NULL);
  CHECK(err == NULL);
  CHECK(v->kind == VAL_OBJ);
  CHECK_STREQ(v->key, "EndDeviceList");
  CHECK_STREQ(val_get_str(v, "@href"), "/edev");
  CHECK(val_get_i64(v, "@all", 0) == 2);
  CHECK(val_len(v, "EndDevice") == 2);
  CHECK_STREQ(val_get_str(v, "EndDevice/0/LFDI"), "000102");
  CHECK(val_get_i64(v, "EndDevice/0/SFDI", 0) == 12345678901LL);
  CHECK_STREQ(val_get_str(v, "EndDevice/1/@href"), "/edev/2");
  val_free(v);
}

static void test_roundtrip(void) {
  char *err = NULL;
  uint8_t *out = NULL;
  size_t out_len = 0;
  Val *v = codec_xml()->decode((const uint8_t *)sample, strlen(sample), &err);
  Val *v2;
  CHECK(v != NULL);
  CHECK(codec_xml()->encode(v, &out, &out_len, &err) == 0);
  CHECK(out != NULL && out_len > 0);
  v2 = codec_xml()->decode(out, out_len, &err);
  CHECK(v2 != NULL);
  CHECK_STREQ(val_get_str(v2, "EndDevice/0/LFDI"), "000102");
  CHECK(val_get_i64(v2, "@all", -1) == 2);
  free(out);
  val_free(v);
  val_free(v2);
}

int main(void) {
  test_decode();
  test_roundtrip();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("xml_codec_test OK\n");
  return 0;
}
