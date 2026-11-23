#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using Microsoft::WRL::ComPtr;

namespace np {

struct PointF {
    float x = 0.0f;
    float y = 0.0f;
    PointF() = default;
    PointF(float x_, float y_) : x(x_), y(y_) {}
    PointF operator+(const PointF& o) const { return {x + o.x, y + o.y}; }
    PointF operator-(const PointF& o) const { return {x - o.x, y - o.y}; }
    PointF operator*(float s) const { return {x * s, y * s}; }
};

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    RectF() = default;
    RectF(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    float left() const { return x; }
    float top() const { return y; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    PointF center() const { return {cx(), cy()}; }
    bool empty() const { return w <= 0.0f || h <= 0.0f; }

    bool contains(PointF p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }

    RectF adjusted(float dx1, float dy1, float dx2, float dy2) const {
        return {x + dx1, y + dy1, w - dx1 + dx2, h - dy1 + dy2};
    }

    RectF translated(float dx, float dy) const {
        return {x + dx, y + dy, w, h};
    }
};

struct ColorF {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    ColorF() = default;
    ColorF(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    static ColorF rgba(int r, int g, int b, int a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }

    static ColorF hex(uint32_t rgb, float alpha = 1.0f) {
        return {
            ((rgb >> 16) & 0xFF) / 255.0f,
            ((rgb >> 8) & 0xFF) / 255.0f,
            (rgb & 0xFF) / 255.0f,
            alpha
        };
    }

    ColorF with_alpha(float alpha) const { return {r, g, b, alpha}; }
    ColorF mul_alpha(float factor) const { return {r, g, b, a * factor}; }
};

inline float clampf(float v, float lo = 0.0f, float hi = 1.0f) {
    return std::max(lo, std::min(hi, v));
}

inline double clampd(double v, double lo = 0.0, double hi = 1.0) {
    return std::max(lo, std::min(hi, v));
}

inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

inline ColorF mix_color(const ColorF& a, const ColorF& b, float t) {
    t = clampf(t);
    return {
        lerpf(a.r, b.r, t),
        lerpf(a.g, b.g, t),
        lerpf(a.b, b.b, t),
        lerpf(a.a, b.a, t),
    };
}

inline float ease_out(float t) {
    t = clampf(t);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

inline float ease_in_out(float t) {
    t = clampf(t);
    return t * t * (3.0f - 2.0f * t);
}

inline std::wstring to_wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

inline std::string to_utf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}

inline double now_sec() {
    static LARGE_INTEGER freq = []() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}

} // namespace np
