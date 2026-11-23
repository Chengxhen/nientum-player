#include "detail_overlay.hpp"
#include "../core/paths.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace np {

TrackDetailOverlay::TrackDetailOverlay(AppContext& app, const Track* track)
    : Overlay(app), track_(track) {}

void TrackDetailOverlay::tick(float dt, double) {
    fade_ = std::min(1.0f, fade_ + dt / 0.24f);
}

void TrackDetailOverlay::paint(Gfx& gfx, double) {
    if (!track_) return;
    float e = ease_out(fade_);
    gfx.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(0, 0, 0, static_cast<int>(182 * e)));

    RectF panel = panel_;
    gfx.save_transform();
    float sc = 0.94f + 0.06f * e;
    gfx.translate(0.0f, (1.0f - e) * 18.0f);
    gfx.scale(sc, sc, panel.center());
    gfx.fill_rounded(panel.adjusted(6, 12, 6, 16), 8.0f, ColorF::rgba(0, 0, 0, static_cast<int>(96 * e)));
    ColorF pf = ColorF::rgba(18, 14, 12, static_cast<int>(244 * e));
    ColorF pb = ColorF::rgba(184, 148, 98, static_cast<int>(185 * e));
    gfx.rounded(panel, 6.0f, &pf, &pb, 1.2f);
    gfx.draw_rounded(panel.adjusted(8, 8, -8, -8), 4.0f, ColorF::rgba(196, 160, 108, static_cast<int>(76 * e)), 1.0f);

    gfx.text(panel.x + 48, panel.y + 64, "曲目详情", {30.0f, true}, palette::CREAM);
    gfx.text(panel.x + 50, panel.y + 92, "TRACK DETAIL", {13.0f, false, 400, 2.5f}, ColorF::rgba(198, 168, 128, 220));
    gfx.draw_icon_button(close_btn_, "close", close_btn_.contains(mouse_), ColorF::rgba(228, 214, 194, 240), 32.0f);

    float cs = 280.0f;
    float cx = panel.x + 48.0f + cs * 0.5f;
    float cy = panel.y + 320.0f;
    gfx.fill_rect({cx - cs * 0.5f - 8, cy - cs * 0.5f - 8, cs + 16, cs + 16}, ColorF::rgba(8, 6, 5, 220));
    gfx.draw_rect({cx - cs * 0.5f - 8, cy - cs * 0.5f - 8, cs + 16, cs + 16}, ColorF::rgba(156, 128, 94, 160), 1.0f);
    gfx.draw_corner_filigree({cx - cs * 0.5f - 13, cy - cs * 0.5f - 13, cs + 26, cs + 26}, 16.0f,
                             ColorF::rgba(218, 182, 124, static_cast<int>(180 * e)), 1.5f);

    const Track* ref = track_;
    auto ct = app_.library().act_tracks(track_->act);
    if (!ct.empty()) ref = ct[0];
    if (ID2D1Bitmap1* cov = app_.assets().cover(ref, static_cast<int>(cs))) {
        gfx.draw_bitmap(cov, {cx - cs * 0.5f, cy - cs * 0.5f, cs, cs}, 1.0f, true);
    }

    float tx = panel.x + 380.0f;
    FontKey tf{32.0f, true};
    gfx.text(tx, panel.y + 180, gfx.elide_text(track_->title, tf, 600.0f), tf, palette::CREAM);
    gfx.text(tx, panel.y + 224, track_->artist, {18.0f, false}, ColorF::rgba(198, 178, 156, 240));

    char qbuf[64];
    snprintf(qbuf, sizeof(qbuf), "%s / %dch", track_->quality().c_str(), track_->channels);
    struct Row { const char* k; std::string v; };
    Row rows[5] = {
        {"章节", track_->act_label + " · " + track_->act_zh},
        {"专辑", track_->album.empty() ? "—" : track_->album},
        {"时长", track_->duration_str()},
        {"音质", qbuf},
        {"来源", track_->act_en},
    };
    float y = panel.y + 286.0f;
    FontKey vf{17.0f, false};
    for (const auto& r : rows) {
        gfx.text(tx, y, r.k, {15.0f, false}, ColorF::rgba(168, 148, 128, 235));
        gfx.text(tx + 90.0f, y, gfx.elide_text(r.v, vf, 500.0f), vf, ColorF::rgba(226, 212, 194, 240));
        gfx.draw_line({tx, y + 16.0f}, {panel.right() - 48.0f, y + 16.0f}, ColorF::rgba(110, 94, 80, 90), 1.0f);
        y += 48.0f;
    }

    FontKey pf2{11.0f, false};
    std::string ptxt = gfx.elide_text(track_->path, pf2, panel.right() - 48.0f - tx, ElideMode::Middle);
    gfx.text(tx, panel.y + 514, ptxt, pf2, ColorF::rgba(136, 120, 104, 205));

    gfx.draw_paper_button(btn_play_, "开 演", {21.0f, false}, btn_play_.contains(mouse_), true);
    gfx.draw_paper_button(btn_cg_, "章节 CG", {21.0f, false}, btn_cg_.contains(mouse_));
    gfx.draw_paper_button(btn_close_, "关闭", {21.0f, false}, btn_close_.contains(mouse_));
    gfx.draw_paper_button(btn_src_, "源文件", {21.0f, false}, btn_src_.contains(mouse_));
    bool can_set_title = (app_.settings().get_string("title_track") != track_->id);
    gfx.draw_paper_button(btn_title_, "设为主界面音乐", {17.0f, false},
                          can_set_title && btn_title_.contains(mouse_), false, can_set_title);

    if (title_set_until_ > 0.0 && now_sec() < title_set_until_) {
        gfx.text(btn_title_.right() + 14.0f, btn_title_.cy() + 6.0f, "已设为主界面音乐",
                 {13.0f, false}, ColorF::rgba(214, 178, 118, 240));
    }
    gfx.restore_transform();
}

void TrackDetailOverlay::mouse_move(PointF pos) {
    mouse_ = pos;
}

void TrackDetailOverlay::mouse_press(PointF pos, MouseButton btn) {
    if (btn != MouseButton::Left || !track_) return;
    mouse_ = pos;
    if (close_btn_.contains(pos) || btn_close_.contains(pos)) { app_.close_overlay(); return; }
    if (!panel_.contains(pos)) { app_.close_overlay(); return; }
    if (btn_play_.contains(pos)) {
        const Track* t = track_;
        app_.close_overlay();
        app_.open_player(t);
        return;
    }
    if (btn_src_.contains(pos)) {
        paths::reveal_in_explorer(track_->abs_path());
        return;
    }
    if (btn_cg_.contains(pos)) {
        open_chapter_cg();
        return;
    }
    if (btn_title_.contains(pos)) {
        if (app_.settings().get_string("title_track") != track_->id) {
            app_.settings().set("title_track", track_->id);
            title_set_until_ = now_sec() + 2.0;
        }
    }
}

void TrackDetailOverlay::open_chapter_cg() {
    if (!track_) return;
    const Chapter* ch = app_.library().chapter(track_->act);
    static const std::unordered_map<std::string, std::string> fixed = {
        {"ActT", "CG/谢幕/ActT_终章.png"},
        {"ActU", "CG/谢幕/ActU_外传.png"},
        {"其他", "CG/谢幕/其他_界面与菜单.png"},
        {"未分类", "CG/谢幕/未分类_未分类.png"},
    };
    std::string rel;
    auto it = fixed.find(track_->act);
    if (it != fixed.end()) {
        rel = it->second;
    } else {
        auto git = app_.library().gallery.find("curtain");
        if (git != app_.library().gallery.end()) {
            const auto& items = git->second;
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                if (items[i].act == track_->act && items[i].variant.empty()) {
                    app_.open_viewer("curtain", i, &items, "谢幕 CG");
                    return;
                }
            }
        }
        rel = track_->cg_rel();
    }
    if (!paths::file_exists(paths::media_path_utf8(rel))) return;

    int w = 0, h = 0;
    if (ID2D1Bitmap1* bmp = app_.assets().full(rel, 3840)) {
        D2D1_SIZE_F sz = bmp->GetSize();
        w = static_cast<int>(sz.width);
        h = static_cast<int>(sz.height);
    }
    GalleryItem item;
    item.id = "chapter_cg_" + track_->act;
    item.title = ch ? ch->label : track_->act_label;
    item.act = track_->act;
    item.path = rel;
    item.w = w;
    item.h = h;
    std::vector<GalleryItem> one = {item};
    app_.open_viewer("chapter", 0, &one, ch ? ch->label : "章节 CG");
}

bool TrackDetailOverlay::key_press(int vk, bool, bool) {
    if (vk == key::Escape || vk == key::Tab) { app_.close_overlay(); return true; }
    if (vk == key::Enter) {
        const Track* t = track_;
        app_.close_overlay();
        if (t) app_.open_player(t);
        return true;
    }
    if (vk == 'O' && track_) {
        paths::reveal_in_explorer(track_->abs_path());
        return true;
    }
    return true;
}

} // namespace np
