#pragma once

#include "../audio/audio_engine.hpp"
#include "../core/models.hpp"
#include "../core/settings.hpp"
#include "../core/types.hpp"
#include "../gfx/assets.hpp"
#include "../gfx/gfx.hpp"

#include <string>
#include <vector>

namespace np {

enum class MouseButton { Left, Right, Middle };

namespace key {
constexpr int Escape    = 0x1B;
constexpr int Enter     = 0x0D;
constexpr int Space     = 0x20;
constexpr int Tab       = 0x09;
constexpr int Backspace = 0x08;
constexpr int Left      = 0x25;
constexpr int Up        = 0x26;
constexpr int Right     = 0x27;
constexpr int Down      = 0x28;
constexpr int PageUp    = 0x21;
constexpr int PageDown  = 0x22;
constexpr int Home      = 0x24;
constexpr int F11       = 0x7A;
constexpr int F3        = 0x72;
constexpr int Plus      = 0xBB;
constexpr int Minus     = 0xBD;
} // namespace key

class AppContext {
public:
    virtual ~AppContext() = default;

    virtual Gfx& gfx() = 0;
    virtual Assets& assets() = 0;
    virtual AudioEngine& audio() = 0;
    virtual Library& library() = 0;
    virtual Settings& settings() = 0;

    virtual void show_screen(const std::string& name, const std::string& transition = "curtain",
                             const Track* track = nullptr, const std::string& act = "",
                             const std::string& category = "", int index = 0,
                             bool suppress_preview = false) = 0;
    virtual void open_player(const Track* track) = 0;
    virtual void back_to_freeplay(const Track* track = nullptr, bool suppress_preview = false) = 0;
    virtual void open_gallery(const std::string& category = "", int index = 0) = 0;
    virtual void open_viewer(const std::string& category, int index = 0,
                             const std::vector<GalleryItem>* items = nullptr,
                             const std::string& label = "") = 0;
    virtual void open_detail(const Track* track) = 0;
    virtual void open_settings() = 0;
    virtual void close_overlay() = 0;

    virtual void toggle_fullscreen() = 0;
    virtual void set_fullscreen(bool full) = 0;
    virtual bool is_fullscreen() const = 0;
    virtual void set_cursor_hidden(bool hidden) = 0;
    virtual void apply_fps_setting(int fps) = 0;
    virtual void quit_app() = 0;
};

class Screen {
public:
    explicit Screen(AppContext& app) : app_(app) {}
    virtual ~Screen() = default;

    virtual std::string name() const = 0;
    virtual void enter(const Track* track = nullptr, const std::string& act = "",
                       const std::string& category = "", int index = 0,
                       bool suppress_preview = false) {}
    virtual void leave() {}
    virtual void on_overlay_closed() {}
    virtual void tick(float dt, double now) {}
    virtual void paint(Gfx& gfx, double now) = 0;
    virtual bool animating() const { return false; }

    virtual void mouse_move(PointF pos) {}
    virtual void mouse_press(PointF pos, MouseButton btn) {}
    virtual void mouse_release(PointF pos, MouseButton btn) {}
    virtual void mouse_double_click(PointF pos) {}
    virtual void mouse_wheel(PointF pos, int delta) {}
    virtual bool key_press(int vk, bool ctrl, bool shift) { return false; }

protected:
    AppContext& app_;
};

class Overlay {
public:
    explicit Overlay(AppContext& app) : app_(app) {}
    virtual ~Overlay() = default;

    virtual void commit() {}
    virtual void tick(float dt, double now) {}
    virtual void paint(Gfx& gfx, double now) = 0;
    virtual bool animating() const { return false; }
    virtual bool wants_backdrop_blur() const { return false; }
    virtual float open_progress() const { return 1.0f; }

    virtual void mouse_move(PointF pos) {}
    virtual void mouse_press(PointF pos, MouseButton btn) {}
    virtual void mouse_release(PointF pos, MouseButton btn) {}
    virtual void mouse_double_click(PointF pos) {}
    virtual void mouse_wheel(PointF pos, int delta) {}
    virtual bool key_press(int vk, bool ctrl, bool shift) { return false; }

protected:
    AppContext& app_;
};

} // namespace np
