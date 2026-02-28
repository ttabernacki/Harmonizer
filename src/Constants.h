#pragma once

// Shared constants for the Harmonizer plugin.

inline constexpr int   kMaxVoices            = 12;
inline constexpr float kPitchSmoothTimeMs    = 40.0f;   // Pitch interpolation time (snappy for live use)
inline constexpr float kFadeOutTimeMs        = 10.0f;   // Click-free deactivation fade
inline constexpr float kPanSmoothTimeMs      = 20.0f;   // Fast pan response
