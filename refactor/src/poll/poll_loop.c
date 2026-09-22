#include "se/poll.h"

#include <limits.h>

void poll_engine_init(PollEngine *pe, ResourceClient *client, ResourceNode *root) {
  if (!pe) return;
  pe->client = client;
  pe->root = root;
  pe->base_retry_sec = 5;
  pe->max_retry_sec = 300;
}

static int retry_delay(const PollEngine *pe, const ResourceNode *node) {
  int base = pe->base_retry_sec > 0 ? pe->base_retry_sec : 5;
  int cap = pe->max_retry_sec > 0 ? pe->max_retry_sec : 300;
  int shift = node->retry_count;
  int delay;
  if (shift > 16) shift = 16;
  delay = base << shift;
  if (delay > cap || delay <= 0) delay = cap;
  return delay;
}

typedef struct TickCtx {
  PollEngine *pe;
  time_t now;
  int attempts;
} TickCtx;

static void tick_one(ResourceNode *node, void *user) {
  TickCtx *ctx = user;
  PollEngine *pe = ctx->pe;

  if (node->state == NODE_ERROR && node->retry) {
    time_t due = node->fetched_at; /* reuse fetched_at as last attempt time */
    if (node->poll_next > 0) due = node->poll_next;
    else due = ctx->now; /* immediate first retry scheduling */
    if (due <= ctx->now) {
      if (resource_fetch(pe->client, node) == 0) {
        ctx->attempts++;
      } else {
        node->poll_next = ctx->now + retry_delay(pe, node);
        ctx->attempts++;
      }
    }
    return;
  }

  if (node->poll_rate_sec > 0 && node->poll_next > 0 &&
      node->poll_next <= ctx->now) {
    if (resource_fetch(pe->client, node) == 0) {
      resource_schedule_poll(node, ctx->now);
      ctx->attempts++;
    } else {
      /* keep last-known data; schedule retry via ERROR path next tick */
      node->poll_next = ctx->now + retry_delay(pe, node);
      ctx->attempts++;
    }
  }
}

int poll_tick(PollEngine *pe, time_t now) {
  TickCtx ctx;
  if (!pe || !pe->root || !pe->client) return 0;
  ctx.pe = pe;
  ctx.now = now;
  ctx.attempts = 0;
  resource_walk(pe->root, tick_one, &ctx);
  return ctx.attempts;
}

typedef struct NextCtx {
  const PollEngine *pe;
  time_t now;
  time_t best;
  int found;
} NextCtx;

static void next_one(ResourceNode *node, void *user) {
  NextCtx *ctx = user;
  time_t due = 0;
  int have = 0;

  if (node->state == NODE_ERROR && node->retry) {
    due = node->poll_next > 0 ? node->poll_next
                              : ctx->now + retry_delay(ctx->pe, node);
    have = 1;
  } else if (node->poll_rate_sec > 0 && node->poll_next > 0) {
    due = node->poll_next;
    have = 1;
  }
  if (!have) return;
  if (!ctx->found || due < ctx->best) {
    ctx->best = due;
    ctx->found = 1;
  }
}

int poll_seconds_until_next(PollEngine *pe, time_t now) {
  NextCtx ctx;
  long diff;
  if (!pe || !pe->root) return -1;
  ctx.pe = pe;
  ctx.now = now;
  ctx.best = 0;
  ctx.found = 0;
  resource_walk(pe->root, next_one, &ctx);
  if (!ctx.found) return -1;
  diff = (long)(ctx.best - now);
  if (diff < 0) return 0;
  if (diff > INT_MAX) return INT_MAX;
  return (int)diff;
}
