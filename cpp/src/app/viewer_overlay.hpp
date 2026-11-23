#pragma once

#include "screen.hpp"
#include "../gfx/assets.hpp"

#include <string>
#include <vector>

namespace np {

class GalleryViewer : public Overlay {
public:
    GalleryViewer(AppContext& app, const std::string& category, int index = 0,
                  const std::vector<GalleryItem>* items = nullptr,
                  const std::string& label = "");
    ~GalleryViewer() override { anim_.stop(); }

    void tick(float dt, double now) override;
    void paint(Gfx& gfx, double now) override;
    bool animating() const override;

    void mouse_move(PointF pos) override;
    void mouse_press(PointF pos, MouseButton btn) override;
    void mouse_release(PointF pos, MouseButton btn) override;
    void mouse_double_click(PointF pos) override;
    void mouse_wheel(PointF pos, int delta) override;
    bool key_press(int vk, bool ctrl, bool shift) override;

private:
    std::string category_;
    std::vector<GalleryItem> items_;
    int index_ = 0;
    std::string label_;

    float zoom_ = 1.0f;
    float target_zoom_ = 1.0f;
    PointF pan_{0, 0};
    PointF target_pan_{0, 0};
    bool drag_ = false;
    PointF drag_start_{0, 0};
    PointF pan_start_{0, 0};

    float t_ = 0.0f;
    float fade_ = 0.0f;
    PointF mouse_{-1, -1};
    std::string hover_;

    ID2D1Bitmap1* pix_ = nullptr;
    ID2D1Bitmap1* prev_pix_ = nullptr;
    WebpAnimator anim_;
    bool is_anim_ = false;
    float anim_speed_ = 1.0f;
    RectF prev_rect_{};
    float step_anim_ = 1.0f;
    int step_dir_ = 0;
    bool press_seen_ = false;
    bool immersive_ = false;
    float immersive_anim_ = 0.0f;
    double bg_toast_until_ = 0.0;

    RectF bar_{60, 1000, 1800, 48};
    RectF prev_btn_{76, 1006, 44, 36};
    RectF next_btn_{128, 1006, 44, 36};
    RectF src_btn_{1150, 1006, 90, 36};
    RectF spd_dec_btn_{918, 1006, 34, 36};
    RectF spd_rst_btn_{956, 1006, 118, 36};
    RectF spd_inc_btn_{1078, 1006, 34, 36};
    RectF zoom_out_btn_{1248, 1006, 34, 36};
    RectF zoom_in_btn_{1374, 1006, 34, 36};
    RectF use_bg_btn_{1420, 1006, 180, 36};
    RectF immerse_btn_{1620, 1006, 190, 36};

    void load_current();
    void step(int d);
    void zoom_by(float factor);
    void clamp_pan();

    RectF content_rect() const;
    RectF fit_rect() const;
    RectF image_rect() const;
    RectF immersive_fit(ID2D1Bitmap1* pix) const;
    void paint_immersive(Gfx& gfx);
};

} // namespace np
