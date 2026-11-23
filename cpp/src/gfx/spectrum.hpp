#pragma once

#include "gfx.hpp"
#include "../audio/audio_engine.hpp"

#include <random>

namespace np {

class Spectrum {
public:
    Spectrum();

    void reset_bars();
    void update(float dt, int ms, const AudioEngine& audio, bool active = true);

    void draw_footlights(Gfx& gfx, float w = 1920.0f, float h = 1080.0f, float alpha = 1.0f, float lift = 0.0f);
    void draw_halo(Gfx& gfx, float cx, float cy, float radius, float alpha = 1.0f,
                   ColorF base = ColorF::rgba(226, 180, 112));
    void draw_halo_ellipse(Gfx& gfx, float cx, float cy, float rx, float ry, float alpha = 0.85f,
                           ColorF base = ColorF::rgba(229, 198, 135));

    float bars[48]{};
    float bass = 0.0f;
    float energy = 0.0f;
    float t = 0.0f;

private:
    struct Particle {
        float x, y, spd, r, alpha, ph;
    };
    std::mt19937 rng_{8};
    Particle particles_[64]{};
};

} // namespace np
