#include "PitchDetector.h"
#include <cmath>
#include <algorithm>
#include <cstring>

PitchDetector::PitchDetector() = default;

// ============================================================================
// Prepare — allocate buffers and create the FFT object
// ============================================================================

void PitchDetector::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Analysis window must be large enough that W = bufferSize/2 covers at
    // least one full period of the lowest detectable frequency:
    //   W >= sampleRate / minFrequency  →  bufferSize >= 2 * sampleRate / minFrequency
    //
    // At 44.1 kHz / 60 Hz → need ≥ 1470; use 1536.
    // At 48 kHz   / 60 Hz → need ≥ 1600; use 1600.
    // At 96 kHz   / 60 Hz → need ≥ 3200; use 3200.
    int minForFreqRange = static_cast<int>(2.0 * sampleRate / static_cast<double>(minFrequency_)) + 2;
    // Round up to even so W = bufferSize/2 is an integer
    if (minForFreqRange % 2 != 0) minForFreqRange++;
    bufferSize_ = std::max(1536, minForFreqRange);
    halfBufferSize_ = bufferSize_ / 2;  // W: reference window length
    hopSize_ = bufferSize_ / 2;          // 50% overlap

    internalBuffer_.resize(static_cast<size_t>(bufferSize_), 0.0f);
    internalBufferWritePos_ = 0;
    bufferFilled_ = false;

    yinBuffer_.resize(static_cast<size_t>(halfBufferSize_), 0.0f);

    // FFT size must be ≥ W + N to avoid circular-correlation wrap-around.
    // W = 768, N = 1536 → need ≥ 2304.  Next power of 2 is 4096.
    int minFftSize = bufferSize_ + halfBufferSize_;
    fftOrder_ = 0;
    fftSize_ = 1;
    while (fftSize_ < minFftSize)
    {
        fftSize_ *= 2;
        fftOrder_++;
    }
    fft_ = std::make_unique<juce::dsp::FFT>(fftOrder_);

    // Each FFT buffer needs 2 × fftSize floats (interleaved real/imag pairs)
    fftBufferA_.resize(static_cast<size_t>(fftSize_ * 2), 0.0f);
    fftBufferB_.resize(static_cast<size_t>(fftSize_ * 2), 0.0f);

    // Prefix sum of squared samples: entry i = Σ x[k]² for k in [0, i)
    powerPrefixSum_.resize(static_cast<size_t>(bufferSize_ + 1), 0.0f);

    lastDetectedPitch_ = -1.0f;
}

// ============================================================================
// detectPitch — accumulate audio and run YIN when the buffer is full
// ============================================================================

float PitchDetector::detectPitch(const float* audioBuffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        internalBuffer_[static_cast<size_t>(internalBufferWritePos_)] = audioBuffer[i];
        internalBufferWritePos_++;

        if (internalBufferWritePos_ >= bufferSize_)
        {
            bufferFilled_ = true;

            // Run YIN on the full analysis window
            lastDetectedPitch_ = yinDetect(internalBuffer_.data(), bufferSize_);

            // Shift by hopSize for 50% overlap: copy the second half of the
            // buffer to the first half so the next window shares half its data
            // with the previous one.
            std::copy(internalBuffer_.begin() + hopSize_,
                      internalBuffer_.begin() + bufferSize_,
                      internalBuffer_.begin());
            internalBufferWritePos_ = bufferSize_ - hopSize_;
        }
    }

    return lastDetectedPitch_;
}

// ============================================================================
// yinDetect — YIN algorithm with FFT-accelerated autocorrelation
//
// Reference: de Cheveigné & Kawahara, "YIN, a fundamental frequency
// estimator for speech and music", JASA 2002.
// ============================================================================

float PitchDetector::yinDetect(const float* buffer, int numSamples)
{
    const int W = numSamples / 2;  // Reference window length
    const int N = numSamples;      // Full buffer length

    // ------------------------------------------------------------------
    // Step 0: Power prefix sum (reused for silence gate + energy terms)
    // ------------------------------------------------------------------
    // powerPrefixSum_[i] = Σ buffer[k]² for k in [0, i)
    powerPrefixSum_[0] = 0.0f;
    for (int i = 0; i < N; ++i)
        powerPrefixSum_[static_cast<size_t>(i + 1)] = powerPrefixSum_[static_cast<size_t>(i)]
                                                       + buffer[i] * buffer[i];

    // Silence gate: skip detection if the signal is below the noise floor.
    // RMS = sqrt(totalEnergy / N)
    float rms = std::sqrt(powerPrefixSum_[static_cast<size_t>(N)] / static_cast<float>(N));
    if (rms < silenceThresholdRms_)
        return -1.0f;

    // energy₀ = Σ x[0..W-1]² — energy of the reference window (constant for all τ)
    float energy0 = powerPrefixSum_[static_cast<size_t>(W)];

    // ------------------------------------------------------------------
    // Step 1: FFT-based autocorrelation
    // ------------------------------------------------------------------
    // We need:  autocorr(τ) = Σ_{j=0}^{W-1} x[j] · x[j+τ]
    //
    // Computed as a cross-correlation via FFT:
    //   a = x[0..W-1], zero-padded to fftSize   (reference window)
    //   b = x[0..N-1], zero-padded to fftSize   (full buffer)
    //   autocorr = IFFT( conj(FFT(a)) · FFT(b) )
    //
    // Zero-padding ensures linear (non-circular) correlation for τ < W.

    // Prepare buffer A: reference window (first W samples)
    std::memset(fftBufferA_.data(), 0, fftBufferA_.size() * sizeof(float));
    std::memcpy(fftBufferA_.data(), buffer, static_cast<size_t>(W) * sizeof(float));

    // Prepare buffer B: full signal (all N samples)
    std::memset(fftBufferB_.data(), 0, fftBufferB_.size() * sizeof(float));
    std::memcpy(fftBufferB_.data(), buffer, static_cast<size_t>(N) * sizeof(float));

    // Forward FFT both signals
    fft_->performRealOnlyForwardTransform(fftBufferA_.data());
    fft_->performRealOnlyForwardTransform(fftBufferB_.data());

    // Complex multiply: conj(A) · B → result stored in A
    // conj(ar + ai·j) · (br + bi·j) = (ar·br + ai·bi) + (ar·bi - ai·br)·j
    for (int i = 0; i < fftSize_ * 2; i += 2)
    {
        float ar = fftBufferA_[static_cast<size_t>(i)];
        float ai = fftBufferA_[static_cast<size_t>(i + 1)];
        float br = fftBufferB_[static_cast<size_t>(i)];
        float bi = fftBufferB_[static_cast<size_t>(i + 1)];
        fftBufferA_[static_cast<size_t>(i)]     = ar * br + ai * bi;  // real part
        fftBufferA_[static_cast<size_t>(i + 1)] = ar * bi - ai * br;  // imag part
    }

    // Inverse FFT → autocorrelation values at each lag τ
    fft_->performRealOnlyInverseTransform(fftBufferA_.data());

    // ------------------------------------------------------------------
    // Step 2: YIN difference function + CMNDF normalisation
    // ------------------------------------------------------------------
    // The difference function measures how different the signal is from a
    // shifted copy of itself:
    //
    //   d(τ) = Σ_{j=0}^{W-1} (x[j] - x[j+τ])²
    //        = energy₀ + energyτ - 2 · autocorr(τ)
    //
    // where energyτ = Σ x[τ..τ+W-1]² (computed from the prefix sum).
    //
    // The CMNDF normalisation prevents d(τ) from having a systematic bias
    // toward small τ values:
    //
    //   d'(τ) = d(τ) · τ / Σ_{j=1}^{τ} d(j)
    //
    // d'(0) is defined as 1.0 by convention.

    yinBuffer_[0] = 1.0f;
    float runningSum = 0.0f;

    for (int tau = 1; tau < W; ++tau)
    {
        // energyτ = Σ x[tau..tau+W-1]² = prefixSum[tau+W] - prefixSum[tau]
        float energyTau = powerPrefixSum_[static_cast<size_t>(tau + W)]
                        - powerPrefixSum_[static_cast<size_t>(tau)];
        float autocorr = fftBufferA_[static_cast<size_t>(tau)];
        float diff = energy0 + energyTau - 2.0f * autocorr;

        // Floating-point rounding can produce tiny negative values
        if (diff < 0.0f) diff = 0.0f;

        runningSum += diff;
        yinBuffer_[static_cast<size_t>(tau)] = (runningSum > 0.0f)
            ? diff * static_cast<float>(tau) / runningSum
            : 1.0f;
    }

    // ------------------------------------------------------------------
    // Step 3: Absolute threshold search
    // ------------------------------------------------------------------
    // Search for the first lag τ where CMNDF dips below yinThreshold_.
    // Restrict the search to the plausible period range:
    //   tauMin = sampleRate / maxFrequency  (shortest period = highest pitch)
    //   tauMax = sampleRate / minFrequency  (longest period  = lowest pitch)
    int tauMin = static_cast<int>(sampleRate_ / maxFrequency_);
    int tauMax = static_cast<int>(sampleRate_ / minFrequency_);
    tauMin = std::max(tauMin, 2);          // τ < 2 is meaningless
    tauMax = std::min(tauMax, W - 1);      // can't exceed half the window

    int bestTau = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (yinBuffer_[static_cast<size_t>(tau)] < yinThreshold_)
        {
            // Walk forward to find the local minimum in this dip
            while (tau + 1 < tauMax &&
                   yinBuffer_[static_cast<size_t>(tau + 1)] < yinBuffer_[static_cast<size_t>(tau)])
            {
                ++tau;
            }
            bestTau = tau;
            break;
        }
    }

    if (bestTau < 0)
        return -1.0f;  // No periodicity found

    // ------------------------------------------------------------------
    // Step 4: Parabolic interpolation for sub-sample accuracy
    // ------------------------------------------------------------------
    // Fit a parabola through the three CMNDF values around the minimum and
    // find its vertex.  This typically improves accuracy by ~1 cent.
    // Skip interpolation at the boundaries where we don't have 3 neighbours.
    float betterTau = static_cast<float>(bestTau);
    if (bestTau > 0 && bestTau < W - 1)
    {
        float s0 = yinBuffer_[static_cast<size_t>(bestTau - 1)];
        float s1 = yinBuffer_[static_cast<size_t>(bestTau)];
        float s2 = yinBuffer_[static_cast<size_t>(bestTau + 1)];
        // Parabola through (-1,s0), (0,s1), (1,s2) — vertex at x = (s0-s2)/(2*(s0-2s1+s2))
        float denom = s0 - 2.0f * s1 + s2;
        // Guard against near-zero denominator (flat region = no useful parabola)
        // and clamp the shift to ±1 sample (parabolic interpolation can't move further)
        if (std::abs(denom) > 1e-6f)
        {
            float shift = (s0 - s2) / (2.0f * denom);
            shift = std::clamp(shift, -1.0f, 1.0f);
            betterTau = static_cast<float>(bestTau) + shift;
        }
    }

    float frequency = static_cast<float>(sampleRate_) / betterTau;

    // Final sanity check — reject frequencies outside the expected range
    if (frequency < minFrequency_ || frequency > maxFrequency_)
        return -1.0f;

    return frequency;
}
