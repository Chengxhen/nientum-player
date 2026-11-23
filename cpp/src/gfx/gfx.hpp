#pragma once

#include "../core/types.hpp"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dwrite_3.h>
#include <dxgi1_5.h>
#include <wincodec.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace np {

enum class TextAlign { Left, Center, Right };
enum class ElideMode { None, Right, Middle };

struct FontKey {
    float size = 16.0f;
    bool serif = true;
    int weight = 400;
    float spacing = 0.0f;

    bool operator==(const FontKey& o) const {
        return static_cast<int>(std::lround(size * 10.0f)) == static_cast<int>(std::lround(o.size * 10.0f)) &&
               serif == o.serif && weight == o.weight &&
               static_cast<int>(std::lround(spacing * 10.0f)) == static_cast<int>(std::lround(o.spacing * 10.0f));
    }
};

struct FontKeyHash {
    size_t operator()(const FontKey& k) const {
        size_t h = std::hash<int>{}(static_cast<int>(std::lround(k.size * 10.0f)));
        h ^= (std::hash<bool>{}(k.serif) << 1);
        h ^= (std::hash<int>{}(k.weight) << 2);
        h ^= (std::hash<int>{}(static_cast<int>(std::lround(k.spacing * 10.0f))) << 3);
        return h;
    }
};

namespace palette {
inline const ColorF INK        = ColorF::hex(0x30241f);
inline const ColorF INK_SOFT   = ColorF::hex(0x5b4b42);
inline const ColorF PAPER      = ColorF::hex(0xe6ddd0);
inline const ColorF PAPER_DARK = ColorF::hex(0xcfc4b4);
inline const ColorF CREAM      = ColorF::hex(0xf4ecdf);
inline const ColorF GOLD       = ColorF::hex(0xc8a166);
inline const ColorF GOLD_LIGHT = ColorF::hex(0xecd6a9);
inline const ColorF GOLD_DEEP  = ColorF::hex(0x9a713a);
inline const ColorF RED        = ColorF::hex(0x6b1d1f);
inline const ColorF RED_DARK   = ColorF::hex(0x2a0d0e);
inline const ColorF DARK       = ColorF::hex(0x0c0a09);
inline const ColorF BLACK      = ColorF::hex(0x000000);
inline const ColorF MUTED      = ColorF::hex(0x9d8d7b);
} // namespace palette

class Gfx {
public:
    Gfx() = default;
    ~Gfx();

    bool init(HWND hwnd, int width, int height);
    void resize(int width, int height);
    void shutdown();

    void begin_frame(int resolution_height, bool animating, int window_w, int window_h,
                     const RectF& viewport);
    void end_frame(bool vsync, bool allow_tearing);
    bool dump_backbuffer_png(const std::wstring& path);

    ID2D1DeviceContext* dc() const { return dc_.Get(); }
    ID3D11Device* d3d() const { return d3d_.Get(); }
    IWICImagingFactory* wic() const { return wic_.Get(); }
    bool tearing_supported() const { return tearing_supported_; }

    ComPtr<ID2D1Bitmap1> create_target_bitmap(int w, int h);
    void set_target(ID2D1Bitmap1* bmp);
    void restore_scene_target();
    void draw_blurred_bitmap(ID2D1Bitmap* src, const RectF& dst, float blur_radius, float opacity = 1.0f);
    void draw_blurred_bitmap_cover(ID2D1Bitmap* src, const RectF& dst, float blur_radius, float opacity = 1.0f);

    void push_clip(const RectF& r);
    void pop_clip();
    void push_rounded_clip(const RectF& r, float radius);
    void push_ellipse_clip(PointF center, float rx, float ry);
    void pop_geometry_clip();
    void save_transform();
    void restore_transform();
    void translate(float dx, float dy);
    void rotate(float deg, PointF center = {0.0f, 0.0f});
    void scale(float sx, float sy, PointF center = {0.0f, 0.0f});
    void set_additive(bool additive);

    void fill_rect(const RectF& r, const ColorF& c);
    void draw_rect(const RectF& r, const ColorF& c, float stroke_w = 1.0f);
    void fill_rounded(const RectF& r, float radius, const ColorF& c);
    void draw_rounded(const RectF& r, float radius, const ColorF& c, float stroke_w = 1.0f);
    void rounded(const RectF& r, float radius, const ColorF* fill, const ColorF* border = nullptr, float border_w = 1.0f);
    void fill_ellipse(PointF c, float rx, float ry, const ColorF& col);
    void draw_ellipse(PointF c, float rx, float ry, const ColorF& col, float stroke_w = 1.0f);
    void draw_line(PointF a, PointF b, const ColorF& col, float stroke_w = 1.0f, bool round_cap = false);
    void fill_polygon(const PointF* pts, size_t count, const ColorF& col);
    void draw_polyline(const PointF* pts, size_t count, const ColorF& col, float stroke_w = 1.0f, bool round_cap = true);
    void draw_arc(PointF center, float rx, float ry, float start_deg, float sweep_deg, const ColorF& col, float stroke_w = 1.0f);

    struct GradStop {
        float pos;
        ColorF color;
    };
    void fill_linear_grad(const RectF& r, PointF p0, PointF p1, const GradStop* stops, size_t count);
    void fill_rounded_linear_grad(const RectF& r, float radius, PointF p0, PointF p1, const GradStop* stops, size_t count);
    void velvet_panel(const RectF& r, float radius, float opacity = 1.0f);
    void fill_radial_grad_rect(const RectF& r, PointF center, float rx, float ry, const GradStop* stops, size_t count);
    void fill_radial_grad_ellipse(PointF center, float rx, float ry, const GradStop* stops, size_t count);

    void draw_bitmap(ID2D1Bitmap* bmp, const RectF& dst, float opacity = 1.0f, bool linear = true);
    void draw_bitmap_flipped_y(ID2D1Bitmap* bmp, const RectF& dst, float opacity = 1.0f);

    float text_width(const std::string& utf8, const FontKey& fk);
    std::string elide_text(const std::string& utf8, const FontKey& fk, float max_w, ElideMode mode = ElideMode::Right);
    void font_metrics(const FontKey& fk, float& ascent, float& descent);
    void text(float x, float baseline_y, const std::string& utf8, const FontKey& fk,
              const ColorF& col = palette::CREAM, bool shadow = false, float alpha = 1.0f);
    void text_center(float cx, float baseline_y, const std::string& utf8, const FontKey& fk,
                     const ColorF& col = palette::CREAM, bool shadow = false, float alpha = 1.0f);
    void text_rect(const RectF& r, const std::string& utf8, const FontKey& fk,
                   const ColorF& col = palette::CREAM, TextAlign align = TextAlign::Left,
                   ElideMode elide = ElideMode::Right);

    void draw_icon(const std::string& name, const RectF& r, const ColorF& col = palette::CREAM, float width = 2.0f);
    void draw_glass_pill(const RectF& r, float radius, bool active = false, bool hover = false,
                         float alpha = 1.0f, bool dimmed = false);
    void draw_gold_divider(float x0, float x1, float y, float alpha = 1.0f, float stroke_w = 1.0f);
    void draw_corner_filigree(const RectF& r, float len, const ColorF& col, float stroke_w = 1.4f, float inset = 0.0f);
    void draw_paper_button(const RectF& r, const std::string& label, const FontKey& fk,
                           bool hover = false, bool primary = false, bool enabled = true,
                           const std::string& icon = "");
    void draw_icon_button(const RectF& r, const std::string& icon, bool hover = false,
                          const ColorF& col = palette::CREAM, float diameter = 0.0f, bool active = false);
    float draw_keycap(float x, float y, const std::string& label, const FontKey& fk, float h = 28.0f);

    ID2D1Bitmap1* glow_sprite() const { return glow_sprite_.Get(); }

private:
    HWND hwnd_ = nullptr;
    int win_w_ = 1600;
    int win_h_ = 900;
    bool tearing_supported_ = false;

    ComPtr<ID3D11Device> d3d_;
    ComPtr<ID3D11DeviceContext> d3d_ctx_;
    ComPtr<IDXGISwapChain1> swapchain_;
    ComPtr<ID2D1Factory1> d2d_factory_;
    ComPtr<ID2D1Device> d2d_device_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1Bitmap1> backbuffer_bmp_;
    ComPtr<ID2D1Bitmap1> offscreen_bmp_;
    int offscreen_w_ = 0;
    int offscreen_h_ = 0;
    bool using_offscreen_ = false;
    RectF current_vp_{0, 0, 1920, 1080};

    ComPtr<ID2D1Effect> blur_effect_;
    ComPtr<ID2D1Bitmap1> glow_sprite_;

    ComPtr<IDWriteFactory5> dw_factory_;
    ComPtr<IDWriteFontCollection1> font_collection_;
    std::wstring serif_family_ = L"Noto Serif SC";
    std::wstring sans_family_ = L"Noto Sans SC";
    std::unordered_map<FontKey, ComPtr<IDWriteTextFormat>, FontKeyHash> fmt_cache_;
    std::unordered_map<FontKey, std::pair<float, float>, FontKeyHash> metrics_cache_;

    struct ElideKey {
        std::string text;
        FontKey fk;
        int max_w_px;
        int mode;
        bool operator==(const ElideKey& o) const {
            return max_w_px == o.max_w_px && mode == o.mode && fk == o.fk && text == o.text;
        }
    };
    struct ElideKeyHash {
        size_t operator()(const ElideKey& k) const {
            return std::hash<std::string>{}(k.text) ^ (FontKeyHash{}(k.fk) << 1) ^ (k.max_w_px << 4);
        }
    };
    std::unordered_map<ElideKey, std::string, ElideKeyHash> elide_cache_;

    ComPtr<IWICImagingFactory> wic_;
    ComPtr<ID2D1SolidColorBrush> solid_brush_;
    ComPtr<ID2D1StrokeStyle> round_cap_stroke_;
    std::vector<D2D1_MATRIX_3X2_F> xform_stack_;
    std::vector<ComPtr<ID2D1Layer>> layer_stack_;

    bool create_backbuffer();
    void init_fonts();
    void create_glow_sprite();
    IDWriteTextFormat* get_format(const FontKey& fk);
    ID2D1SolidColorBrush* brush(const ColorF& c);
};

} // namespace np
