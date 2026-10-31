#pragma once
#include "state.h"

// Enhancement 4: the session_summary text. Returns false (and leaves out empty)
// for sessions under MIN_SUMMARY_MIN study minutes. Event text is capped at 300.
constexpr size_t SUMMARY_LEN = 200;
bool formatSummary(char* out, size_t n, const Session& s, int8_t focus);

// Formats into the pending buffer only when the session qualifies, so a short
// session never erases a summary still waiting for Blynk to come back.
bool queueSummary(char* pending, size_t n, const Session& s, int8_t focus);
