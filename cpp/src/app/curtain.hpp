#pragma once

#include "../gfx/assets.hpp"
#include "../gfx/gfx.hpp"

#include <functional>

namespace np {

class Curtain {
public:
    bool active = false;
    int state = 0; // 0 idle, 1 closing, 2 holding, 3 opening
    float progress = 0.0f;

    void start(std::function<void()> on_mid);
    void update(float dt);
    void paint(Gfx& gfx, Assets& assets, float w = 1920.0f, float h = 1080.0f);

private:
    float t_ = 0.0f;
    float hold_ = 0.0f;
    float close_time_ = 0.54f;
    float open_time_ = 0.66f;
    bool mid_done_ = false;
    std::function<void()> mid_;
};

} // namespace np
