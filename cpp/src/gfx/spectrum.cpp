#include "spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace np {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

Spectrum::Spectrum() {
    std::uniform_real_distribution<float> ux(0.0f, 1920.0f);
    std::uniform_real_distribution<float> uy(500.0f, 1080.0f);
    std::uniform_real_distribution<float> uspd(4.0f, 16.0f);
    std::uniform_real_distribution<float> ua(20.0f, 90.0f);
    std::uniform_real_distribution<float> uph(0.0f, 6.28f);
    const float radii[5] = {1.0f, 1.0f, 2.0f, 2.0f, 3.0f};
    std::uniform_int_distribution<int> ur(0, 4);
    for (auto& p : particles_) {
        p = {ux(rng_), uy(rng_), uspd(rng_), radii[ur(rng_)], ua(rng_), uph(rng_)};
    }
}

void Spectrum::reset_bars() {
    for (float& b : bars) b = 0.0f;
}

void Spectrum::update(float dt, int ms, const AudioEngine& audio, bool active) {
    t += dt;
    float target[48]{};
    float tb = 0.0f, te = 0.0f;
    if (active) {
        audio.spectrum_at(ms, target, tb, te);
    }
    float attack = 1.0f - std::exp(-dt / 0.045f);
    float release = 1.0f - std::exp(-dt / 0.16f);
    for (int i = 0; i < 48; ++i) {
        float k = (target[i] > bars[i]) ? attack : release;
        bars[i] += (target[i] - bars[i]) * k;
    }
    float kb = 1.0f - std::exp(-dt / 0.09f);
    bass += (tb - bass) * kb;
    energy += (te - energy) * kb;

    std::uniform_real_distribution<float> ux(0.0f, 1920.0f);
    std::uniform_real_distribution<float> udy(10.0f, 80.0f);
    std::uniform_real_distribution<float> uspd(4.0f, 16.0f);
    std::uniform_real_distribution<float> ua(20.0f, 90.0f);
    for (auto& p : particles_) {
        p.y -= p.spd * dt * (0.55f + energy * 1.4f);
        p.x += std::sin(t * 0.35f + p.ph) * 11.0f * dt;
        if (p.y < 360.0f) {
            p.x = ux(rng_);
            p.y = 1080.0f - udy(rng_);
            p.spd = uspd(rng_);
            p.alpha = ua(rng_);
        }
    }
}

void Spectrum::draw_footlights(Gfx& gfx, float w, float h, float alpha, float lift) {
    if (alpha <= 0.01f) return;

    gfx.set_additive(true);

    // Warm floor glow
    Gfx::GradStop gs[3] = {
        {0.00f, ColorF::rgba(220, 150, 66, 0)},
        {0.72f, ColorF::rgba(232, 162, 78, static_cast<int>(28 * alpha))},
        {1.00f, ColorF::rgba(252, 206, 130, static_cast<int>(56 * alpha))},
    };
    gfx.fill_linear_grad({0.0f, h - 220.0f - lift, w, 220.0f},
                         {0.0f, h - 220.0f - lift}, {0.0f, h - lift}, gs, 3);

    const int n = 48;
    const float margin = 18.0f;
    const float span = w - margin * 2.0f;
    const float step = span / (n - 1);
    const float base_y = h - 2.0f - lift;
    ID2D1Bitmap1* glow = gfx.glow_sprite();

    for (int i = 0; i < n; ++i) {
        float vv = std::pow(clampf(bars[i]), 1.18f);
        float flicker = 0.94f + 0.06f * std::sin(t * 19.0f + i * 1.7f);
        float bh = (14.0f + 152.0f * vv) * flicker;
        float x = margin + i * step;

        // Multi-layer GPU additive glow beams
        static const struct { float wid; float a; } layers[4] = {
            {22.0f, 11.0f / 255.0f}, {14.0f, 20.0f / 255.0f},
            {8.0f, 38.0f / 255.0f}, {4.0f, 92.0f / 255.0f}
        };
        for (const auto& ly : layers) {
            ColorF c = ColorF::rgba(246, 185, 94).with_alpha(ly.a * alpha);
            gfx.draw_line({x, base_y}, {x, base_y - bh}, c, ly.wid, true);
        }
        // Hot incandescent core
        gfx.draw_line({x, base_y}, {x, base_y - bh + 5.0f},
                      ColorF::rgba(255, 236, 193, static_cast<int>(210 * alpha)), 1.6f, true);

        // Soft floor lamp bloom using pre-baked glow sprite (zero per-frame gradient allocations)
        float r = (3.0f + 4.2f * vv) * 3.2f;
        if (glow) {
            gfx.draw_bitmap(glow, {x - r, base_y - 2.0f - r, r * 2.0f, r * 2.0f}, 0.72f * alpha, true);
        }
        gfx.fill_ellipse({x, base_y - 2.0f}, 2.4f + 1.8f * vv, 2.4f + 1.8f * vv,
                         ColorF::rgba(255, 238, 196, static_cast<int>(190 * alpha)));
    }

    // Floating warm dust embers
    for (const auto& p : particles_) {
        float aa = p.alpha * (0.45f + energy * 0.9f) * alpha / 255.0f;
        if (aa <= 0.004f) continue;
        float core_a = std::min(155.0f / 255.0f, aa);
        if (glow) {
            float hr = p.r * 3.5f;
            gfx.draw_bitmap(glow, {p.x - hr, p.y - hr, hr * 2.0f, hr * 2.0f}, core_a * 0.55f, true);
        }
        gfx.fill_ellipse({p.x, p.y}, p.r, p.r, ColorF::rgba(248, 214, 156).with_alpha(core_a));
    }

    gfx.set_additive(false);
}

void Spectrum::draw_halo(Gfx& gfx, float cx, float cy, float radius, float alpha, ColorF base) {
    if (alpha <= 0.01f) return;
    const int n = 72;
    const float pulse = radius + bass * 4.5f;

    gfx.save_transform();
    gfx.translate(cx, cy);
    gfx.rotate(t * 3.0f);

    // Subtle dark backing halo for crisp contrast against bright CGs
    for (int i = 0; i < n; ++i) {
        float v = bars[i % 48];
        if (i % 2) v *= 0.62f;
        float length = 22.0f + 94.0f * std::pow(clampf(v), 1.25f);
        float ang = (2.0f * kPi * i) / n;
        float sx = std::sin(ang), sy = -std::cos(ang);
        PointF p0{sx * pulse, sy * pulse};
        PointF p1{sx * (pulse + length), sy * (pulse + length)};
        gfx.draw_line(p0, p1, ColorF::rgba(12, 8, 5, static_cast<int>(52 * alpha)), 11.0f, true);
    }

    gfx.set_additive(true);
    float boost = std::min(1.0f, alpha * 1.35f);
    for (int i = 0; i < n; ++i) {
        float v = bars[i % 48];
        if (i % 2) v *= 0.62f;
        float length = 22.0f + 94.0f * std::pow(clampf(v), 1.25f);
        float ang = (2.0f * kPi * i) / n;
        float sx = std::sin(ang), sy = -std::cos(ang);
        PointF p0{sx * pulse, sy * pulse};
        PointF p1{sx * (pulse + length), sy * (pulse + length)};
        gfx.draw_line(p0, p1, base.with_alpha((38.0f / 255.0f) * boost), 10.0f, true);
        gfx.draw_line(p0, p1, base.with_alpha((84.0f / 255.0f) * boost), 6.0f, true);
        gfx.draw_line(p0, p1, ColorF::rgba(255, 224, 168).with_alpha((235.0f / 255.0f) * boost), 2.6f, true);
    }
    gfx.set_additive(false);

    gfx.draw_ellipse({0, 0}, pulse - 4.0f, pulse - 4.0f,
                     base.with_alpha((105.0f / 255.0f) * alpha), 1.4f);
    gfx.draw_ellipse({0, 0}, pulse - 8.5f, pulse - 8.5f,
                     base.with_alpha((42.0f / 255.0f) * alpha), 0.9f);
    gfx.restore_transform();
}

void Spectrum::draw_halo_ellipse(Gfx& gfx, float cx, float cy, float rx, float ry, float alpha, ColorF base) {
    if (alpha <= 0.01f) return;
    const int n = 72;
    const float prx = rx + bass * 6.0f;
    const float pry = ry + bass * 4.5f;
    const float rot_rad = t * 3.0f * (kPi / 180.0f);

    for (int i = 0; i < n; ++i) {
        float v = bars[i % 48];
        if (i % 2) v *= 0.62f;
        float length = 26.0f + 104.0f * std::pow(clampf(v), 1.25f);
        float ang = rot_rad + (2.0f * kPi * i) / n;
        float cs = std::cos(ang), sn = std::sin(ang);
        PointF p0{cx + cs * prx, cy + sn * pry};
        float nx = cs / std::max(1.0f, prx), ny = sn / std::max(1.0f, pry);
        float inv = 1.0f / std::max(0.0001f, std::sqrt(nx * nx + ny * ny));
        nx *= inv; ny *= inv;
        PointF p1{p0.x + nx * length, p0.y + ny * length};
        gfx.draw_line(p0, p1, ColorF::rgba(12, 8, 5, static_cast<int>(52 * alpha)), 11.0f, true);
    }

    gfx.set_additive(true);
    float boost = std::min(1.0f, alpha * 1.35f);
    for (int i = 0; i < n; ++i) {
        float v = bars[i % 48];
        if (i % 2) v *= 0.62f;
        float length = 26.0f + 104.0f * std::pow(clampf(v), 1.25f);
        float ang = rot_rad + (2.0f * kPi * i) / n;
        float cs = std::cos(ang), sn = std::sin(ang);
        PointF p0{cx + cs * prx, cy + sn * pry};
        float nx = cs / std::max(1.0f, prx), ny = sn / std::max(1.0f, pry);
        float inv = 1.0f / std::max(0.0001f, std::sqrt(nx * nx + ny * ny));
        nx *= inv; ny *= inv;
        PointF p1{p0.x + nx * length, p0.y + ny * length};
        gfx.draw_line(p0, p1, base.with_alpha((44.0f / 255.0f) * boost), 11.0f, true);
        gfx.draw_line(p0, p1, base.with_alpha((92.0f / 255.0f) * boost), 6.5f, true);
        gfx.draw_line(p0, p1, ColorF::rgba(255, 228, 174).with_alpha((240.0f / 255.0f) * boost), 2.8f, true);
    }
    gfx.set_additive(false);
}

} // namespace np
