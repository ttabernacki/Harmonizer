#pragma once

// Shared constants for the Harmonizer plugin.

inline constexpr int   kMaxVoices            = 12;
inline constexpr float kPitchSmoothTimeMs    = 100.0f;  // Pitch interpolation time (50–150ms range)
inline constexpr float kFadeOutTimeMs        = 10.0f;   // Click-free deactivation fade
inline constexpr float kPanSmoothTimeMs      = 50.0f;
