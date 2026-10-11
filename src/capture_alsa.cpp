#include "capture.hpp"
#include <alsa/asoundlib.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace msvis {
namespace {
void require(int result, const std::string& action) {
    if (result < 0) throw std::runtime_error(action + ": " + snd_strerror(result));
}
}

struct Capture::Impl {
    snd_pcm_t* pcm = nullptr;
    std::atomic<bool> active{false};
    std::thread worker;
    AudioQueue queue;
    std::mutex errorMutex;
    std::string error;

    void fail(const std::string& message) {
        std::lock_guard<std::mutex> lock(errorMutex);
        error = message;
        active = false;
    }
    void read() {
        std::array<int16_t, 882> input{};
        std::array<float, 882> audio{};
        while (active) {
            const auto frames = snd_pcm_readi(pcm, input.data(), input.size() / channels);
            if (frames == -EAGAIN || frames == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (frames < 0) {
                const auto recovered = snd_pcm_recover(pcm, static_cast<int>(frames), 1);
                if (recovered < 0) {
                    fail(std::string("Audio input stopped: ") + snd_strerror(recovered) +
                         ". Refresh inputs and press Start input.");
                    return;
                }
                const auto restarted = snd_pcm_start(pcm);
                if (restarted < 0) {
                    fail(std::string("Cannot restart audio input: ") + snd_strerror(restarted));
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(errorMutex);
                    error = "Audio capture recovered an overrun. Check input and graphics load.";
                }
                continue;
            }
            for (snd_pcm_sframes_t i = 0; i < frames * channels; ++i)
                audio[static_cast<std::size_t>(i)] = input[static_cast<std::size_t>(i)] / 32768.0F;
            queue.push(audio.data(), static_cast<std::size_t>(frames));
        }
    }
};

Capture::Capture() : impl_(std::make_unique<Impl>()) {}
Capture::~Capture() { stop(); }

std::vector<Input> Capture::enumerate() {
    std::vector<Input> result{{"visualizer_monitor", "System audio - Spotify visualizer monitor", true},
                              {"default", "Default microphone / recording input", false}};
    void** hints = nullptr;
    require(snd_device_name_hint(-1, "pcm", &hints), "Cannot enumerate ALSA capture inputs");
    try {
        for (void** hint = hints; hint && *hint; ++hint) {
            using Text = std::unique_ptr<char, decltype(&std::free)>;
            Text name(snd_device_name_get_hint(*hint, "NAME"), std::free);
            Text description(snd_device_name_get_hint(*hint, "DESC"), std::free);
            Text direction(snd_device_name_get_hint(*hint, "IOID"), std::free);
            if (!name || (direction && std::string(direction.get()) == "Output")) continue;
            const std::string id(name.get());
            if (id == "null" || id == "default" || id == "visualizer_monitor") continue;
            std::string label = description ? description.get() : id;
            std::replace(label.begin(), label.end(), '\n', ' ');
            result.push_back({id, "Input - " + label + " [" + id + "]", false});
        }
    } catch (...) {
        snd_device_name_free_hint(hints);
        throw;
    }
    snd_device_name_free_hint(hints);
    return result;
}

void Capture::start(const Input& input) {
    stop();
    try {
        require(snd_pcm_open(&impl_->pcm, input.id.c_str(), SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK),
                "Cannot open ALSA input '" + input.id + "'");
        require(snd_pcm_set_params(impl_->pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                  channels, sampleRate, 1, 100000), "Cannot configure ALSA capture");
        require(snd_pcm_start(impl_->pcm), "Cannot start ALSA capture");
        impl_->active = true;
        impl_->worker = std::thread([this] { impl_->read(); });
    } catch (...) {
        stop();
        throw;
    }
}

void Capture::stop() {
    impl_->active = false;
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->pcm) {
        snd_pcm_close(impl_->pcm);
        impl_->pcm = nullptr;
    }
    impl_->queue.clear();
    std::lock_guard<std::mutex> lock(impl_->errorMutex);
    impl_->error.clear();
}
bool Capture::running() const { return impl_->active.load(); }
AudioQueue& Capture::queue() { return impl_->queue; }
std::string Capture::notification() {
    std::lock_guard<std::mutex> lock(impl_->errorMutex);
    std::string error;
    error.swap(impl_->error);
    return error;
}
} // namespace msvis
