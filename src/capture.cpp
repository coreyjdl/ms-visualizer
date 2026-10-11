#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "capture.hpp"
#include <atomic>
#include <stdexcept>
#include <windows.h>

namespace msvis {
namespace {
void require(ma_result result, const char* action) {
    if (result != MA_SUCCESS)
        throw std::runtime_error(std::string(action) + ": " + ma_result_description(result));
}
std::string identity(const ma_device_id& id) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, id.wasapi, -1, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Cannot read audio device identity");
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, id.wasapi, -1, text.data(), size, nullptr, nullptr))
        throw std::runtime_error("Cannot convert audio device identity");
    text.pop_back();
    return text;
}
}

struct Capture::Impl {
    ma_context context{};
    ma_device device{};
    bool initialized = false;
    std::atomic<bool> active{false};
    std::atomic<bool> interrupted{false};
    std::atomic<bool> rerouted{false};
    AudioQueue queue;

    Impl() {
        const ma_backend backend = ma_backend_wasapi;
        require(ma_context_init(&backend, 1, nullptr, &context), "Cannot initialize Windows audio");
    }
    ~Impl() {
        if (initialized) ma_device_uninit(&device);
        ma_context_uninit(&context);
    }
    static void data(ma_device* device, void*, const void* input, ma_uint32 frames) {
        auto& self = *static_cast<Impl*>(device->pUserData);
        self.queue.push(static_cast<const float*>(input), frames);
    }
    static void notify(const ma_device_notification* event) {
        auto& self = *static_cast<Impl*>(event->pDevice->pUserData);
        if (event->type == ma_device_notification_type_stopped ||
            event->type == ma_device_notification_type_interruption_began) {
            self.active = false;
            self.interrupted = true;
        } else if (event->type == ma_device_notification_type_started ||
                   event->type == ma_device_notification_type_interruption_ended) {
            self.active = true;
        } else if (event->type == ma_device_notification_type_rerouted) {
            self.rerouted = true;
        }
    }
};
Capture::Capture() : impl_(std::make_unique<Impl>()) {}
Capture::~Capture() = default;

std::vector<Input> Capture::enumerate() {
    ma_device_info* playback = nullptr;
    ma_device_info* recording = nullptr;
    ma_uint32 playbackCount = 0, recordingCount = 0;
    require(ma_context_get_devices(&impl_->context, &playback, &playbackCount,
                                  &recording, &recordingCount), "Cannot list audio devices");
    std::vector<Input> result{{"", "System audio - default speakers (Spotify / DJ software)", true},
                              {"", "Default microphone / recording input", false}};
    for (ma_uint32 i = 0; i < playbackCount; ++i)
        result.push_back({identity(playback[i].id), std::string("System audio - ") + playback[i].name, true});
    for (ma_uint32 i = 0; i < recordingCount; ++i)
        result.push_back({identity(recording[i].id), std::string("Input - ") + recording[i].name, false});
    return result;
}

void Capture::start(const Input& input) {
    stop();
    ma_device_id selected{};
    const ma_device_id* deviceId = nullptr;
    if (!input.id.empty()) {
        ma_device_info* playback = nullptr;
        ma_device_info* recording = nullptr;
        ma_uint32 playbackCount = 0, recordingCount = 0;
        require(ma_context_get_devices(&impl_->context, &playback, &playbackCount,
                                      &recording, &recordingCount), "Cannot list audio devices");
        auto* devices = input.loopback ? playback : recording;
        const auto count = input.loopback ? playbackCount : recordingCount;
        bool found = false;
        for (ma_uint32 i = 0; i < count; ++i) {
            if (identity(devices[i].id) == input.id) {
                selected = devices[i].id;
                deviceId = &selected;
                found = true;
                break;
            }
        }
        if (!found) throw std::runtime_error("Saved input is disconnected. Choose an available input.");
    }
    auto config = ma_device_config_init(input.loopback ? ma_device_type_loopback : ma_device_type_capture);
    config.capture.pDeviceID = deviceId;
    config.capture.format = ma_format_f32;
    config.capture.channels = channels;
    config.sampleRate = sampleRate;
    config.periodSizeInMilliseconds = 10;
    config.dataCallback = Impl::data;
    config.notificationCallback = Impl::notify;
    config.pUserData = impl_.get();
    require(ma_device_init(&impl_->context, &config, &impl_->device),
            "Cannot open input. Check USB connection and Windows microphone privacy permissions");
    impl_->initialized = true;
    const auto result = ma_device_start(&impl_->device);
    if (result != MA_SUCCESS) {
        stop();
        require(result, "Cannot start audio input");
    }
    impl_->active = true;
    impl_->interrupted = false;
    impl_->rerouted = false;
}

void Capture::stop() {
    if (impl_->initialized) {
        ma_device_uninit(&impl_->device);
        impl_->initialized = false;
    }
    impl_->active = false;
    impl_->interrupted = false;
    impl_->rerouted = false;
    impl_->queue.clear();
}
bool Capture::running() const { return impl_->active.load(); }
AudioQueue& Capture::queue() { return impl_->queue; }
std::string Capture::notification() {
    if (impl_->interrupted.exchange(false))
        return "Audio input stopped or disconnected. Refresh inputs, then press Start input.";
    if (impl_->rerouted.exchange(false))
        return "Windows changed the audio endpoint. Verify the selected input and live meter.";
    return {};
}
} // namespace msvis
