// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <algorithm>
#include <cmath>

// Pure sample mixer: cue never enters program, and the microphone is added after ducking.
struct StationMix {
    double sampleRate = 48000;
    float remoteMix = 0, duckGain = 1, microphoneGain = 0;
    void reset(double rate) { sampleRate = rate; remoteMix = 0; duckGain = 1; microphoneGain = 0; }
    void advance(bool remote, float fadeSeconds, bool duck, float micTarget) {
        const float step = 1.0f / std::max(1.0, sampleRate * fadeSeconds);
        remoteMix = approach(remoteMix, remote ? 1.f : 0.f, step);
        duckGain = approach(duckGain, duck ? 0.25f : 1.f, float(1.0 / (sampleRate * (duck ? 0.03 : 0.25))));
        microphoneGain = approach(microphoneGain, micTarget, float(1.0 / (sampleRate * 0.01)));
    }
    float program(float local, float remote, float mic, float localLevel, float remoteLevel) const {
        return (local * localLevel * (1 - remoteMix) + remote * remoteLevel * remoteMix) * duckGain + mic * microphoneGain;
    }
    static float approach(float value, float target, float step) {
        return value < target ? std::min(target, value + step) : std::max(target, value - step);
    }
};

// Stereo-linked feed-forward compressor for the complete broadcast mix.
// One detector envelope controls both channels so compression cannot pull the
// stereo image toward whichever side happens to be louder.
struct StationCompressor {
    double sampleRate = 48000.0;
    float envelope = 0.0f;
    float gainReductionDb = 0.0f;

    void reset(double rate) {
        sampleRate = std::max(1.0, rate);
        envelope = 0.0f;
        gainReductionDb = 0.0f;
    }

    std::array<float, 2> process(float left, float right, bool enabled,
                                 float thresholdDb, float ratio,
                                 float attackMs, float releaseMs,
                                 float makeupDb) {
        if (!enabled) {
            gainReductionDb = 0.0f;
            return {left, right};
        }

        const float peak = std::max(std::abs(left), std::abs(right));
        const float timeMs = peak > envelope ? attackMs : releaseMs;
        const float timeSeconds = std::max(0.0001f, timeMs * 0.001f);
        const float coefficient = std::exp(-1.0f / float(sampleRate * timeSeconds));
        envelope = coefficient * envelope + (1.0f - coefficient) * peak;

        constexpr float kneeDb = 6.0f;
        const float levelDb = envelope > 0.000001f
            ? 20.0f * std::log10(envelope) : -120.0f;
        const float overDb = levelDb - thresholdDb;
        const float safeRatio = std::max(1.0f, ratio);
        float compressedDb = 0.0f;
        if (overDb > -0.5f * kneeDb) {
            if (overDb >= 0.5f * kneeDb)
                compressedDb = overDb * (1.0f / safeRatio - 1.0f);
            else {
                const float kneePosition = overDb + 0.5f * kneeDb;
                compressedDb = (1.0f / safeRatio - 1.0f)
                    * kneePosition * kneePosition / (2.0f * kneeDb);
            }
        }

        gainReductionDb = -compressedDb;
        const float gain = std::pow(10.0f, (compressedDb + makeupDb) / 20.0f);
        return {left * gain, right * gain};
    }
};
