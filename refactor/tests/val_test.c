#include "se/val.h"

#include <assert.h>
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

static void test_basic_set_get(void) {
  Val *root = val_obj("EndDevice");
  CHECK(root != NULL);
  CHECK(val_set_str(root, "LFDI", "000102") == 0);
  CHECK(val_set_i64(root, "SFDI", 12345678901LL) == 0);
  CHECK(strcmp(val_get_str(root, "LFDI"), "000102") == 0);
  CHECK(val_get_i64(root, "SFDI", 0) == 12345678901LL);
  val_free(root);
}

static void test_array_path(void) {
  Val *root = val_obj("EndDeviceList");
  Val *arr = val_arr("EndDevice");
  Val *e0 = val_obj("EndDevice");
  Val *e1 = val_obj("EndDevice");
  CHECK(val_set_str(e0, "LFDI", "a") == 0);
  CHECK(val_set_str(e1, "LFDI", "b") == 0);
  CHECK(val_push(arr, e0) == 0);
  CHECK(val_push(arr, e1) == 0);
  CHECK(val_push(root, arr) == 0);
  CHECK(val_len(root, "EndDevice") == 2);
  CHECK(strcmp(val_get_str(root, "EndDevice/0/LFDI"), "a") == 0);
  CHECK(strcmp(val_get_str(root, "EndDevice/1/LFDI"), "b") == 0);
  val_free(root);
}

int main(void) {
  test_basic_set_get();
  test_array_path();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("val_test OK\n");
  return 0;
}
