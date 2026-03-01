#include "PitchDetector.h"
#include <cmath>
#include <algorithm>
#include <cstring>

PitchDetector::PitchDetector() = default;

void PitchDetector::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Use 1536 samples for YIN analysis (~35ms at 44.1kHz).
    // Minimum safe size: 2 * (sampleRate / minFrequency).
    // At 44.1kHz/60Hz that's 1470, so 1536 gives headroom while cutting ~11ms vs 2048.
    bufferSize_ = 1536;
    halfBufferSize_ = bufferSize_ / 2;
    hopSize_ = bufferSize_ / 2;  // 50% overlap: detect twice as often

    internalBuffer_.resize(static_cast<size_t>(bufferSize_), 0.0f);
    internalBufferWritePos_ = 0;
    bufferFilled_ = false;

    yinBuffer_.resize(static_cast<size_t>(halfBufferSize_), 0.0f);

    // FFT setup: need fftSize >= halfBufferSize + bufferSize for linear correlation
    int minFftSize = bufferSize_ + halfBufferSize_;
    fftOrder_ = 0;
    fftSize_ = 1;
    while (fftSize_ < minFftSize)
    {
        fftSize_ *= 2;
        fftOrder_++;
    }
    fft_ = std::make_unique<juce::dsp::FFT>(fftOrder_);
    fftBufferA_.resize(static_cast<size_t>(fftSize_ * 2), 0.0f);
    fftBufferB_.resize(static_cast<size_t>(fftSize_ * 2), 0.0f);
    powerPrefixSum_.resize(static_cast<size_t>(bufferSize_ + 1), 0.0f);

    lastDetectedPitch_ = -1.0f;
}

float PitchDetector::detectPitch(const float* audioBuffer, int numSamples)
{
    // Accumulate samples into internal buffer
    for (int i = 0; i < numSamples; ++i)
    {
        internalBuffer_[static_cast<size_t>(internalBufferWritePos_)] = audioBuffer[i];
        internalBufferWritePos_++;

        if (internalBufferWritePos_ >= bufferSize_)
        {
            bufferFilled_ = true;
            // Buffer full — run YIN detection
            lastDetectedPitch_ = yinDetect(internalBuffer_.data(), bufferSize_);

            // Shift by hopSize (50% overlap): move second half to first half
            std::copy(internalBuffer_.begin() + hopSize_,
                      internalBuffer_.begin() + bufferSize_,
                      internalBuffer_.begin());
            internalBufferWritePos_ = bufferSize_ - hopSize_;
        }
    }

    return lastDetectedPitch_;
}

float PitchDetector::yinDetect(const float* buffer, int numSamples)
{
    const int halfN = numSamples / 2;
    const int N = numSamples;

    // Step 0: Compute power prefix sum (reused for silence gate + energy terms)
    powerPrefixSum_[0] = 0.0f;
    for (int i = 0; i < N; ++i)
        powerPrefixSum_[static_cast<size_t>(i + 1)] = powerPrefixSum_[static_cast<size_t>(i)]
                                                       + buffer[i] * buffer[i];

    // RMS silence gate using prefix sum
    float rms = std::sqrt(powerPrefixSum_[static_cast<size_t>(N)] / static_cast<float>(N));
    if (rms < silenceThresholdRms_)
        return -1.0f;

    // Energy of first window: sum of x[0..W-1]^2
    float energy0 = powerPrefixSum_[static_cast<size_t>(halfN)];

    // Step 1: Compute autocorrelation via FFT
    // r(tau) = sum_{j=0}^{W-1} x[j] * x[j+tau]
    //
    // Using two-signal correlation:
    //   a = x[0..W-1] zero-padded to fftSize
    //   b = x[0..N-1] zero-padded to fftSize
    //   correlation = IFFT(conj(FFT(a)) * FFT(b))

    // Prepare buffer A: first halfN samples, rest zeros
    std::memset(fftBufferA_.data(), 0, fftBufferA_.size() * sizeof(float));
    std::memcpy(fftBufferA_.data(), buffer, static_cast<size_t>(halfN) * sizeof(float));

    // Prepare buffer B: full signal, rest zeros
    std::memset(fftBufferB_.data(), 0, fftBufferB_.size() * sizeof(float));
    std::memcpy(fftBufferB_.data(), buffer, static_cast<size_t>(N) * sizeof(float));

    // Forward FFTs
    fft_->performRealOnlyForwardTransform(fftBufferA_.data());
    fft_->performRealOnlyForwardTransform(fftBufferB_.data());

    // Complex multiply: conj(A) * B → store in A
    for (int i = 0; i < fftSize_ * 2; i += 2)
    {
        float ar = fftBufferA_[static_cast<size_t>(i)];
        float ai = fftBufferA_[static_cast<size_t>(i + 1)];
        float br = fftBufferB_[static_cast<size_t>(i)];
        float bi = fftBufferB_[static_cast<size_t>(i + 1)];
        fftBufferA_[static_cast<size_t>(i)]     = ar * br + ai * bi;  // real
        fftBufferA_[static_cast<size_t>(i + 1)] = ar * bi - ai * br;  // imag
    }

    // Inverse FFT → correlation values in fftBufferA_[tau]
    fft_->performRealOnlyInverseTransform(fftBufferA_.data());

    // Step 2: Difference function + cumulative mean normalized difference (CMNDF)
    // d(tau) = energy0 + energyTau - 2 * autocorr(tau)
    // where energyTau = sum of x[tau..tau+W-1]^2 (from prefix sum)
    yinBuffer_[0] = 1.0f;
    float runningSum = 0.0f;

    for (int tau = 1; tau < halfN; ++tau)
    {
        float energyTau = powerPrefixSum_[static_cast<size_t>(tau + halfN)]
                        - powerPrefixSum_[static_cast<size_t>(tau)];
        float autocorr = fftBufferA_[static_cast<size_t>(tau)];
        float diff = energy0 + energyTau - 2.0f * autocorr;

        // Guard against floating-point rounding producing small negatives
        if (diff < 0.0f) diff = 0.0f;

        runningSum += diff;
        yinBuffer_[static_cast<size_t>(tau)] = (runningSum > 0.0f)
            ? diff * static_cast<float>(tau) / runningSum
            : 1.0f;
    }

    // Step 3: Absolute threshold — find first dip below threshold
    int tauMin = static_cast<int>(sampleRate_ / maxFrequency_);
    int tauMax = static_cast<int>(sampleRate_ / minFrequency_);
    tauMin = std::max(tauMin, 2);
    tauMax = std::min(tauMax, halfN - 1);

    int bestTau = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (yinBuffer_[static_cast<size_t>(tau)] < yinThreshold_)
        {
            // Find the local minimum
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
        return -1.0f;

    // Step 4: Parabolic interpolation for sub-sample accuracy
    float betterTau = static_cast<float>(bestTau);
    if (bestTau > 0 && bestTau < halfN - 1)
    {
        float s0 = yinBuffer_[static_cast<size_t>(bestTau - 1)];
        float s1 = yinBuffer_[static_cast<size_t>(bestTau)];
        float s2 = yinBuffer_[static_cast<size_t>(bestTau + 1)];
        float denom = 2.0f * s1 - s2 - s0;
        if (std::abs(denom) > 1e-12f)
        {
            betterTau = static_cast<float>(bestTau) + (s0 - s2) / (2.0f * denom);
        }
    }

    float frequency = static_cast<float>(sampleRate_) / betterTau;

    // Sanity check
    if (frequency < minFrequency_ || frequency > maxFrequency_)
        return -1.0f;

    return frequency;
}
