#include <GL/glew.h>
#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <shlobj.h>
#include "audio.hpp"
#include "capture.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
std::ofstream logFile;
void log(const std::string& message) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    logFile << std::setfill('0') << std::setw(2) << time.wHour << ':'
            << std::setw(2) << time.wMinute << ':' << std::setw(2) << time.wSecond
            << " " << message << std::endl;
}

fs::path executableDirectory() {
    std::vector<wchar_t> path(32768);
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) throw std::runtime_error("Cannot locate the application folder");
    return fs::path(std::wstring(path.data(), length)).parent_path();
}
fs::path settingsDirectory() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder)))
        throw std::runtime_error("Cannot locate Windows application settings folder");
    const auto path = fs::path(folder) / "MS-Visualizer";
    CoTaskMemFree(folder);
    fs::create_directories(path);
    return path;
}

struct Settings {
    std::string deviceId;
    bool loopback = true;
    bool autoStart = true;
    bool reactive = true;
    bool freezeOnSilence = true;
    float gain = 2;
    float strength = 0.7F;
    float noiseFloor = -55;
    int cycleSeconds = 30;
};

Settings readSettings(const fs::path& path) {
    if (!fs::exists(path)) return {};
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot read saved settings");
    const auto json = nlohmann::json::parse(stream);
    Settings settings;
    settings.deviceId = json.at("deviceId").get<std::string>();
    settings.loopback = json.at("loopback").get<bool>();
    settings.autoStart = json.at("autoStart").get<bool>();
    settings.reactive = json.at("reactive").get<bool>();
    settings.freezeOnSilence = json.at("freezeOnSilence").get<bool>();
    settings.gain = json.at("gain").get<float>();
    settings.strength = json.at("strength").get<float>();
    settings.noiseFloor = json.at("noiseFloor").get<float>();
    settings.cycleSeconds = json.at("cycleSeconds").get<int>();
    if (!std::isfinite(settings.gain) || settings.gain < 0.25F || settings.gain > 16 ||
        !std::isfinite(settings.strength) || settings.strength < 0 || settings.strength > 1 ||
        !std::isfinite(settings.noiseFloor) || settings.noiseFloor < -80 || settings.noiseFloor > -20 ||
        settings.cycleSeconds < 0 || settings.cycleSeconds > 300)
        throw std::runtime_error("Saved settings are outside supported ranges");
    return settings;
}
void saveSettings(const fs::path& path, const Settings& settings) {
    const nlohmann::json json{
        {"deviceId", settings.deviceId}, {"loopback", settings.loopback},
        {"autoStart", settings.autoStart}, {"reactive", settings.reactive},
        {"freezeOnSilence", settings.freezeOnSilence}, {"gain", settings.gain},
        {"strength", settings.strength}, {"noiseFloor", settings.noiseFloor},
        {"cycleSeconds", settings.cycleSeconds}};
    const auto temporary = path.parent_path() / "settings.tmp";
    {
        std::ofstream output(temporary);
        output << json.dump(2) << '\n';
        output.close();
        if (!output) throw std::runtime_error("Cannot write settings. Check folder permissions.");
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace saved settings");
}

class ProjectM {
public:
    explicit ProjectM(const fs::path& presetFolder)
        : engine_(projectm_create(), projectm_destroy),
          playlist_(nullptr, projectm_playlist_destroy) {
        if (!engine_) throw std::runtime_error("projectM initialization failed. Update the graphics driver.");
        projectm_set_window_size(engine_.get(), 640, 480);
        projectm_set_mesh_size(engine_.get(), 32, 24);
        projectm_set_fps(engine_.get(), 30);
        projectm_set_hard_cut_enabled(engine_.get(), false);
        projectm_set_soft_cut_duration(engine_.get(), 2);
        projectm_set_preset_locked(engine_.get(), true);
        const auto path = presetFolder.u8string();
        const char* texturePath = path.c_str();
        projectm_set_texture_search_paths(engine_.get(), &texturePath, 1);
        playlist_.reset(projectm_playlist_create(engine_.get()));
        if (!playlist_) throw std::runtime_error("Cannot create projectM playlist");
        projectm_playlist_set_shuffle(playlist_.get(), true);
        projectm_playlist_set_preset_switch_failed_event_callback(
            playlist_.get(), [](const char* filename, const char* error, void* user) {
                auto& self = *static_cast<ProjectM*>(user);
                self.error_ = std::string("Preset failed: ") + filename + " - " + error;
                log(self.error_);
            }, this);
        projectm_playlist_set_preset_switched_event_callback(
            playlist_.get(), [](bool, unsigned index, void* user) {
                auto& self = *static_cast<ProjectM*>(user);
                char* path = projectm_playlist_item(self.playlist_.get(), index);
                if (path) {
                    self.name_ = fs::u8path(path).stem().u8string();
                    projectm_playlist_free_string(path);
                    log("Preset: " + self.name_);
                }
            }, this);
        const auto count = projectm_playlist_add_path(playlist_.get(), path.c_str(), true, false);
        if (!count) throw std::runtime_error("No .milk presets found. Keep the presets folder next to the EXE.");
        log("Loaded " + std::to_string(count) + " projectM presets");
        next(true);
    }
    void next(bool immediate = false) {
        error_.clear();
        projectm_playlist_play_next(playlist_.get(), immediate);
        if (!error_.empty()) throw std::runtime_error(error_);
    }
    void previous() {
        error_.clear();
        projectm_playlist_play_last(playlist_.get(), false);
        if (!error_.empty()) throw std::runtime_error(error_);
    }
    void audio(const float* samples, unsigned frames) {
        const auto maximum = projectm_pcm_get_max_samples();
        while (frames) {
            const auto count = std::min(frames, maximum);
            projectm_pcm_add_float(engine_.get(), samples, count, PROJECTM_STEREO);
            samples += count * 2;
            frames -= count;
        }
    }
    void draw(int width, int height) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        projectm_set_window_size(engine_.get(), width, height);
        projectm_opengl_render_frame(engine_.get());
    }
    const std::string& name() const { return name_; }
private:
    std::unique_ptr<std::remove_pointer_t<projectm_handle>, decltype(&projectm_destroy)> engine_;
    std::unique_ptr<std::remove_pointer_t<projectm_playlist_handle>, decltype(&projectm_playlist_destroy)> playlist_;
    std::string name_;
    std::string error_;
};

GLuint compile(GLenum type, const char* source) {
    const auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint okay = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        char error[2048]{};
        glGetShaderInfoLog(shader, sizeof(error), nullptr, error);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Graphics shader failed: ") + error);
    }
    return shader;
}

class Presentation {
public:
    Presentation() {
        const char* vertex = R"(
#version 330 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
        const char* fragment = R"(
#version 330 core
in vec2 uv;
out vec4 outputColor;
uniform sampler2D frame;
uniform float pulse;
uniform float strength;
uniform float audible;
void main() {
    float beat = pulse * strength;
    vec2 sampleUV = (uv - 0.5) / (1.0 + beat * 0.10) + 0.5;
    vec3 color = texture(frame, clamp(sampleUV, 0.001, 0.999)).rgb;
    color *= 1.0 + beat * 0.65;
    float ring = exp(-abs(length((uv - 0.5) * vec2(1.3333, 1.0)) -
                    (0.18 + (1.0 - pulse) * 0.28)) * 100.0);
    color += vec3(0.06, 0.4, 0.65) * ring * beat * audible;
    outputColor = vec4(color, 1);
})";
        const auto vs = compile(GL_VERTEX_SHADER, vertex);
        const auto fs = compile(GL_FRAGMENT_SHADER, fragment);
        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint okay = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &okay);
        if (!okay) throw std::runtime_error("Cannot link reactive presentation shader");
        glGenVertexArrays(1, &vao_);
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        clear();
    }
    ~Presentation() {
        glDeleteTextures(1, &texture_);
        glDeleteVertexArrays(1, &vao_);
        glDeleteProgram(program_);
    }
    void clear() {
        const std::array<unsigned char, 4> black{0, 0, 0, 255};
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black.data());
    }
    void capture(int width, int height) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 0, 0, width, height, 0);
    }
    void draw(int width, int height, float pulse, float strength, bool audible) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glUniform1i(glGetUniformLocation(program_, "frame"), 0);
        glUniform1f(glGetUniformLocation(program_, "pulse"), pulse);
        glUniform1f(glGetUniformLocation(program_, "strength"), strength);
        glUniform1f(glGetUniformLocation(program_, "audible"), audible ? 1.0F : 0.0F);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
private:
    GLuint program_ = 0, texture_ = 0, vao_ = 0;
};

void screenshot(int width, int height, const fs::path& path) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_FreeSurface)>;
    Surface surface(SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
    if (!surface) throw std::runtime_error(SDL_GetError());
    for (int y = 0; y < height; ++y)
        std::memcpy(static_cast<unsigned char*>(surface->pixels) + y * surface->pitch,
                    pixels.data() + static_cast<std::size_t>(height - 1 - y) * width * 4, width * 4);
    if (SDL_SaveBMP(surface.get(), path.u8string().c_str()) < 0)
        throw std::runtime_error(std::string("Cannot save screenshot: ") + SDL_GetError());
}

std::vector<unsigned char> readFrame(int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

struct Graphics {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    bool uiReady = false;
    Graphics() {
        SDL_SetMainReady();
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) < 0)
            throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        window = SDL_CreateWindow("MS-Visualizer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  1100, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) throw std::runtime_error(std::string("Cannot create window: ") + SDL_GetError());
        SDL_SetWindowMinimumSize(window, 640, 480);
        context = SDL_GL_CreateContext(window);
        if (!context) throw std::runtime_error(std::string("OpenGL 3.3 is required. Update graphics drivers: ") + SDL_GetError());
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) throw std::runtime_error("Cannot initialize OpenGL functions");
        while (glGetError() != GL_NO_ERROR) {}
        log(std::string("GPU: ") + reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        log(std::string("OpenGL: ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        if (SDL_GL_SetSwapInterval(1) < 0) log(std::string("Vsync unavailable: ") + SDL_GetError());
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(1.2F);
        ImGui::GetIO().FontGlobalScale = 1.2F;
        if (!ImGui_ImplSDL2_InitForOpenGL(window, context) || !ImGui_ImplOpenGL3_Init("#version 330"))
            throw std::runtime_error("Cannot initialize control panel");
        uiReady = true;
    }
    ~Graphics() {
        if (uiReady) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
        }
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

int run(const std::vector<std::wstring>& arguments) {
    const auto folder = executableDirectory();
    const auto configFolder = settingsDirectory();
    logFile.open(configFolder / "MS-Visualizer.log", std::ios::trunc);
    if (!logFile) throw std::runtime_error("Cannot open the application log");
    log("MS-Visualizer 0.1.0 starting");
    msvis::Capture capture;
    auto inputs = capture.enumerate();
    for (const auto& input : inputs) log("Input: " + input.name + " | " + input.id);
    if (std::find(arguments.begin(), arguments.end(), L"--list-inputs") != arguments.end()) {
        nlohmann::json list = nlohmann::json::array();
        for (const auto& input : inputs)
            list.push_back({{"name", input.name}, {"id", input.id}, {"loopback", input.loopback}});
        std::ofstream output(configFolder / "inputs.json");
        output << list.dump(2);
        if (!output) throw std::runtime_error("Cannot write input list");
        return 0;
    }
    const bool smoke = std::find(arguments.begin(), arguments.end(), L"--smoke-test") != arguments.end();
    const bool captureTest = std::find(arguments.begin(), arguments.end(), L"--capture-test") != arguments.end();
    const auto settingsPath = configFolder / "settings.json";
    Settings settings;
    std::string error;
    try { settings = readSettings(settingsPath); }
    catch (const std::exception& failure) {
        error = std::string("Saved settings could not be loaded; defaults shown. ") + failure.what();
        log(error);
    }
    if (smoke) {
        settings = {};
        settings.autoStart = false;
        settings.cycleSeconds = 0;
    }
    Graphics graphics;
    ProjectM projectm(folder / "presets");
    Presentation presentation;
    msvis::BeatMeter meter;
    msvis::Levels levels;
    int selected = -1;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (inputs[i].id == settings.deviceId && inputs[i].loopback == settings.loopback) {
            selected = static_cast<int>(i);
            break;
        }
    }
    bool testTone = smoke;
    bool controls = true;
    bool fullscreen = false;
    bool done = false;
    std::string activeInput;
    std::string info;
    auto lastAudio = Clock::now();
    auto lastPreset = lastAudio;
    auto lastLog = lastAudio;
    const auto started = lastAudio;
    auto previous = lastAudio;
    unsigned long long demoSample = 0;
    double captureEnergy = 0;
    std::size_t captureFrames = 0;
    unsigned pulseFrames = 0, silentFrames = 0;
    bool shaderVerified = false, freezeVerified = false;
    std::vector<unsigned char> silentImage;
    auto action = [&](const std::function<void()>& function) {
        try { function(); }
        catch (const std::exception& failure) { error = failure.what(); log("ERROR: " + error); }
    };
    auto startInput = [&] {
        if (selected < 0 || selected >= static_cast<int>(inputs.size()))
            throw std::runtime_error("Saved input is unavailable. Choose a connected input and press Start input.");
        testTone = false;
        meter.reset();
        levels = {};
        presentation.clear();
        capture.start(inputs[selected]);
        activeInput = inputs[selected].name;
        settings.deviceId = inputs[selected].id;
        settings.loopback = inputs[selected].loopback;
        lastAudio = Clock::now();
        error.clear();
        log("Capturing: " + activeInput);
    };
    if (captureTest) {
        selected = std::find(arguments.begin(), arguments.end(), L"--microphone") != arguments.end() ? 1 : 0;
        action(startInput);
        if (!capture.running()) throw std::runtime_error(error);
    } else if (settings.autoStart && !smoke) action(startInput);

    while (!done) {
        const auto now = Clock::now();
        const float seconds = std::chrono::duration<float>(now - previous).count();
        const float elapsed = std::chrono::duration<float>(now - started).count();
        previous = now;
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            if (event.type == SDL_KEYDOWN && !event.key.repeat && !ImGui::GetIO().WantCaptureKeyboard) {
                if (event.key.keysym.sym == SDLK_F1) controls = !controls;
                if (event.key.keysym.sym == SDLK_ESCAPE) controls = true;
                if (event.key.keysym.sym == SDLK_SPACE) action([&] { projectm.next(); lastPreset = now; });
                if (event.key.keysym.sym == SDLK_F11) {
                    const bool target = !fullscreen;
                    if (SDL_SetWindowFullscreen(graphics.window, target ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) < 0)
                        error = SDL_GetError();
                    else fullscreen = target;
                }
            }
        }
        if (done) break;
        const auto notification = capture.notification();
        if (!notification.empty()) { error = notification; log(error); meter.reset(); levels = {}; }
        std::vector<float> audio;
        if (testTone) {
            const auto frames = static_cast<unsigned>(std::clamp(seconds, 0.001F, 0.1F) * msvis::sampleRate);
            audio.resize(frames * 2);
            for (unsigned i = 0; i < frames; ++i) {
                const float time = static_cast<float>(demoSample++) / msvis::sampleRate;
                const bool silent = std::fmod(time, 8.0F) >= 4 && std::fmod(time, 8.0F) < 6;
                const float envelope = std::exp(-std::fmod(time, 0.5F) * 18);
                const float value = silent ? 0.0F :
                    0.18F * envelope * std::sin(6.2831853F * 65 * time) +
                    0.015F * std::sin(6.2831853F * 700 * time);
                audio[i * 2] = audio[i * 2 + 1] = value;
            }
        } else audio = capture.queue().drain();
        if (!audio.empty()) {
            lastAudio = now;
            const auto frames = static_cast<unsigned>(audio.size() / 2);
            for (unsigned offset = 0; offset < frames; offset += 441)
                levels = meter.process(audio.data() + offset * 2, std::min(441U, frames - offset),
                                       settings.gain, settings.noiseFloor);
            for (float& sample : audio) sample = std::clamp(sample * settings.gain, -1.0F, 1.0F);
            projectm.audio(audio.data(), frames);
            if (captureTest) {
                for (float sample : audio) captureEnergy += sample * sample;
                captureFrames += frames;
            }
        } else if (now - lastAudio > std::chrono::milliseconds(150)) {
            meter.reset();
            levels = {};
            const std::array<float, 960> silence{};
            projectm.audio(silence.data(), 480);
        } else levels = meter.idle(seconds);
        if (levels.pulse > 0.3F) ++pulseFrames;
        if (!levels.audible) ++silentFrames;
        if (settings.cycleSeconds > 0 && levels.audible &&
            now - lastPreset > std::chrono::seconds(settings.cycleSeconds)) {
            action([&] { projectm.next(); lastPreset = now; });
        }

        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(graphics.window, &width, &height);
        if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(graphics.window) & SDL_WINDOW_MINIMIZED)) {
            SDL_Delay(50);
            continue;
        }
        const int renderWidth = std::min(640, width);
        const int renderHeight = std::min(480, height);
        if (!settings.freezeOnSilence || levels.audible) {
            projectm.draw(renderWidth, renderHeight);
            presentation.capture(renderWidth, renderHeight);
        }
        presentation.draw(width, height, levels.pulse, settings.reactive ? settings.strength : 0.0F,
                          levels.audible);
        if (smoke && !shaderVerified && levels.pulse > 0.5F && levels.audible) {
            presentation.draw(width, height, 0, 1, true);
            const auto baseline = readFrame(width, height);
            presentation.draw(width, height, 1, 1, true);
            const auto reactive = readFrame(width, height);
            std::size_t changed = 0;
            for (std::size_t i = 0; i < baseline.size(); i += 4)
                if (std::abs(int(baseline[i]) - int(reactive[i])) +
                    std::abs(int(baseline[i + 1]) - int(reactive[i + 1])) +
                    std::abs(int(baseline[i + 2]) - int(reactive[i + 2])) > 8) ++changed;
            if (changed < static_cast<std::size_t>(width) * height / 100)
                throw std::runtime_error("Beat shader did not measurably change the displayed image");
            screenshot(width, height, configFolder / "test-beat.bmp");
            log("VERIFY beat_changed_pixels=" + std::to_string(changed));
            shaderVerified = true;
            presentation.draw(width, height, levels.pulse, settings.strength, levels.audible);
        }
        const float demoTime = static_cast<float>(demoSample) / msvis::sampleRate;
        if (smoke && demoTime > 4.3F && demoTime < 5.8F && !levels.audible) {
            const auto image = readFrame(width, height);
            if (silentImage.empty()) {
                silentImage = image;
                screenshot(width, height, configFolder / "test-silence.bmp");
            } else {
                if (image != silentImage)
                    throw std::runtime_error("Silent input did not freeze the displayed image");
                freezeVerified = true;
            }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        if (controls) {
            ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(510, 650), ImGuiCond_FirstUseEver);
            ImGui::Begin("MS-Visualizer / Audio setup", nullptr, ImGuiWindowFlags_NoCollapse);
            ImGui::TextWrapped("Mixer / band: choose a USB or microphone input. DJ / Spotify: choose the speakers playing the music.");
            ImGui::Separator();
            const char* selectedName = selected >= 0 ? inputs[selected].name.c_str() : "Saved input disconnected - select an input";
            if (ImGui::BeginCombo("Audio input", selectedName)) {
                for (std::size_t i = 0; i < inputs.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(inputs[i].name.c_str(), selected == static_cast<int>(i)))
                        selected = static_cast<int>(i);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Refresh inputs")) action([&] {
                const auto id = selected >= 0 ? inputs[selected].id : settings.deviceId;
                const bool loopback = selected >= 0 ? inputs[selected].loopback : settings.loopback;
                inputs = capture.enumerate();
                selected = -1;
                for (std::size_t i = 0; i < inputs.size(); ++i)
                    if (inputs[i].id == id && inputs[i].loopback == loopback) selected = static_cast<int>(i);
            });
            ImGui::SameLine();
            if (ImGui::Button("Start input")) action(startInput);
            ImGui::SameLine();
            if (ImGui::Button("Stop input")) {
                capture.stop(); testTone = false; meter.reset(); levels = {}; presentation.clear();
            }
            ImGui::TextWrapped("Listening to: %s", testTone ? "Built-in test beats (NOT speaker audio)" :
                               capture.running() ? activeInput.c_str() : "Stopped");
            const float db = levels.rms > 0 ? 20 * std::log10(levels.rms) : -100;
            const auto status = testTone ? "TEST SIGNAL" : !capture.running() ? "STOPPED" :
                !levels.audible ? "SILENT / BELOW THRESHOLD" : "LIVE AUDIO";
            ImGui::Text("%s | %.1f dBFS | Peak %.3f", status, db, levels.peak);
            ImGui::ProgressBar(std::clamp((db + 60) / 60, 0.0F, 1.0F), ImVec2(-1, 20), "Input level");
            ImGui::ProgressBar(levels.pulse, ImVec2(-1, 20), "Detected bass / beat pulse");
            if (levels.peak > 1.0F)
                ImGui::TextColored(ImVec4(1, .5F, .2F, 1), "Visual input clipping - reduce input gain");
            if (capture.queue().dropped())
                ImGui::TextWrapped("Audio frames dropped: %zu. Reduce graphics load.", capture.queue().dropped());
            ImGui::SliderFloat("Visual input gain", &settings.gain, .25F, 16, "%.2fx", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Silence threshold", &settings.noiseFloor, -80, -20, "%.0f dBFS");
            ImGui::Separator();
            ImGui::Checkbox("Beat-driven response", &settings.reactive);
            ImGui::SliderFloat("Beat effect strength", &settings.strength, 0, 1);
            ImGui::Checkbox("Freeze when input is silent", &settings.freezeOnSilence);
            ImGui::TextWrapped("Beat-driven mode adds measured bass pulses to projectM zoom, glow and rings. Preset-native mode leaves its animation untouched.");
            if (ImGui::Button(testTone ? "End test and start selected input" : "Test beats (no speaker sound)"))
                action([&] {
                    if (testTone) startInput();
                    else { capture.stop(); testTone = true; demoSample = 0; meter.reset(); levels = {}; }
                });
            ImGui::TextWrapped("Test: 120 BPM; silence from 4-6 seconds. Meter and pulses must change.");
            ImGui::Separator();
            ImGui::TextWrapped("Preset: %s", projectm.name().c_str());
            if (ImGui::Button("Previous")) action([&] { projectm.previous(); lastPreset = now; });
            ImGui::SameLine();
            if (ImGui::Button("Next preset")) action([&] { projectm.next(); lastPreset = now; });
            ImGui::SliderInt("Cycle seconds (0 = lock)", &settings.cycleSeconds, 0, 300);
            ImGui::Checkbox("Start saved input on launch", &settings.autoStart);
            if (ImGui::Button("Save settings")) action([&] {
                if (selected < 0) throw std::runtime_error("Select an available audio input before saving");
                settings.deviceId = inputs[selected].id;
                settings.loopback = inputs[selected].loopback;
                saveSettings(settingsPath, settings);
                info = "Saved. These settings will be used next time.";
                error.clear();
            });
            ImGui::SameLine();
            if (ImGui::Button("Fullscreen (F11)")) {
                if (SDL_SetWindowFullscreen(graphics.window, !fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) < 0)
                    error = SDL_GetError();
                else fullscreen = !fullscreen;
            }
            if (ImGui::Button("Hide controls (F1)")) controls = false;
            ImGui::SameLine();
            if (ImGui::Button("Open presets folder"))
                ShellExecuteW(nullptr, L"open", (folder / "presets").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::TextWrapped("F1: controls | F11: fullscreen | Space: next preset | Esc: show controls");
            if (!info.empty()) ImGui::TextWrapped("%s", info.c_str());
            if (!error.empty()) {
                ImGui::TextColored(ImVec4(1, .45F, .3F, 1), "Attention");
                ImGui::TextWrapped("%s", error.c_str());
            }
            ImGui::End();
        } else {
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowBgAlpha(.5F);
            ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration);
            if (ImGui::Button("Audio setup (F1)")) controls = true;
            if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
            ImGui::End();
        }
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (smoke && elapsed > 30 && demoTime < 8)
            throw std::runtime_error("Graphics rendering is too slow to complete the beat test");
        if ((smoke && demoTime >= 8) || (captureTest && elapsed >= 8)) {
            screenshot(width, height, configFolder / "smoke-test.bmp");
            log("RESULT pulse_frames=" + std::to_string(pulseFrames) +
                " silent_frames=" + std::to_string(silentFrames) +
                " captured_frames=" + std::to_string(captureFrames) +
                " captured_rms=" + std::to_string(captureFrames ?
                    std::sqrt(captureEnergy / (captureFrames * 2)) : 0));
            if (smoke && (!pulseFrames || !silentFrames || !shaderVerified || !freezeVerified))
                throw std::runtime_error("Synthetic audio response verification failed");
            if (captureTest && !captureFrames)
                throw std::runtime_error("WASAPI input produced no audio callbacks; capture was not verified");
            if (smoke) log("VERIFY PASS: measured beat image change and stable silent image");
            done = true;
        }
        SDL_GL_SwapWindow(graphics.window);
        if (now - lastLog > std::chrono::seconds(1)) {
            log("INPUT rms=" + std::to_string(levels.rms) + " bass=" + std::to_string(levels.bass) +
                " pulse=" + std::to_string(levels.pulse) + " audible=" + (levels.audible ? "yes" : "no"));
            lastLog = now;
        }
        const auto frameTime = Clock::now() - now;
        if (frameTime < std::chrono::milliseconds(33))
            std::this_thread::sleep_for(std::chrono::milliseconds(33) - frameTime);
    }
    capture.stop();
    log("Exited normally");
    return 0;
}
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    try {
        int count = 0;
        auto* argv = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!argv) throw std::runtime_error("Cannot read launch arguments");
        std::vector<std::wstring> arguments;
        for (int i = 1; i < count; ++i) arguments.emplace_back(argv[i]);
        LocalFree(argv);
        return run(arguments);
    } catch (const std::exception& error) {
        log(std::string("FATAL: ") + error.what());
        MessageBoxA(nullptr, error.what(), "MS-Visualizer - cannot start", MB_OK | MB_ICONERROR);
        return 1;
    }
}
