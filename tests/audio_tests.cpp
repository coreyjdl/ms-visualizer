#include "audio.hpp"
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        msvis::BeatMeter meter;
        std::array<float, 882> audio{};
        const auto silent = meter.process(audio.data(), 441, 1, -55);
        check(!silent.audible && silent.pulse == 0 && silent.rms == 0, "Silence must be silent");
        float largestPulse = 0;
        unsigned triggeredBlocks = 0;
        for (unsigned block = 0; block < 300; ++block) {
            for (unsigned i = 0; i < 441; ++i) {
                const float time = static_cast<float>(block * 441 + i) / msvis::sampleRate;
                const float envelope = std::exp(-std::fmod(time, 0.5F) * 18);
                const float sample = 0.2F * envelope * std::sin(6.2831853F * 65 * time);
                audio[i * 2] = audio[i * 2 + 1] = sample;
            }
            const auto levels = meter.process(audio.data(), 441, 1, -55);
            largestPulse = std::max(largestPulse, levels.pulse);
            if (levels.pulse > 0.4F) ++triggeredBlocks;
        }
        check(largestPulse > 0.7F && triggeredBlocks > 12, "Repeated bass beats must create pulses");
        audio.fill(0);
        for (unsigned i = 0; i < 100; ++i) meter.process(audio.data(), 441, 1, -55);
        const auto paused = meter.process(audio.data(), 441, 1, -55);
        check(!paused.audible && paused.pulse == 0, "Pause must clear beat response");
        meter.reset();
        for (unsigned i = 0; i < 441; ++i) audio[i * 2 + 1] = 0.1F;
        const auto right = meter.process(audio.data(), 441, 1, -55);
        check(right.audible && right.rms > 0.05F, "Right-only stereo input must be detected");
        check(audio[1] == 0.1F, "Analysis must not modify source audio");
        msvis::AudioQueue queue;
        queue.push(audio.data(), 441);
        auto copied = queue.drain();
        check(copied.size() == audio.size() && copied[1] == 0.1F, "Queue must preserve stereo PCM");
        queue.push(nullptr, 44100);
        check(queue.dropped() == 22050, "Overflow must report dropped oldest frames");
        check(queue.drain().size() == 44100, "Queue must stay bounded");
        queue.clear();
        check(queue.dropped() == 0 && queue.drain().empty(), "Input switch must clear old PCM");
        std::cout << "PASS: beats, silence, pause, stereo, source isolation, and queue overflow\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
