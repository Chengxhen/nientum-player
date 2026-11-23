#include "gfx.hpp"
#include "../core/paths.hpp"

#include <cmath>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dxguid.lib")

namespace np {

namespace {
inline D2D1_COLOR_F to_d2d(const ColorF& c) {
    return D2D1::ColorF(c.r, c.g, c.b, c.a);
}
inline D2D1_RECT_F to_d2d(const RectF& r) {
    return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
}
inline D2D1_ROUNDED_RECT to_d2d_rr(const RectF& r, float rad) {
    return D2D1::RoundedRect(to_d2d(r), rad, rad);
}
inline D2D1_POINT_2F to_d2d(PointF p) {
    return D2D1::Point2F(p.x, p.y);
}
constexpr float kPi = 3.14159265358979323846f;
} // namespace

Gfx::~Gfx() {
    shutdown();
}

bool Gfx::init(HWND hwnd, int width, int height) {
    hwnd_ = hwnd;
    win_w_ = std::max(1, width);
    win_h_ = std::max(1, height);

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    if (FAILED(hr)) return false;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL chosen{};
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
                           D3D11_SDK_VERSION, &d3d_, &chosen, &d3d_ctx_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels),
                               D3D11_SDK_VERSION, &d3d_, &chosen, &d3d_ctx_);
        if (FAILED(hr)) return false;
    }

    ComPtr<IDXGIDevice> dxgi_dev;
    d3d_.As(&dxgi_dev);

    D2D1_FACTORY_OPTIONS opts{};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &opts,
                      reinterpret_cast<void**>(d2d_factory_.GetAddressOf()));
    if (!d2d_factory_) return false;

    if (FAILED(d2d_factory_->CreateDevice(dxgi_dev.Get(), &d2d_device_))) return false;
    if (FAILED(d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, &dc_))) {
        if (FAILED(d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    dxgi_dev->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> factory2;
    adapter->GetParent(IID_PPV_ARGS(&factory2));

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory2.As(&factory5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) {
            tearing_supported_ = (allow == TRUE);
        }
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = static_cast<UINT>(win_w_);
    scd.Height = static_cast<UINT>(win_h_);
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = tearing_supported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    hr = factory2->CreateSwapChainForHwnd(d3d_.Get(), hwnd_, &scd, nullptr, nullptr, &swapchain_);
    if (FAILED(hr)) {
        scd.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
        scd.Flags = 0;
        tearing_supported_ = false;
        hr = factory2->CreateSwapChainForHwnd(d3d_.Get(), hwnd_, &scd, nullptr, nullptr, &swapchain_);
        if (FAILED(hr)) return false;
    }
    factory2->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);

    if (!create_backbuffer()) return false;

    dc_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &solid_brush_);
    D2D1_STROKE_STYLE_PROPERTIES sprops = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    d2d_factory_->CreateStrokeStyle(sprops, nullptr, 0, &round_cap_stroke_);
    dc_->CreateEffect(CLSID_D2D1GaussianBlur, &blur_effect_);

    init_fonts();
    create_glow_sprite();
    return true;
}

void Gfx::shutdown() {
    fmt_cache_.clear();
    metrics_cache_.clear();
    elide_cache_.clear();
    layer_stack_.clear();
    glow_sprite_.Reset();
    blur_effect_.Reset();
    offscreen_bmp_.Reset();
    backbuffer_bmp_.Reset();
    round_cap_stroke_.Reset();
    solid_brush_.Reset();
    dc_.Reset();
    d2d_device_.Reset();
    d2d_factory_.Reset();
    swapchain_.Reset();
    d3d_ctx_.Reset();
    d3d_.Reset();
    font_collection_.Reset();
    dw_factory_.Reset();
    wic_.Reset();
}

bool Gfx::create_backbuffer() {
    backbuffer_bmp_.Reset();
    ComPtr<IDXGISurface> surf;
    if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&surf)))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    return SUCCEEDED(dc_->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &backbuffer_bmp_));
}

void Gfx::resize(int width, int height) {
    if (!swapchain_ || width <= 0 || height <= 0) return;
    if (width == win_w_ && height == win_h_) return;
    win_w_ = width;
    win_h_ = height;
    dc_->SetTarget(nullptr);
    backbuffer_bmp_.Reset();
    UINT flags = tearing_supported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    swapchain_->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, flags);
    create_backbuffer();
}

ComPtr<ID2D1Bitmap1> Gfx::create_target_bitmap(int w, int h) {
    ComPtr<ID2D1Bitmap1> bmp;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    dc_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(std::max(1, w)), static_cast<UINT32>(std::max(1, h))),
                      nullptr, 0, &bp, &bmp);
    return bmp;
}

void Gfx::set_target(ID2D1Bitmap1* bmp) {
    dc_->SetTarget(bmp);
}

void Gfx::restore_scene_target() {
    if (using_offscreen_ && offscreen_bmp_) {
        dc_->SetTarget(offscreen_bmp_.Get());
    } else if (backbuffer_bmp_) {
        dc_->SetTarget(backbuffer_bmp_.Get());
    }
}

void Gfx::init_fonts() {
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5),
                                   reinterpret_cast<IUnknown**>(dw_factory_.GetAddressOf())))) {
        return;
    }
    ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(dw_factory_->CreateFontSetBuilder(&builder))) return;

    auto add_font = [&](const wchar_t* fname, std::wstring& out_fam) {
        std::wstring p = paths::data_path(std::wstring(L"fonts\\") + fname);
        ComPtr<IDWriteFontFile> ff;
        if (SUCCEEDED(dw_factory_->CreateFontFileReference(p.c_str(), nullptr, &ff))) {
            builder->AddFontFile(ff.Get());
            // Check family name directly
            ComPtr<IDWriteFontSetBuilder1> single_b;
            if (SUCCEEDED(dw_factory_->CreateFontSetBuilder(&single_b))) {
                single_b->AddFontFile(ff.Get());
                ComPtr<IDWriteFontSet> fset;
                if (SUCCEEDED(single_b->CreateFontSet(&fset))) {
                    ComPtr<IDWriteFontCollection1> col;
                    if (SUCCEEDED(dw_factory_->CreateFontCollectionFromFontSet(fset.Get(), &col)) && col->GetFontFamilyCount() > 0) {
                        ComPtr<IDWriteFontFamily> fam;
                        col->GetFontFamily(0, &fam);
                        ComPtr<IDWriteLocalizedStrings> names;
                        if (fam && SUCCEEDED(fam->GetFamilyNames(&names))) {
                            wchar_t buf[128]{};
                            if (SUCCEEDED(names->GetString(0, buf, 128))) out_fam = buf;
                        }
                    }
                }
            }
        }
    };

    add_font(L"NotoSerifSC-VF.ttf", serif_family_);
    add_font(L"NotoSansSC-VF.ttf", sans_family_);

    ComPtr<IDWriteFontSet> fset;
    if (SUCCEEDED(builder->CreateFontSet(&fset))) {
        dw_factory_->CreateFontCollectionFromFontSet(fset.Get(), &font_collection_);
    }
}

void Gfx::create_glow_sprite() {
    const int S = 128;
    std::vector<uint8_t> px(S * S * 4, 0);
    const float c = (S - 1) * 0.5f;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            float dx = (x - c) / c;
            float dy = (y - c) / c;
            float d = std::sqrt(dx * dx + dy * dy);
            float a = 0.0f;
            if (d < 1.0f) {
                float u = 1.0f - d;
                a = u * u * (0.35f + 0.65f * u * u);
            }
            uint8_t v = static_cast<uint8_t>(clampf(a) * 255.0f);
            size_t idx = (static_cast<size_t>(y) * S + x) * 4;
            px[idx + 0] = v; // B
            px[idx + 1] = v; // G
            px[idx + 2] = v; // R
            px[idx + 3] = v; // A (premultiplied white glow)
        }
    }
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    dc_->CreateBitmap(D2D1::SizeU(S, S), px.data(), S * 4, &bp, &glow_sprite_);
}

void Gfx::begin_frame(int resolution_height, bool animating, int window_w, int window_h, const RectF& viewport) {
    current_vp_ = viewport;
    xform_stack_.clear();

    float factor = clampf(static_cast<float>(resolution_height) / 1080.0f, 0.5f, 2.0f);
    if (animating && factor > 1.0f) factor = 1.0f;

    using_offscreen_ = (std::abs(factor - 1.0f) > 0.002f);
    if (using_offscreen_) {
        int sw = std::max(1, static_cast<int>(std::lround(1920.0f * factor)));
        int sh = std::max(1, static_cast<int>(std::lround(1080.0f * factor)));
        if (!offscreen_bmp_ || offscreen_w_ != sw || offscreen_h_ != sh) {
            offscreen_bmp_ = create_target_bitmap(sw, sh);
            offscreen_w_ = sw;
            offscreen_h_ = sh;
        }
        dc_->SetTarget(offscreen_bmp_.Get());
        dc_->BeginDraw();
        dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        dc_->Clear(D2D1::ColorF(0.051f, 0.035f, 0.027f, 1.0f));
        dc_->SetTransform(D2D1::Matrix3x2F::Scale(factor, factor));
        dc_->PushAxisAlignedClip(D2D1::RectF(0, 0, 1920.0f, 1080.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    } else {
        dc_->SetTarget(backbuffer_bmp_.Get());
        dc_->BeginDraw();
        dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
        dc_->Clear(D2D1::ColorF(13.0f / 255.0f, 9.0f / 255.0f, 7.0f / 255.0f, 1.0f));

        // Theatre matte around 16:9 stage when window/fullscreen is non-16:9
        float w = static_cast<float>(window_w);
        float h = static_cast<float>(window_h);
        if (viewport.top() > 0.5f) {
            GradStop gs[2] = {{0.0f, ColorF::rgba(5, 4, 3)}, {1.0f, ColorF::rgba(27, 16, 12, 248)}};
            fill_linear_grad({0, 0, w, viewport.top()}, {0, 0}, {0, viewport.top()}, gs, 2);
            draw_line({viewport.left(), viewport.top()}, {viewport.right(), viewport.top()}, ColorF::rgba(194, 154, 102, 82), 1.0f);
        }
        if (viewport.bottom() < h - 0.5f) {
            GradStop gs[2] = {{0.0f, ColorF::rgba(27, 16, 12, 248)}, {1.0f, ColorF::rgba(5, 4, 3)}};
            fill_linear_grad({0, viewport.bottom(), w, h - viewport.bottom()}, {0, viewport.bottom()}, {0, h}, gs, 2);
            draw_line({viewport.left(), viewport.bottom()}, {viewport.right(), viewport.bottom()}, ColorF::rgba(194, 154, 102, 82), 1.0f);
        }
        if (viewport.left() > 0.5f) {
            GradStop gs[2] = {{0.0f, ColorF::rgba(5, 4, 3)}, {1.0f, ColorF::rgba(27, 16, 12, 248)}};
            fill_linear_grad({0, 0, viewport.left(), h}, {0, 0}, {viewport.left(), 0}, gs, 2);
            draw_line({viewport.left(), viewport.top()}, {viewport.left(), viewport.bottom()}, ColorF::rgba(194, 154, 102, 82), 1.0f);
        }
        if (viewport.right() < w - 0.5f) {
            GradStop gs[2] = {{0.0f, ColorF::rgba(27, 16, 12, 248)}, {1.0f, ColorF::rgba(5, 4, 3)}};
            fill_linear_grad({viewport.right(), 0, w - viewport.right(), h}, {viewport.right(), 0}, {w, 0}, gs, 2);
            draw_line({viewport.right(), viewport.top()}, {viewport.right(), viewport.bottom()}, ColorF::rgba(194, 154, 102, 82), 1.0f);
        }

        float s = viewport.w / 1920.0f;
        D2D1_MATRIX_3X2_F xf = D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(viewport.x, viewport.y);
        dc_->SetTransform(xf);
        dc_->PushAxisAlignedClip(D2D1::RectF(0, 0, 1920.0f, 1080.0f), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
}

void Gfx::end_frame(bool vsync, bool allow_tearing) {
    dc_->PopAxisAlignedClip();

    if (using_offscreen_ && offscreen_bmp_) {
        dc_->EndDraw();
        dc_->SetTarget(backbuffer_bmp_.Get());
        dc_->BeginDraw();
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
        dc_->Clear(D2D1::ColorF(13.0f / 255.0f, 9.0f / 255.0f, 7.0f / 255.0f, 1.0f));
        dc_->DrawBitmap(offscreen_bmp_.Get(), to_d2d(current_vp_), 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        dc_->EndDraw();
    } else {
        dc_->EndDraw();
    }

    UINT sync_interval = vsync ? 1 : 0;
    UINT flags = (!vsync && allow_tearing && tearing_supported_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT hr = swapchain_->Present(sync_interval, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        paths::log("DXGI device lost during Present");
    }
}

bool Gfx::dump_backbuffer_png(const std::wstring& path) {
    if (!swapchain_ || !d3d_ || !d3d_ctx_ || !wic_) return false;
    ComPtr<ID3D11Texture2D> bb;
    if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&bb)))) return false;
    D3D11_TEXTURE2D_DESC desc{};
    bb->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(d3d_->CreateTexture2D(&desc, nullptr, &staging))) return false;
    d3d_ctx_->CopyResource(staging.Get(), bb.Get());

    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(d3d_ctx_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    std::vector<uint8_t> rgba(static_cast<size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        const uint8_t* src = static_cast<const uint8_t*>(map.pData) + y * map.RowPitch;
        uint8_t* dst = rgba.data() + static_cast<size_t>(y) * desc.Width * 4;
        std::memcpy(dst, src, desc.Width * 4);
        for (UINT x = 0; x < desc.Width; ++x) dst[x * 4 + 3] = 255;
    }
    d3d_ctx_->Unmap(staging.Get(), 0);

    ComPtr<IWICStream> stream;
    if (FAILED(wic_->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) return false;
    if (FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(enc->CreateNewFrame(&frame, nullptr))) return false;
    if (FAILED(frame->Initialize(nullptr))) return false;
    frame->SetSize(desc.Width, desc.Height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&fmt);
    frame->WritePixels(desc.Height, desc.Width * 4, static_cast<UINT>(rgba.size()), rgba.data());
    frame->Commit();
    enc->Commit();
    return true;
}

void Gfx::draw_blurred_bitmap(ID2D1Bitmap* src, const RectF& dst, float blur_radius, float opacity) {
    if (!src || opacity <= 0.002f) return;
    if (blur_radius <= 0.25f || !blur_effect_) {
        draw_bitmap(src, dst, opacity, true);
        return;
    }
    blur_effect_->SetInput(0, src);
    blur_effect_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blur_radius);
    blur_effect_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    blur_effect_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);

    ComPtr<ID2D1Image> img;
    blur_effect_->GetOutput(&img);
    D2D1_SIZE_F sz = src->GetSize();
    float sx = dst.w / std::max(1.0f, sz.width);
    float sy = dst.h / std::max(1.0f, sz.height);
    save_transform();
    dc_->PushAxisAlignedClip(to_d2d(dst), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    D2D1_MATRIX_3X2_F cur;
    dc_->GetTransform(&cur);
    dc_->SetTransform(D2D1::Matrix3x2F::Scale(sx, sy) * D2D1::Matrix3x2F::Translation(dst.x, dst.y) * cur);
    if (opacity >= 0.999f) {
        dc_->DrawImage(img.Get(), D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    } else {
        ComPtr<ID2D1Layer> layer;
        dc_->CreateLayer(&layer);
        D2D1_LAYER_PARAMETERS1 lp = D2D1::LayerParameters1(
            D2D1::RectF(0, 0, sz.width, sz.height), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::Matrix3x2F::Identity(), opacity);
        dc_->PushLayer(&lp, layer.Get());
        dc_->DrawImage(img.Get(), D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
        dc_->PopLayer();
    }
    dc_->PopAxisAlignedClip();
    restore_transform();
}

void Gfx::draw_blurred_bitmap_cover(ID2D1Bitmap* src, const RectF& dst, float blur_radius, float opacity) {
    if (!src || opacity <= 0.002f || dst.empty()) return;
    D2D1_SIZE_F sz = src->GetSize();
    float s = std::max(dst.w / std::max(1.0f, sz.width), dst.h / std::max(1.0f, sz.height));
    float dw = sz.width * s;
    float dh = sz.height * s;
    RectF cover_dst{dst.x + (dst.w - dw) * 0.5f, dst.y + (dst.h - dh) * 0.5f, dw, dh};
    push_clip(dst);
    draw_blurred_bitmap(src, cover_dst, blur_radius, opacity);
    pop_clip();
}

void Gfx::push_clip(const RectF& r) {
    dc_->PushAxisAlignedClip(to_d2d(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Gfx::pop_clip() {
    dc_->PopAxisAlignedClip();
}

void Gfx::push_rounded_clip(const RectF& r, float radius) {
    ComPtr<ID2D1RoundedRectangleGeometry> geo;
    d2d_factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(to_d2d(r), radius, radius), &geo);
    ComPtr<ID2D1Layer> layer;
    dc_->CreateLayer(&layer);
    D2D1_LAYER_PARAMETERS1 lp = D2D1::LayerParameters1(
        to_d2d(r), geo.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    dc_->PushLayer(&lp, layer.Get());
    layer_stack_.push_back(std::move(layer));
}

void Gfx::push_ellipse_clip(PointF center, float rx, float ry) {
    ComPtr<ID2D1EllipseGeometry> geo;
    d2d_factory_->CreateEllipseGeometry(D2D1::Ellipse(to_d2d(center), rx, ry), &geo);
    ComPtr<ID2D1Layer> layer;
    dc_->CreateLayer(&layer);
    D2D1_LAYER_PARAMETERS1 lp = D2D1::LayerParameters1(
        D2D1::RectF(center.x - rx, center.y - ry, center.x + rx, center.y + ry),
        geo.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    dc_->PushLayer(&lp, layer.Get());
    layer_stack_.push_back(std::move(layer));
}

void Gfx::pop_geometry_clip() {
    dc_->PopLayer();
    if (!layer_stack_.empty()) layer_stack_.pop_back();
}

void Gfx::save_transform() {
    D2D1_MATRIX_3X2_F m;
    dc_->GetTransform(&m);
    xform_stack_.push_back(m);
}

void Gfx::restore_transform() {
    if (xform_stack_.empty()) return;
    dc_->SetTransform(xform_stack_.back());
    xform_stack_.pop_back();
}

void Gfx::translate(float dx, float dy) {
    D2D1_MATRIX_3X2_F cur;
    dc_->GetTransform(&cur);
    dc_->SetTransform(D2D1::Matrix3x2F::Translation(dx, dy) * cur);
}

void Gfx::rotate(float deg, PointF center) {
    D2D1_MATRIX_3X2_F cur;
    dc_->GetTransform(&cur);
    dc_->SetTransform(D2D1::Matrix3x2F::Rotation(deg, to_d2d(center)) * cur);
}

void Gfx::scale(float sx, float sy, PointF center) {
    D2D1_MATRIX_3X2_F cur;
    dc_->GetTransform(&cur);
    dc_->SetTransform(D2D1::Matrix3x2F::Scale(sx, sy, to_d2d(center)) * cur);
}

void Gfx::set_additive(bool additive) {
    dc_->SetPrimitiveBlend(additive ? D2D1_PRIMITIVE_BLEND_ADD : D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
}

ID2D1SolidColorBrush* Gfx::brush(const ColorF& c) {
    solid_brush_->SetColor(to_d2d(c));
    return solid_brush_.Get();
}

void Gfx::fill_rect(const RectF& r, const ColorF& c) {
    if (c.a <= 0.001f) return;
    dc_->FillRectangle(to_d2d(r), brush(c));
}

void Gfx::draw_rect(const RectF& r, const ColorF& c, float stroke_w) {
    if (c.a <= 0.001f) return;
    dc_->DrawRectangle(to_d2d(r), brush(c), stroke_w);
}

void Gfx::fill_rounded(const RectF& r, float radius, const ColorF& c) {
    if (c.a <= 0.001f) return;
    dc_->FillRoundedRectangle(to_d2d_rr(r, radius), brush(c));
}

void Gfx::draw_rounded(const RectF& r, float radius, const ColorF& c, float stroke_w) {
    if (c.a <= 0.001f) return;
    dc_->DrawRoundedRectangle(to_d2d_rr(r, radius), brush(c), stroke_w);
}

void Gfx::rounded(const RectF& r, float radius, const ColorF* fill, const ColorF* border, float border_w) {
    if (fill) fill_rounded(r, radius, *fill);
    if (border) draw_rounded(r, radius, *border, border_w);
}

void Gfx::fill_ellipse(PointF c, float rx, float ry, const ColorF& col) {
    if (col.a <= 0.001f) return;
    dc_->FillEllipse(D2D1::Ellipse(to_d2d(c), rx, ry), brush(col));
}

void Gfx::draw_ellipse(PointF c, float rx, float ry, const ColorF& col, float stroke_w) {
    if (col.a <= 0.001f) return;
    dc_->DrawEllipse(D2D1::Ellipse(to_d2d(c), rx, ry), brush(col), stroke_w);
}

void Gfx::draw_line(PointF a, PointF b, const ColorF& col, float stroke_w, bool round_cap) {
    if (col.a <= 0.001f) return;
    dc_->DrawLine(to_d2d(a), to_d2d(b), brush(col), stroke_w, round_cap ? round_cap_stroke_.Get() : nullptr);
}

void Gfx::fill_polygon(const PointF* pts, size_t count, const ColorF& col) {
    if (count < 3 || col.a <= 0.001f) return;
    ComPtr<ID2D1PathGeometry> path;
    d2d_factory_->CreatePathGeometry(&path);
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(&sink);
    sink->BeginFigure(to_d2d(pts[0]), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < count; ++i) sink->AddLine(to_d2d(pts[i]));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    dc_->FillGeometry(path.Get(), brush(col));
}

void Gfx::draw_polyline(const PointF* pts, size_t count, const ColorF& col, float stroke_w, bool round_cap) {
    if (count < 2 || col.a <= 0.001f) return;
    for (size_t i = 0; i + 1 < count; ++i) {
        draw_line(pts[i], pts[i + 1], col, stroke_w, round_cap);
    }
}

void Gfx::draw_arc(PointF center, float rx, float ry, float start_deg, float sweep_deg, const ColorF& col, float stroke_w) {
    if (col.a <= 0.001f) return;
    const int segs = 16;
    PointF pts[17];
    for (int i = 0; i <= segs; ++i) {
        float ang = (start_deg + sweep_deg * (static_cast<float>(i) / segs)) * (kPi / 180.0f);
        pts[i] = {center.x + rx * std::cos(ang), center.y - ry * std::sin(ang)};
    }
    draw_polyline(pts, segs + 1, col, stroke_w, true);
}

void Gfx::fill_linear_grad(const RectF& r, PointF p0, PointF p1, const GradStop* stops, size_t count) {
    if (count < 2) return;
    D2D1_GRADIENT_STOP d2stops[16];
    size_t n = std::min<size_t>(count, 16);
    for (size_t i = 0; i < n; ++i) {
        d2stops[i].position = stops[i].pos;
        d2stops[i].color = to_d2d(stops[i].color);
    }
    ComPtr<ID2D1GradientStopCollection> col;
    dc_->CreateGradientStopCollection(d2stops, static_cast<UINT32>(n), &col);
    ComPtr<ID2D1LinearGradientBrush> lg;
    dc_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(to_d2d(p0), to_d2d(p1)), col.Get(), &lg);
    dc_->FillRectangle(to_d2d(r), lg.Get());
}

void Gfx::fill_rounded_linear_grad(const RectF& r, float radius, PointF p0, PointF p1, const GradStop* stops, size_t count) {
    if (count < 2) return;
    D2D1_GRADIENT_STOP d2stops[16];
    size_t n = std::min<size_t>(count, 16);
    for (size_t i = 0; i < n; ++i) {
        d2stops[i].position = stops[i].pos;
        d2stops[i].color = to_d2d(stops[i].color);
    }
    ComPtr<ID2D1GradientStopCollection> col;
    dc_->CreateGradientStopCollection(d2stops, static_cast<UINT32>(n), &col);
    ComPtr<ID2D1LinearGradientBrush> lg;
    dc_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(to_d2d(p0), to_d2d(p1)), col.Get(), &lg);
    ComPtr<ID2D1RoundedRectangleGeometry> geo;
    if (FAILED(d2d_factory_->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(to_d2d(r), radius, radius), &geo))) return;
    dc_->FillGeometry(geo.Get(), lg.Get());
}

void Gfx::velvet_panel(const RectF& r, float radius, float opacity) {
    opacity = clampf(opacity);
    if (opacity <= 0.001f || r.empty()) return;

    // Soft ambient seat shadow under the smoked-glass panel
    fill_rounded({r.x + 2.0f, r.y + 7.0f, r.w, r.h}, radius + 2.0f,
                 ColorF::rgba(0, 0, 0, 82).mul_alpha(opacity));

    // Warm smoked amber glass body (translucent top, deep umber base)
    GradStop body[5] = {
        {0.00f, ColorF::rgba(68, 51, 38, 198)},
        {0.18f, ColorF::rgba(46, 35, 26, 208)},
        {0.54f, ColorF::rgba(28, 21, 16, 218)},
        {0.84f, ColorF::rgba(18, 13, 10, 224)},
        {1.00f, ColorF::rgba(12, 9, 7, 228)},
    };
    for (auto& s : body) s.color = s.color.mul_alpha(opacity);
    fill_rounded_linear_grad(r, radius, {r.x, r.y}, {r.x, r.bottom()}, body, 5);

    // Top specular glass reflection band (upper 42% sheen)
    float sheen_h = std::min(r.h * 0.44f, 52.0f);
    if (sheen_h > 4.0f) {
        GradStop sheen[2] = {
            {0.0f, ColorF::rgba(255, 244, 224, 26).mul_alpha(opacity)},
            {1.0f, ColorF::rgba(255, 244, 224, 0).mul_alpha(opacity)},
        };
        fill_rounded_linear_grad({r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, sheen_h},
                                 std::max(1.0f, radius - 1.0f),
                                 {r.x, r.y + 1.0f}, {r.x, r.y + 1.0f + sheen_h}, sheen, 2);
    }

    // Outer gilt frame + crisp inner glass rim
    draw_rounded(r, radius, ColorF::rgba(216, 178, 118, 136).mul_alpha(opacity), 1.05f);
    draw_rounded(r.adjusted(1.2f, 1.2f, -1.2f, -1.2f), std::max(1.0f, radius - 1.2f),
                 ColorF::rgba(255, 242, 218, 22).mul_alpha(opacity), 0.8f);

    draw_gold_divider(r.x + 16.0f, r.right() - 16.0f, r.y + 1.1f, opacity * 0.68f, 1.2f);
    draw_line({r.x + 14.0f, r.bottom() - 1.6f}, {r.right() - 14.0f, r.bottom() - 1.6f},
              ColorF::rgba(0, 0, 0, 56).mul_alpha(opacity), 1.6f);
}

void Gfx::fill_radial_grad_rect(const RectF& r, PointF center, float rx, float ry, const GradStop* stops, size_t count) {
    if (count < 2) return;
    D2D1_GRADIENT_STOP d2stops[16];
    size_t n = std::min<size_t>(count, 16);
    for (size_t i = 0; i < n; ++i) {
        d2stops[i].position = stops[i].pos;
        d2stops[i].color = to_d2d(stops[i].color);
    }
    ComPtr<ID2D1GradientStopCollection> col;
    dc_->CreateGradientStopCollection(d2stops, static_cast<UINT32>(n), &col);
    ComPtr<ID2D1RadialGradientBrush> rg;
    dc_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(to_d2d(center), {0, 0}, rx, ry), col.Get(), &rg);
    dc_->FillRectangle(to_d2d(r), rg.Get());
}

void Gfx::fill_radial_grad_ellipse(PointF center, float rx, float ry, const GradStop* stops, size_t count) {
    if (count < 2) return;
    D2D1_GRADIENT_STOP d2stops[16];
    size_t n = std::min<size_t>(count, 16);
    for (size_t i = 0; i < n; ++i) {
        d2stops[i].position = stops[i].pos;
        d2stops[i].color = to_d2d(stops[i].color);
    }
    ComPtr<ID2D1GradientStopCollection> col;
    dc_->CreateGradientStopCollection(d2stops, static_cast<UINT32>(n), &col);
    ComPtr<ID2D1RadialGradientBrush> rg;
    dc_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(to_d2d(center), {0, 0}, rx, ry), col.Get(), &rg);
    dc_->FillEllipse(D2D1::Ellipse(to_d2d(center), rx, ry), rg.Get());
}

void Gfx::draw_bitmap(ID2D1Bitmap* bmp, const RectF& dst, float opacity, bool linear) {
    if (!bmp || opacity <= 0.001f) return;
    dc_->DrawBitmap(bmp, to_d2d(dst), opacity,
                    linear ? D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

void Gfx::draw_bitmap_flipped_y(ID2D1Bitmap* bmp, const RectF& dst, float opacity) {
    if (!bmp || opacity <= 0.001f) return;
    save_transform();
    float cy = dst.y + dst.h * 0.5f;
    scale(1.0f, -1.0f, {dst.x + dst.w * 0.5f, cy});
    draw_bitmap(bmp, dst, opacity, true);
    restore_transform();
}

IDWriteTextFormat* Gfx::get_format(const FontKey& fk) {
    auto it = fmt_cache_.find(fk);
    if (it != fmt_cache_.end()) return it->second.Get();
    if (!dw_factory_) return nullptr;

    const std::wstring& fam = fk.serif ? serif_family_ : sans_family_;
    DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL;
    if (fk.weight >= 700) w = DWRITE_FONT_WEIGHT_BOLD;
    else if (fk.weight >= 500) w = DWRITE_FONT_WEIGHT_MEDIUM;

    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = dw_factory_->CreateTextFormat(
        fam.c_str(), font_collection_.Get(), w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        std::max(1.0f, fk.size), L"zh-CN", &fmt);
    if (FAILED(hr) || !fmt) {
        dw_factory_->CreateTextFormat(L"Microsoft YaHei", nullptr, w, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, std::max(1.0f, fk.size), L"zh-CN", &fmt);
    }
    if (fmt) {
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (fk.spacing != 0.0f) {
            ComPtr<IDWriteTextFormat1> fmt1;
            if (SUCCEEDED(fmt.As(&fmt1))) {
                // DirectWrite SetCharacterSpacing exists on TextLayout; stored in FontKey for layout creation
            }
        }
        fmt_cache_[fk] = fmt;
    }
    return fmt.Get();
}

void Gfx::font_metrics(const FontKey& fk, float& ascent, float& descent) {
    auto it = metrics_cache_.find(fk);
    if (it != metrics_cache_.end()) {
        ascent = it->second.first;
        descent = it->second.second;
        return;
    }
    ascent = fk.size * 0.88f;
    descent = fk.size * 0.24f;
    IDWriteTextFormat* fmt = get_format(fk);
    if (fmt && dw_factory_) {
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(dw_factory_->CreateTextLayout(L"Ag中", 3, fmt, 4000.0f, 400.0f, &layout))) {
            DWRITE_LINE_METRICS lm{};
            UINT32 cnt = 0;
            if (SUCCEEDED(layout->GetLineMetrics(&lm, 1, &cnt)) && cnt >= 1) {
                ascent = lm.baseline;
                descent = std::max(1.0f, lm.height - lm.baseline);
            }
        }
    }
    metrics_cache_[fk] = {ascent, descent};
}

float Gfx::text_width(const std::string& utf8, const FontKey& fk) {
    if (utf8.empty() || !dw_factory_) return 0.0f;
    IDWriteTextFormat* fmt = get_format(fk);
    if (!fmt) return 0.0f;
    std::wstring w = to_wide(utf8);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw_factory_->CreateTextLayout(w.c_str(), static_cast<UINT32>(w.size()), fmt, 8000.0f, 400.0f, &layout))) {
        return 0.0f;
    }
    if (fk.spacing != 0.0f) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(layout.As(&l1))) {
            l1->SetCharacterSpacing(0.0f, fk.spacing, 0.0f, {0, static_cast<UINT32>(w.size())});
        }
    }
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    return tm.widthIncludingTrailingWhitespace;
}

std::string Gfx::elide_text(const std::string& utf8, const FontKey& fk, float max_w, ElideMode mode) {
    if (mode == ElideMode::None || utf8.empty() || max_w <= 4.0f) return utf8;
    ElideKey ek{utf8, fk, static_cast<int>(std::lround(max_w)), static_cast<int>(mode)};
    auto it = elide_cache_.find(ek);
    if (it != elide_cache_.end()) return it->second;

    if (text_width(utf8, fk) <= max_w) {
        if (elide_cache_.size() > 2048) elide_cache_.clear();
        elide_cache_[ek] = utf8;
        return utf8;
    }

    // Split UTF-8 into codepoint byte boundaries
    std::vector<size_t> bounds;
    bounds.push_back(0);
    for (size_t i = 0; i < utf8.size();) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        size_t adv = 1;
        if ((c & 0xE0) == 0xC0) adv = 2;
        else if ((c & 0xF0) == 0xE0) adv = 3;
        else if ((c & 0xF8) == 0xF0) adv = 4;
        i = std::min(utf8.size(), i + adv);
        bounds.push_back(i);
    }

    const std::string ell = "...";
    std::string res = ell;
    if (mode == ElideMode::Right) {
        int lo = 0, hi = static_cast<int>(bounds.size()) - 1;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            std::string cand = utf8.substr(0, bounds[mid]) + ell;
            if (text_width(cand, fk) <= max_w) lo = mid;
            else hi = mid - 1;
        }
        res = utf8.substr(0, bounds[lo]) + ell;
    } else {
        int n = static_cast<int>(bounds.size()) - 1;
        int lo = 0, hi = n / 2;
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            std::string cand = utf8.substr(0, bounds[mid]) + ell + utf8.substr(bounds[n - mid]);
            if (text_width(cand, fk) <= max_w) lo = mid;
            else hi = mid - 1;
        }
        res = utf8.substr(0, bounds[lo]) + ell + utf8.substr(bounds[n - lo]);
    }
    if (elide_cache_.size() > 2048) elide_cache_.clear();
    elide_cache_[ek] = res;
    return res;
}

void Gfx::text(float x, float baseline_y, const std::string& utf8, const FontKey& fk,
               const ColorF& col, bool shadow, float alpha) {
    if (utf8.empty() || !dw_factory_ || alpha <= 0.002f) return;
    IDWriteTextFormat* fmt = get_format(fk);
    if (!fmt) return;
    std::wstring w = to_wide(utf8);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw_factory_->CreateTextLayout(w.c_str(), static_cast<UINT32>(w.size()), fmt, 8000.0f, 400.0f, &layout))) {
        return;
    }
    if (fk.spacing != 0.0f) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(layout.As(&l1))) {
            l1->SetCharacterSpacing(0.0f, fk.spacing, 0.0f, {0, static_cast<UINT32>(w.size())});
        }
    }
    DWRITE_LINE_METRICS lm{};
    UINT32 cnt = 0;
    float ascent = fk.size * 0.88f;
    if (SUCCEEDED(layout->GetLineMetrics(&lm, 1, &cnt)) && cnt >= 1) {
        ascent = lm.baseline;
    }
    float top_y = baseline_y - ascent;

    ColorF c = col.mul_alpha(alpha);
    if (shadow) {
        float sa = std::min(0.75f, c.a * 0.75f);
        dc_->DrawTextLayout({x + 1.6f, top_y + 1.8f}, layout.Get(), brush({0, 0, 0, sa}), D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
    }
    dc_->DrawTextLayout({x, top_y}, layout.Get(), brush(c), D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
}

void Gfx::text_center(float cx, float baseline_y, const std::string& utf8, const FontKey& fk,
                      const ColorF& col, bool shadow, float alpha) {
    float w = text_width(utf8, fk);
    text(cx - w * 0.5f, baseline_y, utf8, fk, col, shadow, alpha);
}

void Gfx::text_rect(const RectF& r, const std::string& utf8, const FontKey& fk,
                    const ColorF& col, TextAlign align, ElideMode elide) {
    std::string t = elide_text(utf8, fk, r.w, elide);
    float w = text_width(t, fk);
    float asc = 0, desc = 0;
    font_metrics(fk, asc, desc);
    float baseline_y = r.y + (r.h + asc - desc) * 0.5f;
    float x = r.x;
    if (align == TextAlign::Center) x = r.cx() - w * 0.5f;
    else if (align == TextAlign::Right) x = r.right() - w;
    text(x, baseline_y, t, fk, col);
}

// -----------------------------------------------------------------------------
// Theatre UI Widgets
// -----------------------------------------------------------------------------
void Gfx::draw_icon(const std::string& name, const RectF& r, const ColorF& col, float width) {
    float cx = r.cx();
    float cy = r.cy();
    float s = std::min(r.w, r.h);

    if (name == "play") {
        PointF pts[3] = {
            {cx - s * 0.20f, cy - s * 0.28f},
            {cx - s * 0.20f, cy + s * 0.28f},
            {cx + s * 0.30f, cy},
        };
        fill_polygon(pts, 3, col);
    } else if (name == "pause") {
        fill_rounded({cx - s * 0.22f, cy - s * 0.28f, s * 0.15f, s * 0.56f}, 1.5f, col);
        fill_rounded({cx + s * 0.07f, cy - s * 0.28f, s * 0.15f, s * 0.56f}, 1.5f, col);
    } else if (name == "prev" || name == "next") {
        float d = (name == "next") ? 1.0f : -1.0f;
        PointF pts[3] = {
            {cx - d * s * 0.24f, cy - s * 0.26f},
            {cx - d * s * 0.24f, cy + s * 0.26f},
            {cx + d * s * 0.10f, cy},
        };
        fill_polygon(pts, 3, col);
        fill_rounded({cx + d * s * 0.16f - s * 0.04f, cy - s * 0.28f, s * 0.08f, s * 0.56f}, 1.0f, col);
    } else if (name == "volume" || name == "mute") {
        PointF pts[6] = {
            {cx - s * 0.36f, cy - s * 0.12f},
            {cx - s * 0.20f, cy - s * 0.12f},
            {cx - s * 0.02f, cy - s * 0.30f},
            {cx - s * 0.02f, cy + s * 0.30f},
            {cx - s * 0.20f, cy + s * 0.12f},
            {cx - s * 0.36f, cy + s * 0.12f},
        };
        fill_polygon(pts, 6, col);
        if (name == "mute") {
            draw_line({cx + s * 0.06f, cy - s * 0.14f}, {cx + s * 0.26f, cy + s * 0.14f}, col, width, true);
            draw_line({cx + s * 0.26f, cy - s * 0.14f}, {cx + s * 0.06f, cy + s * 0.14f}, col, width, true);
        } else {
            draw_arc({cx + s * 0.17f, cy}, s * 0.15f, s * 0.20f, -55.0f, 110.0f, col, width);
            draw_arc({cx + s * 0.25f, cy}, s * 0.23f, s * 0.32f, -50.0f, 100.0f, col, width);
        }
    } else if (name == "fullscreen") {
        float k = s * 0.26f;
        for (int sx : {-1, 1}) {
            for (int sy : {-1, 1}) {
                float px = cx + sx * s * 0.30f;
                float py = cy + sy * s * 0.30f;
                draw_line({px, py}, {px - sx * k, py}, col, width, true);
                draw_line({px, py}, {px, py - sy * k}, col, width, true);
            }
        }
    } else if (name == "restore") {
        float k = s * 0.24f;
        for (int sx : {-1, 1}) {
            for (int sy : {-1, 1}) {
                float px = cx + sx * s * 0.16f;
                float py = cy + sy * s * 0.16f;
                draw_line({px, py}, {px + sx * k, py}, col, width, true);
                draw_line({px, py}, {px, py + sy * k}, col, width, true);
            }
        }
    } else if (name == "close") {
        draw_line({cx - s * 0.20f, cy - s * 0.20f}, {cx + s * 0.20f, cy + s * 0.20f}, col, width, true);
        draw_line({cx + s * 0.20f, cy - s * 0.20f}, {cx - s * 0.20f, cy + s * 0.20f}, col, width, true);
    } else if (name == "left" || name == "right" || name == "up" || name == "down") {
        if (name == "left") {
            PointF pts[3] = {{cx + s * 0.12f, cy - s * 0.22f}, {cx - s * 0.12f, cy}, {cx + s * 0.12f, cy + s * 0.22f}};
            draw_polyline(pts, 3, col, width, true);
        } else if (name == "right") {
            PointF pts[3] = {{cx - s * 0.12f, cy - s * 0.22f}, {cx + s * 0.12f, cy}, {cx - s * 0.12f, cy + s * 0.22f}};
            draw_polyline(pts, 3, col, width, true);
        } else if (name == "up") {
            PointF pts[3] = {{cx - s * 0.22f, cy + s * 0.12f}, {cx, cy - s * 0.12f}, {cx + s * 0.22f, cy + s * 0.12f}};
            draw_polyline(pts, 3, col, width, true);
        } else {
            PointF pts[3] = {{cx - s * 0.22f, cy - s * 0.12f}, {cx, cy + s * 0.12f}, {cx + s * 0.22f, cy - s * 0.12f}};
            draw_polyline(pts, 3, col, width, true);
        }
    } else if (name == "back") {
        draw_line({cx + s * 0.22f, cy}, {cx - s * 0.18f, cy}, col, width, true);
        PointF pts[3] = {{cx + s * 0.02f, cy - s * 0.18f}, {cx - s * 0.18f, cy}, {cx + s * 0.02f, cy + s * 0.18f}};
        draw_polyline(pts, 3, col, width, true);
    } else if (name == "stop") {
        fill_rounded({cx - s * 0.20f, cy - s * 0.20f, s * 0.40f, s * 0.40f}, 1.6f, col);
    }
}

void Gfx::draw_glass_pill(const RectF& r, float radius, bool active, bool hover,
                          float alpha, bool dimmed) {
    alpha = clampf(alpha);
    if (alpha <= 0.001f || r.empty()) return;
    float pa = alpha * (dimmed ? 0.65f : 1.0f);

    if (active || hover) {
        fill_rounded(r.adjusted(-3.0f, -2.0f, 3.0f, 3.5f), radius + 2.5f,
                     ColorF::rgba(240, 196, 124, active ? 36 : 24).mul_alpha(pa));
    }
    fill_rounded({r.x + 1.0f, r.y + 2.5f, r.w, r.h}, radius,
                 ColorF::rgba(0, 0, 0, 74).mul_alpha(pa));

    if (active) {
        GradStop gs[3] = {
            {0.00f, ColorF::rgba(246, 224, 178, 246).mul_alpha(pa)},
            {0.50f, ColorF::rgba(214, 174, 110, 244).mul_alpha(pa)},
            {1.00f, ColorF::rgba(168, 126, 68, 244).mul_alpha(pa)},
        };
        fill_rounded_linear_grad(r, radius, {r.x, r.y}, {r.x, r.bottom()}, gs, 3);
        float sh = r.h * 0.46f;
        GradStop sheen[2] = {
            {0.0f, ColorF::rgba(255, 252, 242, 68).mul_alpha(pa)},
            {1.0f, ColorF::rgba(255, 252, 242, 0).mul_alpha(pa)},
        };
        fill_rounded_linear_grad({r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, sh},
                                 std::max(1.0f, radius - 1.0f),
                                 {r.x, r.y + 1.0f}, {r.x, r.y + 1.0f + sh}, sheen, 2);
        draw_rounded(r, radius, ColorF::rgba(255, 242, 210, 232).mul_alpha(pa), 1.15f);
        draw_line({r.x + radius, r.y + 1.3f}, {r.right() - radius, r.y + 1.3f},
                  ColorF::rgba(255, 252, 242, 125).mul_alpha(pa), 1.0f);
    } else {
        GradStop gs[3] = {
            {0.00f, ColorF::rgba(hover ? 66 : 46, hover ? 50 : 35, hover ? 37 : 26, hover ? 204 : 176).mul_alpha(pa)},
            {0.48f, ColorF::rgba(hover ? 40 : 28, hover ? 30 : 21, hover ? 22 : 16, hover ? 216 : 192).mul_alpha(pa)},
            {1.00f, ColorF::rgba(16, 12, 9, hover ? 228 : 208).mul_alpha(pa)},
        };
        fill_rounded_linear_grad(r, radius, {r.x, r.y}, {r.x, r.bottom()}, gs, 3);
        float sh = r.h * 0.46f;
        GradStop sheen[2] = {
            {0.0f, ColorF::rgba(255, 244, 224, hover ? 38 : 24).mul_alpha(pa)},
            {1.0f, ColorF::rgba(255, 244, 224, 0).mul_alpha(pa)},
        };
        fill_rounded_linear_grad({r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, sh},
                                 std::max(1.0f, radius - 1.0f),
                                 {r.x, r.y + 1.0f}, {r.x, r.y + 1.0f + sh}, sheen, 2);
        draw_rounded(r, radius,
                     ColorF::rgba(216, 178, 118, static_cast<int>((hover ? 220 : (dimmed ? 86 : 132)) * alpha)),
                     hover ? 1.2f : 1.0f);
        draw_rounded(r.adjusted(1.1f, 1.1f, -1.1f, -1.1f), std::max(1.0f, radius - 1.1f),
                     ColorF::rgba(255, 242, 218, hover ? 36 : 18).mul_alpha(pa), 0.75f);
        draw_line({r.x + std::min(radius, r.w * 0.25f), r.y + 1.2f},
                  {r.right() - std::min(radius, r.w * 0.25f), r.y + 1.2f},
                  ColorF::rgba(255, 244, 224, hover ? 72 : 34).mul_alpha(pa), 1.0f);
    }
}

void Gfx::draw_gold_divider(float x0, float x1, float y, float alpha, float stroke_w) {
    alpha = clampf(alpha);
    if (alpha <= 0.001f || x1 <= x0 + 2.0f) return;
    GradStop gs[3] = {
        {0.0f, ColorF::rgba(216, 178, 118, 0)},
        {0.5f, ColorF::rgba(222, 186, 126, static_cast<int>(135 * alpha))},
        {1.0f, ColorF::rgba(216, 178, 118, 0)},
    };
    D2D1_GRADIENT_STOP d2stops[3];
    for (int i = 0; i < 3; ++i) d2stops[i] = {gs[i].pos, to_d2d(gs[i].color)};
    ComPtr<ID2D1GradientStopCollection> gsc;
    if (FAILED(dc_->CreateGradientStopCollection(d2stops, 3, &gsc))) return;
    ComPtr<ID2D1LinearGradientBrush> lg;
    if (FAILED(dc_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({x0, y}, {x1, y}), gsc.Get(), &lg))) return;
    dc_->DrawLine({x0, y}, {x1, y}, lg.Get(), stroke_w);
}

void Gfx::draw_corner_filigree(const RectF& r, float len, const ColorF& col, float stroke_w, float inset) {
    if (col.a <= 0.004f || r.empty()) return;
    RectF rr = r.adjusted(inset, inset, -inset, -inset);
    float l = std::min({len, rr.w * 0.35f, rr.h * 0.35f});
    // Top-left
    draw_line({rr.x, rr.y + l}, {rr.x, rr.y}, col, stroke_w, true);
    draw_line({rr.x, rr.y}, {rr.x + l, rr.y}, col, stroke_w, true);
    // Top-right
    draw_line({rr.right() - l, rr.y}, {rr.right(), rr.y}, col, stroke_w, true);
    draw_line({rr.right(), rr.y}, {rr.right(), rr.y + l}, col, stroke_w, true);
    // Bottom-left
    draw_line({rr.x, rr.bottom() - l}, {rr.x, rr.bottom()}, col, stroke_w, true);
    draw_line({rr.x, rr.bottom()}, {rr.x + l, rr.bottom()}, col, stroke_w, true);
    // Bottom-right
    draw_line({rr.right() - l, rr.bottom()}, {rr.right(), rr.bottom()}, col, stroke_w, true);
    draw_line({rr.right(), rr.bottom()}, {rr.right(), rr.bottom() - l}, col, stroke_w, true);
}

void Gfx::draw_paper_button(const RectF& rect, const std::string& label, const FontKey& fk,
                            bool hover, bool primary, bool enabled, const std::string& icon) {
    float alpha = enabled ? 1.0f : (90.0f / 255.0f);
    RectF shadow = {rect.x + 1.5f, rect.y + 3.5f, rect.w, rect.h};
    fill_rounded(shadow, 6.0f, ColorF::rgba(0, 0, 0, enabled ? 105 : 32));
    RectF rr = rect.adjusted(0, (hover && enabled) ? -1.5f : 0.0f, 0, (hover && enabled) ? -1.5f : 0.0f);
    draw_glass_pill(rr, 6.0f, primary, hover && enabled, alpha, !enabled);

    ColorF txt_col = primary
        ? ColorF::rgba(35, 24, 17, static_cast<int>(255 * alpha))
        : ColorF::rgba(244, 232, 212, static_cast<int>(255 * alpha));

    if (!icon.empty()) {
        RectF icon_r(rr.x + 14, rr.cy() - 11, 22, 22);
        draw_icon(icon, icon_r, txt_col, 1.8f);
        text_rect({rr.x + 42, rr.y, rr.w - 50, rr.h}, label, fk, txt_col, TextAlign::Left, ElideMode::None);
    } else {
        text_rect(rr, label, fk, txt_col, TextAlign::Center, ElideMode::None);
    }
}

void Gfx::draw_icon_button(const RectF& rect, const std::string& icon, bool hover,
                           const ColorF& col, float diameter, bool active) {
    RectF r = rect;
    if (diameter > 0.0f) {
        r = {rect.cx() - diameter * 0.5f, rect.cy() - diameter * 0.5f, diameter, diameter};
    }
    float rx = r.w * 0.5f;
    float ry = r.h * 0.5f;
    PointF c = r.center();
    float a = col.a;

    // Ambient elevation shadow & hover aura
    fill_ellipse({c.x, c.y + 2.5f}, rx + 0.5f, ry + 0.5f, ColorF::rgba(0, 0, 0, static_cast<int>(92 * a)));
    if (hover || active) {
        fill_ellipse(c, rx + 6.0f, ry + 6.0f, ColorF::rgba(238, 194, 124, static_cast<int>(30 * a)));
    }

    // Translucent smoked-glass convex lens body
    GradStop stops[3] = {
        {0.00f, ColorF::rgba(hover ? 72 : 50, hover ? 54 : 38, hover ? 40 : 28, static_cast<int>((hover ? 212 : 172) * a))},
        {0.62f, ColorF::rgba(hover ? 38 : 26, hover ? 29 : 20, hover ? 22 : 15, static_cast<int>((hover ? 224 : 196) * a))},
        {1.00f, ColorF::rgba(14, 10, 8, static_cast<int>((hover ? 236 : 216) * a))},
    };
    fill_radial_grad_ellipse(c, rx, ry, stops, 3);

    // Upper dome glass specular sheen
    GradStop sheen[2] = {
        {0.0f, ColorF::rgba(255, 246, 226, static_cast<int>((hover ? 64 : 40) * a))},
        {1.0f, ColorF::rgba(255, 246, 226, 0)},
    };
    fill_radial_grad_ellipse({c.x, c.y - ry * 0.42f}, rx * 0.72f, ry * 0.42f, sheen, 2);

    // Outer gilt ring + top/bottom glass rim arcs
    draw_ellipse(c, rx, ry, ColorF::rgba(220, 182, 120, static_cast<int>(((hover || active) ? 232 : 124) * a)),
                 (hover || active) ? 1.35f : 1.05f);
    draw_ellipse(c, rx - 1.5f, ry - 1.5f,
                 ColorF::rgba(255, 242, 218, static_cast<int>(((hover || active) ? 54 : 22) * a)), 0.85f);
    draw_arc(c, rx - 2.0f, ry - 2.0f, 215.0f, 110.0f,
             ColorF::rgba(255, 248, 232, static_cast<int>((hover ? 105 : 64) * a)), 1.15f);
    draw_arc(c, rx - 2.0f, ry - 2.0f, 45.0f, 90.0f,
             ColorF::rgba(218, 178, 116, static_cast<int>((hover ? 64 : 32) * a)), 0.9f);

    RectF ir = r.adjusted(r.w * 0.235f, r.h * 0.235f, -r.w * 0.235f, -r.h * 0.235f);
    draw_icon(icon, ir, col, 1.95f);
}

float Gfx::draw_keycap(float x, float y, const std::string& label, const FontKey& fk, float h) {
    float tw = text_width(label, fk);
    float w = tw + 20.0f;
    RectF r(x, y, w, h);
    fill_rounded({x + 0.8f, y + 2.5f, w, h}, 5.0f, ColorF::rgba(0, 0, 0, 110));

    // Smoked warm glass keycap with top ivory-amber sheen
    GradStop stops[2] = {
        {0.0f, ColorF::rgba(58, 44, 33, 224)},
        {1.0f, ColorF::rgba(24, 18, 14, 238)},
    };
    fill_rounded_linear_grad(r, 5.0f, {r.x, r.y}, {r.x, r.bottom()}, stops, 2);
    float sh = h * 0.45f;
    GradStop sheen[2] = {
        {0.0f, ColorF::rgba(255, 244, 224, 34)},
        {1.0f, ColorF::rgba(255, 244, 224, 0)},
    };
    fill_rounded_linear_grad({r.x + 1.0f, r.y + 1.0f, w - 2.0f, sh}, 4.0f,
                             {r.x, r.y + 1.0f}, {r.x, r.y + 1.0f + sh}, sheen, 2);
    draw_rounded(r, 5.0f, ColorF::rgba(210, 172, 114, 155), 1.0f);
    draw_line({r.x + 4.0f, r.y + 1.1f}, {r.right() - 4.0f, r.y + 1.1f}, ColorF::rgba(255, 244, 224, 58), 1.0f);
    text_rect(r, label, fk, ColorF::rgba(242, 228, 206, 248), TextAlign::Center, ElideMode::None);
    return w;
}

} // namespace np
