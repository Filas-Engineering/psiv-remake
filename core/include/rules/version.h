#pragma once

namespace rules {

// Changes whenever a rule, a formula or the order of random draws changes, so old battle records are refused.
inline constexpr int kRulesVersion = 3;

// Changes whenever an event type or field changes.
inline constexpr int kEventSchemaVersion = 3;

inline constexpr int kRecordFormatVersion = 2;

}
