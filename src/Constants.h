#pragma once

// ============================================================================
// Shared constants for the Harmonizer plugin.
//
// All timing values are in milliseconds.  Audio-thread code references these
// at prepare() time to derive per-sample coefficients; nothing here is
// evaluated on the real-time path.
// ============================================================================

// ---------------------------------------------------------------------------
// Voice engine
// ---------------------------------------------------------------------------

// Maximum number of simultaneous harmony voices.  12 covers full chromatic
// chords with headroom.  Each voice owns a RubberBand stretcher instance, so
// memory scales linearly with this value.
inline constexpr int kMaxVoices = 12;

// Maximum number of voices that may run their pitch-shifter concurrently.
// When the total active count (sustaining + fading) exceeds this, the oldest
// fading voices are force-killed to stay within CPU budget.  This prevents
// long release tails from stacking up and causing buffer overruns.
inline constexpr int kMaxProcessingVoices = 8;

// Exponential smoothing time for pitch-ratio changes.  40 ms is fast enough
// to track vibrato (~6 Hz) without audible lag, yet slow enough to suppress
// jitter from frame-to-frame pitch-detection noise.
inline constexpr float kPitchSmoothTimeMs = 40.0f;

// Linear fade-out duration when a voice is deactivated (note released).
// 10 ms prevents audible clicks without introducing a noticeable tail.
inline constexpr float kFadeOutTimeMs = 10.0f;

// ---------------------------------------------------------------------------
// Stereo panning
// ---------------------------------------------------------------------------

// Exponential smoothing time for per-voice pan position changes.  20 ms
// avoids audible jumps when notes are added/removed and the stereo spread
// is redistributed.
inline constexpr float kPanSmoothTimeMs = 20.0f;

// Hard-limit for the stereo pan range.  ±0.8 leaves a small gap before
// full left/right to keep the image from collapsing into the speakers and
// to leave room for the dry signal in the center.
inline constexpr float kPanRangeLimit = 0.8f;

// ---------------------------------------------------------------------------
// Metering / UI
// ---------------------------------------------------------------------------

// Input-level meter color thresholds (dBFS).  Green below -12, yellow
// between -12 and -3, red above -3.
inline constexpr float kMeterYellowDb = -12.0f;
inline constexpr float kMeterRedDb    =  -3.0f;

// Number of UI-timer frames the MIDI activity LED stays lit after the last
// MIDI event.  At the default 30 Hz timer rate this gives ~133 ms visibility.
inline constexpr int kMidiLedFrames = 4;

// Floor value used when no meaningful dB level is available (e.g. silence).
inline constexpr float kSilenceDb = -100.0f;
