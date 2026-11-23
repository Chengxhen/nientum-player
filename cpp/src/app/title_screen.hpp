#pragma once

#include "screen.hpp"

#include <random>
#include <string>
#include <vector>

namespace np {

class TitleScreen : public Screen {
public:
    explicit TitleScreen(AppContext& app);

    std::string name() const override { return "title"; }
    void enter(const Track* track = nullptr, const std::string& act = "",
               const std::string& category = "", int index = 0,
               bool suppress_preview = false) override;
    void leave() override;
    void tick(float dt, double now) override;
    void paint(Gfx& gfx, double now) override;
    bool animating() const override;

    void mouse_move(PointF pos) override;
    void mouse_press(PointF pos, MouseButton btn) override;
    bool key_press(int vk, bool ctrl, bool shift) override;

private:
    struct MenuItem {
        std::string zh;
        std::string en;
        std::string action;
    };
    struct Petal {
        float x, y, spd, size, ph, alpha;
    };

    float t_ = 0.0f;
    float dt_last_ = 0.016f;
    int selected_ = 0;
    float menu_focus_ = 0.0f;
    std::vector<MenuItem> items_;
    std::vector<RectF> rects_;
    std::vector<Petal> petals_;
    std::mt19937 rng_{21};

    RectF fullscreen_rect_{1818, 58, 44, 44};
    RectF now_rect_{1370, 966, 460, 52};
    bool hover_full_ = false;
    bool hover_now_ = false;

    bool anim_init_ = false;
    WebpAnimator liora_anim_;
    WebpAnimator aleph_anim_;

    ComPtr<ID2D1Bitmap1> bg_cache_;
    std::string bg_key_;
    bool bg_built_ = false;

    void ensure_animations();
    void build_background(const std::string& custom);
    void paint_characters(Gfx& gfx);
    void activate(int idx);
};

} // namespace np
