#pragma once

#include "screen.hpp"

namespace np {

class TrackDetailOverlay : public Overlay {
public:
    TrackDetailOverlay(AppContext& app, const Track* track);

    void tick(float dt, double now) override;
    void paint(Gfx& gfx, double now) override;
    bool animating() const override { return fade_ < 0.999f; }
    bool wants_backdrop_blur() const override { return true; }
    float open_progress() const override { return fade_; }

    void mouse_move(PointF pos) override;
    void mouse_press(PointF pos, MouseButton btn) override;
    bool key_press(int vk, bool ctrl, bool shift) override;

private:
    const Track* track_ = nullptr;
    PointF mouse_{-1, -1};
    float fade_ = 0.0f;
    double title_set_until_ = 0.0;

    RectF panel_{430, 210, 1060, 660};
    RectF btn_play_{760, 756, 168, 46};
    RectF btn_cg_{942, 756, 168, 46};
    RectF btn_close_{1124, 756, 168, 46};
    RectF btn_src_{1306, 756, 168, 46};
    RectF btn_title_{760, 810, 168, 46};
    RectF close_btn_{1440, 222, 36, 36};

    void open_chapter_cg();
};

} // namespace np
