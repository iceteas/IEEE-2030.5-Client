#ifndef SE_CODEC_H
#define SE_CODEC_H

#include "se/val.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CodecOps {
  const char *mime;
  Val *(*decode)(const uint8_t *buf, size_t len, char **err);
  int (*encode)(const Val *v, uint8_t **out, size_t *out_len, char **err);
} CodecOps;

const CodecOps *codec_xml(void);
const CodecOps *codec_for_mime(const char *mime);

#ifdef __cplusplus
}
#endif

#endif
