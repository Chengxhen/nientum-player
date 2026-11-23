#include "freeplay_screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace np {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

FreePlayScreen::FreePlayScreen(AppContext& app) : Screen(app) {
    spin = static_cast<float>(app_.settings().get_double("lp_angle", 0.0));
    Category all_cat;
    all_cat.code = "all";
    all_cat.label = "全部";
    for (const auto& t : app_.library().tracks) {
        all_cat.tracks.push_back(&t);
    }
    categories_.push_back(std::move(all_cat));
    for (const auto& c : app_.library().chapters) {
        Category cat;
        cat.code = c.code;
        cat.label = c.label;
        cat.tracks = app_.library().act_tracks(c.code);
        categories_.push_back(std::move(cat));
    }
}

void FreePlayScreen::enter(const Track* track, const std::string& act,
                           const std::string&, int, bool suppress_preview) {
    if (!bg_built_) build_background();
    active_ = true;
    fade_ = 0.0f;
    t_ = 0.0f;
    suppress_preview_ = suppress_preview;
    pending_preview_at_ = 0.0;

    if (!act.empty()) {
        for (int i = 0; i < static_cast<int>(categories_.size()); ++i) {
            if (categories_[i].code == act) { cat_idx_ = i; break; }
        }
        track_idx_ = 0;
    } else if (track != nullptr) {
        cat_idx_ = 0;
        for (int i = 0; i < static_cast<int>(categories_.size()); ++i) {
            if (categories_[i].code == track->act) { cat_idx_ = i; break; }
        }
        track_idx_ = 0;
        const auto& trs = categories_[cat_idx_].tracks;
        for (int i = 0; i < static_cast<int>(trs.size()); ++i) {
            if (trs[i]->id == track->id) { track_idx_ = i; break; }
        }
    } else {
        std::string last = app_.settings().get_string("last_track");
        auto it = app_.library().index_by_id.find(last);
        cat_idx_ = 0;
        track_idx_ = (it != app_.library().index_by_id.end()) ? it->second : 0;
    }

    refresh_tracks();
    if (cat_idx_ < tab_count_) {
        tab_start_ = 0.0f;
    } else {
        int max_s = std::max(0, static_cast<int>(categories_.size()) - tab_count_);
        tab_start_ = static_cast<float>(std::clamp(cat_idx_ - 3, 0, max_s));
    }
    scroll_target_ = scroll_index();
    scroll_ = scroll_target_;
    list_fade_ = 0.0f;
    tab_focus_ = static_cast<float>(cat_idx_);
    cat_anim_ = 0.0f;
    if (const Track* ct = current_track()) {
        last_cover_act_ = ct->act;
        prev_cover_act_ = ct->act;
        cover_fade_ = 1.0f;
    }

    auto& audio = app_.audio();
    const Track* cur = current_track();
    previewing_ = (audio.previewing() && cur != nullptr && audio.track() != nullptr && audio.track()->id == cur->id);
    audio.on_preview_ended = [this]() {
        if (active_) previewing_ = false;
    };

    app_.set_cursor_hidden(false);

    bool auto_p = !suppress_preview_ && app_.settings().get_bool("preview_on_select", true);
    if (audio.full_mode() && audio.track() != nullptr && audio.playing() && audio.track()->id != title_track_id()) {
        auto_p = false;
    }
    suppress_preview_ = false;
    if (auto_p) schedule_preview(0.65);
}

void FreePlayScreen::leave() {
    active_ = false;
    has_detail_return_ = false;
    ++preview_gen_;
    pending_preview_at_ = 0.0;
    app_.audio().on_preview_ended = nullptr;
    app_.audio().cancel_handover(true);
    if (!app_.audio().full_mode()) {
        app_.audio().stop();
    }
    previewing_ = false;
    app_.set_cursor_hidden(false);
    app_.settings().set("lp_angle", std::round(spin * 1000.0) / 1000.0);
}

void FreePlayScreen::on_overlay_closed() {
    if (!has_detail_return_) return;
    has_detail_return_ = false;
    if (detail_ret_cat_ != cat_idx_) {
        cat_idx_ = std::clamp(detail_ret_cat_, 0, std::max(0, static_cast<int>(categories_.size()) - 1));
        refresh_tracks();
        int max_s = std::max(0, static_cast<int>(categories_.size()) - tab_count_);
        tab_start_ = static_cast<float>(std::clamp(cat_idx_ - 3, 0, max_s));
        tab_focus_ = static_cast<float>(cat_idx_);
        scroll_ = 0.0f;
        scroll_target_ = 0.0f;
    }
    track_idx_ = tracks_.empty() ? 0 : std::clamp(detail_ret_track_, 0, static_cast<int>(tracks_.size()) - 1);
    scroll_target_ = scroll_index();
    scroll_ = scroll_target_;
}

void FreePlayScreen::refresh_tracks() {
    if (cat_idx_ < 0 || cat_idx_ >= static_cast<int>(categories_.size())) {
        tracks_.clear();
        track_idx_ = 0;
        return;
    }
    tracks_ = categories_[cat_idx_].tracks;
    if (tracks_.empty()) track_idx_ = 0;
    else track_idx_ = ((track_idx_ % static_cast<int>(tracks_.size())) + static_cast<int>(tracks_.size())) % static_cast<int>(tracks_.size());
}

const Track* FreePlayScreen::current_track() const {
    if (tracks_.empty() || track_idx_ < 0 || track_idx_ >= static_cast<int>(tracks_.size())) return nullptr;
    return tracks_[track_idx_];
}

float FreePlayScreen::scroll_index() const {
    int n = static_cast<int>(tracks_.size());
    int max_s = std::max(0, n - VISIBLE_SLOTS);
    return static_cast<float>(std::clamp(track_idx_ - 2, 0, max_s));
}

int FreePlayScreen::preview_seconds() const {
    int s = app_.settings().get_int("preview_seconds", 20);
    return s > 0 ? s : 20;
}

std::string FreePlayScreen::title_track_id() const {
    std::string tid = app_.settings().get_string("title_track");
    if (!tid.empty() && app_.library().track_by_id(tid)) return tid;
    for (const char* wanted : {"Liora's Song Unwithering", "Liora's Song"}) {
        for (const auto& t : app_.library().tracks) {
            if (t.title.find(wanted) != std::string::npos) return t.id;
        }
    }
    return "";
}

void FreePlayScreen::schedule_preview(double delay_sec) {
    pending_preview_gen_ = preview_gen_;
    pending_preview_at_ = now_sec() + delay_sec;
}

void FreePlayScreen::start_preview(uint64_t gen, bool explicit_req) {
    if (!explicit_req && gen != preview_gen_) return;
    if (!active_) return;
    if (!explicit_req && !app_.settings().get_bool("preview_on_select", true)) return;
    const Track* t = current_track();
    if (!t) return;
    auto& audio = app_.audio();
    if (audio.full_mode() && audio.track() != nullptr && audio.track()->id == t->id && !explicit_req) return;
    if (audio.previewing() && audio.track() != nullptr && audio.track()->id == t->id && audio.playing() && !explicit_req) return;

    int secs = preview_seconds();
    audio.play_preview(t, 0, secs * 1000, 0.55);
    previewing_ = true;
    preview_started_at_ = now_sec();
    preview_end_at_ = preview_started_at_ + secs;
}

void FreePlayScreen::stop_preview() {
    ++preview_gen_;
    pending_preview_at_ = 0.0;
    app_.audio().stop_preview(true, true);
    previewing_ = false;
}

void FreePlayScreen::tick(float dt, double now) {
    if (!bg_built_) build_background();
    t_ += dt;
    fade_ = std::min(1.0f, fade_ + dt / 0.55f);
    list_fade_ = std::min(1.0f, list_fade_ + dt / 0.34f);

    if (pending_preview_at_ > 0.0 && now >= pending_preview_at_) {
        pending_preview_at_ = 0.0;
        start_preview(pending_preview_gen_, false);
    }

    auto& audio = app_.audio();
    const Track* t = current_track();
    bool turning = (t != nullptr && audio.track() != nullptr && audio.track()->id == t->id && audio.playing());
    float speed = turning ? 30.0f : 0.0f;
    spin_speed_ += (speed - spin_speed_) * (1.0f - std::exp(-dt / 0.55f));
    if (std::abs(spin_speed_) > 0.02f) {
        spin = std::fmod(spin - spin_speed_ * dt, 360.0f);
        if (spin < 0.0f) spin += 360.0f;
    }

    float target_glow = turning ? 1.0f : 0.0f;
    record_glow_ += (target_glow - record_glow_) * (1.0f - std::exp(-dt / 0.45f));

    if (t && t->act != last_cover_act_) {
        auto prev_list = app_.library().act_tracks(last_cover_act_);
        prev_cover_ref_ = prev_list.empty() ? nullptr : prev_list[0];
        prev_cover_act_ = last_cover_act_;
        last_cover_act_ = t->act;
        cover_fade_ = 0.0f;
    }
    if (cover_fade_ < 1.0f) {
        cover_fade_ = std::min(1.0f, cover_fade_ + dt / 0.28f);
    }

    tab_focus_ += (cat_idx_ - tab_focus_) * (1.0f - std::exp(-dt / 0.16f));
    if (std::abs(cat_idx_ - tab_focus_) < 0.002f) tab_focus_ = static_cast<float>(cat_idx_);
    cat_anim_ = std::min(1.0f, cat_anim_ + dt / 0.42f);

    if (std::abs(scroll_ - scroll_target_) > 0.001f) {
        scroll_ += (scroll_target_ - scroll_) * (1.0f - std::exp(-dt / 0.17f));
        if (std::abs(scroll_ - scroll_target_) < 0.002f) scroll_ = scroll_target_;
    }

    float hk = 1.0f - std::exp(-dt / 0.085f);
    for (auto it = card_hover_anim_.begin(); it != card_hover_anim_.end(); ) {
        float target = (it->first == hover_track_ && it->first != track_idx_) ? 1.0f : 0.0f;
        it->second += (target - it->second) * hk;
        if (target == 0.0f && it->second < 0.005f) it = card_hover_anim_.erase(it);
        else ++it;
    }
    if (hover_track_ >= 0 && hover_track_ != track_idx_ && card_hover_anim_.find(hover_track_) == card_hover_anim_.end()) {
        card_hover_anim_[hover_track_] = hk;
    }
}

bool FreePlayScreen::animating() const {
    return fade_ < 0.999f || list_fade_ < 0.999f || cat_anim_ < 0.999f || cover_fade_ < 0.999f ||
           std::abs(scroll_ - scroll_target_) > 0.002f ||
           std::abs(tab_focus_ - cat_idx_) > 0.002f ||
           std::abs(spin_speed_) > 0.02f || tab_drag_ || !card_hover_anim_.empty();
}

void FreePlayScreen::select_track(int idx) {
    if (tracks_.empty()) return;
    idx = std::clamp(idx, 0, static_cast<int>(tracks_.size()) - 1);
    if (idx == track_idx_) return;
    track_idx_ = idx;
    scroll_target_ = scroll_index();
    ++preview_gen_;
    pending_preview_at_ = 0.0;

    auto& audio = app_.audio();
    if (audio.full_mode() && audio.track() != nullptr && audio.playing() && audio.track()->id != title_track_id()) {
        return;
    }
    audio.cancel_handover(true);
    if (!audio.full_mode()) {
        if (audio.previewing()) audio.stop_preview(true, false);
        else audio.stop();
    }
    previewing_ = false;
    if (app_.settings().get_bool("preview_on_select", true)) {
        schedule_preview(0.65);
    }
}

void FreePlayScreen::select_category(int idx) {
    int n = static_cast<int>(categories_.size());
    if (n == 0) return;
    idx = std::clamp(idx, 0, n - 1);
    if (idx == cat_idx_) return;
    cat_idx_ = idx;
    track_idx_ = 0;
    refresh_tracks();
    if (cat_idx_ < tab_count_) {
        tab_start_ = 0.0f;
    } else {
        int max_s = std::max(0, n - tab_count_);
        tab_start_ = static_cast<float>(std::clamp(cat_idx_ - 3, 0, max_s));
    }
    scroll_ = 0.0f;
    scroll_target_ = 0.0f;
    list_fade_ = 0.0f;
    cat_anim_ = 0.0f;
    ++preview_gen_;
    pending_preview_at_ = 0.0;

    auto& audio = app_.audio();
    if (audio.full_mode() && audio.track() != nullptr && audio.playing() && audio.track()->id != title_track_id()) {
        return;
    }
    audio.cancel_handover(true);
    if (!audio.full_mode()) {
        if (audio.previewing()) audio.stop_preview(true, false);
        else audio.stop();
    }
    previewing_ = false;
    if (app_.settings().get_bool("preview_on_select", true)) {
        schedule_preview(0.65);
    }
}

std::vector<std::pair<int, RectF>> FreePlayScreen::tab_rects() const {
    std::vector<std::pair<int, RectF>> out;
    const float slot = 100.0f;
    const float x0 = 70.0f;
    int base = static_cast<int>(std::floor(tab_start_));
    float shift = (tab_start_ - base) * slot;
    for (int j = 0; j < tab_count_; ++j) {
        int i = base + j;
        if (i >= static_cast<int>(categories_.size())) break;
        out.push_back({i, {x0 + j * slot - shift, 150.0f, slot - 10.0f, 60.0f}});
    }
    return out;
}

float FreePlayScreen::curve_offset(float cy) const {
    float t = clampf((cy - (LIST_TOP + 8.0f)) / std::max(1.0f, (LIST_BOTTOM - LIST_TOP - 16.0f)));
    return 52.0f * std::sin(kPi * t);
}

float FreePlayScreen::edge_fade(float cy) const {
    float d = std::min(cy - LIST_TOP, LIST_BOTTOM - cy);
    return clampf(d / 72.0f);
}

std::vector<std::pair<int, RectF>> FreePlayScreen::card_rects() const {
    std::vector<std::pair<int, RectF>> out;
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        float cy = 348.0f + (i - scroll_) * CARD_SPACING;
        if (cy < LIST_TOP - 90.0f || cy > LIST_BOTTOM + 90.0f) continue;
        bool active = (i == track_idx_);
        float w = active ? 922.0f : 900.0f;
        float h = active ? 148.0f : 134.0f;
        float x = 64.0f + (900.0f - w) * 0.5f + curve_offset(cy);
        out.push_back({i, {x, cy - h * 0.5f, w, h}});
    }
    return out;
}

std::vector<FreePlayScreen::HelpItem> FreePlayScreen::help_rects() const {
    auto& gfx = app_.gfx();
    FontKey kf{11.0f, false};
    FontKey lf{13.0f, false};
    struct Def { const char* a; const char* k; const char* l; };
    Def defs[] = {
        {"title", "ESC", "返回"},
        {"select", "↑↓", "选择"},
        {"category", "←→", "分类"},
        {"play", "Enter", "开演"},
        {"detail", "Tab", "详情"},
    };
    std::vector<HelpItem> out;
    float x = 62.0f, y = 1012.0f;
    for (const auto& d : defs) {
        float kw = std::max(26.0f, gfx.text_width(d.k, kf) + 14.0f);
        float lw = gfx.text_width(d.l, lf);
        RectF r{x, y, kw + lw + 20.0f, 32.0f};
        out.push_back({d.a, r, d.k, d.l});
        x = r.right() + 6.0f;
    }
    return out;
}

void FreePlayScreen::build_background() {
    auto& gfx = app_.gfx();
    auto& assets = app_.assets();
    bg_cache_ = gfx.create_target_bitmap(1920, 1080);
    if (!bg_cache_) return;

    gfx.set_target(bg_cache_.Get());
    gfx.dc()->BeginDraw();
    gfx.dc()->SetTransform(D2D1::Matrix3x2F::Identity());
    gfx.dc()->Clear(D2D1::ColorF(26 / 255.f, 22 / 255.f, 19 / 255.f, 1.0f));

    if (ID2D1Bitmap1* bg = assets.ui("BG_Playlist.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(bg, {0, 0, 1920, 1080}, 1.0f, true);
    }
    if (ID2D1Bitmap1* flower = assets.ui("PZImgTitleBackgroundFlower.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(flower, {0, 0, 1920, 1080}, 0.16f, true);
    }
    if (ID2D1Bitmap1* light = assets.ui("PZImgTitleBackgroundLight.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(light, {0, 0, 1920, 1080}, 0.24f, true);
    }
    if (ID2D1Bitmap1* vig = assets.ui("PZImgVignetting.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(vig, {0, 0, 1920, 1080}, 0.82f, true);
    }
    gfx.fill_rect({0, 0, 1920, 142}, ColorF::rgba(8, 6, 5, 190));
    if (ID2D1Bitmap1* film = assets.ui("film.png", 1920, 142, "stretch")) {
        gfx.draw_bitmap(film, {0, 0, 1920, 142}, 0.72f, true);
    }
    if (ID2D1Bitmap1* valance = assets.ui("PZImgLobbyCurtain.png", 1920, 136, "stretch")) {
        gfx.draw_bitmap(valance, {0.0f, -6.0f, 1920.0f, 136.0f}, 0.70f, true);
    }
    Gfx::GradStop gs[2] = {{0.0f, ColorF::rgba(8, 6, 5, 195)}, {1.0f, ColorF::rgba(8, 6, 5, 0)}};
    gfx.fill_linear_grad({0, 100, 1920, 90}, {0, 100}, {0, 190}, gs, 2);
    if (ID2D1Bitmap1* deco = assets.ui("PZImgLabelDeco.png", 344, 68, "contain")) {
        gfx.draw_bitmap(deco, {1520.0f, 22.0f, 344.0f, 68.0f}, 0.82f, true);
    }

    gfx.text(56, 72, "FREE PLAY", {46.0f, true, 400, 0.5f}, ColorF::rgba(239, 228, 211, 252));
    gfx.text(332, 78, "自 由 选 曲", {20.0f, true, 400, 1.2f}, ColorF::rgba(198, 176, 150, 238));
    gfx.draw_gold_divider(56.0f, 480.0f, 88.0f, 0.92f, 2.0f);

    gfx.dc()->EndDraw();
    gfx.restore_scene_target();
    bg_built_ = true;
}

void FreePlayScreen::paint(Gfx& gfx, double) {
    if (bg_cache_) gfx.draw_bitmap(bg_cache_.Get(), {0, 0, 1920, 1080}, 1.0f, true);

    paint_tabs(gfx);

    if (!tracks_.empty()) {
        int n = static_cast<int>(tracks_.size());
        char buf[64];
        snprintf(buf, sizeof(buf), "曲目  %02d / %02d", track_idx_ + 1, n);
        std::string label = buf;
        FontKey f{15.0f, false, 400, 1.2f};
        gfx.text(70, 248, label, f, ColorF::rgba(190, 168, 142, 225));
        snprintf(buf, sizeof(buf), "%zu 曲 · %zu 章", app_.library().tracks.size(), app_.library().chapters.size());
        std::string count = buf;
        float lw = gfx.text_width(label, f);
        float cw = gfx.text_width(count, f);
        gfx.text(1000.0f - cw, 248, count, f, ColorF::rgba(176, 156, 136, 210));
        gfx.draw_line({70.0f + lw + 18.0f, 244}, {1000.0f - cw - 18.0f, 244}, ColorF::rgba(160, 128, 88, 110), 1.0f);
    }

    paint_list(gfx);
    paint_now_playing(gfx);
    paint_record(gfx);
    paint_preview_bar(gfx);
    paint_guide(gfx);

    if (fade_ < 1.0f) {
        float u = 1.0f - fade_;
        gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(0, 0, 0, static_cast<int>(255 * u * u)));
    }
}

void FreePlayScreen::paint_tabs(Gfx& gfx) {
    gfx.velvet_panel({58.0f, 150.0f, 848.0f, 52.0f}, 12.0f, 0.88f);
    float focus = tab_focus_;
    const float slot = 100.0f;
    const float x0 = 70.0f;
    if (tab_start_ - 0.01f <= focus && focus <= tab_start_ + tab_count_ - 0.99f) {
        float fx = x0 + (focus - tab_start_) * slot + (slot - 10.0f) * 0.5f;
        RectF pill(fx - 43.0f, 155.0f, 86.0f, 38.0f);
        gfx.draw_glass_pill(pill, 18.0f, false, true, 0.92f, false);
        gfx.draw_gold_divider(fx - 39.0f, fx + 39.0f, 197.0f, 1.0f, 2.0f);
        gfx.fill_ellipse({fx, 203.0f}, 2.8f, 2.8f, ColorF::rgba(220, 178, 114, 255));
    }

    ColorF inactive = ColorF::rgba(176, 162, 146, 195);
    ColorF active = ColorF::rgba(242, 228, 208, 255);
    ColorF hover_col = ColorF::rgba(202, 188, 168, 242);

    for (const auto& [i, r] : tab_rects()) {
        float prox = clampf(1.0f - std::abs(i - focus));
        bool hover = (i == hover_tab_);
        FontKey fk{19.0f + 4.0f * prox, true, 400, 0.4f};
        ColorF col = mix_color(inactive, active, prox);
        if (hover && prox < 0.5f) col = mix_color(col, hover_col, 0.7f);
        const std::string& lbl = categories_[i].label;
        float tw = gfx.text_width(lbl, fk);
        float cx = r.cx();
        gfx.text(cx - tw * 0.5f + 1.0f, r.y + 33.0f, lbl, fk, ColorF::rgba(0, 0, 0, 150));
        gfx.text(cx - tw * 0.5f, r.y + 32.0f, lbl, fk, col);
    }

    if (tab_start_ > 0.0f) {
        ColorF col = (hover_chev_ == -1) ? ColorF::rgba(238, 204, 148, 245) : ColorF::rgba(210, 180, 132, 200);
        gfx.draw_icon("left", {42, 164, 20, 20}, col, 2.0f);
    }
    if (tab_start_ + tab_count_ < static_cast<float>(categories_.size())) {
        ColorF col = (hover_chev_ == 1) ? ColorF::rgba(238, 204, 148, 245) : ColorF::rgba(210, 180, 132, 200);
        gfx.draw_icon("right", {878, 164, 20, 20}, col, 2.0f);
    }
}

void FreePlayScreen::paint_list(Gfx& gfx) {
    if (tracks_.empty()) return;
    gfx.push_clip(LIST_CLIP);

    auto rects = card_rects();
    std::sort(rects.begin(), rects.end(), [this](const auto& a, const auto& b) {
        return std::abs(a.first - track_idx_) > std::abs(b.first - track_idx_);
    });
    for (const auto& [i, r] : rects) {
        float stagger = clampf((cat_anim_ - std::min(0.32f, std::abs(i - track_idx_) * 0.045f)) / 0.68f);
        float se = ease_out(stagger);
        RectF rr = (cat_anim_ < 0.999f) ? r.translated(0.0f, (1.0f - se) * 28.0f) : r;
        float cf = (cat_anim_ < 0.999f) ? se : 1.0f;
        paint_card(gfx, i, rr, edge_fade(rr.cy()) * cf);
    }

    gfx.pop_clip();
}

void FreePlayScreen::paint_card(Gfx& gfx, int i, const RectF& r, float fade) {
    auto& assets = app_.assets();
    const Track* t = tracks_[i];
    bool active = (i == track_idx_);
    bool hover = (i == hover_track_);
    float h = active ? 0.0f : (card_hover_anim_.count(i) ? card_hover_anim_.at(i) : (hover ? 1.0f : 0.0f));
    RectF rr = r.translated(0.0f, -4.0f * h);

    float base = list_fade_ * std::max(0.0f, fade);
    if (base <= 0.01f) return;

    if (active) {
        gfx.fill_rounded(rr.adjusted(-11, -11, 11, 11), 12.0f, ColorF::rgba(214, 176, 112, 18).mul_alpha(base));
        gfx.fill_rounded(rr.adjusted(-5, -5, 5, 5), 8.0f, ColorF::rgba(214, 176, 112, 34).mul_alpha(base));
    } else if (h > 0.01f) {
        gfx.fill_rounded(rr.adjusted(-6, -6, 6, 6), 9.0f, ColorF::rgba(214, 176, 112, static_cast<int>(20 * h)).mul_alpha(base));
    }
    gfx.fill_rounded(rr.adjusted(5, 6 + 2.0f * h, 5, 8 + 3.0f * h), 6.0f, ColorF::rgba(0, 0, 0, active ? 112 : static_cast<int>(64 + 28 * h)).mul_alpha(base));

    if (ID2D1Bitmap1* paper = assets.ui("List_active.png", static_cast<int>(rr.w), static_cast<int>(rr.h), "stretch")) {
        gfx.draw_bitmap(paper, rr, base * (active ? 1.0f : (0.9f + 0.08f * h)), true);
    } else {
        ColorF pf = ColorF::rgba(226, 217, 204, active ? 250 : 195).mul_alpha(base);
        ColorF pb = ColorF::rgba(140, 120, 100, 180).mul_alpha(base);
        gfx.rounded(rr, 4.0f, &pf, &pb, 1.0f);
    }

    if (active) {
        gfx.draw_rounded(rr.adjusted(1, 1, -1, -1), 4.0f, ColorF::rgba(218, 184, 126, 248).mul_alpha(base), 1.8f);
        gfx.fill_rect({rr.x + 3, rr.y + 16, 3, rr.h - 32}, ColorF::rgba(204, 160, 100, 250).mul_alpha(base));
    } else if (h > 0.01f) {
        gfx.draw_rounded(rr.adjusted(1, 1, -1, -1), 4.0f, ColorF::rgba(218, 184, 126, static_cast<int>(175 * h)).mul_alpha(base), 1.3f);
    }

    // Chapter curtain-call thumbnail
    float th = 110.0f, tw = 196.0f;
    RectF tr(rr.x + 18.0f, rr.cy() - th * 0.5f, tw, th);
    gfx.fill_rect(tr.adjusted(-2, -2, 2, 2), ColorF::rgba(26, 21, 18, 235).mul_alpha(base));
    gfx.draw_rect(tr.adjusted(-2, -2, 2, 2), ColorF::rgba(145, 120, 92, 190).mul_alpha(base), 1.0f);

    auto it_c = app_.library().curtain_preview.find(t->act);
    if (it_c != app_.library().curtain_preview.end()) {
        if (ID2D1Bitmap1* thumb = assets.thumb(&it_c->second, static_cast<int>(tw), static_cast<int>(th))) {
            gfx.draw_bitmap(thumb, tr, base * (active ? 1.0f : 0.84f), true);
        } else {
            gfx.fill_rect(tr, ColorF::rgba(46, 36, 30, 255).mul_alpha(base));
        }
    } else {
        gfx.fill_rect(tr, ColorF::rgba(46, 36, 30, 255).mul_alpha(base));
    }
    gfx.draw_rect(tr, ColorF::rgba(210, 174, 116, active ? 170 : 95).mul_alpha(base), 1.0f);
    if (active) {
        gfx.draw_line({tr.x, tr.y}, {tr.x + 26.0f, tr.y}, ColorF::rgba(228, 192, 132, 235).mul_alpha(base), 2.0f);
    }

    float ta = (active ? 1.0f : 192.0f / 255.0f) * base;
    float tx = tr.right() + 26.0f;
    FontKey tf{active ? 28.0f : 24.0f, true};
    float title_w = std::max(120.0f, rr.w - (tx - rr.x) - 176.0f);
    std::string title = gfx.elide_text(t->title, tf, title_w);
    gfx.text(tx, rr.cy() - 3.0f, title, tf, ColorF::rgba(78, 62, 55).with_alpha(ta));

    FontKey af{active ? 17.0f : 15.0f, false};
    std::string artist = gfx.elide_text(t->artist, af, title_w - 10.0f);
    gfx.text(tx, rr.cy() + 25.0f, artist, af, ColorF::rgba(118, 98, 88).with_alpha(ta * 0.92f));

    FontKey mf{active ? 14.0f : 13.0f, false};
    std::string meta = t->act_label + " · " + t->act_zh + "      " + t->duration_str();
    gfx.text(tx, rr.cy() + 48.0f, meta, mf, ColorF::rgba(150, 118, 88).with_alpha(ta * 0.92f));

    float right = rr.right() - 22.0f;
    if (active && t->original) {
        if (ID2D1Bitmap1* badge = assets.ui("label_original.png", 100, 33, "contain")) {
            gfx.draw_bitmap(badge, {right - 100.0f, rr.cy() - 44.0f, 100.0f, 33.0f}, base, true);
        }
    }

    auto& audio = app_.audio();
    if (audio.track() != nullptr && audio.track()->id == t->id && (audio.previewing() || audio.full_mode())) {
        bool is_prev = audio.previewing();
        bool sounding = audio.playing();
        std::string label;
        if (is_prev && previewing_) {
            int remain = std::max(0, static_cast<int>(preview_end_at_ - now_sec()));
            char cbuf[32];
            snprintf(cbuf, sizeof(cbuf), "试听中 · %02ds", remain);
            label = cbuf;
        } else if (is_prev) {
            label = "试听中";
        } else {
            label = sounding ? "正在播放" : "已暂停";
        }
        float chip_w = is_prev ? 148.0f : 118.0f;
        RectF chip(right - chip_w, rr.cy() + (active ? 19.0f : -14.0f), chip_w, 28.0f);
        ColorF cf = ColorF::rgba(24, 18, 14, 228).mul_alpha(base);
        ColorF cb = ColorF::rgba(206, 168, 108, 205).mul_alpha(base);
        gfx.rounded(chip, 14.0f, &cf, &cb, 1.0f);
        draw_eq(gfx, chip.x + 12.0f, chip.cy(), sounding, ColorF::rgba(234, 192, 126, 255).mul_alpha(base));
        gfx.text(chip.x + 27.0f, chip.y + 19.0f, label, {13.0f, false}, ColorF::rgba(240, 224, 200, 248).mul_alpha(base));
    }
}

void FreePlayScreen::draw_eq(Gfx& gfx, float x, float cy, bool sounding, const ColorF& col) {
    for (int j = 0; j < 3; ++j) {
        float h = sounding ? (5.0f + 8.0f * (0.5f + 0.5f * std::sin(t_ * 7.0f + j * 1.7f))) : 4.0f;
        gfx.fill_rounded({x + j * 4.2f, cy - h * 0.5f, 2.4f, h}, 1.2f, col);
    }
}

void FreePlayScreen::paint_now_playing(Gfx& gfx) {
    auto& audio = app_.audio();
    const Track* t = audio.track();
    if (!t || !audio.full_mode()) return;

    RectF r = now_rect();
    gfx.velvet_panel(r, 8.0f);
    if (hover_now_) {
        gfx.fill_rounded(r, 8.0f, ColorF::rgba(238, 202, 146, 20));
        gfx.draw_rounded(r, 8.0f, ColorF::rgba(226, 188, 130, 178), 1.2f);
    }
    gfx.fill_rounded({r.x + 12, r.y + 10, 3, r.h - 20}, 1.5f, ColorF::rgba(218, 176, 112, 245));
    draw_eq(gfx, r.x + 26, r.cy(), audio.playing(), ColorF::rgba(235, 192, 126, 255));

    gfx.text(r.x + 46, r.y + 35, audio.playing() ? "正在播放" : "已暂停",
             {12.0f, false, 400, 0.8f}, ColorF::rgba(208, 168, 110, 245));
    FontKey tf{19.0f, true};
    gfx.text(r.x + 142, r.y + 35, gfx.elide_text(t->title, tf, 390.0f), tf, ColorF::rgba(242, 230, 212, 250));

    FontKey sf{12.0f, false};
    std::string sub = gfx.elide_text(t->act_label + " · " + t->act_zh + "      " + t->artist, sf, 270.0f);
    gfx.text(r.x + 552, r.y + 25, sub, sf, ColorF::rgba(184, 164, 142, 235));

    RectF pr(r.x + 552, r.y + 36, 300, 4);
    int dur = std::max(1, static_cast<int>(t->duration * 1000.0));
    float frac = clampf(static_cast<float>(audio.position_ms()) / dur);
    gfx.draw_line({pr.x, pr.cy()}, {pr.right(), pr.cy()}, ColorF::rgba(74, 62, 52, 220), 3.0f, true);
    gfx.draw_line({pr.x, pr.cy()}, {pr.x + pr.w * frac, pr.cy()}, ColorF::rgba(212, 172, 112, 245), 3.0f, true);

    std::string hint = "点击回到播放";
    FontKey hf{12.0f, false};
    gfx.text(r.right() - 20.0f - gfx.text_width(hint, hf), r.y + 33, hint, hf,
             ColorF::rgba(214, 184, 146, hover_now_ ? 230 : 170));
}

void FreePlayScreen::paint_record(Gfx& gfx) {
    const Track* t = current_track();
    if (!t) return;
    auto& assets = app_.assets();
    const float cx = 1548.0f, cy = 506.0f, radius = 500.0f;

    // Subtle warm golden aura behind the vinyl when active/playing
    if (record_glow_ > 0.01f) {
        gfx.set_additive(true);
        if (ID2D1Bitmap1* glow = gfx.glow_sprite()) {
            float gs = 560.0f;
            gfx.draw_bitmap(glow, {cx - gs, cy - gs, gs * 2.0f, gs * 2.0f}, 0.16f * record_glow_, true);
        }
        gfx.set_additive(false);
    }

    // Soft vinyl drop shadow
    Gfx::GradStop sg[3] = {
        {0.0f, ColorF::rgba(0, 0, 0, 120)},
        {0.8f, ColorF::rgba(0, 0, 0, 52)},
        {1.0f, ColorF::rgba(0, 0, 0, 0)},
    };
    gfx.fill_radial_grad_ellipse({cx + 12.0f, cy + 24.0f}, 548.0f, 548.0f, sg, 3);

    ID2D1Bitmap1* record = assets.ui("LP_original.png", 1000, 1000, "cover");
    const Track* ref = t;
    auto ctracks = app_.library().act_tracks(t->act);
    if (!ctracks.empty()) ref = ctracks[0];
    ID2D1Bitmap1* cover = assets.cover(ref, 384);

    gfx.save_transform();
    gfx.translate(cx, cy);
    gfx.rotate(spin);
    if (record) {
        gfx.draw_bitmap(record, {-radius, -radius, radius * 2.0f, radius * 2.0f}, 1.0f, true);
    }
    if (cover_fade_ < 0.999f && prev_cover_ref_) {
        if (ID2D1Bitmap1* pcov = assets.cover(prev_cover_ref_, 384)) {
            gfx.draw_bitmap(pcov, {-224.0f, -224.0f, 448.0f, 448.0f}, 1.0f - cover_fade_, true);
        }
    }
    if (cover) {
        gfx.draw_bitmap(cover, {-224.0f, -224.0f, 448.0f, 448.0f}, cover_fade_, true);
    }
    // Centre spindle hole
    gfx.fill_ellipse({0, 0}, 11.0f, 11.0f, ColorF::rgba(206, 202, 194, 242));
    gfx.draw_ellipse({0, 0}, 11.0f, 11.0f, ColorF::rgba(60, 52, 46, 185), 1.5f);
    gfx.fill_ellipse({-2.5f, -2.5f}, 3.5f, 3.5f, ColorF::rgba(255, 250, 240, 80));
    gfx.restore_transform();

    // Subtle stationary specular light sheen on vinyl grooves (keeps authentic look + depth)
    gfx.push_ellipse_clip({cx, cy}, radius, radius);
    gfx.set_additive(true);
    Gfx::GradStop sheen[5] = {
        {0.00f, ColorF::rgba(255, 232, 192, 0)},
        {0.38f, ColorF::rgba(255, 232, 192, 0)},
        {0.50f, ColorF::rgba(255, 236, 204, 16)},
        {0.62f, ColorF::rgba(255, 232, 192, 0)},
        {1.00f, ColorF::rgba(255, 232, 192, 0)},
    };
    gfx.fill_linear_grad({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f},
                         {cx - radius, cy - radius}, {cx + radius, cy + radius}, sheen, 5);
    gfx.set_additive(false);

    // Static cover rim highlight
    gfx.draw_ellipse({cx, cy}, 225.0f, 225.0f, ColorF::rgba(236, 210, 168, 170), 1.4f);

    // Soft clipped bottom scrim behind title
    gfx.save_transform();
    gfx.translate(cx, 920.0f);
    gfx.scale(1.0f, 0.34f);
    Gfx::GradStop scrim[3] = {
        {0.00f, ColorF::rgba(8, 6, 5, 138)},
        {0.55f, ColorF::rgba(8, 6, 5, 96)},
        {1.00f, ColorF::rgba(8, 6, 5, 0)},
    };
    gfx.fill_radial_grad_ellipse({0.0f, 0.0f}, 470.0f, 470.0f, scrim, 3);
    gfx.restore_transform();
    gfx.pop_geometry_clip();

    if (t->original) {
        gfx.text_center(cx, 814.0f, "O R I G I N A L", {13.0f, false, 400, 2.4f}, ColorF::rgba(206, 164, 104, 245));
    }
    FontKey title_f{40.0f, true};
    std::string title = gfx.elide_text(t->title, title_f, 860.0f);
    gfx.text_center(cx, 870.0f, title, title_f, ColorF::rgba(243, 233, 218, 255), true);
    gfx.text_center(cx, 912.0f, t->artist, {21.0f, false}, ColorF::rgba(208, 190, 168, 242), true);

    int count = 0, idx = 0;
    if (!tracks_.empty()) {
        if (categories_[cat_idx_].code == "all") {
            count = static_cast<int>(app_.library().act_tracks(t->act).size());
            idx = t->index;
        } else {
            count = static_cast<int>(tracks_.size());
            idx = track_idx_ + 1;
        }
    }
    char mbuf[128];
    snprintf(mbuf, sizeof(mbuf), "%s · %s      %02d / %02d", t->act_label.c_str(), t->act_zh.c_str(), idx, count);
    gfx.text_center(cx, 946.0f, mbuf, {18.0f, false}, ColorF::rgba(202, 166, 120, 248), true);
}

void FreePlayScreen::paint_preview_bar(Gfx& gfx) {
    const Track* t = current_track();
    if (!t) return;
    RectF bar(1232, 1016, 628, 48);
    gfx.velvet_panel(bar, 10.0f);

    auto& audio = app_.audio();
    bool same_full = (audio.full_mode() && audio.track() != nullptr && audio.track()->id == t->id);
    bool prev_here = (audio.previewing() && audio.track() != nullptr && audio.track()->id == t->id);

    RectF hr(bar.x + 16, bar.y + 8, 310, 32);
    gfx.text(hr.x + 2, hr.cy() + 6, "点击唱片播放/停止试听", {12.0f, false}, ColorF::rgba(206, 190, 168, 235));

    RectF st(bar.x + 330, bar.y + 8, 116, 32);
    std::string state;
    ColorF s_col;
    if (prev_here) {
        int rem = std::max(0, static_cast<int>(preview_end_at_ - now_sec()));
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "试听中 · %02ds", rem);
        state = sbuf;
        s_col = ColorF::rgba(238, 198, 132, 250);
    } else if (same_full) {
        state = "正式播放中";
        s_col = ColorF::rgba(214, 182, 142, 235);
    } else {
        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "试听 %d 秒", preview_seconds());
        state = sbuf;
        s_col = ColorF::rgba(190, 172, 150, 230);
    }
    gfx.text(st.x + 2, st.cy() + 6, state, {13.0f, false}, s_col);

    int cur = preview_seconds();
    const int secs[3] = {10, 20, 30};
    for (int i = 0; i < 3; ++i) {
        RectF r(bar.x + 456 + i * 54, bar.y + 8, 50, 32);
        bool act = (cur == secs[i]);
        bool hov = (hover_preview_ == ("dur:" + std::to_string(i)));
        gfx.draw_glass_pill(r, 7.0f, act, hov, 1.0f, false);
        char dbuf[16];
        snprintf(dbuf, sizeof(dbuf), "%ds", secs[i]);
        gfx.text_center(r.cx(), r.y + 21, dbuf, {13.0f, false, act ? 600 : 400},
                        act ? ColorF::rgba(34, 24, 18, 255) : ColorF::rgba(226, 210, 190, 242));
    }
}

void FreePlayScreen::paint_guide(Gfx& gfx) {
    auto rects = help_rects();
    if (rects.empty()) return;
    RectF panel(50, 1004, rects.back().rect.right() + 10.0f - 50.0f, 46);
    gfx.velvet_panel(panel, 8.0f);
    for (const auto& h : rects) {
        bool hov = (hover_help_ == h.action);
        gfx.draw_glass_pill(h.rect, 6.0f, false, hov, 1.0f, false);
        float kw = gfx.draw_keycap(h.rect.x + 4, h.rect.y + 6, h.key, {11.0f, false}, 20.0f);
        gfx.text(h.rect.x + 10.0f + kw, h.rect.y + 21.0f, h.label, {13.0f, false},
                 hov ? ColorF::rgba(246, 232, 212, 250) : ColorF::rgba(222, 208, 188, 238));
    }
}

void FreePlayScreen::mouse_move(PointF pos) {
    if (tab_drag_) {
        float dx = pos.x - tab_drag_x_;
        if (std::abs(dx) > 3.0f) tab_drag_moved_ = true;
        float max_start = std::max(0.0f, static_cast<float>(categories_.size()) - tab_count_);
        tab_start_ = clampf(tab_drag_start_ - dx / 100.0f, 0.0f, max_start);
        return;
    }
    hover_tab_ = -1;
    hover_track_ = -1;
    hover_chev_ = 0;
    hover_help_.clear();
    hover_preview_.clear();
    hover_now_ = false;

    for (const auto& h : help_rects()) {
        if (h.rect.contains(pos)) { hover_help_ = h.action; return; }
    }
    for (int i = 0; i < 3; ++i) {
        RectF r(1232 + 456 + i * 54, 1016 + 8, 50, 32);
        if (r.adjusted(-2, -4, 2, 4).contains(pos)) { hover_preview_ = "dur:" + std::to_string(i); return; }
    }
    if (app_.audio().full_mode() && now_rect().contains(pos)) {
        hover_now_ = true;
        return;
    }
    if (tab_start_ > 0.0f && RectF(36, 158, 32, 32).contains(pos)) hover_chev_ = -1;
    else if (tab_start_ + tab_count_ < static_cast<float>(categories_.size()) && RectF(872, 158, 32, 32).contains(pos)) hover_chev_ = 1;

    for (const auto& [i, r] : tab_rects()) {
        if (r.contains(pos)) { hover_tab_ = i; break; }
    }
    auto cards = card_rects();
    std::sort(cards.begin(), cards.end(), [this](const auto& a, const auto& b) {
        int da = (a.first == track_idx_) ? 0 : std::abs(a.first - track_idx_);
        int db = (b.first == track_idx_) ? 0 : std::abs(b.first - track_idx_);
        return da < db;
    });
    for (const auto& [i, r] : cards) {
        if (r.contains(pos)) { hover_track_ = i; break; }
    }
}

void FreePlayScreen::mouse_press(PointF pos, MouseButton btn) {
    if (btn == MouseButton::Right) {
        auto cards = card_rects();
        std::sort(cards.begin(), cards.end(), [this](const auto& a, const auto& b) {
            int da = (a.first == track_idx_) ? 0 : std::abs(a.first - track_idx_);
            int db = (b.first == track_idx_) ? 0 : std::abs(b.first - track_idx_);
            return da < db;
        });
        for (const auto& [i, r] : cards) {
            if (r.contains(pos)) {
                if (i != track_idx_) select_track(i);
                play_full();
                return;
            }
        }
        return;
    }
    if (btn != MouseButton::Left) return;

    for (const auto& h : help_rects()) {
        if (h.rect.contains(pos)) { help_action(h.action); return; }
    }
    for (int i = 0; i < 3; ++i) {
        RectF r(1232 + 456 + i * 54, 1016 + 8, 50, 32);
        if (r.adjusted(-2, -4, 2, 4).contains(pos)) {
            set_preview_seconds((i == 0) ? 10 : (i == 1 ? 20 : 30));
            return;
        }
    }
    if (app_.audio().full_mode() && now_rect().contains(pos)) {
        app_.open_player(app_.audio().track());
        return;
    }
    if (tab_start_ > 0.0f && RectF(36, 158, 32, 32).contains(pos)) {
        tab_start_ = std::max(0.0f, tab_start_ - 1.0f);
        return;
    }
    if (tab_start_ + tab_count_ < static_cast<float>(categories_.size()) && RectF(872, 158, 32, 32).contains(pos)) {
        float max_s = std::max(0.0f, static_cast<float>(categories_.size()) - tab_count_);
        tab_start_ = std::min(max_s, tab_start_ + 1.0f);
        return;
    }
    if (RectF(30, 145, 880, 70).contains(pos)) {
        int hit_i = -1;
        for (const auto& [i, r] : tab_rects()) {
            if (r.contains(pos)) { hit_i = i; break; }
        }
        tab_drag_ = true;
        tab_drag_x_ = pos.x;
        tab_drag_start_ = tab_start_;
        tab_drag_moved_ = false;
        tab_press_i_ = hit_i;
        return;
    }
    auto cards = card_rects();
    std::sort(cards.begin(), cards.end(), [this](const auto& a, const auto& b) {
        int da = (a.first == track_idx_) ? 0 : std::abs(a.first - track_idx_);
        int db = (b.first == track_idx_) ? 0 : std::abs(b.first - track_idx_);
        return da < db;
    });
    for (const auto& [i, r] : cards) {
        if (r.contains(pos)) {
            select_track(i);
            return;
        }
    }
    if (current_track() != nullptr && RectF(1048, 6, 1000, 1000).contains(pos)) {
        if (previewing_) stop_preview();
        else start_preview(0, true);
    }
}

void FreePlayScreen::mouse_release(PointF, MouseButton) {
    if (!tab_drag_) return;
    tab_drag_ = false;
    if (!tab_drag_moved_ && tab_press_i_ >= 0) {
        select_category(tab_press_i_);
    }
    tab_press_i_ = -1;
    tab_drag_moved_ = false;
}

void FreePlayScreen::mouse_double_click(PointF pos) {
    auto cards = card_rects();
    std::sort(cards.begin(), cards.end(), [this](const auto& a, const auto& b) {
        int da = (a.first == track_idx_) ? 0 : std::abs(a.first - track_idx_);
        int db = (b.first == track_idx_) ? 0 : std::abs(b.first - track_idx_);
        return da < db;
    });
    for (const auto& [i, r] : cards) {
        if (r.contains(pos)) {
            if (i != track_idx_) select_track(i);
            play_full();
            return;
        }
    }
}

void FreePlayScreen::mouse_wheel(PointF pos, int delta) {
    if (delta == 0) return;
    int step = (delta > 0) ? -1 : 1;
    if (pos.y < 230.0f) select_category(cat_idx_ + step);
    else select_track(track_idx_ + step);
}

bool FreePlayScreen::key_press(int vk, bool, bool) {
    if (vk == key::Up || vk == 'W') { select_track(track_idx_ - 1); return true; }
    if (vk == key::Down || vk == 'S') { select_track(track_idx_ + 1); return true; }
    if (vk == key::Left || vk == 'A') { select_category(cat_idx_ - 1); return true; }
    if (vk == key::Right || vk == 'D') { select_category(cat_idx_ + 1); return true; }
    if (vk == key::Enter) { play_full(); return true; }
    if (vk == key::Space) {
        if (!current_track()) return true;
        if (previewing_) stop_preview();
        else start_preview(0, true);
        return true;
    }
    if (vk == key::Tab) { open_detail(); return true; }
    if (vk == key::Escape) { app_.show_screen("title", "curtain"); return true; }
    return false;
}

void FreePlayScreen::help_action(const std::string& action) {
    if (action == "title") app_.show_screen("title", "curtain");
    else if (action == "select") {
        if (!tracks_.empty()) select_track((track_idx_ + 1) % static_cast<int>(tracks_.size()));
    } else if (action == "category") {
        if (!categories_.empty()) select_category((cat_idx_ + 1) % static_cast<int>(categories_.size()));
    } else if (action == "play") play_full();
    else if (action == "detail") open_detail();
}

void FreePlayScreen::set_preview_seconds(int sec) {
    app_.settings().set("preview_seconds", sec);
    if (app_.audio().previewing()) {
        double end = preview_started_at_ + sec;
        preview_end_at_ = end;
        int rem_ms = static_cast<int>(std::max(0.0, end - now_sec()) * 1000.0);
        app_.audio().set_preview_remaining(rem_ms);
    }
}

void FreePlayScreen::play_full() {
    const Track* t = current_track();
    if (!t) return;
    app_.settings().set("last_track", t->id);
    ++preview_gen_;
    pending_preview_at_ = 0.0;
    app_.audio().stop();
    previewing_ = false;
    app_.open_player(t);
}

void FreePlayScreen::open_detail() {
    const Track* t = current_track();
    if (!t) return;
    has_detail_return_ = true;
    detail_ret_cat_ = cat_idx_;
    detail_ret_track_ = track_idx_;
    app_.open_detail(t);
}

} // namespace np
