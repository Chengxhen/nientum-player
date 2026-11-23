#include "settings_overlay.hpp"

#include <algorithm>
#include <cstdio>

namespace np {

SettingsOverlay::SettingsOverlay(AppContext& app) : Overlay(app) {
    app_.audio().refresh_devices();
    devices_ = app_.audio().output_devices();
    int dev = app_.settings().get_int("device", -1);
    dev_idx_ = (dev >= 0 && dev < static_cast<int>(devices_.size())) ? dev : -1;
}

void SettingsOverlay::tick(float dt, double) {
    fade_ = std::min(1.0f, fade_ + dt / 0.24f);
    blink_t_ += dt;
}

int SettingsOverlay::current_fps() const {
    return app_.settings().get_int("fps", 120);
}

namespace {
enum FocusItem {
    F_Volume = 0, F_Device, F_Viz, F_Style, F_Blur, F_Preview, F_Duration,
    F_Fps, F_FpsCap, F_Res, F_Bg, F_Music, F_Save, F_Close, F_Count
};
}

RectF SettingsOverlay::focus_rect() const {
    const float px = panel_.x + 44.0f;
    const float pw = panel_.w - 88.0f;
    switch (focus_) {
        case F_Volume:   return {px, row_y_ - 16.0f, pw, 46.0f};
        case F_Device:   return {px, row_y_ + 56.0f, pw, 46.0f};
        case F_Viz:      return {782.0f, 437.0f, 252.0f, 44.0f};
        case F_Style:    return {1118.0f, 437.0f, 244.0f, 44.0f};
        case F_Blur:     return {px, row_y_ + 204.0f, pw, 46.0f};
        case F_Preview:  return {px, row_y_ + 276.0f, pw, 46.0f};
        case F_Duration: return {px, row_y_ + 348.0f, pw, 46.0f};
        case F_Fps:      return fps_box_.adjusted(-7, -7, 7, 7);
        case F_FpsCap:   return fps_cap_.adjusted(-7, -7, 7, 7);
        case F_Res:      return {1128.0f, 707.0f, 296.0f, 46.0f};
        case F_Bg:       return {px, 766.0f, pw, 54.0f};
        case F_Music:    return {px, 820.0f, pw, 54.0f};
        case F_Save:     return save_btn_.adjusted(-8, -8, 8, 8);
        case F_Close:    return close_btn_.adjusted(-8, -8, 8, 8);
    }
    return {};
}

void SettingsOverlay::set_volume_value(float v) {
    v = clampf(v);
    app_.audio().set_volume(v);
    app_.audio().set_muted(false);
    app_.settings().set("volume", static_cast<double>(v));
    app_.settings().set("muted", false);
}

void SettingsOverlay::cycle_device(int dir) {
    if (dir == 0) return;
    int total = static_cast<int>(devices_.size()) + 1; // +1 for -1 (system default)
    int pos = (dev_idx_ < 0) ? 0 : (dev_idx_ + 1);
    pos = ((pos + dir) % total + total) % total;
    dev_idx_ = (pos == 0) ? -1 : (pos - 1);
    app_.audio().set_output_device(dev_idx_);
    app_.settings().set("device", dev_idx_);
}

void SettingsOverlay::cycle_viz(int dir) {
    const char* modes[3] = {"footlights", "halo", "off"};
    std::string cur = app_.settings().get_string("viz", "footlights");
    int idx = 0;
    for (int i = 0; i < 3; ++i) if (cur == modes[i]) idx = i;
    idx = ((idx + dir) % 3 + 3) % 3;
    app_.settings().set("viz", modes[idx]);
}

void SettingsOverlay::cycle_style(int dir) {
    std::string cur = app_.settings().get_string("player_style", "card");
    bool is_card = (cur == "card");
    if (dir == 0) is_card = !is_card;
    else is_card = !is_card;
    app_.settings().set("player_style", is_card ? "card" : "fullscreen");
}

void SettingsOverlay::cycle_duration(int dir) {
    const int durs[3] = {10, 20, 30};
    int cur = app_.settings().get_int("preview_seconds", 20);
    int idx = 1;
    for (int i = 0; i < 3; ++i) if (cur == durs[i]) idx = i;
    idx = ((idx + dir) % 3 + 3) % 3;
    app_.settings().set("preview_seconds", durs[idx]);
}

void SettingsOverlay::cycle_resolution(int dir) {
    const int rvals[5] = {720, 900, 1080, 1440, 2160};
    int cur = app_.settings().get_int("resolution", 1080);
    int idx = 2;
    for (int i = 0; i < 5; ++i) if (cur == rvals[i]) idx = i;
    idx = ((idx + dir) % 5 + 5) % 5;
    app_.settings().set("resolution", rvals[idx]);
}

void SettingsOverlay::toggle_fps_cap(int dir) {
    int cur = current_fps();
    bool cap_on;
    if (dir > 0) cap_on = true;
    else if (dir < 0) cap_on = false;
    else cap_on = (cur > 0);
    int next = cap_on ? 0 : 120;
    app_.settings().set("fps", next);
    app_.apply_fps_setting(next);
}

void SettingsOverlay::reset_title_bg() {
    app_.settings().set("title_bg", "");
    bg_reset_until_ = now_sec() + 2.0;
}

void SettingsOverlay::reset_title_music() {
    app_.settings().set("title_track", "");
    app_.audio().stop();
    const Track* def = nullptr;
    for (const auto& t : app_.library().tracks) {
        if (t.title.find("Liora's Song Unwithering") != std::string::npos) { def = &t; break; }
    }
    if (!def) {
        for (const auto& t : app_.library().tracks) {
            if (t.title.find("Liora's Song") != std::string::npos) { def = &t; break; }
        }
    }
    if (!def && !app_.library().tracks.empty()) def = &app_.library().tracks[0];
    if (def) app_.audio().play(def, 0, false);
    music_reset_until_ = now_sec() + 2.0;
}

void SettingsOverlay::save_settings() {
    commit();
    app_.settings().save();
    save_until_ = now_sec() + 2.0;
}

void SettingsOverlay::adjust_focus(int dir) {
    switch (focus_) {
        case F_Volume:   set_volume_value(app_.audio().volume() + dir * 0.04f); break;
        case F_Device:   cycle_device(dir); break;
        case F_Viz:      cycle_viz(dir); break;
        case F_Style:    cycle_style(dir); break;
        case F_Blur:     app_.settings().set("blur_cg", dir > 0); break;
        case F_Preview:  app_.settings().set("preview_on_select", dir > 0); break;
        case F_Duration: cycle_duration(dir); break;
        case F_Fps: {
            int cur = current_fps();
            int next = (cur <= 0) ? (dir > 0 ? 120 : 0) : std::clamp(cur + dir * 10, 0, 9999);
            app_.settings().set("fps", next);
            app_.apply_fps_setting(next);
            break;
        }
        case F_FpsCap:   toggle_fps_cap(dir); break;
        case F_Res:      cycle_resolution(dir); break;
        default: break;
    }
}

void SettingsOverlay::activate_focus() {
    switch (focus_) {
        case F_Device:   cycle_device(1); break;
        case F_Viz:      cycle_viz(1); break;
        case F_Style:    cycle_style(0); break;
        case F_Blur:     app_.settings().set("blur_cg", !app_.settings().get_bool("blur_cg", true)); break;
        case F_Preview:  app_.settings().set("preview_on_select", !app_.settings().get_bool("preview_on_select", true)); break;
        case F_Duration: cycle_duration(1); break;
        case F_Fps: {
            int cur = current_fps();
            fps_buf_ = (cur <= 0) ? "" : std::to_string(cur);
            fps_replace_ = true;
            fps_editing_ = true;
            break;
        }
        case F_FpsCap:   toggle_fps_cap(0); break;
        case F_Res:      cycle_resolution(1); break;
        case F_Bg:       reset_title_bg(); break;
        case F_Music:    reset_title_music(); break;
        case F_Save:     save_settings(); break;
        case F_Close:    app_.close_overlay(); break;
        default: break;
    }
}

void SettingsOverlay::commit() {
    if (fps_editing_) {
        if (!fps_buf_.empty()) {
            try {
                int v = std::clamp(std::stoi(fps_buf_), 0, 9999);
                app_.settings().set("fps", v);
                app_.apply_fps_setting(v);
            } catch (...) {}
        }
        fps_editing_ = false;
        fps_buf_.clear();
    }
    app_.settings().save();
}

std::vector<RectF> SettingsOverlay::viz_rects() const {
    return {{790, 444, 72, 30}, {872, 444, 72, 30}, {954, 444, 72, 30}};
}

std::vector<RectF> SettingsOverlay::style_rects() const {
    return {{1190, 444, 78, 30}, {1276, 444, 78, 30}};
}

std::vector<RectF> SettingsOverlay::dur_rects() const {
    return {{790, 666, 62, 30}, {862, 666, 62, 30}, {934, 666, 62, 30}};
}

std::vector<RectF> SettingsOverlay::res_rects() const {
    std::vector<RectF> out;
    for (int i = 0; i < 5; ++i) out.push_back({1146.0f + i * 52.0f, 721.0f, 48.0f, 28.0f});
    return out;
}

std::vector<RectF> SettingsOverlay::toggle_rects() const {
    return {{1268, 520, 52, 26}, {1268, 592, 52, 26}};
}

std::pair<RectF, RectF> SettingsOverlay::dev_arrows() const {
    return {{1260, 362, 30, 30}, {1320, 362, 30, 30}};
}

void SettingsOverlay::paint(Gfx& gfx, double) {
    float e = ease_out(fade_);
    gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(0, 0, 0, static_cast<int>(184 * e)));

    RectF panel = panel_;
    gfx.save_transform();
    float sc = 0.94f + 0.06f * e;
    gfx.translate(0.0f, (1.0f - e) * 18.0f);
    gfx.scale(sc, sc, panel.center());
    gfx.fill_rounded({panel.x + 4.0f, panel.y + 12.0f, panel.w, panel.h}, 10.0f, ColorF::rgba(0, 0, 0, static_cast<int>(96 * e)));
    Gfx::GradStop pg[3] = {
        {0.0f, ColorF::rgba(38, 29, 23, 250)},
        {0.38f, ColorF::rgba(25, 19, 15, 250)},
        {1.0f, ColorF::rgba(12, 9, 7, 250)},
    };
    gfx.fill_rounded_linear_grad(panel, 6.0f, {panel.x, panel.y}, {panel.x, panel.bottom()}, pg, 3);
    gfx.draw_rounded(panel, 6.0f, ColorF::rgba(208, 170, 112, 170), 1.2f);
    gfx.draw_rounded(panel.adjusted(7, 7, -7, -7), 4.0f, ColorF::rgba(190, 154, 102, 55), 1.0f);
    gfx.draw_gold_divider(panel.x + 38, panel.right() - 38, panel.y + 1.3f, 0.7f, 1.0f);
    gfx.draw_gold_divider(panel.x + 40, panel.right() - 40, panel.y + 112, 0.85f, 1.0f);
    gfx.fill_ellipse({panel.x + 31, panel.y + 112}, 2.2f, 2.2f, ColorF::rgba(206, 166, 108, 220));

    gfx.text(panel.x + 60, panel.y + 66, "设置", {32.0f, true}, palette::CREAM);
    gfx.text(panel.x + 62, panel.y + 96, "SETTINGS", {13.0f, false, 400, 2.6f}, ColorF::rgba(198, 168, 128, 220));
    gfx.draw_icon_button(close_x_, "close", close_x_.contains(mouse_), ColorF::rgba(230, 216, 196, 240), 32.0f);

    ColorF label_col = ColorF::rgba(210, 194, 174, 245);
    ColorF value_col = ColorF::rgba(232, 218, 198, 245);
    auto label = [&](float by, const char* text) {
        gfx.fill_ellipse({panel.x + 46, by - 6.0f}, 2.2f, 2.2f, ColorF::rgba(206, 166, 108, 225));
        gfx.text(panel.x + 60, by, text, {20.0f, true}, label_col);
    };
    auto pill = [&](const RectF& r, bool act, bool hov, const std::string& text, const FontKey& f) {
        gfx.draw_glass_pill(r, 6.0f, act, hov, 1.0f, false);
        gfx.text_center(r.cx(), r.cy() + 5.0f, text, {f.size, f.serif, act ? 600 : f.weight, f.spacing},
                        act ? ColorF::rgba(34, 24, 18, 255) : ColorF::rgba(232, 218, 198, 245));
    };
    float y = row_y_;

    // Volume
    label(y + 14, "音量");
    float vol = app_.audio().volume();
    RectF vr = vol_rect_;
    gfx.draw_line({vr.x, vr.cy()}, {vr.right(), vr.cy()}, ColorF::rgba(86, 72, 60, 240), 3.0f, true);
    gfx.draw_line({vr.x, vr.cy()}, {vr.x + vr.w * vol, vr.cy()}, ColorF::rgba(214, 172, 110, 255), 3.0f, true);
    gfx.fill_ellipse({vr.x + vr.w * vol, vr.cy()}, 6.0f, 6.0f, ColorF::rgba(242, 220, 188, 255));
    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "%3d%%", static_cast<int>(vol * 100.0f));
    gfx.text(vr.right() + 18, y + 14, vbuf, {16.0f, false}, value_col);

    // Output device
    y += 72.0f;
    label(y + 14, "输出设备");
    std::string desc = (dev_idx_ < 0)
                           ? "系统默认音频设备"
                           : ((dev_idx_ < static_cast<int>(devices_.size())) ? devices_[dev_idx_].name : "未检测到输出设备");
    FontKey df{16.0f, false};
    gfx.text(790, y + 14, gfx.elide_text(desc, df, 420.0f), df, value_col);
    auto [a1, a2] = dev_arrows();
    gfx.draw_icon_button(a1, "left", a1.contains(mouse_), ColorF::rgba(226, 212, 192, 235), 26.0f);
    gfx.draw_icon_button(a2, "right", a2.contains(mouse_), ColorF::rgba(226, 212, 192, 235), 26.0f);

    // Visualization & Player Style
    y += 72.0f;
    label(y + 14, "音频可视化");
    std::string viz = app_.settings().get_string("viz", "footlights");
    const char* vlabels[3] = {"脚灯", "光环", "关闭"};
    const char* vmodes[3] = {"footlights", "halo", "off"};
    auto vrects = viz_rects();
    for (int i = 0; i < 3; ++i) {
        bool act = (viz == vmodes[i]);
        bool hov = vrects[i].adjusted(-2, -2, 2, 2).contains(mouse_);
        pill(vrects[i], act, hov, vlabels[i], {15.0f, false});
    }

    gfx.text(1124, y + 14, "播放样式", {14.0f, false}, ColorF::rgba(176, 158, 138, 225));
    std::string pstyle = app_.settings().get_string("player_style", "card");
    const char* slabels[2] = {"剧场卡牌", "全屏 CG"};
    const char* smodes[2] = {"card", "fullscreen"};
    auto srects = style_rects();
    for (int i = 0; i < 2; ++i) {
        bool act = (pstyle == smodes[i]);
        bool hov = srects[i].adjusted(-2, -2, 2, 2).contains(mouse_);
        pill(srects[i], act, hov, slabels[i], {13.5f, false});
    }

    // Toggles
    y += 76.0f;
    label(y + 14, "悬停 UI 时 CG 柔焦");
    auto trects = toggle_rects();
    draw_toggle(gfx, trects[0], app_.settings().get_bool("blur_cg", true));

    y += 72.0f;
    label(y + 14, "选中曲目时自动试听");
    draw_toggle(gfx, trects[1], app_.settings().get_bool("preview_on_select", true));

    // Preview duration
    y += 72.0f;
    label(y + 14, "试听时长");
    int cur_sec = app_.settings().get_int("preview_seconds", 20);
    if (cur_sec <= 0) cur_sec = 20;
    const int durs[3] = {10, 20, 30};
    auto drects = dur_rects();
    for (int i = 0; i < 3; ++i) {
        bool act = (cur_sec == durs[i]);
        bool hov = drects[i].adjusted(-2, -2, 2, 2).contains(mouse_);
        char dbuf[16];
        snprintf(dbuf, sizeof(dbuf), "%d 秒", durs[i]);
        pill(drects[i], act, hov, dbuf, {15.0f, false});
    }

    // FPS & Render resolution
    y += 72.0f;
    label(y + 14, "界面帧率");
    int cur_fps = current_fps();
    bool hov_box = fps_box_.adjusted(-2, -2, 2, 2).contains(mouse_);
    ColorF fbf = ColorF::rgba(9, 7, 5, fps_editing_ ? 238 : 195);
    ColorF fbb = ColorF::rgba(218, 178, 116, (hov_box || fps_editing_) ? 235 : 135);
    gfx.rounded(fps_box_, 5.0f, &fbf, &fbb, 1.2f);
    gfx.draw_line({fps_box_.x + 6, fps_box_.y + 1.2f}, {fps_box_.right() - 6, fps_box_.y + 1.2f},
                  ColorF::rgba(255, 241, 216, (hov_box || fps_editing_) ? 42 : 20), 1.0f);
    std::string val = fps_editing_ ? fps_buf_ : (cur_fps <= 0 ? "无上限" : std::to_string(cur_fps));
    FontKey vfk{20.0f, false};
    gfx.text(fps_box_.x + 14, fps_box_.cy() + 7, val, vfk,
             (fps_editing_ || cur_fps > 0) ? ColorF::rgba(242, 228, 206, 250) : ColorF::rgba(206, 178, 136, 245));
    if (fps_editing_ && (static_cast<int>(blink_t_ * 2.4f) % 2 == 0)) {
        float tw = gfx.text_width(val, vfk);
        float cx = fps_box_.x + 14 + tw + 2;
        gfx.draw_line({cx, fps_box_.y + 8}, {cx, fps_box_.bottom() - 8}, ColorF::rgba(238, 198, 130, 230), 1.5f);
    }
    gfx.text(fps_box_.right() + 14, fps_box_.cy() + 5, "fps", {13.0f, false}, ColorF::rgba(186, 168, 146, 235));

    bool cap_act = (cur_fps <= 0);
    bool cap_hov = fps_cap_.adjusted(-2, -2, 2, 2).contains(mouse_);
    pill(fps_cap_, cap_act, cap_hov, "无上限", {14.0f, false});

    gfx.text(1124, y + 14, "渲染", {13.0f, false}, ColorF::rgba(168, 150, 132, 215));
    int cur_res = app_.settings().get_int("resolution", 1080);
    const int rvals[5] = {720, 900, 1080, 1440, 2160};
    const char* rlbls[5] = {"720p", "900p", "1080p", "1440p", "4K"};
    auto rrects = res_rects();
    for (int i = 0; i < 5; ++i) {
        bool act = (cur_res == rvals[i]);
        bool hov = rrects[i].adjusted(-2, -2, 2, 2).contains(mouse_);
        pill(rrects[i], act, hov, rlbls[i], {11.0f, false});
    }

    // Title BG reset
    label(804, "主界面背景");
    std::string custom_bg = app_.settings().get_string("title_bg");
    std::string bg_name = "默认";
    if (!custom_bg.empty()) {
        std::string tmp = custom_bg;
        std::replace(tmp.begin(), tmp.end(), '\\', '/');
        auto slash = tmp.rfind('/');
        bg_name = (slash != std::string::npos) ? tmp.substr(slash + 1) : tmp;
    }
    FontKey bgf{14.0f, false};
    bg_name = gfx.elide_text(bg_name, bgf, 240.0f, ElideMode::Middle);
    RectF bg_box(790, 786, 262, 34);
    ColorF bxf = ColorF::rgba(9, 7, 5, 195);
    ColorF bxb = ColorF::rgba(180, 150, 105, 135);
    gfx.rounded(bg_box, 5.0f, &bxf, &bxb, 1.0f);
    gfx.draw_line({bg_box.x + 6, bg_box.y + 1.2f}, {bg_box.right() - 6, bg_box.y + 1.2f},
                  ColorF::rgba(255, 241, 216, 20), 1.0f);
    gfx.text(bg_box.x + 12, bg_box.cy() + 5, bg_name, bgf, ColorF::rgba(224, 210, 190, 240));
    if (bg_reset_until_ > 0.0 && now_sec() < bg_reset_until_) {
        gfx.text(bg_box.right() + 12, bg_box.cy() + 5, "已重置", {12.0f, false}, ColorF::rgba(206, 176, 120, 240));
    } else {
        gfx.draw_paper_button(bg_reset_, "重置背景", {14.0f, false},
                              bg_reset_.adjusted(-2, -2, 2, 2).contains(mouse_), false, !custom_bg.empty());
    }

    // Title music reset
    label(852, "主界面音乐");
    std::string tid = app_.settings().get_string("title_track");
    const Track* tr = tid.empty() ? nullptr : app_.library().track_by_id(tid);
    std::string mname = tr ? tr->title : "默认（Liora's Song Unwithering）";
    FontKey mf{14.0f, false};
    mname = gfx.elide_text(mname, mf, 240.0f, ElideMode::Right);
    RectF mbox(790, 840, 262, 34);
    gfx.rounded(mbox, 5.0f, &bxf, &bxb, 1.0f);
    gfx.draw_line({mbox.x + 6, mbox.y + 1.2f}, {mbox.right() - 6, mbox.y + 1.2f},
                  ColorF::rgba(255, 241, 216, 20), 1.0f);
    gfx.text(mbox.x + 12, mbox.cy() + 5, mname, mf, ColorF::rgba(224, 210, 190, 240));
    if (music_reset_until_ > 0.0 && now_sec() < music_reset_until_) {
        gfx.text(mbox.right() + 12, mbox.cy() + 5, "已重置", {12.0f, false}, ColorF::rgba(206, 176, 120, 240));
    } else {
        gfx.draw_paper_button(music_reset_, "重置音乐", {14.0f, false},
                              music_reset_.adjusted(-2, -2, 2, 2).contains(mouse_), false, !tid.empty());
    }

    gfx.text(panel.x + 60, 892, "↑↓ 选择  ·  ←→ 调整  ·  Enter 确认  ·  Esc 关闭  ·  F3 实时帧率",
             {12.5f, false}, ColorF::rgba(184, 166, 146, 232));
    gfx.draw_paper_button(save_btn_, "保 存", {19.0f, false}, save_btn_.contains(mouse_), true);
    gfx.draw_paper_button(close_btn_, "关 闭", {20.0f, false}, close_btn_.contains(mouse_));

    if (save_until_ > 0.0 && now_sec() < save_until_) {
        gfx.text(save_btn_.x + 6, save_btn_.y - 6, "已保存", {13.0f, false}, ColorF::rgba(206, 176, 120, 240));
    }

    if (show_focus_ && !fps_editing_) {
        RectF fr = focus_rect();
        if (!fr.empty()) {
            gfx.fill_rounded(fr.adjusted(-2, -2, 2, 2), 9.0f, ColorF::rgba(236, 190, 120, 10));
            gfx.draw_rounded(fr, 9.0f, ColorF::rgba(230, 192, 132, 145), 1.4f);
        }
    }
    gfx.restore_transform();
}

void SettingsOverlay::draw_toggle(Gfx& gfx, const RectF& r, bool on) {
    ColorF f = on ? ColorF::rgba(190, 150, 94, 230) : ColorF::rgba(42, 34, 28, 230);
    ColorF b = ColorF::rgba(206, 170, 112, 220);
    gfx.rounded(r, r.h * 0.5f, &f, &b, 1.0f);
    gfx.draw_line({r.x + 6, r.y + 1.2f}, {r.right() - 6, r.y + 1.2f},
                  ColorF::rgba(255, 241, 216, on ? 34 : 18), 1.0f);
    float cx = on ? (r.right() - r.h * 0.5f) : (r.x + r.h * 0.5f);
    gfx.fill_ellipse({cx, r.cy()}, r.h * 0.5f - 4.0f, r.h * 0.5f - 4.0f,
                     on ? ColorF::rgba(246, 232, 212, 255) : ColorF::rgba(158, 144, 128, 255));
}

void SettingsOverlay::mouse_move(PointF pos) {
    mouse_ = pos;
    show_focus_ = false;
    if (drag_vol_) set_vol(pos);
}

void SettingsOverlay::mouse_press(PointF pos, MouseButton btn) {
    if (btn != MouseButton::Left) return;
    mouse_ = pos;
    show_focus_ = false;
    if (fps_editing_ && !fps_box_.contains(pos)) {
        commit();
    }

    if (close_x_.contains(pos) || close_btn_.contains(pos)) {
        app_.close_overlay();
        return;
    }
    if (!panel_.contains(pos)) {
        app_.close_overlay();
        return;
    }

    if (fps_box_.contains(pos)) {
        int cur = current_fps();
        fps_buf_ = (cur <= 0) ? "" : std::to_string(cur);
        fps_replace_ = true;
        fps_editing_ = true;
        focus_ = F_Fps;
        return;
    }
    if (fps_cap_.contains(pos)) {
        focus_ = F_FpsCap;
        toggle_fps_cap(0);
        return;
    }
    const int rvals[5] = {720, 900, 1080, 1440, 2160};
    auto rrects = res_rects();
    for (int i = 0; i < 5; ++i) {
        if (rrects[i].contains(pos)) {
            app_.settings().set("resolution", rvals[i]);
            focus_ = F_Res;
            return;
        }
    }
    if (music_reset_.contains(pos)) {
        focus_ = F_Music;
        reset_title_music();
        return;
    }
    if (bg_reset_.contains(pos)) {
        focus_ = F_Bg;
        reset_title_bg();
        return;
    }
    if (save_btn_.contains(pos)) {
        focus_ = F_Save;
        save_settings();
        return;
    }
    if (vol_rect_.adjusted(0, -14, 0, 14).contains(pos)) {
        focus_ = F_Volume;
        drag_vol_ = true;
        set_vol(pos);
        return;
    }
    auto [da1, da2] = dev_arrows();
    if (da1.contains(pos) || da2.contains(pos)) {
        focus_ = F_Device;
        cycle_device(da1.contains(pos) ? -1 : 1);
        return;
    }
    const char* vmodes[3] = {"footlights", "halo", "off"};
    auto vrects = viz_rects();
    for (int i = 0; i < 3; ++i) {
        if (vrects[i].contains(pos)) {
            app_.settings().set("viz", vmodes[i]);
            focus_ = F_Viz;
            return;
        }
    }
    const char* smodes[2] = {"card", "fullscreen"};
    auto srects = style_rects();
    for (int i = 0; i < 2; ++i) {
        if (srects[i].contains(pos)) {
            app_.settings().set("player_style", smodes[i]);
            focus_ = F_Style;
            return;
        }
    }
    auto toggles = toggle_rects();
    if (toggles[0].contains(pos)) {
        app_.settings().set("blur_cg", !app_.settings().get_bool("blur_cg", true));
        focus_ = F_Blur;
        return;
    }
    if (toggles[1].contains(pos)) {
        app_.settings().set("preview_on_select", !app_.settings().get_bool("preview_on_select", true));
        focus_ = F_Preview;
        return;
    }
    const int durs[3] = {10, 20, 30};
    auto drects = dur_rects();
    for (int i = 0; i < 3; ++i) {
        if (drects[i].contains(pos)) {
            app_.settings().set("preview_seconds", durs[i]);
            focus_ = F_Duration;
            return;
        }
    }
}

void SettingsOverlay::mouse_release(PointF, MouseButton) {
    drag_vol_ = false;
}

void SettingsOverlay::set_vol(PointF pos) {
    set_volume_value(clampf((pos.x - vol_rect_.x) / vol_rect_.w));
}

bool SettingsOverlay::key_press(int vk, bool ctrl, bool shift) {
    if (fps_editing_) {
        if ((vk >= '0' && vk <= '9') || (vk >= 0x60 && vk <= 0x69)) {
            char ch = static_cast<char>('0' + (vk >= 0x60 ? vk - 0x60 : vk - '0'));
            if (fps_replace_) {
                fps_buf_ = std::string(1, ch);
                fps_replace_ = false;
            } else if (fps_buf_.size() < 4) {
                fps_buf_.push_back(ch);
            }
            return true;
        }
        if (vk == key::Backspace) {
            if (fps_replace_) { fps_buf_.clear(); fps_replace_ = false; }
            else if (!fps_buf_.empty()) fps_buf_.pop_back();
            return true;
        }
        if (vk == key::Enter || vk == key::Tab) { commit(); return true; }
        if (vk == key::Escape) { commit(); app_.close_overlay(); return true; }
        return true;
    }

    if (ctrl && (vk == 'S' || vk == 's')) { save_settings(); return true; }
    if (vk == key::Escape) { app_.close_overlay(); return true; }
    show_focus_ = true;
    if (vk == key::Up || (vk == key::Tab && shift)) {
        focus_ = (focus_ - 1 + F_Count) % F_Count;
        return true;
    }
    if (vk == key::Down || (vk == key::Tab && !shift)) {
        focus_ = (focus_ + 1) % F_Count;
        return true;
    }
    if (vk == key::Left) { adjust_focus(-1); return true; }
    if (vk == key::Right) { adjust_focus(1); return true; }
    if (vk == key::Enter || vk == key::Space) { activate_focus(); return true; }
    return true;
}

} // namespace np
