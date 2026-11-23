#include "player_screen.hpp"
#include "../core/paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace np {

namespace {
std::string fmt_time(int ms) {
    int s = std::max(0, ms / 1000);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
    return buf;
}
} // namespace

PlayerScreen::PlayerScreen(AppContext& app) : Screen(app) {}

void PlayerScreen::enter(const Track* track, const std::string&, const std::string&, int, bool) {
    active_ = true;
    t_ = 0.0f;
    fade_ = 1.0f;
    intro_ = 1.55f;
    ui_visible_ = true;
    bar_anim_ = 1.0f;
    blur_anim_ = 0.0f;
    hover_ui_ = false;
    hover_prog_ = false;
    hover_dot_ = -1;
    dragging_ = false;
    drag_vol_ = false;
    last_mouse_ = now_sec();
    app_.set_cursor_hidden(false);

    app_.audio().on_finished = [this]() {
        if (!active_) return;
        const Track* nxt = app_.library().next_track(track_, 1);
        if (nxt) set_track(nxt, true);
    };

    if (track) {
        set_track(track, true);
    } else if (!app_.library().tracks.empty()) {
        std::string last = app_.settings().get_string("last_track");
        const Track* t = app_.library().track_by_id(last);
        if (!t) t = &app_.library().tracks[0];
        set_track(t, true);
    }
}

void PlayerScreen::leave() {
    active_ = false;
    app_.audio().on_finished = nullptr;
    app_.set_cursor_hidden(false);
}

void PlayerScreen::set_track(const Track* track, bool autoplay) {
    if (!track) return;
    bool changed = (track_ == nullptr || track_->id != track->id);
    if (changed && track_ != nullptr) {
        ID2D1Bitmap1* old_cg = app_.assets().act_cg(track_, 3840);
        ID2D1Bitmap1* new_cg = app_.assets().act_cg(track, 3840);
        if (old_cg && old_cg != new_cg) {
            prev_cg_ = old_cg;
            cg_fade_ = 0.0f;
        }
    }
    track_ = track;
    app_.settings().set("last_track", track->id);
    if (changed) {
        intro_ = 1.35f;
        spec_.reset_bars();
    }
    if (autoplay) {
        auto& audio = app_.audio();
        bool same_live = (audio.track() != nullptr && audio.track()->id == track->id && audio.full_mode());
        if (same_live) {
            if (!audio.playing()) audio.toggle();
        } else {
            audio.play(track, 0, false);
        }
    } else {
        app_.audio().set_track_ptr_only(track);
    }
}

bool PlayerScreen::is_card_style() const {
    return app_.settings().get_string("player_style", "card") == "card";
}

void PlayerScreen::tick(float dt, double now) {
    t_ += dt;
    fade_ = std::min(1.0f, fade_ + dt / 0.62f);
    if (cg_fade_ < 1.0f) {
        cg_fade_ = std::min(1.0f, cg_fade_ + dt / 0.48f);
        if (cg_fade_ >= 1.0f) prev_cg_ = nullptr;
    }
    if (intro_ > 0.0f) intro_ = std::max(0.0f, intro_ - dt);

    if (ui_visible_ && (now - last_mouse_ > 2.6) && !hover_ui_ && !dragging_ && hover_dot_ < 0) {
        ui_visible_ = false;
    }
    if (!ui_visible_ && !dragging_) hover_ui_ = false;

    bar_anim_ += ((ui_visible_ ? 1.0f : 0.0f) - bar_anim_) * (1.0f - std::exp(-dt / 0.19f));
    float want_blur = (hover_ui_ && app_.settings().get_bool("blur_cg", true)) ? 1.0f : 0.0f;
    blur_anim_ += (want_blur - blur_anim_) * (1.0f - std::exp(-dt / 0.19f));

    bool act = app_.audio().playing();
    int pos = track_ ? app_.audio().position_ms() : 0;
    spec_.update(dt, pos, app_.audio(), act);

    app_.set_cursor_hidden(!ui_visible_ && (now - last_mouse_ > 2.6));
}

bool PlayerScreen::animating() const {
    return bar_anim_ < 0.999f || intro_ > 0.01f || blur_anim_ < 0.999f || cg_fade_ < 0.999f ||
           app_.audio().playing() || is_card_style();
}

RectF PlayerScreen::bar_rect() const {
    float y = 1080.0f - 132.0f - 26.0f + (1.0f - bar_anim_) * 150.0f;
    return {60.0f, y, 1800.0f, 132.0f};
}

RectF PlayerScreen::card_rect() const {
    // 16:9 card centered slightly above middle to leave room for top dots + bottom title & footlights
    float ox = 11.5f * std::sin(t_ * 0.62f) + 4.0f * std::sin(t_ * 1.13f + 0.7f);
    float oy = 8.0f * std::cos(t_ * 0.78f) + 3.0f * std::sin(t_ * 1.37f);
    float w = 1152.0f;
    float h = 648.0f;
    return {960.0f - w * 0.5f + ox, 468.0f - h * 0.5f + oy, w, h};
}

void PlayerScreen::layout_dots() {
    dot_hits_.clear();
    if (!track_) return;
    auto ctracks = app_.library().act_tracks(track_->act);
    int n = static_cast<int>(ctracks.size());
    if (n <= 0) return;

    RectF card = card_rect();
    float gap = (n > 12) ? 20.0f : 24.0f;
    float span = (n - 1) * gap;
    float x0 = card.cx() - span * 0.5f;
    float cy = card.y - 38.0f;
    for (int i = 0; i < n; ++i) {
        float x = x0 + i * gap;
        dot_hits_.push_back({ctracks[i], {x - 10.0f, cy - 12.0f, 20.0f, 24.0f}});
    }
}

void PlayerScreen::layout_bar() {
    RectF b = bar_rect();
    float bx = b.x, by = b.y;
    buttons_.clear();
    progress_ = {bx + 34, by + 99, 926, 6};

    buttons_.push_back({"prev", {bx + 1002, by + 61, 46, 46}});
    buttons_.push_back({"play", {bx + 1060, by + 54, 60, 60}});
    buttons_.push_back({"next", {bx + 1132, by + 61, 46, 46}});
    buttons_.push_back({"volume", {bx + 1196, by + 65, 40, 40}});
    vol_rect_ = {bx + 1244, by + 81, 86, 8};

    for (int i = 0; i < 3; ++i) {
        buttons_.push_back({"viz:" + std::to_string(i), {bx + 1352 + i * 58, by + 69, 52, 30}});
    }
    buttons_.push_back({"style:0", {bx + 1546, by + 69, 54, 30}});
    buttons_.push_back({"style:1", {bx + 1606, by + 69, 54, 30}});
    buttons_.push_back({"full", {bx + 1692, by + 61, 46, 46}});
}

bool PlayerScreen::hover_named(const std::string& name) const {
    for (const auto& [n, r] : buttons_) {
        if (n == name && r.adjusted(-4, -4, 4, 4).contains(mouse_)) return true;
    }
    return false;
}

void PlayerScreen::paint_fullscreen_cg(Gfx& gfx, const Track*, ID2D1Bitmap1* cg, const std::string& viz) {
    auto draw_cover = [&](ID2D1Bitmap1* bmp, float alpha) {
        if (!bmp || alpha <= 0.005f) return;
        D2D1_SIZE_F sz = bmp->GetSize();
        float s = std::max(1920.0f / std::max(1.0f, sz.width), 1080.0f / std::max(1.0f, sz.height));
        float dw = sz.width * s, dh = sz.height * s;
        RectF dst{(1920.0f - dw) * 0.5f, (1080.0f - dh) * 0.5f, dw, dh};
        if (blur_anim_ > 0.01f) {
            gfx.draw_blurred_bitmap(bmp, dst, blur_anim_ * 14.0f, alpha);
        } else {
            gfx.draw_bitmap(bmp, dst, alpha, true);
        }
    };
    if (cg) {
        if (cg_fade_ < 0.999f && prev_cg_) {
            draw_cover(prev_cg_, 1.0f);
            draw_cover(cg, ease_out(cg_fade_));
        } else {
            draw_cover(cg, 1.0f);
        }
        if (blur_anim_ > 0.01f) {
            gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(5, 4, 4, static_cast<int>(76 * blur_anim_)));
        }
    } else {
        gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(18, 12, 10));
    }

    if (ID2D1Bitmap1* vig = app_.assets().ui("PZImgVignetting.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(vig, {0, 0, 1920, 1080}, 0.66f, true);
    }

    Gfx::GradStop gt[2] = {{0.0f, ColorF::rgba(5, 4, 4, 150)}, {1.0f, ColorF::rgba(5, 4, 4, 0)}};
    gfx.fill_linear_grad({0, 0, 1920, 160}, {0, 0}, {0, 160}, gt, 2);
    Gfx::GradStop gb[2] = {{0.0f, ColorF::rgba(14, 10, 8, 0)}, {1.0f, ColorF::rgba(14, 10, 8, 118)}};
    gfx.fill_linear_grad({0, 900, 1920, 180}, {0, 900}, {0, 1080}, gb, 2);

    if (viz == "footlights") {
        spec_.draw_footlights(gfx, 1920.0f, 1080.0f, 0.95f, 180.0f * bar_anim_);
    } else if (viz == "halo") {
        spec_.draw_halo(gfx, 960.0f, 500.0f, 285.0f, 0.72f, ColorF::rgba(226, 180, 112));
    }
}

void PlayerScreen::paint_card_stage(Gfx& gfx, const Track* t, ID2D1Bitmap1* cg, const std::string& viz) {
    // 1. Blurred curtain CG backdrop
    if (cg) {
        gfx.draw_blurred_bitmap_cover(cg, {0, 0, 1920, 1080}, 22.0f + blur_anim_ * 14.0f, 1.0f);
        gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(8, 6, 5, static_cast<int>(92 + 68 * blur_anim_)));
    } else {
        gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(16, 12, 10));
    }

    if (ID2D1Bitmap1* vig = app_.assets().ui("PZImgVignetting.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(vig, {0, 0, 1920, 1080}, 0.78f, true);
    }

    Gfx::GradStop gt[2] = {{0.0f, ColorF::rgba(6, 4, 4, 160)}, {1.0f, ColorF::rgba(6, 4, 4, 0)}};
    gfx.fill_linear_grad({0, 0, 1920, 170}, {0, 0}, {0, 170}, gt, 2);
    Gfx::GradStop gb[2] = {{0.0f, ColorF::rgba(10, 7, 6, 0)}, {1.0f, ColorF::rgba(10, 7, 6, 145)}};
    gfx.fill_linear_grad({0, 880, 1920, 200}, {0, 880}, {0, 1080}, gb, 2);

    RectF card = card_rect();
    float stage_alpha = 1.0f - 0.55f * blur_anim_;

    // 2. Soft multi-layer elevation shadow & subtle warm aura behind card
    gfx.fill_rounded(card.adjusted(-16, -8, 16, 26), 34.0f, ColorF::rgba(0, 0, 0, static_cast<int>(54 * stage_alpha)));
    gfx.fill_rounded(card.adjusted(-9, -4, 9, 17), 28.0f, ColorF::rgba(0, 0, 0, static_cast<int>(82 * stage_alpha)));
    gfx.fill_rounded(card.adjusted(-4, -2, 4, 10), 25.0f, ColorF::rgba(0, 0, 0, static_cast<int>(105 * stage_alpha)));

    // 3. Rounded-corner crisp CG card (also blurs smoothly when hovering UI!)
    const float radius = 22.0f;
    gfx.push_rounded_clip(card, radius);
    auto draw_card_bmp = [&](ID2D1Bitmap1* bmp, float alpha) {
        if (!bmp || alpha <= 0.005f) return;
        if (blur_anim_ > 0.01f) {
            gfx.draw_blurred_bitmap_cover(bmp, card, blur_anim_ * 12.0f, alpha);
        } else {
            D2D1_SIZE_F sz = bmp->GetSize();
            float s = std::max(card.w / std::max(1.0f, sz.width), card.h / std::max(1.0f, sz.height));
            float dw = sz.width * s, dh = sz.height * s;
            gfx.draw_bitmap(bmp, {card.cx() - dw * 0.5f, card.cy() - dh * 0.5f, dw, dh}, alpha, true);
        }
    };
    if (cg) {
        if (cg_fade_ < 0.999f && prev_cg_) {
            draw_card_bmp(prev_cg_, 1.0f);
            draw_card_bmp(cg, ease_out(cg_fade_));
        } else {
            draw_card_bmp(cg, 1.0f);
        }
        if (blur_anim_ > 0.01f) {
            gfx.fill_rect(card, ColorF::rgba(6, 5, 4, static_cast<int>(76 * blur_anim_)));
        }
    } else {
        gfx.fill_rect(card, ColorF::rgba(28, 22, 18));
    }
    // F-drive .cp-card.glass diagonal specular glass sheen (crisp, non-blurring)
    Gfx::GradStop sheen[5] = {
        {0.08f, ColorF::rgba(255, 255, 255, 0)},
        {0.16f, ColorF::rgba(255, 252, 244, static_cast<int>(38 * stage_alpha))},
        {0.26f, ColorF::rgba(255, 252, 244, 0)},
        {0.76f, ColorF::rgba(255, 252, 244, 0)},
        {1.00f, ColorF::rgba(255, 252, 244, static_cast<int>(26 * stage_alpha))},
    };
    gfx.fill_linear_grad(card, {card.x, card.y}, {card.right(), card.y + card.h * 0.56f}, sheen, 5);
    gfx.pop_geometry_clip();

    // Subtle luminous glass rim + inner antique gold frame around the rounded card
    gfx.draw_rounded(card, radius, ColorF::rgba(255, 248, 234, static_cast<int>(148 * stage_alpha)), 1.5f);
    gfx.draw_rounded(card.adjusted(1.5f, 1.5f, -1.5f, -1.5f), radius - 1.5f,
                     ColorF::rgba(229, 198, 135, static_cast<int>(132 * stage_alpha)), 1.1f);
    gfx.draw_gold_divider(card.x + 36.0f, card.right() - 36.0f, card.y + 1.5f, stage_alpha * 0.82f, 1.3f);
    gfx.draw_corner_filigree(card.adjusted(-10, -10, 10, 10), 22.0f,
                             ColorF::rgba(222, 186, 126, static_cast<int>(138 * stage_alpha)), 1.4f);

    // 4. Circular halo visualizer centered over the card stage (matching F-drive)
    if (viz == "halo") {
        spec_.draw_halo(gfx, card.cx(), card.cy(), 242.0f, 0.85f * stage_alpha, ColorF::rgba(229, 198, 135));
    }

    // 5. Chapter track indicator dots above the card
    layout_dots();
    if (!dot_hits_.empty()) {
        ID2D1Bitmap1* glow = gfx.glow_sprite();
        for (int i = 0; i < static_cast<int>(dot_hits_.size()); ++i) {
            const Track* dt_track = dot_hits_[i].first;
            const RectF& hr = dot_hits_[i].second;
            PointF c = hr.center();
            bool active = (dt_track && dt_track->id == t->id);
            bool hov = (i == hover_dot_);
            if (active) {
                float pulse = 0.85f + 0.15f * std::sin(t_ * 3.0f);
                if (glow) {
                    float gs = 20.0f * pulse;
                    gfx.set_additive(true);
                    gfx.draw_bitmap(glow, {c.x - gs, c.y - gs, gs * 2.0f, gs * 2.0f}, 0.85f * stage_alpha, true);
                    gfx.set_additive(false);
                }
                gfx.fill_ellipse(c, 5.8f, 5.8f, ColorF::rgba(232, 192, 128, static_cast<int>(110 * stage_alpha)));
                gfx.fill_ellipse(c, 4.3f, 4.3f, ColorF::rgba(255, 246, 224, static_cast<int>(250 * stage_alpha)));
            } else {
                float r = hov ? 4.4f : 3.2f;
                gfx.fill_ellipse(c, r + 1.1f, r + 1.1f, ColorF::rgba(8, 6, 5, static_cast<int>(130 * stage_alpha)));
                gfx.fill_ellipse(c, r, r, ColorF::rgba(220, 206, 186, static_cast<int>((hov ? 225 : 132) * stage_alpha)));
            }
        }
        // Subtle tooltip when hovering a chapter track dot
        if (hover_dot_ >= 0 && hover_dot_ < static_cast<int>(dot_hits_.size())) {
            const Track* ht = dot_hits_[hover_dot_].first;
            PointF hc = dot_hits_[hover_dot_].second.center();
            if (ht) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%02d. %s", hover_dot_ + 1, ht->title.c_str());
                FontKey tf{12.5f, false};
                float tw = gfx.text_width(buf, tf) + 22.0f;
                RectF tip(std::clamp(hc.x - tw * 0.5f, 40.0f, 1880.0f - tw), hc.y - 38.0f, tw, 24.0f);
                ColorF f = ColorF::rgba(16, 12, 9, 228);
                ColorF b = ColorF::rgba(206, 168, 108, 175);
                gfx.rounded(tip, 6.0f, &f, &b, 1.0f);
                gfx.text_center(tip.cx(), tip.cy() + 5.0f, buf, tf, ColorF::rgba(244, 232, 212, 248));
            }
        }
    }

    // 6. Track title & metadata directly under the card
    float info_alpha = clampf(stage_alpha * (1.0f - bar_anim_ * 0.72f));
    if (info_alpha > 0.02f) {
        FontKey tf{30.0f, true};
        std::string title = gfx.elide_text(t->title, tf, 1100.0f);
        gfx.text_center(card.cx(), card.bottom() + 45.0f, title, tf,
                        ColorF::rgba(246, 237, 224).with_alpha(info_alpha), true);

        char mbuf[256];
        snprintf(mbuf, sizeof(mbuf), "%s · %s   ·   FLAC %d-bit / %.0f kHz",
                 t->act_label.c_str(), t->act_zh.c_str(), t->bits, t->sr / 1000.0);
        gfx.text_center(card.cx(), card.bottom() + 73.0f, mbuf, {14.5f, false, 400, 0.3f},
                        ColorF::rgba(214, 196, 172).with_alpha(info_alpha * 0.92f), true);
    }

    // 7. Footlights visualizer along the bottom
    if (viz == "footlights") {
        spec_.draw_footlights(gfx, 1920.0f, 1080.0f, 0.95f, 180.0f * bar_anim_);
    }
}

void PlayerScreen::paint(Gfx& gfx, double) {
    const Track* t = track_;
    if (!t) {
        gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(4, 3, 3));
        return;
    }

    ID2D1Bitmap1* cg = app_.assets().act_cg(t, 3840);
    std::string viz = app_.settings().get_string("viz", "footlights");

    if (is_card_style()) {
        paint_card_stage(gfx, t, cg, viz);
    } else {
        paint_fullscreen_cg(gfx, t, cg, viz);
    }

    // Top-left act header & buttons
    float top_a = clampf(bar_anim_ * 1.4f);
    if (top_a > 0.03f) {
        gfx.draw_line({58, 68}, {58, 152}, ColorF::rgba(198, 156, 96).with_alpha((210.0f / 255.0f) * bar_anim_), 2.0f);
        gfx.draw_icon_button({67, 75, 42, 42}, "down", back_top_.contains(mouse_),
                             ColorF::rgba(238, 224, 204).with_alpha(top_a));
        gfx.text(122, 92, t->act_label, {19.0f, true}, ColorF::rgba(214, 174, 118).with_alpha(top_a));
        gfx.text(122, 126, t->act_zh, {32.0f, true}, ColorF::rgba(240, 230, 214).with_alpha(top_a), true);
        gfx.text(124, 152, t->act_en, {14.0f, false}, ColorF::rgba(200, 182, 158).with_alpha(top_a * 0.8f));

        auto draw_top_pill = [&](const RectF& r, bool hov, bool dimmed, const char* lbl) {
            gfx.draw_glass_pill(r, 6.0f, false, hov, top_a, dimmed);
            gfx.text_center(r.cx(), r.cy() + 5.0f, lbl, {13.0f, false},
                            ColorF::rgba(244, 232, 212).with_alpha(top_a * (dimmed ? 0.62f : 1.0f)));
        };

        draw_top_pill(src_top_, src_top_.contains(mouse_), false, "打开源文件");

        bool is_title = (app_.settings().get_string("title_track") == t->id);
        bool hov_title = (!is_title && bar_anim_ > 0.35f && title_top_.contains(mouse_));
        draw_top_pill(title_top_, hov_title, is_title, is_title ? "已设为主界面音乐" : "设为主界面音乐");
    }

    layout_bar();
    if (bar_anim_ > 0.01f) {
        paint_bar(gfx, t);
    }

    if (intro_ > 0.0f) {
        float k = intro_;
        float a = (k > 0.7f) ? 1.0f : (k / 0.7f);
        gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(3, 2, 2).with_alpha(a * (is_card_style() ? 0.50f : 0.62f)));
        if (k > 0.25f) {
            gfx.text_center(960, 470, t->act_label, {20.0f, true, 400, 4.0f}, ColorF::rgba(216, 178, 122).with_alpha(a));
            float rule_half = 140.0f * clampf((1.55f - k) * 2.5f + 0.35f);
            gfx.draw_line({960 - rule_half, 498}, {960 + rule_half, 498}, ColorF::rgba(204, 162, 102).with_alpha(a * 0.8f), 1.5f);
            gfx.text_center(960, 556, t->title, {46.0f, true}, ColorF::rgba(244, 234, 218).with_alpha(a), true);
            gfx.text_center(960, 600, t->artist, {18.0f, false}, ColorF::rgba(210, 192, 170).with_alpha(a * 0.9f));
        }
    }

    if (fade_ < 1.0f) {
        float u = 1.0f - fade_;
        gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(0, 0, 0, static_cast<int>(255 * u * u)));
    }
}

void PlayerScreen::paint_bar(Gfx& gfx, const Track* t) {
    RectF b = bar_rect();
    float ba = clampf(bar_anim_ * 1.2f);
    gfx.velvet_panel(b, 6.0f, ba);

    gfx.draw_gold_divider(b.x + 28, b.right() - 28, b.y + 34, ba * 0.72f, 1.0f);
    gfx.draw_line({b.x + 984, b.y + 24}, {b.x + 984, b.bottom() - 24},
                  ColorF::rgba(198, 160, 104, 52).mul_alpha(ba), 1.0f);

    gfx.text(b.x + 34, b.y + 30, t->title, {24.0f, true}, ColorF::rgba(246, 237, 222).mul_alpha(ba));
    FontKey sub_f{14.0f, false};
    std::string chapter = t->act_label + " · " + t->act_zh;
    float sx = b.x + 36.0f, sy = b.y + 58.0f;
    gfx.text(sx, sy, chapter, sub_f, ColorF::rgba(216, 178, 118, 242).mul_alpha(ba));
    sx += gfx.text_width(chapter, sub_f);
    gfx.text(sx, sy, "   ·   " + t->artist + "   ·   " + t->quality(), sub_f,
             ColorF::rgba(192, 174, 154, 228).mul_alpha(ba));

    auto& audio = app_.audio();
    int dur = std::max(1, static_cast<int>(t->duration * 1000.0));
    int pos = (audio.track() && audio.track()->id == t->id) ? audio.position_ms() : 0;
    float frac = dragging_ ? drag_frac_ : clampf(static_cast<float>(pos) / dur);
    int display_pos = dragging_ ? static_cast<int>(drag_frac_ * dur) : pos;

    RectF pr = progress_;
    gfx.draw_line({pr.x, pr.cy()}, {pr.x + pr.w * frac, pr.cy()},
                  ColorF::rgba(226, 184, 118, 32).mul_alpha(ba), 11.0f, true);
    gfx.draw_line({pr.x, pr.cy()}, {pr.right(), pr.cy()}, ColorF::rgba(96, 80, 66, 225).mul_alpha(ba), hover_prog_ ? 4.0f : 3.0f, true);
    gfx.draw_line({pr.x, pr.cy()}, {pr.x + pr.w * frac, pr.cy()}, ColorF::rgba(222, 180, 116, 255).mul_alpha(ba), hover_prog_ ? 4.0f : 3.0f, true);
    float knob_r = (hover_prog_ || dragging_) ? 6.5f : 5.0f;
    PointF knob{pr.x + pr.w * frac, pr.cy()};
    gfx.fill_ellipse(knob, knob_r + 4.0f, knob_r + 4.0f, ColorF::rgba(236, 198, 138, 42).mul_alpha(ba));
    gfx.fill_ellipse(knob, knob_r, knob_r, ColorF::rgba(246, 224, 192).mul_alpha(ba));
    gfx.draw_ellipse(knob, knob_r, knob_r, ColorF::rgba(90, 70, 50, 180).mul_alpha(ba), 1.0f);

    if (hover_prog_ || dragging_) {
        float hf = dragging_ ? drag_frac_ : clampf((mouse_.x - pr.x) / pr.w);
        int hms = static_cast<int>(hf * dur);
        std::string tip = fmt_time(hms);
        float tx = std::clamp(pr.x + pr.w * hf, pr.x + 24.0f, pr.right() - 24.0f);
        RectF tr(tx - 28.0f, pr.y - 30.0f, 56.0f, 22.0f);
        ColorF tf = ColorF::rgba(18, 13, 10, 230).mul_alpha(ba);
        ColorF tb = ColorF::rgba(206, 168, 108, 170).mul_alpha(ba);
        gfx.rounded(tr, 4.0f, &tf, &tb, 1.0f);
        gfx.text_center(tr.cx(), tr.y + 15.0f, tip, {12.0f, false}, ColorF::rgba(240, 226, 204).with_alpha(ba));
    }

    gfx.text(pr.x, b.y + 122, fmt_time(display_pos), {13.0f, false}, ColorF::rgba(206, 190, 170, 232).mul_alpha(ba));
    std::string total = fmt_time(dur);
    gfx.text(pr.right() - gfx.text_width(total, {13.0f, false}), b.y + 122, total, {13.0f, false},
             ColorF::rgba(206, 190, 170, 232).mul_alpha(ba));

    bool playing = audio.playing();
    std::string cur_viz = app_.settings().get_string("viz", "footlights");
    std::string cur_style = app_.settings().get_string("player_style", "card");

    // Divider between viz mode and stage style pills
    gfx.draw_line({b.x + 1534, b.y + 67}, {b.x + 1534, b.y + 101},
                  ColorF::rgba(198, 160, 104, 58).mul_alpha(ba), 1.0f);

    for (const auto& [name, r] : buttons_) {
        if (name == "play") {
            float boost = hover_named(name) ? 1.0f : 0.65f;
            gfx.fill_ellipse(r.center(), 46.0f, 46.0f, ColorF::rgba(236, 192, 120, 16).mul_alpha(ba * boost));
            gfx.fill_ellipse(r.center(), 36.0f, 36.0f, ColorF::rgba(240, 200, 134, 22).mul_alpha(ba * boost));
        }
    }

    auto draw_pill = [&](const RectF& r, bool active, bool hov, const char* lbl) {
        gfx.draw_glass_pill(r, 6.0f, active, hov, ba, false);
        if (active) {
            gfx.text_center(r.cx(), r.y + 19.5f, lbl, {13.5f, false, 600},
                            ColorF::rgba(32, 22, 15, 248).mul_alpha(ba));
        } else {
            gfx.text_center(r.cx(), r.y + 19.5f, lbl, {13.5f, false},
                            ColorF::rgba(238, 224, 204, hov ? 252 : 234).mul_alpha(ba));
        }
    };

    for (const auto& [name, r] : buttons_) {
        if (name == "prev" || name == "play" || name == "next" || name == "volume" || name == "full") {
            std::string icon = name;
            if (name == "play") icon = playing ? "pause" : "play";
            else if (name == "full") icon = app_.is_fullscreen() ? "restore" : "fullscreen";
            else if (name == "volume") icon = (audio.muted() || audio.volume() <= 0.001f) ? "mute" : "volume";
            gfx.draw_icon_button(r, icon, hover_named(name), ColorF::rgba(236, 222, 202).with_alpha(ba));
        } else if (name.rfind("viz:", 0) == 0) {
            int idx = std::stoi(name.substr(4));
            const char* labels[3] = {"脚灯", "光环", "关闭"};
            const char* modes[3] = {"footlights", "halo", "off"};
            draw_pill(r, cur_viz == modes[idx], hover_named(name), labels[idx]);
        } else if (name.rfind("style:", 0) == 0) {
            int idx = std::stoi(name.substr(6));
            const char* labels[2] = {"全屏", "卡牌"};
            const char* styles[2] = {"fullscreen", "card"};
            draw_pill(r, cur_style == styles[idx], hover_named(name), labels[idx]);
        }
    }

    RectF vr = vol_rect_;
    float vol = audio.volume();
    gfx.draw_line({vr.x, vr.cy()}, {vr.right(), vr.cy()}, ColorF::rgba(98, 82, 68, 225).mul_alpha(ba), 2.0f, true);
    gfx.draw_line({vr.x, vr.cy()}, {vr.x + vr.w * vol, vr.cy()}, ColorF::rgba(206, 168, 108, 32).mul_alpha(ba), 6.0f, true);
    gfx.draw_line({vr.x, vr.cy()}, {vr.x + vr.w * vol, vr.cy()}, ColorF::rgba(218, 178, 116, 245).mul_alpha(ba), 2.0f, true);
    PointF vk{vr.x + vr.w * vol, vr.cy()};
    gfx.fill_ellipse(vk, 7.0f, 7.0f, ColorF::rgba(236, 198, 138, 38).mul_alpha(ba));
    gfx.fill_ellipse(vk, 4.0f, 4.0f, ColorF::rgba(246, 224, 192).mul_alpha(ba));
}

void PlayerScreen::mouse_move(PointF pos) {
    mouse_ = pos;
    last_mouse_ = now_sec();
    ui_visible_ = true;
    app_.set_cursor_hidden(false);
    layout_bar();

    hover_dot_ = -1;
    if (is_card_style()) {
        layout_dots();
        for (int i = 0; i < static_cast<int>(dot_hits_.size()); ++i) {
            if (dot_hits_[i].second.contains(pos)) {
                hover_dot_ = i;
                break;
            }
        }
    }

    hover_ui_ = false;
    for (const auto& [n, r] : buttons_) {
        if (r.adjusted(-6, -6, 6, 6).contains(pos)) { hover_ui_ = true; break; }
    }
    hover_prog_ = progress_.adjusted(0, -14, 0, 14).contains(pos);
    if (hover_prog_ || vol_rect_.adjusted(0, -14, 0, 14).contains(pos)) hover_ui_ = true;
    if (RectF(56, 40, 310, 215).contains(pos)) hover_ui_ = true;
    if (back_top_.contains(pos) || src_top_.contains(pos)) hover_ui_ = true;
    if (bar_anim_ > 0.35f && title_top_.contains(pos) && track_ && app_.settings().get_string("title_track") != track_->id) {
        hover_ui_ = true;
    }
    if (pos.y >= 884.0f) hover_ui_ = true;

    if (dragging_) {
        update_drag_frac(pos);
        double now = now_sec();
        if (now - last_drag_seek_ >= 0.14) {
            last_drag_seek_ = now;
            seek_from_frac(drag_frac_);
        }
    }
    if (drag_vol_) set_volume_from_pos(pos);
}

void PlayerScreen::mouse_press(PointF pos, MouseButton btn) {
    if (btn != MouseButton::Left) return;
    last_mouse_ = now_sec();
    mouse_ = pos;
    ui_visible_ = true;
    layout_bar();

    if (back_top_.contains(pos)) {
        app_.back_to_freeplay(track_, true);
        return;
    }
    if (src_top_.contains(pos) && track_) {
        paths::reveal_in_explorer(track_->abs_path());
        return;
    }
    if (bar_anim_ > 0.35f && title_top_.contains(pos) && track_) {
        if (app_.settings().get_string("title_track") != track_->id) {
            app_.settings().set("title_track", track_->id);
        }
        return;
    }
    if (is_card_style()) {
        layout_dots();
        for (const auto& [dt_track, hr] : dot_hits_) {
            if (hr.contains(pos) && dt_track) {
                set_track(dt_track, true);
                return;
            }
        }
    }
    for (const auto& [name, r] : buttons_) {
        if (r.adjusted(-5, -5, 5, 5).contains(pos)) {
            if (name == "prev") step(-1);
            else if (name == "next") step(1);
            else if (name == "play") app_.audio().toggle();
            else if (name == "volume") {
                bool m = !app_.audio().muted();
                app_.audio().set_muted(m);
                app_.settings().set("muted", m);
            } else if (name == "full") {
                app_.toggle_fullscreen();
            } else if (name.rfind("viz:", 0) == 0) {
                int idx = std::stoi(name.substr(4));
                const char* modes[3] = {"footlights", "halo", "off"};
                app_.settings().set("viz", modes[idx]);
            } else if (name.rfind("style:", 0) == 0) {
                int idx = std::stoi(name.substr(6));
                const char* styles[2] = {"fullscreen", "card"};
                app_.settings().set("player_style", styles[idx]);
            }
            return;
        }
    }
    if (progress_.adjusted(0, -14, 0, 14).contains(pos)) {
        dragging_ = true;
        update_drag_frac(pos);
        last_drag_seek_ = now_sec();
        return;
    }
    if (vol_rect_.adjusted(0, -14, 0, 14).contains(pos)) {
        drag_vol_ = true;
        set_volume_from_pos(pos);
        return;
    }
    if (bar_rect().contains(pos) || RectF(56, 40, 600, 130).contains(pos)) return;
    app_.audio().toggle();
}

void PlayerScreen::mouse_release(PointF pos, MouseButton) {
    if (dragging_) {
        update_drag_frac(pos);
        seek_from_frac(drag_frac_);
        dragging_ = false;
    }
    drag_vol_ = false;
}

void PlayerScreen::mouse_wheel(PointF, int delta) {
    float v = clampf(app_.audio().volume() + (delta > 0 ? 0.03f : -0.03f));
    app_.audio().set_volume(v);
    app_.settings().set("volume", static_cast<double>(v));
    ui_visible_ = true;
    last_mouse_ = now_sec();
}

bool PlayerScreen::key_press(int vk, bool ctrl, bool) {
    if (vk == key::Space) {
        app_.audio().toggle();
        ui_visible_ = true;
        last_mouse_ = now_sec();
        return true;
    }
    if (vk == key::Left || vk == key::Right) {
        if (ctrl) step(vk == key::Left ? -1 : 1);
        else app_.audio().seek(app_.audio().position_ms() + (vk == key::Left ? -5000 : 5000));
        return true;
    }
    if (vk == key::Up || vk == key::Down) {
        float v = clampf(app_.audio().volume() + (vk == key::Up ? 0.04f : -0.04f));
        app_.audio().set_volume(v);
        app_.settings().set("volume", static_cast<double>(v));
        return true;
    }
    if (vk == key::Escape) {
        app_.back_to_freeplay(track_, true);
        return true;
    }
    if (vk == 'F') {
        app_.toggle_fullscreen();
        return true;
    }
    if (vk == 'M') {
        bool m = !app_.audio().muted();
        app_.audio().set_muted(m);
        app_.settings().set("muted", m);
        return true;
    }
    if (vk == 'V') {
        const char* modes[3] = {"footlights", "halo", "off"};
        std::string cur = app_.settings().get_string("viz", "footlights");
        int idx = 0;
        for (int i = 0; i < 3; ++i) if (cur == modes[i]) idx = i;
        app_.settings().set("viz", modes[(idx + 1) % 3]);
        return true;
    }
    if (vk == 'C') {
        std::string nxt = is_card_style() ? "fullscreen" : "card";
        app_.settings().set("player_style", nxt);
        return true;
    }
    if (vk == 'O') {
        if (track_) paths::reveal_in_explorer(track_->abs_path());
        return true;
    }
    if (vk == 'T') {
        if (track_ && app_.settings().get_string("title_track") != track_->id) {
            app_.settings().set("title_track", track_->id);
        }
        return true;
    }
    return false;
}

void PlayerScreen::step(int delta) {
    if (const Track* nxt = app_.library().next_track(track_, delta)) {
        set_track(nxt, true);
    }
}

void PlayerScreen::update_drag_frac(PointF pos) {
    drag_frac_ = clampf((pos.x - progress_.x) / std::max(1.0f, progress_.w));
}

void PlayerScreen::seek_from_frac(float f) {
    if (!track_) return;
    app_.audio().seek(static_cast<int>(clampf(f) * track_->duration * 1000.0));
}

void PlayerScreen::set_volume_from_pos(PointF pos) {
    float f = clampf((pos.x - vol_rect_.x) / vol_rect_.w);
    app_.audio().set_volume(f);
    app_.audio().set_muted(false);
    app_.settings().set("volume", static_cast<double>(f));
    app_.settings().set("muted", false);
}

} // namespace np
