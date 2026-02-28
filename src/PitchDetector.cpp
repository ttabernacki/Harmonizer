#include "PitchDetector.h"
#include <cmath>
#include <algorithm>

PitchDetector::PitchDetector() = default;

void PitchDetector::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Use 2048 samples for YIN analysis (~46ms at 44.1kHz)
    bufferSize_ = 2048;
    halfBufferSize_ = bufferSize_ / 2;

    internalBuffer_.resize(static_cast<size_t>(bufferSize_), 0.0f);
    internalBufferWritePos_ = 0;

    yinBuffer_.resize(static_cast<size_t>(halfBufferSize_), 0.0f);

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
            // Buffer full — run YIN detection
            lastDetectedPitch_ = yinDetect(internalBuffer_.data(), bufferSize_);
            internalBufferWritePos_ = 0;
        }
    }

    return lastDetectedPitch_;
}

float PitchDetector::yinDetect(const float* buffer, int numSamples)
{
    const int halfN = numSamples / 2;

    // Step 1 & 2: Difference function and cumulative mean normalized difference
    yinBuffer_[0] = 1.0f;
    float runningSum = 0.0f;

    for (int tau = 1; tau < halfN; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < halfN; ++j)
        {
            float delta = buffer[j] - buffer[j + tau];
            sum += delta * delta;
        }

        runningSum += sum;
        yinBuffer_[static_cast<size_t>(tau)] = (runningSum > 0.0f)
            ? sum * static_cast<float>(tau) / runningSum
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
