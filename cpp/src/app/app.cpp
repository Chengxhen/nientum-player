#include "app.hpp"
#include "detail_overlay.hpp"
#include "freeplay_screen.hpp"
#include "gallery_screen.hpp"
#include "player_screen.hpp"
#include "settings_overlay.hpp"
#include "title_screen.hpp"
#include "viewer_overlay.hpp"
#include "../core/paths.hpp"

#include <dwmapi.h>
#include <mmsystem.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

namespace np {

namespace {
constexpr int IDI_APPICON = 101;
constexpr const wchar_t* kWndClass = L"NientumPlayerV2Window";

LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->wnd_proc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

App::~App() {
    if (wait_timer_) {
        CloseHandle(wait_timer_);
        wait_timer_ = nullptr;
    }
    timeEndPeriod(1);
    set_cursor_hidden(false);
    audio_.shutdown();
    screens_.clear();
    overlay_.reset();
    slide_.prev_bmp.Reset();
    modal_blur_bmp_.Reset();
    assets_.reset();
    gfx_.shutdown();
}

bool App::init(HINSTANCE hinst, bool verify_mode) {
    verify_mode_ = verify_mode;
    paths::init();
    paths::log("NientumPlayer v2 starting; data_dir=" + to_utf8(paths::data_dir()) +
               " media_dir=" + to_utf8(paths::media_dir()));
    timeBeginPeriod(1);
    wait_timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!wait_timer_) wait_timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    settings_.load();
    if (!library_.load()) {
        paths::log("Failed to load library JSON");
        MessageBoxW(nullptr, L"未找到曲库数据文件 (data/tracks.json)。", L"Nientum Player", MB_ICONERROR);
        return false;
    }

    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    // Default centered 16:9 windowed size for when user toggles out of fullscreen
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int avail_w = wa.right - wa.left;
    int avail_h = wa.bottom - wa.top;
    int w = std::min(1600, avail_w - 80);
    int h = static_cast<int>(std::lround(w * 9.0 / 16.0));
    if (h > avail_h - 80) {
        h = avail_h - 80;
        w = static_cast<int>(std::lround(h * 16.0 / 9.0));
    }
    RECT wr{0, 0, w, h};
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int wx = wa.left + (avail_w - (wr.right - wr.left)) / 2;
    int wy = wa.top + (avail_h - (wr.bottom - wr.top)) / 2;

    hwnd_ = CreateWindowExW(
        0, kWndClass, L"Nientum - Op.ZERO · 音乐与 CG 鉴赏",
        WS_OVERLAPPEDWINDOW, wx, wy, wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hinst, this);
    if (!hwnd_) return false;

    // Dark DWM title bar matching original app
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    COLORREF caption = 0x0008090C;
    COLORREF text_col = 0x00DCE6F0;
    COLORREF border = 0x00608CA8;
    DwmSetWindowAttribute(hwnd_, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd_, 36, &text_col, sizeof(text_col));
    DwmSetWindowAttribute(hwnd_, 34, &border, sizeof(border));

    RECT cr{};
    GetClientRect(hwnd_, &cr);
    win_w_ = std::max(1, static_cast<int>(cr.right - cr.left));
    win_h_ = std::max(1, static_cast<int>(cr.bottom - cr.top));

    if (!gfx_.init(hwnd_, win_w_, win_h_)) {
        paths::log("Gfx init failed");
        MessageBoxW(hwnd_, L"Direct2D / Direct3D 11 图形初始化失败。", L"Nientum Player", MB_ICONERROR);
        return false;
    }

    assets_ = std::make_unique<Assets>(gfx_);

    int dev = settings_.get_int("device", -1);
    audio_.init(dev);
    audio_.set_volume(static_cast<float>(settings_.get_double("volume", 0.78)));
    audio_.set_muted(settings_.get_bool("muted", false));

    apply_fps_setting(settings_.get_int("fps", 120));

    screens_["title"] = std::make_unique<TitleScreen>(*this);
    screens_["freeplay"] = std::make_unique<FreePlayScreen>(*this);
    screens_["player"] = std::make_unique<PlayerScreen>(*this);
    screens_["gallery"] = std::make_unique<GalleryScreen>(*this);

    show_screen("title", "");

    if (verify_mode_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(hwnd_, SW_SHOW);
        set_fullscreen(true);
    }
    UpdateWindow(hwnd_);
    paths::log("window shown; tracks=" + std::to_string(library_.tracks.size()) +
               " chapters=" + std::to_string(library_.chapters.size()) +
               " audio_devices=" + std::to_string(audio_.output_devices().size()));
    return true;
}

int App::run() {
    MSG msg{};
    double last = now_sec();

    if (verify_mode_) {
        target_fps_ = 0;
        std::wstring dump_dir = paths::app_dir() + L"\\_verify";
        CreateDirectoryW(dump_dir.c_str(), nullptr);
        std::string report;
        const char* stages[] = {"title", "freeplay", "player", "gallery", "settings"};
        for (const char* st : stages) {
            std::string sname(st);
            if (sname == "player" && !library_.tracks.empty()) {
                open_player(&library_.tracks[0]);
            } else if (sname == "settings") {
                show_screen("title", "");
                open_settings();
            } else {
                show_screen(sname, "");
            }
            double t0 = now_sec();
            int frames = (sname == "player") ? 105 : 60;
            for (int f = 0; f < frames; ++f) {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                double now = now_sec();
                tick_and_render(now, 0.016);
            }
            double elapsed = now_sec() - t0;
            double fps = frames / std::max(0.0001, elapsed);
            gfx_.dump_backbuffer_png(dump_dir + L"\\verify_" + to_wide(sname) + L".png");
            char buf[256];
            snprintf(buf, sizeof(buf), "stage=%s frames=%d elapsed=%.3fs avg_fps=%.1f\r\n",
                     st, frames, elapsed, fps);
            report += buf;
            paths::log(buf);
            close_overlay();
        }
        std::wstring rpath = dump_dir + L"\\verify_report.txt";
        HANDLE rh = CreateFileW(rpath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (rh != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(rh, report.data(), static_cast<DWORD>(report.size()), &w, nullptr);
            CloseHandle(rh);
        }
        save_window_state();
        audio_.stop();
        return 0;
    }

    while (running_) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_ = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running_) break;

        double frame_start = now_sec();
        double dt = std::clamp(frame_start - last, 0.0, 0.05);
        last = frame_start;

        tick_and_render(frame_start, dt);

        // Precise frame pacing
        if (target_fps_ > 0) {
            double frame_budget = 1.0 / static_cast<double>(target_fps_);
            double elapsed = now_sec() - frame_start;
            double rem = frame_budget - elapsed;
            if (rem > 0.0015 && wait_timer_) {
                LARGE_INTEGER due{};
                due.QuadPart = -static_cast<LONGLONG>((rem - 0.0007) * 10000000.0);
                if (due.QuadPart < 0) {
                    SetWaitableTimer(wait_timer_, &due, 0, nullptr, nullptr, FALSE);
                    WaitForSingleObject(wait_timer_, 50);
                }
            }
            while (now_sec() - frame_start < frame_budget) {
                YieldProcessor();
            }
        }
    }
    return static_cast<int>(msg.wParam);
}

void App::apply_fps_setting(int fps) {
    target_fps_ = (fps <= 0) ? 0 : std::clamp(fps, 1, 9999);
}

void App::set_cursor_hidden(bool hidden) {
    if (hidden == cursor_hidden_) return;
    cursor_hidden_ = hidden;
    ShowCursor(hidden ? FALSE : TRUE);
}

void App::set_fullscreen(bool full) {
    if (full == fullscreen_ || !hwnd_) return;
    fullscreen_ = full;
    DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE));
    if (full) {
        GetWindowPlacement(hwnd_, &saved_place_);
        MONITORINFO mi{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd_, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd_, &saved_place_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

void App::toggle_fullscreen() {
    set_fullscreen(!fullscreen_);
}

void App::quit_app() {
    save_window_state();
    audio_.stop();
    running_ = false;
    PostQuitMessage(0);
}

void App::save_window_state() {
    if (overlay_) overlay_->commit();
    auto it = screens_.find("freeplay");
    if (it != screens_.end()) {
        auto* fp = static_cast<FreePlayScreen*>(it->second.get());
        settings_.set("lp_angle", std::round(fp->spin * 1000.0) / 1000.0);
    }
    settings_.save();
}

void App::show_screen(const std::string& name, const std::string& transition,
                      const Track* track, const std::string& act,
                      const std::string& category, int index, bool suppress_preview) {
    if (name == current_name_ && !track && act.empty() && category.empty() && index == 0) return;
    if (transition == "curtain" && screen_ != nullptr) {
        overlay_.reset();
        curtain_.start([this, name, track, act, category, index, suppress_preview]() {
            set_screen(name, track, act, category, index, suppress_preview);
        });
    } else {
        set_screen(name, track, act, category, index, suppress_preview);
    }
}

void App::set_screen(const std::string& name, const Track* track, const std::string& act,
                     const std::string& category, int index, bool suppress_preview) {
    if (screen_) screen_->leave();
    auto it = screens_.find(name);
    if (it == screens_.end()) return;
    screen_ = it->second.get();
    current_name_ = name;
    overlay_.reset();
    screen_->enter(track, act, category, index, suppress_preview);
}

void App::slide_to(const std::string& name, const std::string& phase,
                   const Track* track, bool suppress_preview) {
    render_screen_snapshot(slide_.prev_bmp, 1920, 1080, now_sec());
    set_screen(name, track, "", "", 0, suppress_preview);
    slide_.active = true;
    slide_.phase = phase;
    slide_.t = 0.0f;
    slide_.dur = 0.56f;
}

void App::open_player(const Track* track) {
    slide_to("player", "up", track, false);
}

void App::back_to_freeplay(const Track* track, bool suppress_preview) {
    if (current_name_ == "player") {
        slide_to("freeplay", "down", track, suppress_preview);
    } else {
        show_screen("freeplay", "curtain", track, "", "", 0, suppress_preview);
    }
}

void App::open_gallery(const std::string& category, int index) {
    show_screen("gallery", "curtain", nullptr, "", category, index, false);
}

void App::open_viewer(const std::string& category, int index,
                      const std::vector<GalleryItem>* items, const std::string& label) {
    overlay_ = std::make_unique<GalleryViewer>(*this, category, index, items, label);
}

void App::open_detail(const Track* track) {
    overlay_ = std::make_unique<TrackDetailOverlay>(*this, track);
}

void App::open_settings() {
    overlay_ = std::make_unique<SettingsOverlay>(*this);
}

void App::close_overlay() {
    if (overlay_) overlay_->commit();
    overlay_.reset();
    if (screen_) screen_->on_overlay_closed();
}

RectF App::viewport_rect() const {
    float w = static_cast<float>(win_w_);
    float h = static_cast<float>(win_h_);
    if (w <= 0.0f || h <= 0.0f) return {0, 0, 1920, 1080};
    float vw = std::min(w, h * 16.0f / 9.0f);
    float vh = vw * 9.0f / 16.0f;
    return {(w - vw) * 0.5f, (h - vh) * 0.5f, vw, vh};
}

bool App::to_logical(float wx, float wy, PointF& out) const {
    RectF vp = viewport_rect();
    if (!vp.contains({wx, wy})) return false;
    float s = std::max(0.0001f, vp.w / 1920.0f);
    out = {(wx - vp.x) / s, (wy - vp.y) / s};
    return true;
}

void App::render_screen_snapshot(ComPtr<ID2D1Bitmap1>& target, int w, int h, double now) {
    if (!target || static_cast<int>(target->GetPixelSize().width) != w ||
        static_cast<int>(target->GetPixelSize().height) != h) {
        target = gfx_.create_target_bitmap(w, h);
    }
    if (!target) return;
    gfx_.set_target(target.Get());
    gfx_.dc()->BeginDraw();
    float s = static_cast<float>(w) / 1920.0f;
    gfx_.dc()->SetTransform(D2D1::Matrix3x2F::Scale(s, s));
    gfx_.dc()->Clear(D2D1::ColorF(0, 0, 0, 1));
    if (screen_) screen_->paint(gfx_, now);
    if (overlay_) overlay_->paint(gfx_, now);
    gfx_.dc()->EndDraw();
    gfx_.restore_scene_target();
}

void App::tick_and_render(double now, double dt) {
    audio_.update(now);

    float fdt = static_cast<float>(dt);
    if (curtain_.active) curtain_.update(fdt);
    if (slide_.active) {
        slide_.t = std::min(1.0f, slide_.t + fdt / slide_.dur);
        if (slide_.t >= 1.0f) {
            slide_.active = false;
            slide_.prev_bmp.Reset();
        }
    }
    if (overlay_) overlay_->tick(fdt, now);
    if (screen_) screen_->tick(fdt, now);

    // Prepare backdrop blur snapshot before BeginDraw if modal wants blur
    bool modal_blur = (overlay_ && overlay_->wants_backdrop_blur() && !slide_.active);
    if (modal_blur) {
        if (!modal_blur_bmp_) modal_blur_bmp_ = gfx_.create_target_bitmap(960, 540);
        if (modal_blur_bmp_) {
            gfx_.set_target(modal_blur_bmp_.Get());
            gfx_.dc()->BeginDraw();
            gfx_.dc()->SetTransform(D2D1::Matrix3x2F::Scale(0.5f, 0.5f));
            gfx_.dc()->Clear(D2D1::ColorF(0, 0, 0, 1));
            if (screen_) screen_->paint(gfx_, now);
            gfx_.dc()->EndDraw();
        }
    }

    bool animating = slide_.active || curtain_.active ||
                     (screen_ && screen_->animating()) ||
                     (overlay_ && overlay_->animating());
    int res = settings_.get_int("resolution", 1080);

    gfx_.begin_frame(res, animating, win_w_, win_h_, viewport_rect());
    paint_scene(now);

    if (show_fps_hud_) {
        char fbuf[64];
        snprintf(fbuf, sizeof(fbuf), "%.0f FPS  (%.2f ms)", displayed_fps_, displayed_ms_);
        RectF pill(1710, 14, 194, 28);
        ColorF pf = ColorF::rgba(10, 8, 6, 185);
        ColorF pb = ColorF::rgba(204, 164, 104, 150);
        gfx_.rounded(pill, 6.0f, &pf, &pb, 1.0f);
        gfx_.text_center(pill.cx(), pill.cy() + 5, fbuf, {12.0f, false}, ColorF::rgba(238, 222, 198, 240));
    }

    gfx_.end_frame(false, target_fps_ == 0);

    fps_accum_ += dt;
    ++fps_frames_;
    if (fps_accum_ >= 0.35) {
        displayed_fps_ = fps_frames_ / fps_accum_;
        displayed_ms_ = (fps_accum_ * 1000.0) / std::max(1, fps_frames_);
        fps_accum_ = 0.0;
        fps_frames_ = 0;
    }
}

void App::paint_scene(double now) {
    if (slide_.active) {
        paint_slide(now);
    } else {
        if (overlay_ && overlay_->wants_backdrop_blur() && modal_blur_bmp_) {
            float op = ease_out(clampf(overlay_->open_progress()));
            if (op < 0.99f && screen_) {
                screen_->paint(gfx_, now);
            }
            gfx_.draw_blurred_bitmap(modal_blur_bmp_.Get(), {0, 0, 1920, 1080}, 8.5f * op, op);
        } else if (screen_) {
            screen_->paint(gfx_, now);
        }
        if (overlay_) overlay_->paint(gfx_, now);
    }
    if (curtain_.active) {
        curtain_.paint(gfx_, *assets_, 1920.0f, 1080.0f);
    }
}

void App::paint_slide(double now) {
    float k = clampf(slide_.t);
    float e = 1.0f - std::pow(1.0f - k, 3.6f);
    float off = 0.0f;
    if (slide_.phase == "up") {
        if (slide_.prev_bmp) {
            float bg_scale = 1.0f - 0.032f * e;
            gfx_.save_transform();
            gfx_.scale(bg_scale, bg_scale, {960.0f, 540.0f});
            gfx_.draw_bitmap(slide_.prev_bmp.Get(), {0, 0, 1920, 1080}, 1.0f, true);
            gfx_.restore_transform();
            gfx_.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(4, 3, 3, static_cast<int>(135 * e)));
        }
        off = (1.0f - e) * 1080.0f;
        float fg_scale = 1.018f - 0.018f * e;
        gfx_.save_transform();
        gfx_.translate(0.0f, off);
        gfx_.scale(fg_scale, fg_scale, {960.0f, 540.0f});
        if (screen_) screen_->paint(gfx_, now);
        gfx_.restore_transform();
    } else {
        float bg_scale = 0.968f + 0.032f * e;
        gfx_.save_transform();
        gfx_.scale(bg_scale, bg_scale, {960.0f, 540.0f});
        if (screen_) screen_->paint(gfx_, now);
        gfx_.restore_transform();
        gfx_.fill_rect({0, 0, 1920, 1080}, ColorF::rgba(4, 3, 3, static_cast<int>(135 * (1.0f - e))));

        off = e * 1080.0f;
        gfx_.save_transform();
        gfx_.translate(0.0f, off);
        if (slide_.prev_bmp) gfx_.draw_bitmap(slide_.prev_bmp.Get(), {0, 0, 1920, 1080}, 1.0f, true);
        gfx_.restore_transform();
    }
    if (off > 1.0f && off < 1079.0f) {
        Gfx::GradStop sh[2] = {{0.0f, ColorF::rgba(0, 0, 0, 0)}, {1.0f, ColorF::rgba(0, 0, 0, 185)}};
        gfx_.fill_linear_grad({0, off - 48.0f, 1920, 48}, {0, off - 48.0f}, {0, off}, sh, 2);
        gfx_.draw_line({0, off}, {1920, off}, ColorF::rgba(220, 180, 118, 175), 1.4f);
    }
}

LRESULT App::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE: {
        int w = LOWORD(lp);
        int h = HIWORD(lp);
        if (w > 0 && h > 0) {
            win_w_ = w;
            win_h_ = h;
            gfx_.resize(w, h);
        }
        return 0;
    }
    case WM_SIZING: {
        if (!fullscreen_) {
            RECT* rc = reinterpret_cast<RECT*>(lp);
            RECT adj{0, 0, 0, 0};
            AdjustWindowRectEx(&adj, static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE)), FALSE, 0);
            int pad_w = (adj.right - adj.left);
            int pad_h = (adj.bottom - adj.top);
            int cw = std::max(960, static_cast<int>((rc->right - rc->left) - pad_w));
            int ch = std::max(540, static_cast<int>((rc->bottom - rc->top) - pad_h));
            if (wp == WMSZ_TOP || wp == WMSZ_BOTTOM) {
                cw = static_cast<int>(std::lround(ch * 16.0 / 9.0));
                rc->right = rc->left + cw + pad_w;
            } else {
                ch = static_cast<int>(std::lround(cw * 9.0 / 16.0));
                if (wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT) {
                    rc->top = rc->bottom - (ch + pad_h);
                } else {
                    rc->bottom = rc->top + ch + pad_h;
                }
            }
            return TRUE;
        }
        break;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 960;
        mmi->ptMinTrackSize.y = 540;
        return 0;
    }
    case WM_MOUSEMOVE: {
        set_cursor_hidden(false);
        PointF pos;
        if (to_logical(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)), pos)) {
            if (!curtain_.active && !slide_.active) {
                if (overlay_) overlay_->mouse_move(pos);
                else if (screen_) screen_->mouse_move(pos);
            }
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        SetFocus(hwnd);
        SetCapture(hwnd);
        PointF pos;
        if (to_logical(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)), pos)) {
            if (!curtain_.active && !slide_.active) {
                MouseButton btn = (msg == WM_RBUTTONDOWN) ? MouseButton::Right
                                : ((msg == WM_MBUTTONDOWN) ? MouseButton::Middle : MouseButton::Left);
                if (overlay_) overlay_->mouse_press(pos, btn);
                else if (screen_) screen_->mouse_press(pos, btn);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        ReleaseCapture();
        PointF pos;
        if (to_logical(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)), pos)) {
            if (!curtain_.active && !slide_.active) {
                MouseButton btn = (msg == WM_RBUTTONUP) ? MouseButton::Right
                                : ((msg == WM_MBUTTONUP) ? MouseButton::Middle : MouseButton::Left);
                if (overlay_) overlay_->mouse_release(pos, btn);
                else if (screen_) screen_->mouse_release(pos, btn);
            }
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        PointF pos;
        if (to_logical(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)), pos)) {
            if (!curtain_.active && !slide_.active) {
                if (overlay_) overlay_->mouse_double_click(pos);
                else if (screen_) screen_->mouse_double_click(pos);
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        PointF pos;
        if (to_logical(static_cast<float>(pt.x), static_cast<float>(pt.y), pos)) {
            if (!curtain_.active && !slide_.active) {
                int delta = GET_WHEEL_DELTA_WPARAM(wp);
                if (overlay_) overlay_->mouse_wheel(pos, delta);
                else if (screen_) screen_->mouse_wheel(pos, delta);
            }
        }
        return 0;
    }
    case WM_KEYDOWN: {
        int vk = static_cast<int>(wp);
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (vk == key::F11) {
            toggle_fullscreen();
            return 0;
        }
        if (vk == key::F3) {
            show_fps_hud_ = !show_fps_hud_;
            return 0;
        }
        if (curtain_.active || slide_.active) return 0;
        if (overlay_) {
            if (overlay_->key_press(vk, ctrl, shift)) return 0;
        }
        if (screen_ && screen_->key_press(vk, ctrl, shift)) return 0;
        if (vk == key::Escape && fullscreen_) {
            set_fullscreen(false);
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        save_window_state();
        audio_.stop();
        running_ = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        running_ = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace np
