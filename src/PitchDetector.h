#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

// ============================================================================
// PitchDetector — Monophonic fundamental frequency estimator (YIN algorithm).
//
// YIN (de Cheveigné & Kawahara, 2002) detects pitch by finding the lag τ
// that minimises a "cumulative mean normalised difference" (CMNDF) of the
// signal with itself.
//
// Implementation overview:
//   1. Accumulate audio into a 1536-sample circular buffer (~35 ms at 44.1 kHz).
//   2. On each full buffer, compute the YIN difference function using
//      FFT-based autocorrelation (O(N log N) instead of O(N²)).
//   3. Apply CMNDF normalisation + absolute threshold to find the period.
//   4. Refine with parabolic interpolation for sub-sample accuracy.
//   5. 50% overlap (hop = 768 samples) means pitch is updated ~57×/sec.
//
// Returns the detected frequency in Hz, or -1.0f if no pitch is found
// (silence, noise, or polyphonic input).
// ============================================================================
class PitchDetector
{
public:
    PitchDetector();

    void prepare(double sampleRate, int maxBlockSize);

    // Feed audio and return the most recent pitch estimate (Hz), or -1.0f.
    float detectPitch(const float* audioBuffer, int numSamples);

private:
    // Run the YIN algorithm on a full analysis window.
    float yinDetect(const float* buffer, int numSamples);

    double sampleRate_    = 44100.0;
    int    bufferSize_     = 2048;   // Analysis window length (samples)
    int    halfBufferSize_ = 1024;   // W = N/2 (YIN uses the first half as reference)
    int    hopSize_        = 1024;   // 50% overlap for ~2× detection rate

    // Circular accumulation buffer — fills up to bufferSize_, then triggers
    // yinDetect() and shifts by hopSize_ for 50% overlap.
    std::vector<float> internalBuffer_;
    int   internalBufferWritePos_ = 0;
    float lastDetectedPitch_      = -1.0f;
    bool  bufferFilled_           = false;

    // --- YIN working memory ---
    std::vector<float> yinBuffer_;  // CMNDF values for each lag τ

    // --- FFT workspace (for O(N log N) autocorrelation) ---
    //
    // The YIN difference function d(τ) = Σ(x[j] - x[j+τ])² can be rewritten as:
    //   d(τ) = energy₀ + energyτ - 2·autocorr(τ)
    //
    // The autocorrelation term is computed via FFT cross-correlation:
    //   autocorr = IFFT( conj(FFT(a)) · FFT(b) )
    //   where a = x[0..W-1] (reference window, zero-padded)
    //         b = x[0..N-1] (full buffer, zero-padded)
    //
    // Energy terms are computed incrementally from a prefix sum of x[i]².
    std::unique_ptr<juce::dsp::FFT> fft_;
    int fftOrder_ = 0;
    int fftSize_  = 0;                // Next power of 2 ≥ (W + N) for linear correlation
    std::vector<float> fftBufferA_;   // 2 × fftSize (interleaved real/imag)
    std::vector<float> fftBufferB_;   // 2 × fftSize (interleaved real/imag)
    std::vector<float> powerPrefixSum_;  // bufferSize + 1

    // --- Tuning constants ---

    // CMNDF threshold: values below this are considered periodic.  Lower =
    // stricter (fewer false positives, more missed detections).  0.15 is a
    // good default for clean vocals.
    static constexpr float yinThreshold_ = 0.15f;

    // Detectable frequency range (Hz).  60 Hz covers low male voices (B1);
    // 1500 Hz covers high soprano fundamentals with headroom.
    static constexpr float minFrequency_  = 60.0f;
    static constexpr float maxFrequency_  = 1500.0f;

    // RMS noise gate.  If the input signal's RMS is below this (~-40 dBFS),
    // skip detection entirely to avoid spurious results from background noise.
    static constexpr float silenceThresholdRms_ = 0.01f;
};
