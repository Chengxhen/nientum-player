#pragma once

#include "curtain.hpp"
#include "screen.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace np {

class App : public AppContext {
public:
    App() = default;
    ~App() override;

    bool init(HINSTANCE hinst, bool verify_mode = false);
    int run();

    Gfx& gfx() override { return gfx_; }
    Assets& assets() override { return *assets_; }
    AudioEngine& audio() override { return audio_; }
    Library& library() override { return library_; }
    Settings& settings() override { return settings_; }

    void show_screen(const std::string& name, const std::string& transition = "curtain",
                     const Track* track = nullptr, const std::string& act = "",
                     const std::string& category = "", int index = 0,
                     bool suppress_preview = false) override;
    void open_player(const Track* track) override;
    void back_to_freeplay(const Track* track = nullptr, bool suppress_preview = false) override;
    void open_gallery(const std::string& category = "", int index = 0) override;
    void open_viewer(const std::string& category, int index = 0,
                     const std::vector<GalleryItem>* items = nullptr,
                     const std::string& label = "") override;
    void open_detail(const Track* track) override;
    void open_settings() override;
    void close_overlay() override;

    void toggle_fullscreen() override;
    void set_fullscreen(bool full) override;
    bool is_fullscreen() const override { return fullscreen_; }
    void set_cursor_hidden(bool hidden) override;
    void apply_fps_setting(int fps) override;
    void quit_app() override;

    LRESULT wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    HWND hwnd_ = nullptr;
    int win_w_ = 1600;
    int win_h_ = 900;
    bool fullscreen_ = false;
    WINDOWPLACEMENT saved_place_{sizeof(WINDOWPLACEMENT)};
    bool cursor_hidden_ = false;
    bool running_ = true;
    bool verify_mode_ = false;

    Gfx gfx_;
    std::unique_ptr<Assets> assets_;
    Library library_;
    Settings settings_;
    AudioEngine audio_;
    Curtain curtain_;

    std::unordered_map<std::string, std::unique_ptr<Screen>> screens_;
    Screen* screen_ = nullptr;
    std::string current_name_;
    std::unique_ptr<Overlay> overlay_;

    struct SlideState {
        bool active = false;
        std::string phase = "up";
        float t = 0.0f;
        float dur = 0.62f;
        ComPtr<ID2D1Bitmap1> prev_bmp;
    } slide_;

    ComPtr<ID2D1Bitmap1> modal_blur_bmp_;

    int target_fps_ = 120;
    HANDLE wait_timer_ = nullptr;
    bool show_fps_hud_ = false;
    double fps_accum_ = 0.0;
    int fps_frames_ = 0;
    double displayed_fps_ = 0.0;
    double displayed_ms_ = 0.0;

    void set_screen(const std::string& name, const Track* track = nullptr,
                    const std::string& act = "", const std::string& category = "",
                    int index = 0, bool suppress_preview = false);
    void slide_to(const std::string& name, const std::string& phase,
                  const Track* track = nullptr, bool suppress_preview = false);

    RectF viewport_rect() const;
    bool to_logical(float wx, float wy, PointF& out) const;

    void tick_and_render(double now, double dt);
    void paint_scene(double now);
    void paint_slide(double now);
    void render_screen_snapshot(ComPtr<ID2D1Bitmap1>& target, int w, int h, double now);
    void save_window_state();
};

} // namespace np
