#ifndef SE_DER_SCHEDULE_H
#define SE_DER_SCHEDULE_H

#include "se/val.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DerIntervalView {
  time_t start;
  time_t duration;
} DerIntervalView;

typedef enum {
  DER_PHASE_NONE = 0,
  DER_PHASE_SCHEDULED, /* now < start */
  DER_PHASE_ACTIVE,    /* start <= now < start+duration */
  DER_PHASE_ENDED      /* now >= end */
} DerPhase;

/* Read interval/start and interval/duration from a DERControl-like Val. */
int der_interval_view(const Val *ctrl, DerIntervalView *out);

/* currentStatus under EventStatus, or -1 if missing. */
int der_event_status(const Val *ctrl);

DerPhase der_phase(const DerIntervalView *iv, time_t now);

/* end = start + duration (clamped). */
time_t der_interval_end(const DerIntervalView *iv);

#ifdef __cplusplus
}
#endif

#endif
