#ifndef SE_POLL_H
#define SE_POLL_H

#include "se/resource.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PollEngine {
  ResourceClient *client;
  ResourceNode *root;
  int base_retry_sec; /* default 5 */
  int max_retry_sec;  /* default 300 */
} PollEngine;

void poll_engine_init(PollEngine *pe, ResourceClient *client, ResourceNode *root);

/* Walk tree: refresh nodes whose poll_next <= now; retry ERROR nodes with backoff.
   Returns number of fetch attempts started. */
int poll_tick(PollEngine *pe, time_t now);

/* Seconds until next due poll/retry, or -1 if nothing scheduled. */
int poll_seconds_until_next(PollEngine *pe, time_t now);

#ifdef __cplusplus
}
#endif

#endif
