#include "gallery_screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace np {

namespace {
struct CatDef { const char* id; const char* label; };
constexpr int kCatCount = 5;
const CatDef kCategories[kCatCount] = {
    {"curtain", "谢幕 CG"},
    {"original", "CG 原画"},
    {"story", "剧情 · 尾声"},
    {"tachie", "立绘"},
    {"diff", "差分"},
};

std::string cat_label(const std::string& id) {
    for (const auto& c : kCategories) if (id == c.id) return c.label;
    return "";
}

std::string char_label(const std::string& id) {
    if (id == "Liora") return "莉奥拉";
    if (id == "Aleph") return "阿莱夫";
    if (id == "Alice") return "爱丽丝";
    return id;
}
} // namespace

GalleryScreen::GalleryScreen(AppContext& app) : Screen(app) {}

void GalleryScreen::enter(const Track*, const std::string&, const std::string& category, int index, bool) {
    if (!bg_built_) build_background();
    if (!card_bg_cg_ || !card_bg_small_) build_card_backgrounds();
    t_ = 0.0f;
    fade_ = 0.0f;
    if (!category.empty() && !cat_label(category).empty()) {
        cat_ = category;
    }
    filter_ = "all";
    page_ = 0;
    selected_ = 0;
    refresh();
    if (index > 0 && !items_.empty()) {
        selected_ = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
        page_ = selected_ / per_page();
    }
    tab_focus_ = static_cast<float>(tab_index());
    target_pill_w_ = target_pill_w();
    pill_w_ = target_pill_w_;
    grid_anim_ = 0.0f;
    grid_dir_ = 0;
    app_.set_cursor_hidden(false);
}

void GalleryScreen::tick(float dt, double) {
    if (!bg_built_) build_background();
    t_ += dt;
    fade_ = std::min(1.0f, fade_ + dt / 0.58f);
    int ti = tab_index();
    tab_focus_ += (ti - tab_focus_) * (1.0f - std::exp(-dt / 0.15f));
    if (std::abs(ti - tab_focus_) < 0.002f) tab_focus_ = static_cast<float>(ti);
    target_pill_w_ = target_pill_w();
    pill_w_ += (target_pill_w_ - pill_w_) * (1.0f - std::exp(-dt / 0.18f));
    grid_anim_ = std::min(1.0f, grid_anim_ + dt / 0.42f);

    float hk = 1.0f - std::exp(-dt / 0.085f);
    for (auto it = card_hover_anim_.begin(); it != card_hover_anim_.end(); ) {
        float target = (it->first == hover_card_) ? 1.0f : 0.0f;
        it->second += (target - it->second) * hk;
        if (target == 0.0f && it->second < 0.005f) it = card_hover_anim_.erase(it);
        else ++it;
    }
    if (hover_card_ >= 0 && card_hover_anim_.find(hover_card_) == card_hover_anim_.end()) {
        card_hover_anim_[hover_card_] = hk;
    }
}

bool GalleryScreen::animating() const {
    return fade_ < 0.999f || grid_anim_ < 0.999f ||
           std::abs(tab_focus_ - tab_index()) > 0.002f ||
           std::abs(pill_w_ - target_pill_w_) > 0.5f || !card_hover_anim_.empty();
}

bool GalleryScreen::is_cg() const {
    return cat_ == "curtain" || cat_ == "original" || cat_ == "story";
}

int GalleryScreen::pages() const {
    return std::max(1, static_cast<int>(std::ceil(static_cast<double>(items_.size()) / per_page())));
}

int GalleryScreen::tab_index() const {
    for (int i = 0; i < kCatCount; ++i) if (cat_ == kCategories[i].id) return i;
    return 0;
}

float GalleryScreen::target_pill_w() {
    return app_.gfx().text_width(kCategories[tab_index()].label, {23.0f, true}) + 38.0f;
}

void GalleryScreen::refresh() {
    items_.clear();
    auto it = app_.library().gallery.find(cat_);
    if (it != app_.library().gallery.end()) {
        for (const auto& item : it->second) {
            if (is_cg()) {
                if (filter_ == "all" || item.act == filter_) items_.push_back(item);
            } else {
                if (filter_ == "all" || item.char_name == filter_) items_.push_back(item);
            }
        }
    }
    selected_ = items_.empty() ? 0 : std::min(selected_, static_cast<int>(items_.size()) - 1);
}

std::vector<std::pair<std::string, std::string>> GalleryScreen::filter_options() const {
    std::vector<std::pair<std::string, std::string>> out = {{"all", "全部"}};
    auto it = app_.library().gallery.find(cat_);
    if (it == app_.library().gallery.end()) return out;

    if (is_cg()) {
        std::vector<std::string> codes;
        for (const auto& item : it->second) {
            if (!item.act.empty() && std::find(codes.begin(), codes.end(), item.act) == codes.end()) {
                codes.push_back(item.act);
            }
        }
        const auto& ord = app_.library().act_order;
        std::sort(codes.begin(), codes.end(), [&](const std::string& a, const std::string& b) {
            auto ia = ord.find(a);
            auto ib = ord.find(b);
            int oa = (ia != ord.end()) ? ia->second : 99;
            int ob = (ib != ord.end()) ? ib->second : 99;
            return oa < ob;
        });
        for (const auto& c : codes) {
            const Chapter* ch = app_.library().chapter(c);
            out.push_back({c, ch ? ch->label : c});
        }
        return out;
    }

    struct CharMap { const char* k; const char* zh; };
    const CharMap cm[3] = {{"Liora", "莉奥拉"}, {"Aleph", "阿莱夫"}, {"Alice", "爱丽丝"}};
    for (const auto& c : cm) {
        bool present = false;
        for (const auto& item : it->second) {
            if (item.char_name == c.k) { present = true; break; }
        }
        if (present) out.push_back({c.k, c.zh});
    }
    return out;
}

std::vector<std::pair<int, RectF>> GalleryScreen::tab_rects() const {
    std::vector<std::pair<int, RectF>> out;
    for (int i = 0; i < kCatCount; ++i) {
        out.push_back({i, {TAB_X + i * TAB_SLOT, TAB_Y, TAB_SLOT - 10.0f, 60.0f}});
    }
    return out;
}

std::vector<std::pair<int, RectF>> GalleryScreen::filter_rects() {
    std::vector<std::pair<int, RectF>> out;
    float x = 70.0f;
    FontKey f{14.0f, false};
    auto opts = filter_options();
    for (int i = 0; i < static_cast<int>(opts.size()); ++i) {
        float w = app_.gfx().text_width(opts[i].second, f) + 24.0f;
        out.push_back({i, {x, 230.0f, w, 30.0f}});
        x += w + 8.0f;
    }
    return out;
}

std::vector<std::pair<int, RectF>> GalleryScreen::card_rects() const {
    std::vector<std::pair<int, RectF>> out;
    int pp = per_page();
    int start = page_ * pp;
    int col_cnt = cols();
    for (int j = 0; j < pp; ++j) {
        int idx = start + j;
        if (idx >= static_cast<int>(items_.size())) break;
        int row = j / col_cnt;
        int col = j % col_cnt;
        RectF r;
        if (is_cg()) {
            r = {grid_left() + col * 600.0f, 292.0f + row * 358.0f, 540.0f, 344.0f};
        } else {
            r = {grid_left() + col * 352.0f, 286.0f + row * 350.0f, 320.0f, 330.0f};
        }
        if (idx == selected_ || hover_card_ == idx) r = r.translated(0.0f, -5.0f);
        out.push_back({idx, r});
    }
    return out;
}

std::vector<GalleryScreen::HelpItem> GalleryScreen::help_rects() {
    auto& gfx = app_.gfx();
    FontKey kf{11.0f, false};
    FontKey lf{13.0f, false};
    struct Def { const char* a; const char* k; const char* l; };
    Def defs[] = {
        {"title", "ESC", "返回"},
        {"cat", "←→", "分类"},
        {"page", "↑↓", "翻页"},
        {"view", "Enter", "查看"},
        {"full", "F", "全屏"},
    };
    std::vector<HelpItem> out;
    float x = grid_left() + 12.0f, y = 1012.0f;
    for (const auto& d : defs) {
        float kw = std::max(26.0f, gfx.text_width(d.k, kf) + 14.0f);
        float lw = gfx.text_width(d.l, lf);
        RectF r{x, y, kw + lw + 20.0f, 32.0f};
        out.push_back({d.a, r, d.k, d.l});
        x = r.right() + 6.0f;
    }
    return out;
}

void GalleryScreen::build_background() {
    auto& gfx = app_.gfx();
    auto& assets = app_.assets();
    bg_cache_ = gfx.create_target_bitmap(1920, 1080);
    if (!bg_cache_) return;

    gfx.set_target(bg_cache_.Get());
    gfx.dc()->BeginDraw();
    gfx.dc()->SetTransform(D2D1::Matrix3x2F::Identity());
    gfx.dc()->Clear(D2D1::ColorF(28 / 255.f, 23 / 255.f, 20 / 255.f, 1.0f));

    if (ID2D1Bitmap1* bg = assets.ui("Collection_bg.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(bg, {0, 0, 1920, 1080}, 1.0f, true);
    }
    gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(12, 8, 6, 86));
    if (ID2D1Bitmap1* stage = assets.ui("PZImgTitleBackground.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(stage, {0, 0, 1920, 1080}, 0.36f, true);
    }
    if (ID2D1Bitmap1* flower = assets.ui("PZImgTitleBackgroundFlower.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(flower, {0, 0, 1920, 1080}, 0.24f, true);
    }
    if (ID2D1Bitmap1* light = assets.ui("PZImgTitleBackgroundLight.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(light, {0, 0, 1920, 1080}, 0.34f, true);
    }
    if (ID2D1Bitmap1* curtain_side = assets.ui("PZImgCommonCurtainHorizon.png", 360, 1080, "stretch")) {
        gfx.draw_bitmap(curtain_side, {-40.0f, 0.0f, 360.0f, 1080.0f}, 0.56f, true);
        gfx.save_transform();
        gfx.translate(1920.0f, 0.0f);
        gfx.scale(-1.0f, 1.0f);
        gfx.draw_bitmap(curtain_side, {-40.0f, 0.0f, 360.0f, 1080.0f}, 0.56f, true);
        gfx.restore_transform();
    }
    gfx.fill_rect({0, 0, 1920, 142}, ColorF::rgba(8, 6, 5, 190));
    if (ID2D1Bitmap1* film = assets.ui("film.png", 1920, 142, "stretch")) {
        gfx.draw_bitmap(film, {0, 0, 1920, 142}, 0.72f, true);
    }
    if (ID2D1Bitmap1* valance = assets.ui("PZImgLobbyCurtain.png", 1920, 138, "stretch")) {
        gfx.draw_bitmap(valance, {0.0f, -6.0f, 1920.0f, 138.0f}, 0.76f, true);
    }
    Gfx::GradStop gs[2] = {{0.0f, ColorF::rgba(8, 6, 5, 195)}, {1.0f, ColorF::rgba(8, 6, 5, 0)}};
    gfx.fill_linear_grad({0, 100, 1920, 95}, {0, 100}, {0, 195}, gs, 2);

    if (ID2D1Bitmap1* vig = assets.ui("PZImgVignetting.png", 1920, 1080, "cover")) {
        gfx.draw_bitmap(vig, {0, 0, 1920, 1080}, 0.78f, true);
    }
    if (ID2D1Bitmap1* deco = assets.ui("PZImgLabelDeco.png", 344, 68, "contain")) {
        gfx.draw_bitmap(deco, {1520.0f, 22.0f, 344.0f, 68.0f}, 0.82f, true);
    }
    FontKey gtitle_f{46.0f, true, 400, 0.5f};
    gfx.text(56, 72, "CG GALLERY", gtitle_f, ColorF::rgba(239, 228, 211, 252));
    gfx.text(56 + gfx.text_width("CG GALLERY", gtitle_f) + 22.0f, 74, "C G 鉴 赏",
             {20.0f, true, 400, 1.2f}, ColorF::rgba(198, 176, 150, 238));
    gfx.draw_gold_divider(56.0f, 510.0f, 88.0f, 0.92f, 2.0f);
    gfx.fill_ellipse({497, 88}, 2.6f, 2.6f, ColorF::rgba(206, 166, 108, 210));

    gfx.dc()->EndDraw();
    gfx.restore_scene_target();
    bg_built_ = true;
}

void GalleryScreen::build_card_backgrounds() {
    auto& gfx = app_.gfx();
    auto build = [&](int w, int h, bool cg) -> ComPtr<ID2D1Bitmap1> {
        auto bmp = gfx.create_target_bitmap(w * 2, h * 2);
        if (!bmp) return {};
        gfx.set_target(bmp.Get());
        gfx.dc()->BeginDraw();
        gfx.dc()->SetTransform(D2D1::Matrix3x2F::Scale(2.0f, 2.0f));
        gfx.dc()->Clear(D2D1::ColorF(0, 0, 0, 0));
        if (cg) {
            Gfx::GradStop paper[3] = {
                {0.00f, ColorF::rgba(234, 226, 213, 255)},
                {0.56f, ColorF::rgba(222, 212, 198, 255)},
                {1.00f, ColorF::rgba(203, 192, 176, 255)},
            };
            gfx.fill_rounded_linear_grad({0, 0, 540, 344}, 4.0f, {0, 0}, {0, 344}, paper, 3);
            float sy = 294.0f;
            gfx.draw_line({12, sy + 0.6f}, {528, sy + 0.6f}, ColorF::rgba(176, 141, 87, 185), 1.2f);
            gfx.draw_line({12, sy + 2.0f}, {528, sy + 2.0f}, ColorF::rgba(255, 250, 240, 118), 1.0f);
            gfx.fill_rounded({12, sy + 12, 3, 28}, 1.5f, ColorF::rgba(176, 141, 87, 232));
        } else {
            gfx.fill_rounded({0, 0, 320, 330}, 4.0f, ColorF::rgba(221, 212, 198, 255));
            float sy = 278.0f;
            gfx.draw_line({14, sy + 0.6f}, {306, sy + 0.6f}, ColorF::rgba(176, 141, 87, 175), 1.1f);
            gfx.draw_line({14, sy + 1.9f}, {306, sy + 1.9f}, ColorF::rgba(255, 250, 240, 105), 1.0f);
            gfx.fill_rounded({16, sy + 10, 3, 26}, 1.5f, ColorF::rgba(176, 141, 87, 215));
        }
        gfx.dc()->EndDraw();
        gfx.restore_scene_target();
        return bmp;
    };
    card_bg_cg_ = build(540, 344, true);
    card_bg_small_ = build(320, 330, false);
}

void GalleryScreen::paint(Gfx& gfx, double) {
    if (bg_cache_) gfx.draw_bitmap(bg_cache_.Get(), {0, 0, 1920, 1080}, 1.0f, true);

    paint_tabs(gfx);
    paint_filters(gfx);
    paint_grid(gfx);
    paint_now_playing(gfx);
    paint_pager(gfx);
    paint_guide(gfx);

    if (fade_ < 1.0f) {
        float u = 1.0f - fade_;
        gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(0, 0, 0, static_cast<int>(255 * u * u)));
    }
}

void GalleryScreen::paint_tabs(Gfx& gfx) {
    gfx.velvet_panel({58.0f, 150.0f, 1004.0f, 52.0f}, 12.0f, 0.88f);
    float focus = tab_focus_;
    if (focus >= 0.0f && focus <= static_cast<float>(kCatCount - 1) + 0.001f) {
        float fx = TAB_X + focus * TAB_SLOT + (TAB_SLOT - 10.0f) * 0.5f;
        RectF pill(fx - pill_w_ * 0.5f, 156, pill_w_, 40);
        gfx.draw_glass_pill(pill, 19.0f, false, true, 0.96f, false);
        gfx.draw_gold_divider(fx - pill_w_ * 0.5f + 12, fx + pill_w_ * 0.5f - 12, 199, 1.0f, 2.0f);
        gfx.fill_ellipse({fx, 205}, 2.8f, 2.8f, ColorF::rgba(220, 178, 114, 255));
    }

    ColorF inactive = ColorF::rgba(176, 162, 146, 195);
    ColorF active = ColorF::rgba(242, 228, 208, 255);
    ColorF hover_col = ColorF::rgba(202, 188, 168, 242);

    for (const auto& [i, r] : tab_rects()) {
        float prox = clampf(1.0f - std::abs(i - focus));
        bool hover = (i == hover_tab_);
        FontKey f{19.0f + 4.0f * prox, true, 400, 0.3f};
        ColorF col = mix_color(inactive, active, prox);
        if (hover && prox < 0.5f) col = mix_color(col, hover_col, 0.7f);
        std::string lbl = kCategories[i].label;
        float tw = gfx.text_width(lbl, f);
        float cx = r.cx();
        gfx.text(cx - tw * 0.5f + 1, r.y + 33, lbl, f, ColorF::rgba(0, 0, 0, 150));
        gfx.text(cx - tw * 0.5f, r.y + 32, lbl, f, col);
    }
}

void GalleryScreen::paint_filters(Gfx& gfx) {
    auto opts = filter_options();
    auto rects = filter_rects();
    for (size_t k = 0; k < rects.size(); ++k) {
        int i = rects[k].first;
        const RectF& r = rects[k].second;
        bool active = (opts[i].first == filter_);
        bool hover = (i == hover_filter_);
        gfx.draw_glass_pill(r, 6.0f, active, hover, 1.0f, false);
        ColorF col = active ? ColorF::rgba(32, 22, 16, 255) : ColorF::rgba(232, 218, 198, hover ? 255 : 238);
        gfx.text_center(r.cx(), r.y + 21, opts[i].second, {14.0f, false, active ? 600 : 400}, col);
    }
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "%zu 张", items_.size());
    FontKey cf{14.5f, false, 400, 0.3f};
    float cw = gfx.text_width(cbuf, cf) + 24.0f;
    RectF cp(1840.0f - cw, 238.0f, cw, 24.0f);
    gfx.draw_glass_pill(cp, 12.0f, false, false, 1.0f, false);
    gfx.fill_ellipse({cp.x + 10, cp.cy()}, 2.0f, 2.0f, ColorF::rgba(214, 174, 112, 230));
    gfx.text_center(cp.cx() + 3, cp.cy() + 5, cbuf, cf, ColorF::rgba(222, 206, 186, 244));
}

void GalleryScreen::paint_grid(Gfx& gfx) {
    auto rects = card_rects();
    int per = per_page();
    for (size_t k = 0; k < rects.size(); ++k) {
        int idx = rects[k].first;
        RectF r = rects[k].second;
        if (grid_anim_ < 0.999f) {
            float delay = std::min(0.28f, (static_cast<float>(k) / std::max(1, per)) * 0.24f);
            float t = clampf((grid_anim_ - delay) / std::max(0.1f, 1.0f - delay));
            float e = ease_out(t);
            if (grid_dir_ != 0) r = r.translated(grid_dir_ * (1.0f - e) * 44.0f, 0.0f);
            else r = r.translated(0.0f, (1.0f - e) * 26.0f);
        }
        paint_card(gfx, idx, r);
    }
}

void GalleryScreen::paint_card(Gfx& gfx, int idx, const RectF& r) {
    const GalleryItem& item = items_[idx];
    auto& assets = app_.assets();
    bool hover = (idx == hover_card_);
    bool selected = (idx == selected_);
    float h = card_hover_anim_.count(idx) ? card_hover_anim_.at(idx) : (hover ? 1.0f : 0.0f);
    RectF rr = r.translated(0.0f, -5.0f * h);

    if (h > 0.01f || selected) {
        float glow_a = std::max(h, selected ? 0.65f : 0.0f);
        gfx.fill_rounded(rr.adjusted(-10, -10, 10, 10), 12.0f, ColorF::rgba(214, 176, 112, static_cast<int>(26 * glow_a)));
        gfx.fill_rounded(rr.adjusted(-5, -5, 5, 5), 8.0f, ColorF::rgba(214, 176, 112, static_cast<int>(30 * glow_a)));
    }
    gfx.fill_rounded(rr.adjusted(4, 7 + 3.0f * h, 4, 9 + 4.0f * h), 5.0f, ColorF::rgba(0, 0, 0, static_cast<int>(85 + 55 * h)));

    if (is_cg()) {
        // Parchment body and info-strip details are cached once; drawing them
        // per frame used six extra gradient brushes and cost ~17% of the
        // gallery frame budget.
        if (card_bg_cg_) {
            gfx.draw_bitmap(card_bg_cg_.Get(), rr, 1.0f, true);
        } else {
            gfx.fill_rounded(rr, 4.0f, ColorF::rgba(222, 212, 198, 255));
        }

        RectF art(rr.x, rr.y, rr.w, 294);
        if (ID2D1Bitmap1* thumb = assets.thumb(&item, 480, 270)) {
            gfx.push_clip(art);
            RectF dst = (h > 0.01f) ? art.adjusted(-6.0f * h, -4.5f * h, 6.0f * h, 4.5f * h) : art;
            gfx.draw_bitmap(thumb, dst, 1.0f, true);
            gfx.pop_clip();
        }
        gfx.draw_rect(art, ColorF::rgba(110, 92, 75, 205), 1.2f);

        float sy = art.bottom();

        std::string tag = (cat_ == "curtain") ? "CURTAIN CALL" : cat_label(cat_);
        FontKey tagf{10.5f, false, 400, 1.25f};
        float chipw = gfx.text_width(tag, tagf) + 20.0f;
        RectF chip(rr.right() - 16.0f - chipw, sy + 10.0f, chipw, 22.0f);
        gfx.fill_rounded(chip, 11.0f, ColorF::rgba(190, 150, 94, 238));
        gfx.draw_rounded(chip, 11.0f, ColorF::rgba(250, 232, 196, 235), 1.0f);
        gfx.draw_line({chip.x + 6, chip.y + 1.2f}, {chip.right() - 6, chip.y + 1.2f},
                      ColorF::rgba(255, 246, 224, 72), 1.0f);
        gfx.text_center(chip.cx(), chip.cy() + 4.0f, tag, tagf, ColorF::rgba(40, 29, 21, 255));

        FontKey tf{19.0f, true};
        float maxw = rr.w - 24.0f - 16.0f - chip.w - 12.0f;
        std::string title = gfx.elide_text(item.title, tf, std::max(80.0f, maxw));
        gfx.text(rr.x + 24, sy + 33.0f, title, tf, ColorF::rgba(56, 45, 38, 255));

        char dbuf[32];
        snprintf(dbuf, sizeof(dbuf), "%d × %d", item.w, item.h);
        gfx.text(rr.x + 24, sy + 47.0f, dbuf, {10.5f, false, 400, 0.5f},
                 ColorF::rgba(118, 99, 82, 235));

        if (h > 0.02f) {
            RectF chip2(art.right() - 86, art.bottom() - 38 + (1.0f - h) * 6.0f, 72, 26);
            gfx.draw_glass_pill(chip2, 13.0f, true, true, h, false);
            gfx.text_center(chip2.cx(), chip2.cy() + 5.0f, "查看", {12.0f, false, 600},
                            ColorF::rgba(38, 27, 20, static_cast<int>(255 * h)));
        }
        ColorF border = mix_color(ColorF::rgba(151, 129, 105, 255), ColorF::rgba(226, 188, 130, 255), selected ? 1.0f : h);
        gfx.draw_rounded(rr, 4.0f, border, 1.3f + 1.1f * (selected ? 1.0f : h));
        if (h > 0.02f || selected) {
            gfx.draw_corner_filigree(rr, 12.0f, ColorF::rgba(246, 224, 178, static_cast<int>(220 * std::max(h, selected ? 0.7f : 0.0f))), 1.6f, 2.5f);
        }
    } else {
        if (card_bg_small_) {
            gfx.draw_bitmap(card_bg_small_.Get(), rr, 1.0f, true);
        } else {
            gfx.fill_rounded(rr, 4.0f, ColorF::rgba(221, 212, 198, 255));
        }

        RectF art(rr.x + 10, rr.y + 10, rr.w - 20, 268);
        Gfx::GradStop gs[2] = {{0.0f, ColorF::rgba(70, 56, 48, 255)}, {1.0f, ColorF::rgba(24, 19, 17, 255)}};
        gfx.fill_radial_grad_rect(art, {art.cx(), art.y + 100.0f}, art.w * 0.82f, art.w * 0.82f, gs, 2);

        if (ID2D1Bitmap1* thumb = assets.thumb(&item, 300, 400)) {
            D2D1_SIZE_F tsz = thumb->GetSize();
            float th = art.h;
            float tw = tsz.width * (th / std::max(1.0f, tsz.height));
            gfx.push_clip(art);
            gfx.draw_bitmap(thumb, {art.cx() - tw * 0.5f, art.y, tw, th}, 1.0f, true);
            gfx.pop_clip();
        }
        gfx.draw_rect(art, ColorF::rgba(110, 92, 75, 185), 1.0f);

        float sy = art.bottom();

        std::string tag = !item.char_name.empty() ? char_label(item.char_name) : cat_label(cat_);
        FontKey tagf{10.0f, false, 400, 0.8f};
        float chipw = gfx.text_width(tag, tagf) + 18.0f;
        RectF chip(rr.right() - 14.0f - chipw, sy + 8.0f, chipw, 20.0f);
        gfx.fill_rounded(chip, 10.0f, ColorF::rgba(190, 150, 94, 232));
        gfx.draw_rounded(chip, 10.0f, ColorF::rgba(250, 232, 196, 225), 1.0f);
        gfx.text_center(chip.cx(), chip.cy() + 4.0f, tag, tagf, ColorF::rgba(40, 29, 21, 255));

        FontKey tf{17.0f, true};
        float maxw = rr.w - 24.0f - 14.0f - chip.w - 10.0f;
        std::string title = gfx.elide_text(item.title, tf, std::max(60.0f, maxw));
        gfx.text(rr.x + 24, sy + 26.0f, title, tf, ColorF::rgba(56, 45, 38, 255));

        char dbuf[32];
        snprintf(dbuf, sizeof(dbuf), "%d × %d", item.w, item.h);
        gfx.text(rr.x + 24, sy + 42.0f, dbuf, {10.0f, false, 400, 0.4f},
                 ColorF::rgba(118, 99, 82, 230));

        if (h > 0.02f) {
            RectF chip2(art.right() - 70, art.bottom() - 34 + (1.0f - h) * 6.0f, 56, 24);
            gfx.draw_glass_pill(chip2, 12.0f, true, true, h, false);
            gfx.text_center(chip2.cx(), chip2.cy() + 5.0f, "查看", {11.0f, false, 600},
                            ColorF::rgba(38, 27, 20, static_cast<int>(255 * h)));
        }
        ColorF border = mix_color(ColorF::rgba(151, 129, 105, 255), ColorF::rgba(226, 188, 130, 255), selected ? 1.0f : h);
        gfx.draw_rounded(rr, 4.0f, border, 1.3f + 1.1f * (selected ? 1.0f : h));
        if (h > 0.02f || selected) {
            gfx.draw_corner_filigree(rr, 10.0f, ColorF::rgba(246, 224, 178, static_cast<int>(220 * std::max(h, selected ? 0.7f : 0.0f))), 1.5f, 2.5f);
        }
    }
}

void GalleryScreen::paint_now_playing(Gfx& gfx) {
    auto& audio = app_.audio();
    const Track* track = audio.track();
    if (!track || !audio.full_mode()) return;

    RectF r = now_rect();
    gfx.velvet_panel(r, 7.0f);
    if (ID2D1Bitmap1* cov = app_.assets().cover(track, 34)) {
        gfx.draw_bitmap(cov, {r.x + 8, r.y + 7, 34, 34}, 1.0f, true);
    }
    FontKey tf{15.0f, true};
    gfx.text(r.x + 52, r.y + 21, gfx.elide_text(track->title, tf, 430.0f), tf, ColorF::rgba(240, 228, 208, 250));
    FontKey af{11.0f, false};
    gfx.text(r.x + 52, r.y + 38, gfx.elide_text(track->artist, af, 430.0f), af, ColorF::rgba(188, 168, 144, 230));
    std::string state = audio.playing() ? "播放中" : "已暂停";
    FontKey sf{12.0f, false};
    gfx.text(r.right() - 14.0f - gfx.text_width(state, sf), r.y + 30, state, sf, ColorF::rgba(216, 178, 116, 235));
}

void GalleryScreen::paint_pager(Gfx& gfx) {
    int pcnt = pages();
    if (pcnt <= 1) return;
    bool can_prev = page_ > 0;
    bool can_next = page_ < pcnt - 1;
    gfx.draw_icon_button({34, 560, 44, 44}, "left", can_prev && hover_page_ == -1,
                         ColorF::rgba(234, 218, 198, can_prev ? 235 : 70), 40.0f);
    gfx.draw_icon_button({1842, 560, 44, 44}, "right", can_next && hover_page_ == 1,
                         ColorF::rgba(234, 218, 198, can_next ? 235 : 70), 40.0f);
    char pbuf[32];
    snprintf(pbuf, sizeof(pbuf), "%d / %d", page_ + 1, pcnt);
    FontKey pf{14.5f, false, 400, 0.3f};
    float pw = gfx.text_width(pbuf, pf) + 24.0f;
    RectF pp(1840.0f - pw, 1018.0f, pw, 32.0f);
    gfx.draw_glass_pill(pp, 16.0f, false, false, 1.0f, false);
    gfx.text_center(pp.cx(), pp.cy() + 5, pbuf, pf, ColorF::rgba(222, 206, 186, 244));
}

void GalleryScreen::paint_guide(Gfx& gfx) {
    auto rects = help_rects();
    if (rects.empty()) return;
    float gx = grid_left();
    RectF panel(gx, 1004, rects.back().rect.right() + 10.0f - gx, 46);
    gfx.velvet_panel(panel, 8.0f);
    for (const auto& h : rects) {
        bool hov = (hover_help_ == h.action);
        gfx.draw_glass_pill(h.rect, 6.0f, false, hov, 1.0f, false);
        float kw = gfx.draw_keycap(h.rect.x + 4, h.rect.y + 6, h.key, {11.0f, false}, 20.0f);
        gfx.text(h.rect.x + 10.0f + kw, h.rect.y + 21, h.label, {13.0f, false},
                 hov ? ColorF::rgba(246, 232, 212, 250) : ColorF::rgba(222, 208, 188, 238));
    }
}

void GalleryScreen::mouse_move(PointF pos) {
    hover_tab_ = -1;
    hover_filter_ = -1;
    hover_card_ = -1;
    hover_page_ = 0;
    hover_help_.clear();

    for (const auto& h : help_rects()) {
        if (h.rect.contains(pos)) { hover_help_ = h.action; return; }
    }
    for (const auto& [i, r] : tab_rects()) {
        if (r.contains(pos)) { hover_tab_ = i; return; }
    }
    for (const auto& [i, r] : filter_rects()) {
        if (r.contains(pos)) { hover_filter_ = i; return; }
    }
    for (const auto& [idx, r] : card_rects()) {
        if (r.contains(pos)) { hover_card_ = idx; return; }
    }
    if (page_ > 0 && RectF(34, 560, 44, 44).contains(pos)) hover_page_ = -1;
    else if (page_ < pages() - 1 && RectF(1842, 560, 44, 44).contains(pos)) hover_page_ = 1;
}

void GalleryScreen::mouse_press(PointF pos, MouseButton btn) {
    if (btn != MouseButton::Left) return;
    if (app_.audio().full_mode() && app_.audio().track() != nullptr && now_rect().contains(pos)) {
        app_.open_player(app_.audio().track());
        return;
    }
    for (const auto& h : help_rects()) {
        if (h.rect.contains(pos)) { help_action(h.action); return; }
    }
    for (const auto& [i, r] : tab_rects()) {
        if (r.contains(pos)) { set_cat(kCategories[i].id); return; }
    }
    auto opts = filter_options();
    for (const auto& [i, r] : filter_rects()) {
        if (r.contains(pos)) { set_filter(opts[i].first); return; }
    }
    for (const auto& [idx, r] : card_rects()) {
        if (r.contains(pos)) {
            selected_ = idx;
            app_.open_viewer(cat_, idx, &items_, cat_label(cat_));
            return;
        }
    }
    if (page_ > 0 && RectF(34, 560, 44, 44).contains(pos)) goto_page(page_ - 1, -1);
    else if (page_ < pages() - 1 && RectF(1842, 560, 44, 44).contains(pos)) goto_page(page_ + 1, 1);
}

void GalleryScreen::mouse_wheel(PointF, int delta) {
    if (delta == 0) return;
    int d = (delta > 0) ? -1 : 1;
    goto_page(page_ + d, d);
}

bool GalleryScreen::key_press(int vk, bool, bool) {
    if (vk == key::Escape) { app_.show_screen("title", "curtain"); return true; }
    if (vk == key::Left || vk == 'A' || vk == key::Right || vk == 'D') {
        int d = (vk == key::Left || vk == 'A') ? -1 : 1;
        int idx = (tab_index() + d + kCatCount) % kCatCount;
        set_cat(kCategories[idx].id);
        return true;
    }
    if (vk == key::Up || vk == 'W' || vk == key::Down || vk == 'S' || vk == key::PageUp || vk == key::PageDown) {
        int d = (vk == key::Up || vk == 'W' || vk == key::PageUp) ? -1 : 1;
        goto_page(page_ + d, d);
        return true;
    }
    if (vk == key::Enter) {
        if (!items_.empty()) app_.open_viewer(cat_, selected_, &items_, cat_label(cat_));
        return true;
    }
    if (vk == 'F') { app_.toggle_fullscreen(); return true; }
    if (vk >= '1' && vk <= ('0' + kCatCount)) {
        int i = vk - '1';
        set_cat(kCategories[i].id);
        return true;
    }
    return false;
}

void GalleryScreen::set_cat(const std::string& cat) {
    if (cat == cat_) return;
    cat_ = cat;
    filter_ = "all";
    page_ = 0;
    selected_ = 0;
    refresh();
    target_pill_w_ = target_pill_w();
    grid_anim_ = 0.0f;
    grid_dir_ = 0;
}

void GalleryScreen::set_filter(const std::string& code) {
    if (code == filter_) return;
    filter_ = code;
    page_ = 0;
    selected_ = 0;
    refresh();
    target_pill_w_ = target_pill_w();
    grid_anim_ = 0.0f;
    grid_dir_ = 0;
}

void GalleryScreen::goto_page(int page, int dir) {
    page = std::clamp(page, 0, pages() - 1);
    if (page == page_) return;
    page_ = page;
    selected_ = page_ * per_page();
    grid_anim_ = 0.0f;
    grid_dir_ = (dir > 0) ? 1 : (dir < 0 ? -1 : 0);
}

void GalleryScreen::help_action(const std::string& action) {
    if (action == "title") app_.show_screen("title", "curtain");
    else if (action == "cat") set_cat(kCategories[(tab_index() + 1) % kCatCount].id);
    else if (action == "page") {
        int nxt = (page_ < pages() - 1) ? (page_ + 1) : 0;
        goto_page(nxt, nxt > 0 ? 1 : 0);
    } else if (action == "view") {
        if (!items_.empty()) app_.open_viewer(cat_, selected_, &items_, cat_label(cat_));
    } else if (action == "full") {
        app_.toggle_fullscreen();
    }
}

} // namespace np
