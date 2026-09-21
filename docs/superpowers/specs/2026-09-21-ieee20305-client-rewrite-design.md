# IEEE 2030.5 Client Rewrite Design

Date: 2026-09-21  
Status: Draft for review  
Language: C  
HTTP: libcurl  
XML: libxml2 (v1)  
EXI: adapter interface only in v1

## 1. Goals and Non-Goals

### Goals

- Higher maintainability than the current monolithic include-based stack.
- One generic in-memory value model for resource content (no per-schema C struct explosion).
- Simple n-level resource tree built from HTTP GETs and Link/list expansion.
- XML serialization/deserialization via a real XML library (libxml2).
- HTTP via libcurl (connection reuse, TLS client certs, timeouts).
- Clear module boundaries with small, testable APIs.
- Leave a codec adapter seam so EXI can be added later without changing the resource tree or business logic.

### Non-Goals (v1)

- Full EXI encode/decode implementation.
- HTTP server / inbound notification acceptor.
- Regenerating or preserving `se_types.h` style per-resource structs.
- Binary-compatible API with the current EPRI client.
- Supporting every IEEE 2030.5 function set on day one (start with discovery + resource GET/PUT/POST + polling + minimal DER event timing).

## 2. Problems with the Current Codebase

| Area | Current approach | Pain |
|------|------------------|------|
| Types | Huge generated `se_types.h` + schema tables | Hard to change, large surface |
| Parse/output | Custom XML/EXI engines | High maintenance, limited tooling |
| HTTP | Custom HTTP/1.1 + TCP/TLS event loop | Keep-alive/reconnect/timeout bugs; disconnect drops Stub tree |
| Resource model | Stub + deps/reqs/flag bitmasks | Difficult to reason about completion and recovery |
| Integration | `#include "*.c"` amalgamation | Poor incremental build and isolation |

The rewrite keeps the **product capability** (2030.5 client: discover, fetch, poll, act) but replaces the **implementation strategy**.

## 3. Overall Architecture

```text
┌──────────────────────────────────────────────────────────────┐
│ Application (cli / der_app)                                  │
│  - commands, logging, device identity                        │
├──────────────────────────────────────────────────────────────┤
│ Domain services                                              │
│  - poll scheduler                                            │
│  - der event scheduler (reads Val via path helpers)          │
├──────────────────────────────────────────────────────────────┤
│ Resource tree                                                │
│  - ResourceNode n-level lists                                │
│  - expand links, cache, retry marks                          │
├──────────────────────────────────────────────────────────────┤
│ Codec adapter                                                │
│  - CodecOps: bytes <-> Val                                   │
│  - xml_codec (v1)                                            │
│  - exi_codec (later)                                         │
├──────────────────────────────────────────────────────────────┤
│ HTTP client (curl)                                           │
│  - GET/PUT/POST/DELETE                                       │
│  - TLS client cert, CA store, connection reuse               │
├──────────────────────────────────────────────────────────────┤
│ Discovery (optional v1+)                                     │
│  - DNS-SD / mDNS (can keep or wrap existing logic initially) │
└──────────────────────────────────────────────────────────────┘
```

### Design principles

1. **One content model (`Val`)** for all resource payloads.
2. **One topology model (`ResourceNode`)** for the href-linked tree.
3. **Codecs are plugins**; HTTP and tree code never parse XML tags directly.
4. **Transport failures do not destroy the tree**; they mark nodes for retry.
5. Prefer **path-based field access** over generated structs.

## 4. Core Data Model

### 4.1 `Val` — universal key/value tree

`Val` stores resource **content** after decode. All element types share one struct.

```c
typedef enum {
  VAL_NULL = 0,
  VAL_STR,
  VAL_INT,
  VAL_BOOL,
  VAL_OBJ,   /* map-like: children with distinct keys */
  VAL_ARR    /* list-like: children as ordered items (often same key) */
} ValKind;

typedef struct Val {
  ValKind kind;
  char *key;                 /* element/field name, e.g. "LFDI" */
  union {
    char *s;
    int64_t i;
    int b;
    struct {
      struct Val **items;
      int n;
      int cap;
    } kids;                  /* used by VAL_OBJ and VAL_ARR */
  } u;
} Val;
```

#### How types are stored

| Kind | Payload | Typical XML |
|------|---------|-------------|
| `VAL_STR` | `u.s` | `<LFDI>00ab...</LFDI>` |
| `VAL_INT` | `u.i` | `<SFDI>12345678901</SFDI>` or `<all>2</all>` |
| `VAL_BOOL` | `u.b` | `<subscribed>true</subscribed>` |
| `VAL_OBJ` | `u.kids` of named fields | `<EndDevice>...</EndDevice>` |
| `VAL_ARR` | `u.kids` ordered items | repeated `<EndDevice>` siblings |

Keys live on each child `Val.key`. An object is a list of children searched by key (linear search in v1; optional hash later).

Attributes are normalized as fields with an `@` prefix, e.g. `@href`, `@all`.

### 4.2 Path API (access and assignment)

Paths use `/` separators. Numeric segments index arrays.

```c
/* read */
const char *lfdi = val_get_str(root, "EndDevice/0/LFDI");
int64_t sfdi     = val_get_i64(root, "EndDevice/0/SFDI", 0);
int n            = val_len(root, "EndDevice");

/* write */
val_set_str(root, "EndDevice/0/LFDI", "001122...");
val_set_i64(root, "EndDevice/0/SFDI", 99999000013LL);

/* build */
Val *obj = val_obj("DERControlResponse");
val_set_str(obj, "subject", mrid);
val_set_i64(obj, "status", 1);
```

Missing path behavior:

- `get_*` returns default / NULL.
- `set_*` creates intermediate `OBJ`/`ARR` nodes as needed (explicitly documented), or returns error if strict mode is enabled. v1 default: **create-on-write for object fields; arrays require existing length or `val_push`**.

### 4.3 `ResourceNode` — n-level resource tree

`ResourceNode` is topology + cache metadata. Content is `Val *data`.

```c
typedef enum {
  NODE_NEW = 0,
  NODE_FETCHING,
  NODE_READY,
  NODE_STALE,
  NODE_ERROR
} NodeState;

typedef struct ResourceNode {
  char *href;                 /* absolute or server-relative path */
  char *type_hint;            /* optional, e.g. "EndDeviceList" */
  Val *data;                  /* decoded content; NULL if not fetched */
  NodeState state;
  int http_status;
  time_t fetched_at;
  int poll_rate_sec;          /* 0 = inherit / no poll */
  time_t poll_next;
  unsigned retry : 1;
  uint8_t retry_count;
  struct ResourceNode *parent;
  struct ResourceNode **children;
  int child_n;
  int child_cap;
} ResourceNode;
```

#### n-level list model

```text
ResourceNode(/dcap)                level 0
  data = DeviceCapability Val
  children:
    ResourceNode(/edev)            level 1 (EndDeviceList)
      data = list Val
      children:
        ResourceNode(/edev/1)      level 2 (EndDevice)
          children:
            ResourceNode(/fsa)     level 3
              ...
```

Rules:

1. One HTTP resource (one href) → one `ResourceNode`.
2. List resources store list payload in `data`; each list item with its own href becomes a **child node**.
3. Link fields (e.g. `DERProgramListLink/@href`) become child nodes when expanded.
4. Tree operations never require schema-generated C types.

## 5. XML Mapping (bytes ↔ Val)

### 5.1 Codec interface

```c
typedef struct CodecOps {
  const char *mime;                         /* e.g. application/sep+xml */
  Val *(*decode)(const uint8_t *buf, size_t len, char **err);
  int   (*encode)(const Val *v, uint8_t **out, size_t *out_len, char **err);
} CodecOps;

const CodecOps *codec_xml(void);
/* later: const CodecOps *codec_exi(void); */

const CodecOps *codec_for_mime(const char *mime);
```

Application and resource layers depend only on `CodecOps`, not on libxml2.

### 5.2 XML → Val algorithm (`xml_codec`)

1. `xmlReadMemory` → `xmlDoc`.
2. Recursively convert each element:
   - **Leaf** (no element children): create `VAL_STR` / `VAL_INT` / `VAL_BOOL` from text.
   - **Internal**: create `VAL_OBJ` with `key = element name`.
3. Attributes → children with keys `@attrName`.
4. Group element children by name:
   - count == 1 → single field on the object.
   - count > 1 → create `VAL_ARR` with that name, push each child object.
5. Return root `Val*`.

Leaf typing heuristic (v1):

- `"true"` / `"false"` → `VAL_BOOL`
- optional ISO-like integers (entire text matches `-?[0-9]+`) → `VAL_INT`
- else → `VAL_STR`

Numbers that must remain strings (LFDI hex, mRID) stay strings because they are not pure decimal integers, or because callers use `val_get_str`.

### 5.3 Val → XML algorithm

1. Create root element from `v->key` (or caller-supplied root name).
2. For `VAL_OBJ`:
   - keys starting with `@` become XML attributes (without `@`).
   - other children become child elements.
3. For `VAL_ARR`:
   - emit each item as an element named `arr->key` (or item->key if present).
4. For scalars: emit text content.
5. `xmlDocDumpFormatMemory` → bytes for curl body.

### 5.4 Example

Input XML:

```xml
<EndDeviceList href="/edev" all="2">
  <EndDevice href="/edev/1">
    <LFDI>000102</LFDI>
    <SFDI>12345678901</SFDI>
  </EndDevice>
  <EndDevice href="/edev/2">
    <LFDI>aabbcc</LFDI>
  </EndDevice>
</EndDeviceList>
```

Resulting `Val` tree:

```text
OBJ key=EndDeviceList
  STR @href = "/edev"
  INT @all  = 2
  ARR EndDevice
    [0] OBJ EndDevice
          STR @href = "/edev/1"
          STR LFDI  = "000102"
          INT SFDI  = 12345678901
    [1] OBJ EndDevice
          STR @href = "/edev/2"
          STR LFDI  = "aabbcc"
```

Access:

```c
val_get_str(data, "EndDevice/0/LFDI");  /* "000102" */
val_get_i64(data, "@all", 0);           /* 2 */
```

Assign and re-encode:

```c
val_set_i64(data, "EndDevice/0/SFDI", 99999000013LL);
codec->encode(data, &bytes, &len, &err);
```

### 5.5 Ownership and memory

- `decode` returns a tree owned by the caller.
- `ResourceNode` owns `data` when attached (`resource_set_data` frees previous).
- `val_free` recursively frees children.
- Strings in `VAL_STR` are owned by the `Val` (duplicated on set).

## 6. HTTP Client (libcurl)

### 6.1 Responsibilities

- Perform GET/PUT/POST/DELETE against HTTPS/HTTP URLs.
- Mutual TLS: client cert + key + CA bundle.
- Set `Accept` / `Content-Type` from selected codec mime.
- Connection reuse via a shared `CURLM` or `CURLSH` handle.
- Map transport results into a small `HttpResult` (status, headers, body bytes).

### 6.2 API sketch

```c
typedef struct HttpClient HttpClient;

typedef struct HttpResult {
  long status;
  char *content_type;
  uint8_t *body;
  size_t body_len;
} HttpResult;

HttpClient *http_client_new(const HttpClientConfig *cfg);
void http_client_free(HttpClient *c);

int http_get(HttpClient *c, const char *url, HttpResult *out);
int http_send(HttpClient *c, const char *method, const char *url,
              const char *content_type, const uint8_t *body, size_t len,
              HttpResult *out);
void http_result_clear(HttpResult *r);
```

Config includes: base URL or host, cert paths, connect/transfer timeouts, whether to verify peer.

### 6.3 Mapping to resources

```text
http_get(url)
  -> HttpResult
  -> codec_for_mime(content_type)->decode(body)
  -> Val*
  -> attach to ResourceNode.data
  -> optionally expand list/link children
```

v1 may use **synchronous** curl easy interface inside worker calls invoked from a simple poll loop. Async `curl_multi` is an allowed evolution without changing `ResourceNode`/`Val` APIs.

## 7. Building and Refreshing the Resource Tree

### 7.1 Fetch one node

```text
resource_fetch(tree, node):
  node.state = FETCHING
  res = http_get(absolute_url(node.href))
  if fail:
    node.state = ERROR; node.retry = 1; return
  val = codec->decode(res.body)
  resource_set_data(node, val)
  node.state = READY
  node.fetched_at = now
  resource_expand(node)   # create child nodes from links/lists
```

### 7.2 Expand rules (v1)

- If `data` contains an array field of resources with `@href` (or `href` child), ensure a child `ResourceNode` exists per href.
- If known `*Link` object fields exist (`name` ends with `Link` or has `@href`), create/find child by that href.
- Expansion **creates nodes**; it does not necessarily fetch them until requested or until a walk policy says so.

### 7.3 Walk policies

- `fetch_deep(node, max_depth)` — depth-limited BFS/DFS used at bootstrap.
- `fetch_missing(node)` — fetch children in `NEW`/`ERROR` with `retry`.
- Application chooses roots (e.g. DeviceCapability) and depth.

### 7.4 Polling

Each node may set `poll_rate_sec` from payload (`pollRate` field) or inheritance from parent.

Timer loop:

```text
on_tick:
  for node in tree where poll_rate_sec > 0 and now >= poll_next:
    resource_fetch(node)   # refresh data; reconcile children by href
    node.poll_next = now + poll_rate_sec
```

Child reconciliation on refresh:

- same href → reuse node, replace `data`
- new href → add child
- missing href → mark stale or remove per policy (v1: mark `STALE`, detach after N cycles)

## 8. Disconnect / Retry (relative to old client)

Old client: `cleanup_http` removed in-flight Stubs and often exited the process.

New client:

1. HTTP failure sets `node.state = ERROR`, `retry = 1`, increments `retry_count`.
2. Tree and existing `data` remain (last-known-good stays available unless policy clears it).
3. Backoff: `retry_delay = min(cap, base * 2^retry_count)`.
4. `fetch_missing` / poll loop reissues GET/PUT.
5. PUT/POST are not blindly retried: only idempotent methods auto-retry; POST uses app-level policy.

This is simpler than the old Stub dependency bitmask because completion is “node READY and required children READY”, not flag arithmetic.

## 9. Domain Layer (scheduling)

DER event scheduling does **not** get a parallel typed object model in v1.

Instead:

```c
time_t der_start = val_get_i64(ctrl->data, "interval/start", 0);
time_t der_dur   = val_get_i64(ctrl->data, "interval/duration", 0);
int status       = val_get_i64(ctrl->data, "EventStatus/currentStatus", -1);
```

Optional thin helpers (not full structs):

```c
typedef struct DerIntervalView {
  time_t start;
  time_t duration;
} DerIntervalView;

int der_interval_view(const Val *ctrl, DerIntervalView *out);
```

Only add views where path access becomes noisy. Do not regenerate the full schema.

## 10. Directory Layout (proposed)

```text
/
  docs/superpowers/specs/          # design docs
  include/se/                      # public headers
    val.h
    resource.h
    codec.h
    http_client.h
    poll.h
    der_schedule.h
  src/
    val/                           # Val + path API
    codec/
      xml_codec.c
      codec_registry.c
    http/
      curl_client.c
    resource/
      node.c
      expand.c
      fetch.c
    poll/
      poll_loop.c
    der/
      schedule.c
    app/
      main.c
  tests/
    val_test.c
    xml_codec_test.c
    resource_expand_test.c
  third_party notes: libcurl, libxml2, openssl
  CMakeLists.txt or Meson (replace build.sh amalgamation)
```

Build system choice: **CMake** recommended for libcurl/libxml2 discovery. Exact build file is an implementation detail left to the plan phase.

## 11. Module Boundaries and Dependencies

```text
app → poll → resource → codec
                 ↘ http
der_schedule → resource/Val only
http → libcurl (no Val knowledge except app glue)
codec/xml → libxml2 + Val
```

Forbidden dependencies:

- `http` must not include libxml2.
- `val` must not include curl/xml.
- `resource` may call codec + http through interfaces passed in (or a small `Client` facade).

## 12. Phased Delivery

### Phase 1 — Foundation

- `Val` + path get/set/free
- `xml_codec` decode/encode round-trip tests
- `HttpClient` GET with TLS fixtures
- Single-node fetch → `ResourceNode.data`

### Phase 2 — Tree

- expand lists/links
- depth-limited fetch
- poll loop
- retry/backoff on failure

### Phase 3 — Client features

- discovery integration (wrap or port DNS-SD)
- PUT/POST helpers using `val_to_xml`
- minimal DER interval scheduling using path views
- CLI roughly covering current `client_test` happy path for XML servers

### Phase 4 — EXI (optional)

- implement `codec_exi` behind `CodecOps`
- negotiate mime via HTTP `Accept`

## 13. Testing Strategy

| Layer | Tests |
|-------|-------|
| Val | unit tests for path get/set, arr push, free |
| XML codec | fixture XML files ↔ Val ↔ XML golden comparisons |
| HTTP | mock HTTP server or recorded fixtures; TLS optional in CI |
| Resource | expand rules on fixture Vals without network |
| Poll/retry | fake clock + injected HTTP failures |

## 14. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Path typos (`LFDI` vs `lfdi`) | case-sensitive SEP names; debug `val_require`; logging |
| Attribute vs child href | always normalize to `@href` in codec |
| Large lists / paging (`s=`/`l=`) | encode query on fetch; store paging cursor on node |
| EXI-only servers | Phase 4 or interim gateway; v1 documents XML requirement |
| Losing schema validation | optional XSD validate in xml_codec debug builds |
| Sync curl blocking | keep timeouts short; later curl_multi |

## 15. Decisions Record

| Topic | Decision |
|-------|----------|
| Language | C |
| HTTP | libcurl |
| XML | libxml2 via `CodecOps` |
| EXI | interface reserved; implement later |
| Content model | single `Val` tree (no per-type structs) |
| Topology model | `ResourceNode` n-level lists |
| Access style | path strings + small domain views as needed |
| Old Stub/deps model | replaced |
| Old custom HTTP long-poll connection core | replaced by curl |

## 16. Open Items (to resolve in implementation plan)

1. Exact paging query representation on `ResourceNode`.
2. Whether discovery is in Phase 2 or 3.
3. CMake vs Meson.
4. Strict vs create-on-write path `set` semantics final default.
5. Whether to vendor libxml2/curl or require system packages.

These do not block agreement on the architecture above.
