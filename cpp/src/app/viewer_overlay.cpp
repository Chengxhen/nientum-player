#include "viewer_overlay.hpp"
#include "../core/paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace np {

GalleryViewer::GalleryViewer(AppContext& app, const std::string& category, int index,
                             const std::vector<GalleryItem>* items, const std::string& label)
    : Overlay(app), category_(category) {
    if (items) {
        items_ = *items;
    } else {
        auto it = app_.library().gallery.find(category);
        if (it != app_.library().gallery.end()) items_ = it->second;
    }
    index_ = items_.empty() ? 0 : std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
    if (!label.empty()) {
        label_ = label;
    } else if (category == "curtain") label_ = "谢幕 CG";
    else if (category == "original") label_ = "CG 原画";
    else if (category == "story") label_ = "剧情 CG";
    else if (category == "dynamic") label_ = "动态 CG";
    else if (category == "epilogue") label_ = "尾声";
    else if (category == "tachie") label_ = "立绘";
    else if (category == "diff") label_ = "差分";
    else label_ = "鉴赏";

    load_current();
}

void GalleryViewer::load_current() {
    anim_.stop();
    is_anim_ = false;
    if (items_.empty()) { pix_ = nullptr; return; }
    const std::string& p = items_[index_].path;
    bool is_webp = p.size() >= 5 &&
        (p.compare(p.size() - 5, 5, ".webp") == 0 || p.compare(p.size() - 5, 5, ".WEBP") == 0);
    if (is_webp && anim_.start(items_[index_].abs_path(), app_.gfx())) {
        is_anim_ = true;
        pix_ = anim_.current_bitmap(app_.gfx(), 0.0);
        if (!pix_) pix_ = app_.assets().thumb(&items_[index_], 640, 360);
    } else {
        pix_ = app_.assets().full(p, 3840);
    }
    zoom_ = target_zoom_ = 1.0f;
    pan_ = target_pan_ = {0.0f, 0.0f};
}

void GalleryViewer::tick(float dt, double now) {
    t_ += dt;
    if (is_anim_) {
        if (auto* bm = anim_.current_bitmap(app_.gfx(), dt * anim_speed_)) pix_ = bm;
    }
    fade_ = std::min(1.0f, fade_ + dt / 0.34f);
    step_anim_ = std::min(1.0f, step_anim_ + dt / 0.34f);

    float imm_target = immersive_ ? 1.0f : 0.0f;
    immersive_anim_ += (imm_target - immersive_anim_) * (1.0f - std::exp(-dt / 0.14f));
    if (std::abs(imm_target - immersive_anim_) < 0.002f) immersive_anim_ = imm_target;

    float k = 1.0f - std::exp(-dt / 0.085f);
    if (std::abs(target_zoom_ - zoom_) > 0.0005f) zoom_ += (target_zoom_ - zoom_) * k;
    else zoom_ = target_zoom_;
    if (std::abs(target_pan_.x - pan_.x) > 0.1f) pan_.x += (target_pan_.x - pan_.x) * k;
    else pan_.x = target_pan_.x;
    if (std::abs(target_pan_.y - pan_.y) > 0.1f) pan_.y += (target_pan_.y - pan_.y) * k;
    else pan_.y = target_pan_.y;
}

bool GalleryViewer::animating() const {
    return is_anim_ || fade_ < 0.999f || step_anim_ < 0.999f ||
           std::abs((immersive_ ? 1.0f : 0.0f) - immersive_anim_) > 0.002f ||
           std::abs(target_zoom_ - zoom_) > 0.001f ||
           std::abs(target_pan_.x - pan_.x) > 0.2f ||
           std::abs(target_pan_.y - pan_.y) > 0.2f;
}

RectF GalleryViewer::content_rect() const {
    if (category_ == "tachie" || category_ == "diff") return {610, 100, 700, 860};
    return {80, 136, 1760, 820};
}

RectF GalleryViewer::fit_rect() const {
    if (items_.empty() || !pix_) return {};
    RectF cr = content_rect();
    D2D1_SIZE_F sz = pix_->GetSize();
    float s = std::min(cr.w / std::max(1.0f, sz.width), cr.h / std::max(1.0f, sz.height));
    float w = sz.width * s, h = sz.height * s;
    return {cr.cx() - w * 0.5f, cr.cy() - h * 0.5f, w, h};
}

RectF GalleryViewer::image_rect() const {
    RectF base = fit_rect();
    if (base.empty()) return base;
    float w = base.w * zoom_;
    float h = base.h * zoom_;
    float cx = base.cx() + pan_.x;
    float cy = base.cy() + pan_.y;
    return {cx - w * 0.5f, cy - h * 0.5f, w, h};
}

RectF GalleryViewer::immersive_fit(ID2D1Bitmap1* pix) const {
    if (!pix) return {};
    D2D1_SIZE_F sz = pix->GetSize();
    float s = std::min(1920.0f / std::max(1.0f, sz.width), 1080.0f / std::max(1.0f, sz.height));
    float w = sz.width * s, h = sz.height * s;
    return {960.0f - w * 0.5f, 540.0f - h * 0.5f, w, h};
}

void GalleryViewer::paint_immersive(Gfx& gfx) {
    gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(0, 0, 0).with_alpha(fade_));
    float e = ease_out(step_anim_);
    if (prev_pix_ && e < 0.999f) {
        gfx.draw_bitmap(prev_pix_, immersive_fit(prev_pix_), std::max(0.0f, (1.0f - e) * fade_), true);
    }
    if (pix_) {
        gfx.draw_bitmap(pix_, immersive_fit(pix_), fade_, true);
    }
}

void GalleryViewer::paint(Gfx& gfx, double) {
    float fe = ease_out(fade_);
    if (immersive_anim_ > 0.998f) {
        paint_immersive(gfx);
        return;
    }

    float ui_a = fe * (1.0f - immersive_anim_);
    gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(4, 3, 3, static_cast<int>((238.0f + 17.0f * immersive_anim_) * fe)));
    Gfx::GradStop rg[2] = {{0.0f, ColorF::rgba(0, 0, 0, 0)}, {1.0f, ColorF::rgba(0, 0, 0, static_cast<int>(145 * ui_a))}};
    gfx.fill_radial_grad_rect({0, 0, 1920, 1080}, {960, 520}, 1000, 1000, rg, 2);

    Gfx::GradStop gt[2] = {{0.0f, ColorF::rgba(4, 3, 3, static_cast<int>(190 * ui_a))}, {1.0f, ColorF::rgba(4, 3, 3, 0)}};
    gfx.fill_linear_grad({0, 0, 1920, 170}, {0, 0}, {0, 170}, gt, 2);
    Gfx::GradStop gb[2] = {{0.0f, ColorF::rgba(4, 3, 3, 0)}, {1.0f, ColorF::rgba(4, 3, 3, static_cast<int>(210 * ui_a))}};
    gfx.fill_linear_grad({0, 900, 1920, 180}, {0, 900}, {0, 1080}, gb, 2);

    if (items_.empty()) {
        gfx.text_center(960, 540, "没有可显示的内容", {24.0f, true}, palette::CREAM.with_alpha(fe));
        return;
    }

    const GalleryItem& item = items_[index_];
    RectF content = content_rect();
    RectF fit = fit_rect();
    RectF img = image_rect();
    if (immersive_anim_ > 0.001f && pix_) {
        RectF imm = immersive_fit(pix_);
        img = {
            img.x + (imm.x - img.x) * immersive_anim_,
            img.y + (imm.y - img.y) * immersive_anim_,
            img.w + (imm.w - img.w) * immersive_anim_,
            img.h + (imm.h - img.h) * immersive_anim_,
        };
        content = {
            content.x + (0.0f - content.x) * immersive_anim_,
            content.y + (0.0f - content.y) * immersive_anim_,
            content.w + (1920.0f - content.w) * immersive_anim_,
            content.h + (1080.0f - content.h) * immersive_anim_,
        };
    }

    gfx.push_clip(content);
    if (category_ == "tachie" || category_ == "diff") {
        Gfx::GradStop cg[3] = {
            {0.0f, ColorF::rgba(255, 226, 176, static_cast<int>(45 * ui_a))},
            {0.6f, ColorF::rgba(180, 130, 80, static_cast<int>(16 * ui_a))},
            {1.0f, ColorF::rgba(0, 0, 0, 0)},
        };
        gfx.fill_radial_grad_ellipse({content.cx(), content.y + 330.0f}, 430.0f, 490.0f, cg, 3);
    } else if (!fit.empty() && ui_a > 0.02f) {
        gfx.fill_rect(fit.adjusted(-16, -16, 16, 16), ColorF::rgba(12, 9, 8, static_cast<int>(180 * ui_a)));
        gfx.draw_rect(fit.adjusted(-14, -14, 14, 14), ColorF::rgba(172, 142, 98, static_cast<int>(130 * ui_a)), 1.2f);
        gfx.draw_corner_filigree(fit.adjusted(-20, -20, 20, 20), 20.0f,
                                 ColorF::rgba(222, 186, 126, static_cast<int>(160 * ui_a)), 1.5f);
    }

    float e = ease_out(step_anim_);
    if (e < 0.999f && prev_pix_ && !prev_rect_.empty()) {
        float dx = step_dir_ * 56.0f;
        float sc_out = 1.0f + 0.02f * e;
        gfx.save_transform();
        gfx.scale(sc_out, sc_out, prev_rect_.center());
        gfx.draw_bitmap(prev_pix_, prev_rect_.translated(-dx * e, 0.0f), std::max(0.0f, (1.0f - e) * fe), true);
        gfx.restore_transform();
    }
    if (pix_ && !img.empty()) {
        float dx = step_dir_ * 56.0f;
        float sc_in = (step_anim_ < 0.999f) ? (0.972f + 0.028f * e) : (0.955f + 0.045f * fe);
        gfx.save_transform();
        gfx.scale(sc_in, sc_in, img.center());
        gfx.draw_bitmap(pix_, img.translated(dx * (1.0f - e), 0.0f), fe, true);
        gfx.restore_transform();
    }
    gfx.pop_clip();

    if (ui_a <= 0.01f) return;
    float fade_ = ui_a;

    // Top-left info
    gfx.draw_line({64, 44}, {64, 106}, ColorF::rgba(198, 156, 96, static_cast<int>(200 * fade_)), 1.6f);
    gfx.text(84, 62, label_, {15.0f, false, 400, 2.8f}, ColorF::rgba(206, 170, 118).with_alpha(fade_));
    FontKey tf{30.0f, true};
    gfx.text(84, 98, gfx.elide_text(item.title, tf, 1080.0f), tf, ColorF::rgba(242, 232, 216).with_alpha(fade_), true);
    char sbuf[128];
    if (!item.act.empty()) snprintf(sbuf, sizeof(sbuf), "%s   ·   %d × %d", item.act.c_str(), item.w, item.h);
    else snprintf(sbuf, sizeof(sbuf), "%d × %d", item.w, item.h);
    gfx.text(86, 126, sbuf, {14.0f, false}, ColorF::rgba(196, 178, 156).with_alpha(fade_ * 0.8f));

    // Index pill & close
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "%d / %zu", index_ + 1, items_.size());
    RectF pill(1654, 54, 132, 36);
    gfx.draw_glass_pill(pill, 18.0f, false, false, fade_, false);
    gfx.fill_ellipse({pill.x + 13, pill.cy()}, 2.0f, 2.0f,
                     ColorF::rgba(214, 174, 112, static_cast<int>(235 * fade_)));
    gfx.text_center(pill.cx() + 4, pill.cy() + 6, cbuf, {16.0f, false}, ColorF::rgba(238, 224, 204).with_alpha(fade_));
    gfx.draw_icon_button({1814, 50, 46, 46}, "close", hover_ == "close",
                         ColorF::rgba(236, 222, 202).with_alpha(fade_), 40.0f);

    // Bottom bar
    gfx.velvet_panel(bar_, 6.0f, fade_);
    gfx.draw_icon_button(prev_btn_, "left", hover_ == "prev", ColorF::rgba(236, 222, 202).with_alpha(fade_), 34.0f);
    gfx.draw_icon_button(next_btn_, "right", hover_ == "next", ColorF::rgba(236, 222, 202).with_alpha(fade_), 34.0f);
    gfx.draw_line({188, bar_.y + 9}, {188, bar_.bottom() - 9}, ColorF::rgba(174, 140, 92, static_cast<int>(75 * fade_)), 1.0f);

    gfx.text_center(is_anim_ ? 545.0f : 820.0f, bar_.y + 30, "滚轮缩放  ·  拖动平移  ·  双击复位  ·  ←→ 切换  ·  ESC 关闭  ·  I 无UI",
                    {13.0f, false}, ColorF::rgba(208, 192, 172).with_alpha(fade_ * 0.85f));

    auto draw_bar_btn = [&](const RectF& r, const std::string& txt, const std::string& key_id, bool dim = false) {
        bool hov = (hover_ == key_id);
        gfx.draw_glass_pill(r, 6.0f, false, hov && !dim, fade_, dim);
        gfx.text_center(r.cx(), r.cy() + 6, txt, {14.0f, false},
                        ColorF::rgba(242, 230, 210).with_alpha(fade_ * (dim ? 0.65f : 1.0f)));
    };
    if (is_anim_) {
        draw_bar_btn(spd_dec_btn_, "−", "spd_dec", anim_speed_ <= 0.26f);
        char sbuf[24];
        snprintf(sbuf, sizeof(sbuf), "速度 %.2fx", anim_speed_);
        draw_bar_btn(spd_rst_btn_, sbuf, "spd_rst", false);
        draw_bar_btn(spd_inc_btn_, "+", "spd_inc", anim_speed_ >= 1.99f);
    }
    draw_bar_btn(src_btn_, "源文件", "src");
    draw_bar_btn(zoom_out_btn_, "−", "zoom_out");
    draw_bar_btn(zoom_in_btn_, "+", "zoom_in");
    char zbuf[16];
    snprintf(zbuf, sizeof(zbuf), "%d%%", static_cast<int>(std::lround(zoom_ * 100.0f)));
    gfx.text_center(1322, bar_.y + 29, zbuf, {14.0f, false}, ColorF::rgba(216, 198, 176).with_alpha(fade_ * 0.9f));

    bool is_bg = (app_.settings().get_string("title_bg") == item.path);
    draw_bar_btn(use_bg_btn_, is_bg ? "已是主界面" : "设为主界面", "use_bg", is_bg);

    bool hov_imm = (hover_ == "immersive");
    gfx.draw_glass_pill(immerse_btn_, 6.0f, false, hov_imm, fade_, false);
    gfx.draw_icon("fullscreen", {immerse_btn_.x + 10, immerse_btn_.cy() - 8, 16, 16},
                  ColorF::rgba(242, 230, 210).with_alpha(fade_), 1.6f);
    gfx.text(immerse_btn_.x + 34, immerse_btn_.cy() + 6, "全屏无UI", {14.0f, false},
             ColorF::rgba(242, 230, 210).with_alpha(fade_));

    if (!item.variant.empty()) {
        gfx.text_center(960, bar_.y - 16, item.variant, {13.0f, false}, ColorF::rgba(208, 168, 112).with_alpha(fade_));
    }
    if (bg_toast_until_ > now_sec()) {
        RectF toast(760, 890, 400, 42);
        ColorF tf2 = ColorF::rgba(18, 13, 10, 225);
        ColorF tb2 = ColorF::rgba(202, 164, 106, 170);
        gfx.rounded(toast, 8.0f, &tf2, &tb2, 1.0f);
        gfx.draw_line({toast.x + 10, toast.y + 1.2f}, {toast.right() - 10, toast.y + 1.2f},
                      ColorF::rgba(255, 241, 216, 34), 1.0f);
        gfx.text_center(toast.cx(), toast.cy() + 6, "已设为主界面背景", {14.0f, false}, ColorF::rgba(238, 222, 200, 245));
    }
}

void GalleryViewer::clamp_pan() {
    if (target_zoom_ <= 1.001f) {
        target_pan_ = {0, 0};
        return;
    }
    RectF base = fit_rect();
    RectF cr = content_rect();
    if (base.empty()) return;
    float hw = base.w * target_zoom_ * 0.5f;
    float hh = base.h * target_zoom_ * 0.5f;
    float max_x = std::max(0.0f, hw - cr.w * 0.25f);
    float max_y = std::max(0.0f, hh - cr.h * 0.25f);
    target_pan_.x = std::clamp(target_pan_.x, -max_x, max_x);
    target_pan_.y = std::clamp(target_pan_.y, -max_y, max_y);
}

void GalleryViewer::zoom_by(float factor) {
    if (target_zoom_ <= 1.001f && factor > 1.0f) {
        target_zoom_ = clampf(factor, 1.0f, 4.0f);
        target_pan_ = {0, 0};
        return;
    }
    target_zoom_ = clampf(target_zoom_ * factor, 1.0f, 4.0f);
    clamp_pan();
}

void GalleryViewer::step(int d) {
    if (items_.empty()) return;
    if (pix_) {
        prev_pix_ = pix_;
        prev_rect_ = image_rect();
    } else {
        prev_pix_ = nullptr;
        prev_rect_ = {};
    }
    int n = static_cast<int>(items_.size());
    index_ = ((index_ + d) % n + n) % n;
    step_dir_ = (d > 0) ? 1 : -1;
    step_anim_ = 0.0f;
    load_current();
}

void GalleryViewer::mouse_move(PointF pos) {
    mouse_ = pos;
    hover_.clear();
    if (immersive_) {
        // Truly chrome-free: no return button, no transient UI.  A click
        // anywhere closes the viewer.
        app_.set_cursor_hidden(true);
        return;
    }
    if (RectF(1814, 50, 46, 46).adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "close";
    else if (prev_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "prev";
    else if (next_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "next";
    else if (is_anim_ && spd_dec_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "spd_dec";
    else if (is_anim_ && spd_rst_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "spd_rst";
    else if (is_anim_ && spd_inc_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "spd_inc";
    else if (src_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "src";
    else if (zoom_out_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "zoom_out";
    else if (zoom_in_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "zoom_in";
    else if (use_bg_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "use_bg";
    else if (immerse_btn_.adjusted(-4, -4, 4, 4).contains(pos)) hover_ = "immersive";

    if (drag_) {
        PointF d = pos - drag_start_;
        target_pan_ = pan_start_ + d;
        clamp_pan();
        pan_ = target_pan_;
    }
}

void GalleryViewer::mouse_press(PointF pos, MouseButton btn) {
    if (btn != MouseButton::Left) return;
    press_seen_ = true;
    mouse_ = pos;
    if (immersive_) {
        app_.set_cursor_hidden(false);
        app_.close_overlay();
        return;
    }
    if (RectF(1810, 46, 54, 54).contains(pos)) { app_.close_overlay(); return; }
    if (prev_btn_.adjusted(-5, -5, 5, 5).contains(pos)) { step(-1); return; }
    if (next_btn_.adjusted(-5, -5, 5, 5).contains(pos)) { step(1); return; }
    if (is_anim_ && spd_dec_btn_.adjusted(-5, -5, 5, 5).contains(pos)) {
        anim_speed_ = std::max(0.25f, std::round((anim_speed_ - 0.25f) * 4.0f) / 4.0f);
        return;
    }
    if (is_anim_ && spd_rst_btn_.adjusted(-5, -5, 5, 5).contains(pos)) {
        anim_speed_ = 1.0f;
        return;
    }
    if (is_anim_ && spd_inc_btn_.adjusted(-5, -5, 5, 5).contains(pos)) {
        anim_speed_ = std::min(2.0f, std::round((anim_speed_ + 0.25f) * 4.0f) / 4.0f);
        return;
    }
    if (src_btn_.adjusted(-5, -5, 5, 5).contains(pos)) {
        if (!items_.empty()) paths::reveal_in_explorer(items_[index_].abs_path());
        return;
    }
    if (zoom_out_btn_.adjusted(-5, -5, 5, 5).contains(pos)) { zoom_by(1.0f / 1.2f); return; }
    if (zoom_in_btn_.adjusted(-5, -5, 5, 5).contains(pos)) { zoom_by(1.2f); return; }
    if (use_bg_btn_.adjusted(-5, -5, 5, 5).contains(pos)) {
        if (!items_.empty()) {
            app_.settings().set("title_bg", items_[index_].path);
            bg_toast_until_ = now_sec() + 2.4;
        }
        return;
    }
    if (immerse_btn_.adjusted(-5, -5, 5, 5).contains(pos)) {
        immersive_ = true;
        hover_.clear();
        app_.set_fullscreen(true);
        app_.set_cursor_hidden(true);
        return;
    }
    if (content_rect().contains(pos) && target_zoom_ > 1.001f) {
        drag_ = true;
        drag_start_ = pos;
        pan_start_ = target_pan_;
    }
}

void GalleryViewer::mouse_release(PointF, MouseButton) {
    drag_ = false;
}

void GalleryViewer::mouse_double_click(PointF) {
    if (!press_seen_) { press_seen_ = true; return; }
    if (target_zoom_ > 1.001f) {
        target_zoom_ = 1.0f;
        target_pan_ = {0, 0};
    } else {
        target_zoom_ = 2.0f;
    }
}

void GalleryViewer::mouse_wheel(PointF pos, int delta) {
    if (delta == 0) return;
    if (immersive_) {
        step(delta > 0 ? -1 : 1);
        return;
    }
    float old = target_zoom_;
    float nw = clampf(old * (delta > 0 ? 1.16f : 1.0f / 1.16f), 1.0f, 4.0f);
    if (nw <= 1.001f) {
        target_zoom_ = 1.0f;
        target_pan_ = {0, 0};
        return;
    }
    RectF base = fit_rect();
    if (base.empty()) { target_zoom_ = nw; return; }
    PointF c = base.center() + target_pan_;
    PointF rel = {(pos.x - c.x) / old, (pos.y - c.y) / old};
    target_zoom_ = nw;
    target_pan_ = {pos.x - base.cx() - rel.x * nw, pos.y - base.cy() - rel.y * nw};
    clamp_pan();
}

bool GalleryViewer::key_press(int vk, bool, bool) {
    if (vk == key::Escape) { app_.set_cursor_hidden(false); app_.close_overlay(); return true; }
    if (vk == 'I') {
        immersive_ = !immersive_;
        hover_.clear();
        if (immersive_) {
            app_.set_fullscreen(true);
            app_.set_cursor_hidden(true);
        } else {
            app_.set_cursor_hidden(false);
        }
        return true;
    }
    if (vk == 'O') {
        if (!items_.empty()) paths::reveal_in_explorer(items_[index_].abs_path());
        return true;
    }
    if (vk == key::Left || vk == 'A') { step(-1); return true; }
    if (vk == key::Right || vk == 'D') { step(1); return true; }
    if (vk == '0' || vk == key::Home) {
        target_zoom_ = 1.0f;
        target_pan_ = {0, 0};
        return true;
    }
    if (vk == key::Plus || vk == 0x6B) { zoom_by(1.2f); return true; }
    if (vk == key::Minus || vk == 0x6D) { zoom_by(1.0f / 1.2f); return true; }
    if (vk == 'B') {
        if (!items_.empty()) {
            app_.settings().set("title_bg", items_[index_].path);
            bg_toast_until_ = now_sec() + 2.4;
        }
        return true;
    }
    if (vk == 'F') { app_.toggle_fullscreen(); return true; }
    return false;
}

} // namespace np
