#pragma once

namespace Autoclicker::Types {

enum class NumericEditTarget {
    None,
    LeftRate,
    LeftOffset,
    LeftStart,
    LeftMultiplier,
    LeftGap,
    RightRate,
    RightOffset,
    RightStart,
    RightMultiplier,
    RightGap,
};

// Names the binding a capture belongs to so the GUI never holds a pointer into the
// configuration across an IPC round trip.
enum class BindingTarget { None, LeftToggle, RightToggle, Exit };

} // namespace Autoclicker::Types
