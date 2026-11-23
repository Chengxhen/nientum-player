#pragma once

#include "screen.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace np {

class GalleryScreen : public Screen {
public:
    explicit GalleryScreen(AppContext& app);

    std::string name() const override { return "gallery"; }
    void enter(const Track* track = nullptr, const std::string& act = "",
               const std::string& category = "", int index = 0,
               bool suppress_preview = false) override;
    void tick(float dt, double now) override;
    void paint(Gfx& gfx, double now) override;
    bool animating() const override;

    void mouse_move(PointF pos) override;
    void mouse_press(PointF pos, MouseButton btn) override;
    void mouse_wheel(PointF pos, int delta) override;
    bool key_press(int vk, bool ctrl, bool shift) override;

private:
    static constexpr float TAB_X = 70.0f;
    static constexpr float TAB_SLOT = 196.0f;
    static constexpr float TAB_Y = 150.0f;

    struct HelpItem {
        std::string action;
        RectF rect;
        std::string key;
        std::string label;
    };

    std::string cat_ = "curtain";
    std::string filter_ = "all";
    int page_ = 0;
    int selected_ = 0;
    float t_ = 0.0f;
    float fade_ = 1.0f;

    int hover_card_ = -1;
    int hover_tab_ = -1;
    int hover_filter_ = -1;
    int hover_page_ = 0;
    std::unordered_map<int, float> card_hover_anim_;
    std::string hover_help_;

    std::vector<GalleryItem> items_;
    float tab_focus_ = 0.0f;
    float pill_w_ = 120.0f;
    float target_pill_w_ = 120.0f;
    float grid_anim_ = 1.0f;
    int grid_dir_ = 0;

    ComPtr<ID2D1Bitmap1> bg_cache_;
    ComPtr<ID2D1Bitmap1> card_bg_cg_;
    ComPtr<ID2D1Bitmap1> card_bg_small_;
    bool bg_built_ = false;

    bool is_cg() const;
    int per_page() const { return is_cg() ? 6 : 10; }
    int cols() const { return is_cg() ? 3 : 5; }
    int pages() const;
    int tab_index() const;
    float target_pill_w();
    float grid_left() const { return is_cg() ? 90.0f : 96.0f; }

    void refresh();
    void set_cat(const std::string& cat);
    void set_filter(const std::string& code);
    void goto_page(int page, int dir = 0);
    void help_action(const std::string& action);

    std::vector<std::pair<std::string, std::string>> filter_options() const;
    std::vector<std::pair<int, RectF>> tab_rects() const;
    std::vector<std::pair<int, RectF>> filter_rects();
    std::vector<std::pair<int, RectF>> card_rects() const;
    std::vector<HelpItem> help_rects();
    RectF now_rect() const { return {620, 1002, 680, 48}; }

    void build_background();
    void build_card_backgrounds();
    void paint_tabs(Gfx& gfx);
    void paint_filters(Gfx& gfx);
    void paint_grid(Gfx& gfx);
    void paint_card(Gfx& gfx, int idx, const RectF& r);
    void paint_now_playing(Gfx& gfx);
    void paint_pager(Gfx& gfx);
    void paint_guide(Gfx& gfx);
};

} // namespace np
