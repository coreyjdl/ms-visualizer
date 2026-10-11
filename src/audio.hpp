#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <vector>

namespace msvis {
constexpr unsigned sampleRate = 44100;
constexpr unsigned channels = 2;

struct Levels {
    float rms = 0;
    float peak = 0;
    float bass = 0;
    float pulse = 0;
    bool audible = false;
};

class BeatMeter {
public:
    void reset() { *this = BeatMeter{}; }

    Levels process(const float* samples, std::size_t frames, float gain, float thresholdDb) {
        if (!frames) return levels_;
        double energy = 0;
        double bassEnergy = 0;
        float peak = 0;
        constexpr float filter = 1.0F - 0.978852F; // 150 Hz one-pole low pass at 44.1 kHz.
        for (std::size_t i = 0; i < frames; ++i) {
            const float left = samples[i * 2] * gain;
            const float right = samples[i * 2 + 1] * gain;
            energy += (left * left + right * right) * 0.5;
            peak = std::max({peak, std::abs(left), std::abs(right)});
            lowLeft_ += filter * (left - lowLeft_);
            lowRight_ += filter * (right - lowRight_);
            bassEnergy += (lowLeft_ * lowLeft_ + lowRight_ * lowRight_) * 0.5;
        }
        const float rms = static_cast<float>(std::sqrt(energy / frames));
        const float bass = static_cast<float>(std::sqrt(bassEnergy / frames));
        const float dt = static_cast<float>(frames) / sampleRate;
        const float floor = std::pow(10.0F, thresholdDb / 20.0F);
        const bool audible = rms > floor;
        const float oldAverage = bassAverage_;
        bassAverage_ += (bass - bassAverage_) * (1 - std::exp(-dt / 0.8F));
        cooldown_ = std::max(0.0F, cooldown_ - dt);
        pulse_ *= std::exp(-dt / 0.16F);
        if (audible && bass > floor &&
            bass > std::max(oldAverage * 1.45F, floor * 1.3F) && cooldown_ == 0) {
            pulse_ = std::clamp((bass / std::max(oldAverage, floor) - 1.0F) / 2.0F, 0.0F, 1.0F);
            cooldown_ = 0.16F;
        }
        if (!audible) pulse_ = 0;
        levels_ = {rms, peak, bass, pulse_, audible};
        return levels_;
    }

    Levels idle(float seconds) {
        pulse_ *= std::exp(-seconds / 0.16F);
        levels_.pulse = pulse_;
        return levels_;
    }

private:
    float lowLeft_ = 0;
    float lowRight_ = 0;
    float bassAverage_ = 0;
    float pulse_ = 0;
    float cooldown_ = 0;
    Levels levels_;
};

class AudioQueue {
public:
    void push(const float* samples, std::size_t frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < frames; ++i) {
            if (count_ == capacity) {
                read_ = (read_ + 1) % capacity;
                --count_;
                ++dropped_;
            }
            const auto slot = (read_ + count_) % capacity;
            data_[slot * 2] = samples ? samples[i * 2] : 0;
            data_[slot * 2 + 1] = samples ? samples[i * 2 + 1] : 0;
            ++count_;
        }
    }

    std::vector<float> drain() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<float> result(count_ * 2);
        for (std::size_t i = 0; i < count_; ++i) {
            const auto slot = (read_ + i) % capacity;
            result[i * 2] = data_[slot * 2];
            result[i * 2 + 1] = data_[slot * 2 + 1];
        }
        read_ = (read_ + count_) % capacity;
        count_ = 0;
        return result;
    }

    std::size_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        read_ = count_ = dropped_ = 0;
    }

private:
    static constexpr std::size_t capacity = sampleRate / 2;
    std::array<float, capacity * 2> data_{};
    mutable std::mutex mutex_;
    std::size_t read_ = 0;
    std::size_t count_ = 0;
    std::size_t dropped_ = 0;
};
} // namespace msvis
