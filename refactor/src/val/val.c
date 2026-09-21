#include "se/val.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *val_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s) return NULL;
  n = strlen(s);
  out = malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n + 1);
  return out;
}

static Val *val_alloc(ValKind kind, const char *key) {
  Val *v = calloc(1, sizeof(Val));
  if (!v) return NULL;
  v->kind = kind;
  if (key) {
    v->key = val_strdup(key);
    if (!v->key) {
      free(v);
      return NULL;
    }
  }
  return v;
}

Val *val_null(const char *key) { return val_alloc(VAL_NULL, key); }

Val *val_str(const char *key, const char *s) {
  Val *v = val_alloc(VAL_STR, key);
  if (!v) return NULL;
  v->u.s = val_strdup(s ? s : "");
  if (!v->u.s) {
    val_free(v);
    return NULL;
  }
  return v;
}

Val *val_int(const char *key, int64_t i) {
  Val *v = val_alloc(VAL_INT, key);
  if (!v) return NULL;
  v->u.i = i;
  return v;
}

Val *val_bool(const char *key, int b) {
  Val *v = val_alloc(VAL_BOOL, key);
  if (!v) return NULL;
  v->u.b = b ? 1 : 0;
  return v;
}

Val *val_obj(const char *key) { return val_alloc(VAL_OBJ, key); }
Val *val_arr(const char *key) { return val_alloc(VAL_ARR, key); }

void val_free(Val *v) {
  int i;
  if (!v) return;
  free(v->key);
  switch (v->kind) {
  case VAL_STR:
    free(v->u.s);
    break;
  case VAL_OBJ:
  case VAL_ARR:
    for (i = 0; i < v->u.kids.n; i++) val_free(v->u.kids.items[i]);
    free(v->u.kids.items);
    break;
  default:
    break;
  }
  free(v);
}

static int kids_reserve(Val *v, int need) {
  Val **ni;
  int cap = v->u.kids.cap;
  if (need <= cap) return 0;
  if (cap < 4) cap = 4;
  while (cap < need) cap *= 2;
  ni = realloc(v->u.kids.items, (size_t)cap * sizeof(Val *));
  if (!ni) return -1;
  v->u.kids.items = ni;
  v->u.kids.cap = cap;
  return 0;
}

int val_push(Val *arr_or_obj, Val *child) {
  if (!arr_or_obj || !child) return -1;
  if (arr_or_obj->kind != VAL_ARR && arr_or_obj->kind != VAL_OBJ) return -1;
  if (kids_reserve(arr_or_obj, arr_or_obj->u.kids.n + 1) != 0) return -1;
  arr_or_obj->u.kids.items[arr_or_obj->u.kids.n++] = child;
  return 0;
}

Val *val_child(const Val *obj, const char *key) {
  int i;
  if (!obj || !key) return NULL;
  if (obj->kind != VAL_OBJ && obj->kind != VAL_ARR) return NULL;
  for (i = 0; i < obj->u.kids.n; i++) {
    Val *c = obj->u.kids.items[i];
    if (c && c->key && strcmp(c->key, key) == 0) return c;
  }
  return NULL;
}

Val *val_at(const Val *arr, int index) {
  if (!arr || arr->kind != VAL_ARR) return NULL;
  if (index < 0 || index >= arr->u.kids.n) return NULL;
  return arr->u.kids.items[index];
}

static int is_index(const char *seg, int *out) {
  char *end = NULL;
  long v;
  if (!seg || !*seg) return 0;
  v = strtol(seg, &end, 10);
  if (end == seg || *end != '\0') return 0;
  *out = (int)v;
  return 1;
}

/* Walk path; if create_obj is set, create missing OBJ segments (not array indices). */
static Val *val_walk(Val *root, const char *path, int create_obj) {
  char buf[256];
  char *save = NULL;
  char *seg;
  Val *cur;
  size_t n;

  if (!root || !path || !*path) return (Val *)root;
  n = strlen(path);
  if (n >= sizeof(buf)) return NULL;
  memcpy(buf, path, n + 1);

  cur = root;
  for (seg = strtok_r(buf, "/", &save); seg; seg = strtok_r(NULL, "/", &save)) {
    int idx;
    Val *next;
    if (is_index(seg, &idx)) {
      next = val_at(cur, idx);
      if (!next) return NULL;
      cur = next;
      continue;
    }
    next = val_child(cur, seg);
    if (!next) {
      if (!create_obj) return NULL;
      if (cur->kind != VAL_OBJ) return NULL;
      next = val_obj(seg);
      if (!next || val_push(cur, next) != 0) {
        val_free(next);
        return NULL;
      }
    }
    cur = next;
  }
  return cur;
}

Val *val_get(const Val *root, const char *path) {
  return val_walk((Val *)root, path, 0);
}

const char *val_get_str(const Val *root, const char *path) {
  Val *v = val_get(root, path);
  if (!v) return NULL;
  if (v->kind == VAL_STR) return v->u.s;
  return NULL;
}

int64_t val_get_i64(const Val *root, const char *path, int64_t default_value) {
  Val *v = val_get(root, path);
  char *end = NULL;
  long long x;
  if (!v) return default_value;
  if (v->kind == VAL_INT) return v->u.i;
  if (v->kind == VAL_BOOL) return v->u.b;
  if (v->kind == VAL_STR && v->u.s) {
    x = strtoll(v->u.s, &end, 10);
    if (end && end != v->u.s && *end == '\0') return (int64_t)x;
  }
  return default_value;
}

int val_get_bool(const Val *root, const char *path, int default_value) {
  Val *v = val_get(root, path);
  if (!v) return default_value;
  if (v->kind == VAL_BOOL) return v->u.b;
  if (v->kind == VAL_INT) return v->u.i != 0;
  if (v->kind == VAL_STR && v->u.s) {
    if (strcmp(v->u.s, "true") == 0 || strcmp(v->u.s, "1") == 0) return 1;
    if (strcmp(v->u.s, "false") == 0 || strcmp(v->u.s, "0") == 0) return 0;
  }
  return default_value;
}

int val_len(const Val *v, const char *path) {
  Val *n = path && *path ? val_get(v, path) : (Val *)v;
  if (!n) return 0;
  if (n->kind == VAL_ARR || n->kind == VAL_OBJ) return n->u.kids.n;
  return 0;
}

/* Split path into parent path and final segment. */
static int split_parent(const char *path, char *parent, size_t psz, char *leaf,
                        size_t lsz) {
  const char *slash;
  size_t plen, llen;
  if (!path || !*path) return -1;
  slash = strrchr(path, '/');
  if (!slash) {
    if (strlen(path) >= lsz) return -1;
    parent[0] = '\0';
    memcpy(leaf, path, strlen(path) + 1);
    return 0;
  }
  plen = (size_t)(slash - path);
  llen = strlen(slash + 1);
  if (plen >= psz || llen >= lsz || llen == 0) return -1;
  memcpy(parent, path, plen);
  parent[plen] = '\0';
  memcpy(leaf, slash + 1, llen + 1);
  return 0;
}

static Val *ensure_parent(Val *root, const char *parent_path) {
  if (!parent_path || !*parent_path) return root;
  return val_walk(root, parent_path, 1);
}

static int replace_or_set_field(Val *parent, const char *key, Val *neu) {
  int i;
  if (!parent || parent->kind != VAL_OBJ || !key || !neu) return -1;
  for (i = 0; i < parent->u.kids.n; i++) {
    Val *c = parent->u.kids.items[i];
    if (c && c->key && strcmp(c->key, key) == 0) {
      val_free(c);
      parent->u.kids.items[i] = neu;
      return 0;
    }
  }
  return val_push(parent, neu);
}

int val_set(Val *root, const char *path, Val *child) {
  char parent[256], leaf[128];
  Val *p;
  if (split_parent(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0)
    return -1;
  p = ensure_parent(root, parent);
  if (!p) return -1;
  if (!child->key) child->key = val_strdup(leaf);
  return replace_or_set_field(p, leaf, child);
}

int val_set_str(Val *root, const char *path, const char *s) {
  char parent[256], leaf[128];
  Val *p, *neu;
  if (split_parent(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0)
    return -1;
  p = ensure_parent(root, parent);
  if (!p) return -1;
  neu = val_str(leaf, s);
  if (!neu) return -1;
  return replace_or_set_field(p, leaf, neu);
}

int val_set_i64(Val *root, const char *path, int64_t i) {
  char parent[256], leaf[128];
  Val *p, *neu;
  if (split_parent(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0)
    return -1;
  p = ensure_parent(root, parent);
  if (!p) return -1;
  neu = val_int(leaf, i);
  if (!neu) return -1;
  return replace_or_set_field(p, leaf, neu);
}

int val_set_bool(Val *root, const char *path, int b) {
  char parent[256], leaf[128];
  Val *p, *neu;
  if (split_parent(path, parent, sizeof(parent), leaf, sizeof(leaf)) != 0)
    return -1;
  p = ensure_parent(root, parent);
  if (!p) return -1;
  neu = val_bool(leaf, b);
  if (!neu) return -1;
  return replace_or_set_field(p, leaf, neu);
}
