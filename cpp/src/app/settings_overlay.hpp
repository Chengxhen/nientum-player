#pragma once

#include "screen.hpp"

#include <string>
#include <vector>

namespace np {

class SettingsOverlay : public Overlay {
public:
    explicit SettingsOverlay(AppContext& app);

    void commit() override;
    void tick(float dt, double now) override;
    void paint(Gfx& gfx, double now) override;
    bool animating() const override { return fade_ < 0.999f || fps_editing_; }
    bool wants_backdrop_blur() const override { return true; }
    float open_progress() const override { return fade_; }

    void mouse_move(PointF pos) override;
    void mouse_press(PointF pos, MouseButton btn) override;
    void mouse_release(PointF pos, MouseButton btn) override;
    bool key_press(int vk, bool ctrl, bool shift) override;

private:
    PointF mouse_{-1, -1};
    float fade_ = 0.0f;
    RectF panel_{500, 130, 920, 830};
    RectF close_x_{1360, 144, 36, 36};
    RectF vol_rect_{790, 306, 400, 6};
    bool drag_vol_ = false;
    float row_y_ = 290.0f;
    int focus_ = 0;
    bool show_focus_ = false;

    std::vector<AudioDeviceInfo> devices_;
    int dev_idx_ = -1; // -1 = system default

    RectF fps_box_{790, 719, 170, 34};
    RectF fps_cap_{1008, 721, 96, 30};
    RectF bg_reset_{1082, 786, 150, 34};
    RectF music_reset_{1082, 840, 150, 34};
    RectF save_btn_{970, 900, 180, 52};
    RectF close_btn_{1180, 900, 180, 52};

    double bg_reset_until_ = 0.0;
    double music_reset_until_ = 0.0;
    double save_until_ = 0.0;

    bool fps_editing_ = false;
    std::string fps_buf_;
    bool fps_replace_ = false;
    float blink_t_ = 0.0f;

    int current_fps() const;
    std::vector<RectF> viz_rects() const;
    std::vector<RectF> style_rects() const;
    std::vector<RectF> dur_rects() const;
    std::vector<RectF> res_rects() const;
    std::vector<RectF> toggle_rects() const;
    std::pair<RectF, RectF> dev_arrows() const;

    void draw_toggle(Gfx& gfx, const RectF& r, bool on);
    void set_vol(PointF pos);
    void set_volume_value(float v);
    RectF focus_rect() const;
    void adjust_focus(int dir);
    void activate_focus();
    void cycle_device(int dir);
    void cycle_viz(int dir);
    void cycle_style(int dir);
    void cycle_duration(int dir);
    void cycle_resolution(int dir);
    void toggle_fps_cap(int dir);
    void reset_title_bg();
    void reset_title_music();
    void save_settings();
};

} // namespace np
