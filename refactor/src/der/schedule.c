#include "se/der_schedule.h"

int der_interval_view(const Val *ctrl, DerIntervalView *out) {
  if (!ctrl || !out) return -1;
  out->start = (time_t)val_get_i64(ctrl, "interval/start", 0);
  out->duration = (time_t)val_get_i64(ctrl, "interval/duration", 0);
  if (out->start == 0 && out->duration == 0) {
    /* also accept Interval capital-I paths used by some docs */
    out->start = (time_t)val_get_i64(ctrl, "Interval/start", 0);
    out->duration = (time_t)val_get_i64(ctrl, "Interval/duration", 0);
  }
  return 0;
}

int der_event_status(const Val *ctrl) {
  int s;
  if (!ctrl) return -1;
  s = (int)val_get_i64(ctrl, "EventStatus/currentStatus", -1);
  if (s < 0) s = (int)val_get_i64(ctrl, "eventStatus/currentStatus", -1);
  return s;
}

time_t der_interval_end(const DerIntervalView *iv) {
  if (!iv) return 0;
  if (iv->duration < 0) return iv->start;
  return iv->start + iv->duration;
}

DerPhase der_phase(const DerIntervalView *iv, time_t now) {
  time_t end;
  if (!iv || iv->duration < 0) return DER_PHASE_NONE;
  end = der_interval_end(iv);
  if (now < iv->start) return DER_PHASE_SCHEDULED;
  if (now < end) return DER_PHASE_ACTIVE;
  return DER_PHASE_ENDED;
}
