// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
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
