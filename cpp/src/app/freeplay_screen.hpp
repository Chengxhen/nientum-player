#pragma once

#include "screen.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace np {

class FreePlayScreen : public Screen {
public:
    explicit FreePlayScreen(AppContext& app);

    std::string name() const override { return "freeplay"; }
    void enter(const Track* track = nullptr, const std::string& act = "",
               const std::string& category = "", int index = 0,
               bool suppress_preview = false) override;
    void leave() override;
    void on_overlay_closed() override;
    void tick(float dt, double now) override;
    void paint(Gfx& gfx, double now) override;
    bool animating() const override;

    void mouse_move(PointF pos) override;
    void mouse_press(PointF pos, MouseButton btn) override;
    void mouse_release(PointF pos, MouseButton btn) override;
    void mouse_double_click(PointF pos) override;
    void mouse_wheel(PointF pos, int delta) override;
    bool key_press(int vk, bool ctrl, bool shift) override;

    float spin = 0.0f;

private:
    static constexpr float LIST_TOP = 274.0f;
    static constexpr float LIST_BOTTOM = 926.0f;
    static inline const RectF LIST_CLIP{48.0f, 274.0f, 980.0f, 652.0f};
    static constexpr float CARD_SPACING = 100.0f;
    static constexpr int VISIBLE_SLOTS = 6;

    struct Category {
        std::string code;
        std::string label;
        std::vector<const Track*> tracks;
    };

    struct HelpItem {
        std::string action;
        RectF rect;
        std::string key;
        std::string label;
    };

    float t_ = 0.0f;
    int cat_idx_ = 0;
    int track_idx_ = 0;
    float spin_speed_ = 0.0f;
    uint64_t preview_gen_ = 0;
    uint64_t pending_preview_gen_ = 0;
    double pending_preview_at_ = 0.0;
    bool active_ = false;
    bool suppress_preview_ = false;

    int hover_track_ = -1;
    int hover_tab_ = -1;
    int hover_chev_ = 0;
    std::unordered_map<int, float> card_hover_anim_;
    std::string hover_help_;
    std::string hover_preview_;
    bool hover_now_ = false;

    double preview_started_at_ = 0.0;
    double preview_end_at_ = 0.0;
    bool previewing_ = false;

    std::vector<Category> categories_;
    float tab_start_ = 0.0f;
    int tab_count_ = 8;
    bool tab_drag_ = false;
    float tab_drag_x_ = 0.0f;
    float tab_drag_start_ = 0.0f;
    bool tab_drag_moved_ = false;
    int tab_press_i_ = -1;

    std::vector<const Track*> tracks_;
    float fade_ = 0.0f;
    float list_fade_ = 1.0f;
    float scroll_ = 0.0f;
    float scroll_target_ = 0.0f;
    float tab_focus_ = 0.0f;
    float cat_anim_ = 1.0f;

    float record_glow_ = 0.0f;
    std::string last_cover_act_;
    std::string prev_cover_act_;
    const Track* prev_cover_ref_ = nullptr;
    float cover_fade_ = 1.0f;

    bool has_detail_return_ = false;
    int detail_ret_cat_ = 0;
    int detail_ret_track_ = 0;

    ComPtr<ID2D1Bitmap1> bg_cache_;
    bool bg_built_ = false;

    const Track* current_track() const;
    void refresh_tracks();
    float scroll_index() const;
    int preview_seconds() const;
    std::string title_track_id() const;

    void schedule_preview(double delay_sec = 0.65);
    void start_preview(uint64_t gen, bool explicit_req = false);
    void stop_preview();
    void select_track(int idx);
    void select_category(int idx);
    void play_full();
    void open_detail();
    void set_preview_seconds(int sec);
    void help_action(const std::string& action);

    std::vector<std::pair<int, RectF>> tab_rects() const;
    std::vector<std::pair<int, RectF>> card_rects() const;
    std::vector<HelpItem> help_rects() const;
    RectF now_rect() const { return {64, 934, 964, 54}; }
    float curve_offset(float cy) const;
    float edge_fade(float cy) const;

    void build_background();
    void paint_tabs(Gfx& gfx);
    void paint_list(Gfx& gfx);
    void paint_card(Gfx& gfx, int i, const RectF& r, float fade);
    void paint_now_playing(Gfx& gfx);
    void paint_record(Gfx& gfx);
    void paint_preview_bar(Gfx& gfx);
    void paint_guide(Gfx& gfx);
    void draw_eq(Gfx& gfx, float x, float cy, bool sounding, const ColorF& col);
};

} // namespace np
