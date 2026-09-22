#include "se/codec.h"
#include "se/resource.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void test_expand_list_and_link(void) {
  const char *xml =
      "<DeviceCapability href=\"/dcap\">"
      "  <EndDeviceListLink href=\"/edev\" all=\"1\"/>"
      "  <TimeLink href=\"/tm\"/>"
      "</DeviceCapability>";
  char *err = NULL;
  Val *v = codec_xml()->decode((const uint8_t *)xml, strlen(xml), &err);
  ResourceNode *root = resource_node_new("/dcap", "DeviceCapability");
  CHECK(v != NULL);
  resource_set_data(root, v);
  CHECK(resource_expand(root) == 0);
  CHECK(root->child_n == 2);
  CHECK(resource_find_child(root, "/edev") != NULL);
  CHECK(resource_find_child(root, "/tm") != NULL);
  resource_node_free(root);
}

static void test_expand_array_items(void) {
  const char *xml =
      "<EndDeviceList href=\"/edev\" all=\"2\">"
      "  <EndDevice href=\"/edev/1\"><LFDI>a</LFDI></EndDevice>"
      "  <EndDevice href=\"/edev/2\"><LFDI>b</LFDI></EndDevice>"
      "</EndDeviceList>";
  Val *v = codec_xml()->decode((const uint8_t *)xml, strlen(xml), NULL);
  ResourceNode *root = resource_node_new("/edev", "EndDeviceList");
  resource_set_data(root, v);
  resource_expand(root);
  CHECK(root->child_n == 2);
  CHECK(resource_find_child(root, "/edev/1") != NULL);
  CHECK(resource_find_child(root, "/edev/2") != NULL);
  resource_node_free(root);
}

int main(void) {
  test_expand_list_and_link();
  test_expand_array_items();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("resource_expand_test OK\n");
  return 0;
}
