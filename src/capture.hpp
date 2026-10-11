#pragma once
#include "audio.hpp"
#include <memory>
#include <string>
#include <vector>

namespace msvis {
struct Input {
    std::string id;
    std::string name;
    bool loopback = false;
};

class Capture {
public:
    Capture();
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    std::vector<Input> enumerate();
    void start(const Input& input);
    void stop();
    bool running() const;
    AudioQueue& queue();
    std::string notification();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace msvis
