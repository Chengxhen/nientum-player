#pragma once

#include "screen.hpp"
#include "../gfx/spectrum.hpp"

#include <string>
#include <vector>

namespace np {

class PlayerScreen : public Screen {
public:
    explicit PlayerScreen(AppContext& app);

    std::string name() const override { return "player"; }
    void enter(const Track* track = nullptr, const std::string& act = "",
               const std::string& category = "", int index = 0,
               bool suppress_preview = false) override;
    void leave() override;
    void tick(float dt, double now) override;
    void paint(Gfx& gfx, double now) override;
    bool animating() const override;

    void mouse_move(PointF pos) override;
    void mouse_press(PointF pos, MouseButton btn) override;
    void mouse_release(PointF pos, MouseButton btn) override;
    void mouse_wheel(PointF pos, int delta) override;
    bool key_press(int vk, bool ctrl, bool shift) override;

private:
    const Track* track_ = nullptr;
    float t_ = 0.0f;
    bool ui_visible_ = true;
    float bar_anim_ = 1.0f;
    float blur_anim_ = 0.0f;
    Spectrum spec_;
    double last_mouse_ = 0.0;
    bool hover_ui_ = false;

    std::vector<std::pair<std::string, RectF>> buttons_;
    std::vector<std::pair<const Track*, RectF>> dot_hits_;
    RectF vol_rect_;
    RectF progress_;
    bool dragging_ = false;
    bool drag_vol_ = false;
    bool hover_prog_ = false;
    int hover_dot_ = -1;
    float drag_frac_ = 0.0f;
    double last_drag_seek_ = 0.0;
    float intro_ = 0.0f;
    bool active_ = false;
    float fade_ = 1.0f;
    ID2D1Bitmap1* prev_cg_ = nullptr;
    float cg_fade_ = 1.0f;
    PointF mouse_{-1.0f, -1.0f};

    RectF back_top_{62, 70, 52, 52};
    RectF src_top_{122, 166, 128, 32};
    RectF title_top_{122, 206, 156, 32};

    void set_track(const Track* track, bool autoplay = true);
    bool is_card_style() const;
    RectF bar_rect() const;
    RectF card_rect() const;
    void layout_dots();
    void layout_bar();
    bool hover_named(const std::string& name) const;
    void paint_fullscreen_cg(Gfx& gfx, const Track* t, ID2D1Bitmap1* cg, const std::string& viz);
    void paint_card_stage(Gfx& gfx, const Track* t, ID2D1Bitmap1* cg, const std::string& viz);
    void paint_bar(Gfx& gfx, const Track* t);
    void step(int delta);
    void update_drag_frac(PointF pos);
    void seek_from_frac(float f);
    void set_volume_from_pos(PointF pos);
};

} // namespace np
