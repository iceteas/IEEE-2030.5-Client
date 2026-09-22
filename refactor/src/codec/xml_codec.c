#include "se/codec.h"

#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int looks_int(const char *s) {
  const char *p = s;
  if (!s || !*s) return 0;
  if (*p == '-' || *p == '+') p++;
  if (!*p) return 0;
  /* keep leading-zero tokens as strings (e.g. LFDI 000102) */
  if (p[0] == '0' && p[1] != '\0') return 0;
  while (*p) {
    if (!isdigit((unsigned char)*p)) return 0;
    p++;
  }
  return 1;
}

static Val *leaf_from_text(const char *name, const char *text) {
  if (!text) text = "";
  if (strcmp(text, "true") == 0) return val_bool(name, 1);
  if (strcmp(text, "false") == 0) return val_bool(name, 0);
  if (looks_int(text)) {
    char *end = NULL;
    long long v = strtoll(text, &end, 10);
    if (end && *end == '\0') return val_int(name, (int64_t)v);
  }
  return val_str(name, text);
}

static int has_element_child(xmlNode *n) {
  xmlNode *c;
  for (c = n->children; c; c = c->next) {
    if (c->type == XML_ELEMENT_NODE) return 1;
  }
  return 0;
}

typedef struct NameGroup {
  char *name;
  xmlNode **nodes;
  int n;
  int cap;
} NameGroup;

typedef struct GroupList {
  NameGroup *items;
  int n;
  int cap;
} GroupList;

static int group_add(GroupList *g, const char *name, xmlNode *node) {
  int i;
  NameGroup *ng;
  for (i = 0; i < g->n; i++) {
    if (strcmp(g->items[i].name, name) == 0) {
      ng = &g->items[i];
      if (ng->n + 1 > ng->cap) {
        int cap = ng->cap ? ng->cap * 2 : 4;
        xmlNode **nn = realloc(ng->nodes, (size_t)cap * sizeof(xmlNode *));
        if (!nn) return -1;
        ng->nodes = nn;
        ng->cap = cap;
      }
      ng->nodes[ng->n++] = node;
      return 0;
    }
  }
  if (g->n + 1 > g->cap) {
    int cap = g->cap ? g->cap * 2 : 4;
    NameGroup *ni = realloc(g->items, (size_t)cap * sizeof(NameGroup));
    if (!ni) return -1;
    g->items = ni;
    g->cap = cap;
  }
  ng = &g->items[g->n++];
  memset(ng, 0, sizeof(*ng));
  ng->name = xstrdup(name);
  ng->cap = 4;
  ng->nodes = malloc((size_t)ng->cap * sizeof(xmlNode *));
  if (!ng->name || !ng->nodes) return -1;
  ng->nodes[ng->n++] = node;
  return 0;
}

static void groups_free(GroupList *g) {
  int i;
  for (i = 0; i < g->n; i++) {
    free(g->items[i].name);
    free(g->items[i].nodes);
  }
  free(g->items);
  memset(g, 0, sizeof(*g));
}

static Val *xml_node_to_val(xmlNode *n);

static Val *xml_element_to_val(xmlNode *n) {
  const char *name = (const char *)n->name;
  Val *obj;
  xmlAttr *attr;
  xmlNode *c;
  GroupList groups = {0};
  int i;

  if (!has_element_child(n)) {
    xmlChar *content = xmlNodeGetContent(n);
    Val *leaf = leaf_from_text(name, content ? (const char *)content : "");
    if (content) xmlFree(content);
    /* still attach attributes on leaves that have them */
    if (n->properties) {
      Val *wrap = val_obj(name);
      if (!wrap) {
        val_free(leaf);
        return NULL;
      }
      for (attr = n->properties; attr; attr = attr->next) {
        char key[256];
        xmlChar *av = xmlNodeListGetString(n->doc, attr->children, 1);
        snprintf(key, sizeof(key), "@%s", (const char *)attr->name);
        val_push(wrap, leaf_from_text(key, av ? (const char *)av : ""));
        if (av) xmlFree(av);
      }
      /* put text as "." only if we need both; prefer keep as scalar if no
         meaningful text besides attrs-only empty. For SEP, leaves with attrs
         are rare; store text field as value when present. */
      if (leaf->kind == VAL_STR && leaf->u.s && leaf->u.s[0]) {
        Val *t = val_str("#text", leaf->u.s);
        val_push(wrap, t);
      } else if (leaf->kind == VAL_INT || leaf->kind == VAL_BOOL) {
        val_push(wrap, leaf);
        leaf = NULL;
      }
      val_free(leaf);
      return wrap;
    }
    return leaf;
  }

  obj = val_obj(name);
  if (!obj) return NULL;

  for (attr = n->properties; attr; attr = attr->next) {
    char key[256];
    xmlChar *av = xmlNodeListGetString(n->doc, attr->children, 1);
    snprintf(key, sizeof(key), "@%s", (const char *)attr->name);
    if (val_push(obj, leaf_from_text(key, av ? (const char *)av : "")) != 0) {
      if (av) xmlFree(av);
      val_free(obj);
      return NULL;
    }
    if (av) xmlFree(av);
  }

  for (c = n->children; c; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    if (group_add(&groups, (const char *)c->name, c) != 0) {
      groups_free(&groups);
      val_free(obj);
      return NULL;
    }
  }

  for (i = 0; i < groups.n; i++) {
    NameGroup *ng = &groups.items[i];
    if (ng->n == 1) {
      Val *child = xml_node_to_val(ng->nodes[0]);
      if (!child || val_push(obj, child) != 0) {
        val_free(child);
        groups_free(&groups);
        val_free(obj);
        return NULL;
      }
    } else {
      Val *arr = val_arr(ng->name);
      int j;
      if (!arr) {
        groups_free(&groups);
        val_free(obj);
        return NULL;
      }
      for (j = 0; j < ng->n; j++) {
        Val *child = xml_node_to_val(ng->nodes[j]);
        if (!child || val_push(arr, child) != 0) {
          val_free(child);
          val_free(arr);
          groups_free(&groups);
          val_free(obj);
          return NULL;
        }
      }
      if (val_push(obj, arr) != 0) {
        val_free(arr);
        groups_free(&groups);
        val_free(obj);
        return NULL;
      }
    }
  }

  groups_free(&groups);
  return obj;
}

static Val *xml_node_to_val(xmlNode *n) {
  if (!n || n->type != XML_ELEMENT_NODE) return NULL;
  return xml_element_to_val(n);
}

static Val *xml_decode(const uint8_t *buf, size_t len, char **err) {
  xmlDoc *doc;
  xmlNode *root;
  Val *v;
  if (err) *err = NULL;
  if (!buf) {
    if (err) *err = xstrdup("null buffer");
    return NULL;
  }
  doc = xmlReadMemory((const char *)buf, (int)len, "noname.xml", NULL,
                      XML_PARSE_NONET | XML_PARSE_NOBLANKS);
  if (!doc) {
    if (err) *err = xstrdup("xml parse failed");
    return NULL;
  }
  root = xmlDocGetRootElement(doc);
  if (!root) {
    xmlFreeDoc(doc);
    if (err) *err = xstrdup("xml missing root");
    return NULL;
  }
  v = xml_node_to_val(root);
  xmlFreeDoc(doc);
  if (!v && err) *err = xstrdup("xml convert failed");
  return v;
}

static int val_to_xml_node(const Val *v, xmlNode *parent, xmlNode **out_elem);

static void set_text_content(xmlNode *elem, const Val *v) {
  char buf[64];
  switch (v->kind) {
  case VAL_STR:
    xmlNodeSetContent(elem, BAD_CAST(v->u.s ? v->u.s : ""));
    break;
  case VAL_INT:
    snprintf(buf, sizeof(buf), "%lld", (long long)v->u.i);
    xmlNodeSetContent(elem, BAD_CAST buf);
    break;
  case VAL_BOOL:
    xmlNodeSetContent(elem, BAD_CAST(v->u.b ? "true" : "false"));
    break;
  default:
    break;
  }
}

static int emit_obj(const Val *obj, xmlNode *elem) {
  int i;
  for (i = 0; i < obj->u.kids.n; i++) {
    Val *c = obj->u.kids.items[i];
    if (!c || !c->key) continue;
    if (c->key[0] == '@') {
      char buf[64];
      const char *an = c->key + 1;
      const char *av = "";
      if (c->kind == VAL_STR) av = c->u.s ? c->u.s : "";
      else if (c->kind == VAL_INT) {
        snprintf(buf, sizeof(buf), "%lld", (long long)c->u.i);
        av = buf;
      } else if (c->kind == VAL_BOOL) av = c->u.b ? "true" : "false";
      xmlNewProp(elem, BAD_CAST an, BAD_CAST av);
      continue;
    }
    if (c->kind == VAL_ARR) {
      int j;
      for (j = 0; j < c->u.kids.n; j++) {
        xmlNode *child_elem = NULL;
        Val *item = c->u.kids.items[j];
        Val tmp = *item;
        /* force element name to array key if item key differs */
        char *saved = item->key;
        char *use = c->key;
        item->key = use;
        if (val_to_xml_node(item, elem, &child_elem) != 0) {
          item->key = saved;
          return -1;
        }
        item->key = saved;
        (void)tmp;
      }
      continue;
    }
    {
      xmlNode *child_elem = NULL;
      if (val_to_xml_node(c, elem, &child_elem) != 0) return -1;
    }
  }
  return 0;
}

static int val_to_xml_node(const Val *v, xmlNode *parent, xmlNode **out_elem) {
  xmlNode *elem;
  const char *name;
  if (!v) return -1;
  name = v->key ? v->key : "value";
  if (v->kind == VAL_OBJ) {
    elem = xmlNewChild(parent, NULL, BAD_CAST name, NULL);
    if (!elem) return -1;
    if (out_elem) *out_elem = elem;
    return emit_obj(v, elem);
  }
  if (v->kind == VAL_ARR) {
    /* arrays are emitted by parent object handler */
    return -1;
  }
  elem = xmlNewChild(parent, NULL, BAD_CAST name, NULL);
  if (!elem) return -1;
  set_text_content(elem, v);
  if (out_elem) *out_elem = elem;
  return 0;
}

static int xml_encode(const Val *v, uint8_t **out, size_t *out_len, char **err) {
  xmlDoc *doc;
  xmlNode *root = NULL;
  xmlChar *mem = NULL;
  int size = 0;
  if (err) *err = NULL;
  if (!v || !out || !out_len) return -1;
  *out = NULL;
  *out_len = 0;

  doc = xmlNewDoc(BAD_CAST "1.0");
  if (!doc) {
    if (err) *err = xstrdup("xmlNewDoc failed");
    return -1;
  }

  if (v->kind == VAL_OBJ) {
    root = xmlNewNode(NULL, BAD_CAST(v->key ? v->key : "root"));
    xmlDocSetRootElement(doc, root);
    if (emit_obj(v, root) != 0) {
      xmlFreeDoc(doc);
      if (err) *err = xstrdup("encode object failed");
      return -1;
    }
  } else {
    root = xmlNewNode(NULL, BAD_CAST(v->key ? v->key : "value"));
    xmlDocSetRootElement(doc, root);
    set_text_content(root, v);
  }

  xmlDocDumpFormatMemoryEnc(doc, &mem, &size, "UTF-8", 1);
  xmlFreeDoc(doc);
  if (!mem || size < 0) {
    if (err) *err = xstrdup("xml dump failed");
    return -1;
  }
  *out = (uint8_t *)mem; /* caller frees with free()? libxml uses xmlFree */
  /* Transfer to malloc'd buffer so callers can free() uniformly. */
  {
    uint8_t *copy = malloc((size_t)size + 1);
    if (!copy) {
      xmlFree(mem);
      if (err) *err = xstrdup("oom");
      return -1;
    }
    memcpy(copy, mem, (size_t)size);
    copy[size] = '\0';
    xmlFree(mem);
    *out = copy;
    *out_len = (size_t)size;
  }
  return 0;
}

static const CodecOps xml_ops = {
    .mime = "application/sep+xml",
    .decode = xml_decode,
    .encode = xml_encode,
};

const CodecOps *codec_xml(void) { return &xml_ops; }

const CodecOps *codec_for_mime(const char *mime) {
  if (!mime) return codec_xml();
  if (strstr(mime, "sep+xml") || strstr(mime, "xml")) return codec_xml();
  /* EXI reserved */
  return codec_xml();
}
