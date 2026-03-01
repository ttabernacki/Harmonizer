#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

class PitchDetector
{
public:
    PitchDetector();

    void prepare(double sampleRate, int maxBlockSize);

    // Returns detected fundamental frequency in Hz, or -1.0f if no pitch detected.
    float detectPitch(const float* audioBuffer, int numSamples);

private:
    float yinDetect(const float* buffer, int numSamples);

    double sampleRate_ = 44100.0;
    int bufferSize_ = 2048;
    int halfBufferSize_ = 1024;
    int hopSize_ = 1024;  // 50% overlap for faster pitch updates

    std::vector<float> internalBuffer_;
    int internalBufferWritePos_ = 0;
    float lastDetectedPitch_ = -1.0f;
    bool bufferFilled_ = false;  // True once we've filled at least one full buffer

    // YIN working buffers
    std::vector<float> yinBuffer_;

    // FFT-based autocorrelation (reduces YIN difference function from O(N^2) to O(N log N))
    std::unique_ptr<juce::dsp::FFT> fft_;
    int fftOrder_ = 0;
    int fftSize_ = 0;
    std::vector<float> fftBufferA_;       // 2*fftSize workspace
    std::vector<float> fftBufferB_;       // 2*fftSize workspace
    std::vector<float> powerPrefixSum_;   // bufferSize+1 for incremental energy

    static constexpr float yinThreshold_ = 0.15f;
    static constexpr float minFrequency_ = 60.0f;    // Hz — lowest pitch to detect
    static constexpr float maxFrequency_ = 1500.0f;   // Hz — highest pitch to detect
    static constexpr float silenceThresholdRms_ = 0.01f;  // ~-40 dB RMS noise gate
};
