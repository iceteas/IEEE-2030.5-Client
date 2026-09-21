#ifndef SE_VAL_H
#define SE_VAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  VAL_NULL = 0,
  VAL_STR,
  VAL_INT,
  VAL_BOOL,
  VAL_OBJ,
  VAL_ARR
} ValKind;

typedef struct Val {
  ValKind kind;
  char *key;
  union {
    char *s;
    int64_t i;
    int b;
    struct {
      struct Val **items;
      int n;
      int cap;
    } kids;
  } u;
} Val;

Val *val_null(const char *key);
Val *val_str(const char *key, const char *s);
Val *val_int(const char *key, int64_t i);
Val *val_bool(const char *key, int b);
Val *val_obj(const char *key);
Val *val_arr(const char *key);

void val_free(Val *v);

int val_push(Val *arr_or_obj, Val *child);
Val *val_child(const Val *obj, const char *key);
Val *val_at(const Val *arr, int index);
int val_len(const Val *v, const char *path);

Val *val_get(const Val *root, const char *path);
const char *val_get_str(const Val *root, const char *path);
int64_t val_get_i64(const Val *root, const char *path, int64_t default_value);
int val_get_bool(const Val *root, const char *path, int default_value);

int val_set_str(Val *root, const char *path, const char *s);
int val_set_i64(Val *root, const char *path, int64_t i);
int val_set_bool(Val *root, const char *path, int b);
int val_set(Val *root, const char *path, Val *child);

char *val_strdup(const char *s);

#ifdef __cplusplus
}
#endif

#endif
