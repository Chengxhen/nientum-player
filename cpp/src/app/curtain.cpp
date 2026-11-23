#include "curtain.hpp"

namespace np {

void Curtain::start(std::function<void()> on_mid) {
    active = true;
    state = 1;
    t_ = 0.0f;
    hold_ = 0.0f;
    mid_ = std::move(on_mid);
    mid_done_ = false;
    progress = 0.0f;
}

void Curtain::update(float dt) {
    if (!active) return;
    if (state == 1) {
        t_ += dt / close_time_;
        if (t_ >= 1.0f) {
            t_ = 1.0f;
            state = 2;
            hold_ = 0.0f;
            if (mid_ && !mid_done_) {
                mid_done_ = true;
                auto fn = std::move(mid_);
                mid_ = nullptr;
                fn();
            }
        }
    } else if (state == 2) {
        hold_ += dt;
        if (hold_ >= 0.12f) state = 3;
    } else if (state == 3) {
        t_ -= dt / open_time_;
        if (t_ <= 0.0f) {
            t_ = 0.0f;
            active = false;
            state = 0;
            mid_ = nullptr;
        }
    }
    progress = t_;
}

void Curtain::paint(Gfx& gfx, Assets& assets, float w, float h) {
    if (!active) return;
    float e = ease_in_out(progress);
    gfx.fill_rect({0, 0, w, h}, ColorF::rgba(0, 0, 0, static_cast<int>(105 * e)));
    gfx.fill_rect({0, 0, w, h}, ColorF::rgba(20, 5, 8, static_cast<int>(242 * e)));

    if (ID2D1Bitmap1* panel = assets.ui("PZImgCommonCurtainHorizon.png", static_cast<int>(w * 0.66f), static_cast<int>(h), "stretch")) {
        float pw = w * 0.64f;
        float x = -pw + e * pw;
        gfx.draw_bitmap(panel, {x, 0, pw, h}, 1.0f, true);
        gfx.save_transform();
        gfx.translate(w, 0);
        gfx.scale(-1.0f, 1.0f);
        gfx.draw_bitmap(panel, {x, 0, pw, h}, 1.0f, true);
        gfx.restore_transform();
    }

    if (ID2D1Bitmap1* valance = assets.ui("PZImgLobbyCurtain.png", static_cast<int>(w), 150, "stretch")) {
        float vy = -150.0f + e * 150.0f;
        gfx.draw_bitmap(valance, {0, vy, w, 150}, 1.0f, true);
    }

    // Subtle warm theater center glow & gold edge highlight as curtains meet
    if (e > 0.02f) {
        float seam_x = e * (w * 0.5f);
        if (e < 0.98f) {
            Gfx::GradStop edge_l[2] = {{0.0f, ColorF::rgba(220, 180, 118, static_cast<int>(55 * e))}, {1.0f, ColorF::rgba(220, 180, 118, 0)}};
            gfx.fill_linear_grad({seam_x - 28.0f, 0, 28.0f, h}, {seam_x, 0}, {seam_x - 28.0f, 0}, edge_l, 2);
            Gfx::GradStop edge_r[2] = {{0.0f, ColorF::rgba(220, 180, 118, static_cast<int>(55 * e))}, {1.0f, ColorF::rgba(220, 180, 118, 0)}};
            gfx.fill_linear_grad({w - seam_x, 0, 28.0f, h}, {w - seam_x, 0}, {w - seam_x + 28.0f, 0}, edge_r, 2);
        }
        Gfx::GradStop cg[2] = {{0.0f, ColorF::rgba(196, 138, 78, static_cast<int>(28 * e * e))}, {1.0f, ColorF::rgba(0, 0, 0, 0)}};
        gfx.fill_radial_grad_ellipse({w * 0.5f, h * 0.48f}, 420.0f, 360.0f, cg, 2);
    }
}

} // namespace np
