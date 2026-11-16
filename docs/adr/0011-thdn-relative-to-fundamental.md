# 0011. THD+N is relative to the fundamental

Status: accepted (2026-11-15)

**Decision.** `thdn_db` is the power of everything except the fundamental, divided by
the power of the fundamental.

**Alternative rejected.** Dividing by the total power, as AES17 does.

**Why.** It makes THD+N directly comparable with THD, which is also relative to the
fundamental. The two definitions differ by less than 0.05 dB when THD+N is below
-20 dB.
