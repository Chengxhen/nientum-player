#include "title_screen.hpp"
#include "../core/paths.hpp"

#include <cmath>

namespace np {

TitleScreen::TitleScreen(AppContext& app) : Screen(app) {
    items_ = {
        {"自由选曲", "FREE PLAY", "freeplay"},
        {"CG 鉴赏", "COLLECTION", "gallery"},
        {"设置", "SETTINGS", "settings"},
        {"退出", "QUIT", "quit"},
    };
    for (int i = 0; i < 4; ++i) {
        rects_.push_back({112.0f, 662.0f + i * 64.0f, 380.0f, 56.0f});
    }
    std::uniform_real_distribution<float> ux(0, 1920), uy(-100, 1080), us(8, 24), uz(1.2f, 3.4f), up(0, 6.28f), ua(18, 60);
    for (int i = 0; i < 28; ++i) {
        petals_.push_back({ux(rng_), uy(rng_), us(rng_), uz(rng_), up(rng_), ua(rng_)});
    }
}

void TitleScreen::enter(const Track*, const std::string&, const std::string&, int, bool) {
    t_ = 0.0f;
    hover_full_ = false;
    hover_now_ = false;
    menu_focus_ = static_cast<float>(selected_);

    auto& audio = app_.audio();
    if (!(audio.full_mode() && audio.track() != nullptr)) {
        std::string tid = app_.settings().get_string("title_track");
        const Track* track = tid.empty() ? nullptr : app_.library().track_by_id(tid);
        if (!track) {
            for (const auto& t : app_.library().tracks) {
                if (t.title.find("Liora's Song Unwithering") != std::string::npos) { track = &t; break; }
            }
            if (!track) {
                for (const auto& t : app_.library().tracks) {
                    if (t.title.find("Liora's Song") != std::string::npos) { track = &t; break; }
                }
            }
        }
        if (!track && !app_.library().tracks.empty()) track = &app_.library().tracks[0];
        if (track) audio.play(track, 0, false);
    }

    ensure_animations();
    liora_anim_.set_paused(false);
    aleph_anim_.set_paused(false);
    app_.set_cursor_hidden(false);

    std::string custom = app_.settings().get_string("title_bg");
    if (!bg_built_ || bg_key_ != custom) build_background(custom);
}

void TitleScreen::leave() {
    liora_anim_.set_paused(true);
    aleph_anim_.set_paused(true);
}

void TitleScreen::ensure_animations() {
    if (anim_init_) return;
    anim_init_ = true;
    liora_anim_.start(paths::data_path(L"anim\\liora_idle.webp"), app_.gfx());
    aleph_anim_.start(paths::data_path(L"anim\\aleph_idle.webp"), app_.gfx());
}

void TitleScreen::tick(float dt, double) {
    std::string custom = app_.settings().get_string("title_bg");
    if (!bg_built_ || bg_key_ != custom) build_background(custom);
    t_ += dt;
    dt_last_ = dt;
    std::uniform_real_distribution<float> ux(0, 1920);
    for (auto& p : petals_) {
        p.x += std::sin(t_ * 0.4f + p.ph) * 10.0f * dt;
        p.y += p.spd * dt;
        if (p.y > 1120.0f) {
            p.y = -30.0f;
            p.x = ux(rng_);
        }
    }
    menu_focus_ += (selected_ - menu_focus_) * (1.0f - std::exp(-dt / 0.12f));
    if (std::abs(selected_ - menu_focus_) < 0.002f) {
        menu_focus_ = static_cast<float>(selected_);
    }
}

bool TitleScreen::animating() const {
    return std::abs(menu_focus_ - selected_) > 0.002f || (liora_anim_.valid() && aleph_anim_.valid());
}

void TitleScreen::build_background(const std::string& custom) {
    auto& gfx = app_.gfx();
    auto& assets = app_.assets();
    bg_cache_ = gfx.create_target_bitmap(1920, 1080);
    if (!bg_cache_) return;

    gfx.set_target(bg_cache_.Get());
    gfx.dc()->BeginDraw();
    gfx.dc()->SetTransform(D2D1::Matrix3x2F::Identity());
    gfx.dc()->Clear(D2D1::ColorF(12 / 255.f, 10 / 255.f, 9 / 255.f, 1.0f));

    bool drawn = false;
    if (!custom.empty()) {
        if (ID2D1Bitmap1* pix = assets.full(custom, 1920)) {
            D2D1_SIZE_F sz = pix->GetSize();
            float s = std::max(1920.0f / std::max(1.0f, sz.width), 1080.0f / std::max(1.0f, sz.height));
            float dw = sz.width * s, dh = sz.height * s;
            gfx.draw_bitmap(pix, {(1920.0f - dw) * 0.5f, (1080.0f - dh) * 0.5f, dw, dh}, 1.0f, true);

            Gfx::GradStop gs[4] = {
                {0.00f, ColorF::rgba(4, 3, 3, 195)},
                {0.42f, ColorF::rgba(4, 3, 3, 120)},
                {0.78f, ColorF::rgba(4, 3, 3, 28)},
                {1.00f, ColorF::rgba(4, 3, 3, 0)},
            };
            gfx.fill_linear_grad({0, 0, 1920, 1080}, {0, 0}, {1400, 0}, gs, 4);
            if (ID2D1Bitmap1* light = assets.ui("PZImgTitleBackgroundLight.png", 1920, 1080, "cover")) {
                gfx.draw_bitmap(light, {0, 0, 1920, 1080}, 0.14f, true);
            }
            drawn = true;
        }
    }
    if (!drawn) {
        if (ID2D1Bitmap1* bg = assets.ui("PZImgTitleBackground.png", 1920, 1080, "cover")) {
            gfx.draw_bitmap(bg, {0, 0, 1920, 1080}, 1.0f, true);
        }
        if (ID2D1Bitmap1* light = assets.ui("PZImgTitleBackgroundLight.png", 1920, 1080, "cover")) {
            gfx.draw_bitmap(light, {0, 0, 1920, 1080}, 0.50f, true);
        }
    }

    gfx.set_additive(true);
    Gfx::GradStop rg[3] = {
        {0.00f, ColorF::rgba(255, 214, 156, 36)},
        {0.55f, ColorF::rgba(255, 190, 120, 12)},
        {1.00f, ColorF::rgba(255, 190, 120, 0)},
    };
    gfx.fill_radial_grad_ellipse({980, 500}, 620, 620, rg, 3);
    gfx.set_additive(false);

    gfx.dc()->EndDraw();
    gfx.restore_scene_target();
    bg_key_ = custom;
    bg_built_ = true;
}

void TitleScreen::paint_characters(Gfx& gfx) {
    if (liora_anim_.valid() && aleph_anim_.valid()) {
        const float target_h = 800.0f;
        ID2D1Bitmap1* f_l = liora_anim_.current_bitmap(gfx, dt_last_);
        ID2D1Bitmap1* f_a = aleph_anim_.current_bitmap(gfx, dt_last_);

        struct DrawChar { ID2D1Bitmap1* bmp; float left; float w; };
        DrawChar list[2] = {
            {f_l, 500.0f, f_l ? target_h * liora_anim_.width() / std::max(1, liora_anim_.height()) : 0.0f},
            {f_a, 1150.0f, f_a ? target_h * aleph_anim_.width() / std::max(1, aleph_anim_.height()) : 0.0f},
        };
        for (const auto& dc : list) {
            if (!dc.bmp) continue;
            float cx = dc.left + dc.w * 0.5f;
            Gfx::GradStop sg[2] = {{0.0f, ColorF::rgba(0, 0, 0, 125)}, {1.0f, ColorF::rgba(0, 0, 0, 0)}};
            gfx.fill_radial_grad_ellipse({cx, 984.0f}, dc.w * 0.55f, 26.0f, sg, 2);
        }
        for (const auto& dc : list) {
            if (!dc.bmp) continue;
            gfx.draw_bitmap(dc.bmp, {dc.left, 984.0f - target_h, dc.w, target_h}, 1.0f, true);
        }
        return;
    }

    auto& assets = app_.assets();
    ID2D1Bitmap1* liora = assets.char_static("Liora");
    ID2D1Bitmap1* aleph = assets.char_static("Aleph");
    float float_y = std::sin(t_ * 0.55f) * 2.5f;
    struct StaticChar { ID2D1Bitmap1* bmp; float x; float dy; };
    StaticChar chars[2] = {{liora, 620.0f, float_y}, {aleph, 1080.0f, -float_y}};
    for (const auto& sc : chars) {
        if (!sc.bmp) continue;
        D2D1_SIZE_F sz = sc.bmp->GetSize();
        float h = 840.0f;
        float w = sz.width * (h / std::max(1.0f, sz.height));
        float cx = sc.x + w * 0.5f;
        Gfx::GradStop sg[2] = {{0.0f, ColorF::rgba(0, 0, 0, 125)}, {1.0f, ColorF::rgba(0, 0, 0, 0)}};
        gfx.fill_radial_grad_ellipse({cx, 984.0f}, w * 0.55f, 26.0f, sg, 2);
        gfx.draw_bitmap(sc.bmp, {sc.x, 984.0f - h + sc.dy, w, h}, 1.0f, true);
    }
}

void TitleScreen::paint(Gfx& gfx, double) {
    std::string custom = app_.settings().get_string("title_bg");
    if (bg_cache_) {
        gfx.draw_bitmap(bg_cache_.Get(), {0, 0, 1920, 1080}, 1.0f, true);
    }

    // Gentle warm stage breath in center
    gfx.set_additive(true);
    float breath = 0.5f + 0.5f * std::sin(t_ * 0.9f);
    if (ID2D1Bitmap1* glow = gfx.glow_sprite()) {
        gfx.draw_bitmap(glow, {980 - 520, 480 - 420, 1040, 840}, 0.08f + 0.04f * breath, true);
    }
    gfx.set_additive(false);

    if (custom.empty()) {
        paint_characters(gfx);
    }

    // Floating golden stage petals / dust with subtle bloom
    gfx.set_additive(true);
    ID2D1Bitmap1* glow = gfx.glow_sprite();
    for (const auto& pt : petals_) {
        float a = (pt.alpha * 0.72f) / 255.0f;
        if (glow) {
            float gs = pt.size * 3.4f;
            gfx.draw_bitmap(glow, {pt.x - gs, pt.y - gs, gs * 2.0f, gs * 2.0f}, a * 0.45f, true);
        }
        gfx.save_transform();
        gfx.translate(pt.x, pt.y);
        gfx.rotate(t_ * 25.0f + pt.ph * 57.0f);
        gfx.fill_ellipse({0, 0}, pt.size, pt.size * 0.45f, ColorF::rgba(247, 214, 164).with_alpha(a));
        gfx.restore_transform();
    }
    gfx.set_additive(false);

    auto& assets = app_.assets();
    if (ID2D1Bitmap1* logo = assets.logo(540)) {
        D2D1_SIZE_F lsz = logo->GetSize();
        gfx.draw_bitmap(logo, {78, 82, lsz.width, lsz.height}, 1.0f, true);
        gfx.draw_line({84, 440}, {470, 440}, ColorF::rgba(198, 158, 102, 170), 1.2f);
        gfx.text(88, 472, "MUSIC  &  MEMORY  OF  THE  STAGE",
                 {14.0f, false, 400, 2.4f}, ColorF::rgba(214, 190, 162, 220));
    }

    // Menu focus glow & ornamental frame
    float row_y = 662.0f + menu_focus_ * 64.0f;
    int sel = std::clamp(selected_, 0, static_cast<int>(items_.size()) - 1);
    FontKey zh_font{30.0f, true, 400, 1.2f};
    float zh_w = gfx.text_width(items_[sel].zh, zh_font);
    float text_cx = 112.0f + 190.0f;
    float frame_w = std::max(240.0f, zh_w + 88.0f);
    float frame_x = text_cx - frame_w * 0.5f;

    if (ID2D1Bitmap1* aglow = assets.ui("PZImgActive_glow.png", 340, 220, "contain")) {
        gfx.set_additive(true);
        float pulse = 0.38f + 0.06f * std::sin(t_ * 2.4f);
        gfx.draw_bitmap(aglow, {text_cx - 170.0f, row_y - 90.0f, 340.0f, 220.0f}, pulse, true);
        gfx.set_additive(false);
    }
    if (ID2D1Bitmap1* frame = assets.ui("PZImgframe.png", static_cast<int>(frame_w), 20, "contain")) {
        float asc = 0, desc = 0;
        gfx.font_metrics(zh_font, asc, desc);
        float baseline = 662.0f + sel * 64.0f + 32.0f;
        float text_top = baseline - asc;
        float text_bottom = baseline + desc;
        float gap = 3.0f;
        float fh = 20.0f;
        gfx.draw_bitmap(frame, {frame_x, text_top - gap - fh, frame_w, fh}, 0.92f, true);
        gfx.draw_bitmap_flipped_y(frame, {frame_x, text_bottom + gap, frame_w, fh}, 0.92f);
    }

    ColorF base_col = ColorF::rgba(154, 158, 177, 235);
    ColorF hot_col = ColorF::rgba(246, 240, 232, 255);
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const RectF& r = rects_[i];
        float prox = clampf(1.0f - std::abs(i - menu_focus_));
        ColorF col = mix_color(base_col, hot_col, prox);
        FontKey fk{28.0f + 2.0f * prox, true, 400, 1.2f};
        float w = gfx.text_width(items_[i].zh, fk);
        gfx.text(r.cx() - w * 0.5f, r.y + 32.0f, items_[i].zh, fk, col, prox > 0.35f);
    }

    // Now playing mini card, bottom right (clickable to enter player)
    if (const Track* track = app_.audio().track()) {
        RectF nr = now_rect_;
        ColorF fill = ColorF::rgba(14, 11, 9, hover_now_ ? 230 : 204);
        ColorF border = ColorF::rgba(206, 168, 108, hover_now_ ? 205 : 132);
        gfx.rounded(nr, 6.0f, &fill, &border, hover_now_ ? 1.3f : 1.0f);
        gfx.draw_line({nr.x + 10, nr.y + 1.2f}, {nr.right() - 10, nr.y + 1.2f},
                      ColorF::rgba(255, 242, 218, hover_now_ ? 48 : 22), 1.0f);
        if (ID2D1Bitmap1* cov = assets.cover(track, 34)) {
            gfx.draw_bitmap(cov, {nr.x + 9, nr.y + 9, 34, 34}, 1.0f, true);
            gfx.draw_rect({nr.x + 9, nr.y + 9, 34, 34}, ColorF::rgba(206, 170, 112, 95), 1.0f);
        }
        FontKey tf{14.0f, true};
        std::string tt = gfx.elide_text(track->title, tf, 320.0f);
        gfx.text(nr.x + 52, nr.y + 23, tt, tf, ColorF::rgba(238, 226, 208, 250));
        FontKey af{11.0f, false};
        std::string at = gfx.elide_text(track->artist, af, 320.0f);
        gfx.text(nr.x + 52, nr.y + 41, at, af, ColorF::rgba(188, 168, 144, 225));
        FontKey sf{10.0f, false};
        std::string np_lbl = hover_now_ ? "进入演奏" : "正在播放";
        gfx.text(nr.right() - 14.0f - gfx.text_width(np_lbl, sf), nr.y + 19,
                 np_lbl, sf, ColorF::rgba(218, 182, 122, hover_now_ ? 250 : 225));
    }

    // Fullscreen toggle, top right
    bool full = app_.is_fullscreen();
    gfx.draw_icon_button(fullscreen_rect_, full ? "restore" : "fullscreen", hover_full_,
                         ColorF::rgba(240, 226, 206, 240), 0.0f, full);
    gfx.text_center(fullscreen_rect_.cx(), fullscreen_rect_.bottom() + 17.0f,
                    full ? "窗口" : "全屏", {11.0f, false, 400, 1.0f},
                    ColorF::rgba(214, 194, 170, (hover_full_ || full) ? 205 : 150));

    // Footer
    gfx.text(88, 1034, "F11  全屏    ·    ↑↓  选择    ·    Enter  确认",
             {15.0f, false}, ColorF::rgba(210, 194, 174, 220));
    std::string foot = "Nientum - Op.ZERO      CG & MUSIC GALLERY      v2.0";
    FontKey ff{13.0f, false};
    gfx.text(1920.0f - 88.0f - gfx.text_width(foot, ff), 1034, foot, ff, ColorF::rgba(196, 178, 158, 190));
}

void TitleScreen::mouse_move(PointF pos) {
    hover_full_ = fullscreen_rect_.adjusted(-6, -6, 6, 6).contains(pos);
    hover_now_ = (app_.audio().track() != nullptr) && now_rect_.contains(pos);
    if (hover_full_ || hover_now_) return;
    for (int i = 0; i < static_cast<int>(rects_.size()); ++i) {
        if (rects_[i].contains(pos)) {
            if (i != selected_) {
                selected_ = i;
                app_.audio().play_ui("move");
            }
            return;
        }
    }
}

void TitleScreen::mouse_press(PointF pos, MouseButton btn) {
    if (btn != MouseButton::Left) return;
    if (fullscreen_rect_.adjusted(-6, -6, 6, 6).contains(pos)) {
        app_.toggle_fullscreen();
        return;
    }
    if (app_.audio().track() != nullptr && now_rect_.contains(pos)) {
        app_.audio().play_ui("select");
        app_.open_player(app_.audio().track());
        return;
    }
    for (int i = 0; i < static_cast<int>(rects_.size()); ++i) {
        if (rects_[i].contains(pos)) {
            selected_ = i;
            activate(i);
            return;
        }
    }
}

bool TitleScreen::key_press(int vk, bool, bool) {
    if (vk == key::Up || vk == 'W') {
        selected_ = (selected_ - 1 + static_cast<int>(items_.size())) % static_cast<int>(items_.size());
        app_.audio().play_ui("move");
        return true;
    }
    if (vk == key::Down || vk == 'S') {
        selected_ = (selected_ + 1) % static_cast<int>(items_.size());
        app_.audio().play_ui("move");
        return true;
    }
    if (vk == key::Enter || vk == key::Space) {
        activate(selected_);
        return true;
    }
    return false;
}

void TitleScreen::activate(int idx) {
    app_.audio().play_ui("select");
    const std::string& act = items_[idx].action;
    if (act == "freeplay") app_.show_screen("freeplay", "curtain");
    else if (act == "gallery") app_.open_gallery();
    else if (act == "settings") app_.open_settings();
    else if (act == "quit") app_.quit_app();
}

} // namespace np
