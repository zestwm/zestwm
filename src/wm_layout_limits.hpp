/* Shared BSP split-ratio bounds. Use std::clamp with these. */
#pragma once

inline constexpr double kSplitRatioMin  = 0.05;
inline constexpr double kSplitRatioMax  = 0.95;
inline constexpr float  kSplitRatioMinF = 0.05f;
inline constexpr float  kSplitRatioMaxF = 0.95f;

inline constexpr int    kWmDragMotionMaxHz = 60;
inline constexpr int    kWmMoveMotionMaxHz = 360;
inline constexpr int    kWmMinWindowDim    = 1;
