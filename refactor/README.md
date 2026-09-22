# IEEE 2030.5 Client Refactor (v1)

Greenfield rewrite under `refactor/` per
`docs/superpowers/specs/2026-09-21-ieee20305-client-rewrite-design.md`.

The legacy tree at the repo root is unchanged.

## Stack

- C99
- libcurl (HTTP + mTLS)
- libxml2 (XML codec)
- CMake

## Layout

```text
refactor/
  include/se/     public headers (val, codec, http_client, resource, poll, der)
  src/val/        Val + path API
  src/codec/      xml_codec + CodecOps
  src/http/       curl client
  src/resource/   ResourceNode tree, expand, fetch, put/post
  src/poll/       poll_tick / backoff
  src/der/        DER interval views / phase
  src/app/        se_get, se_poll demos
  tests/          unit tests
```

## Build

```bash
cd refactor
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Demo

```bash
./build/se_get https://server/edev
./build/se_get https://server/dcap ca.pem client.crt client.key
./build/se_poll https://server/edev 30
```

## Status

- [x] Val get/set/path/array
- [x] XML decode/encode via CodecOps
- [x] curl HttpClient
- [x] ResourceNode expand + fetch
- [x] poll loop + retry backoff
- [x] PUT/POST helpers
- [x] DER interval/phase helpers
- [ ] EXI codec
- [ ] DNS-SD
