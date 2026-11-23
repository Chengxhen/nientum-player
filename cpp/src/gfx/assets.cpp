#include "assets.hpp"
#include "../core/paths.hpp"

#include <algorithm>
#include <cmath>

namespace np {

// -----------------------------------------------------------------------------
// WebpAnimator (background WIC decoder + ring buffer -> single GPU bitmap)
// -----------------------------------------------------------------------------
WebpAnimator::~WebpAnimator() {
    stop();
}

bool WebpAnimator::start(const std::wstring& path, Gfx& gfx) {
    stop();
    path_ = path;
    valid_ = false;
    frame_count_ = 0;
    current_idx_ = 0;
    uploaded_idx_ = UINT32_MAX;
    accum_ = 0.0;

    if (!paths::file_exists(path_)) return false;

    // Quick probe on main thread for dimensions & frame count
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(gfx.wic()->CreateDecoderFromFilename(path_.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnDemand, &dec))) {
        return false;
    }
    if (FAILED(dec->GetFrameCount(&frame_count_)) || frame_count_ < 2) return false;
    ComPtr<IWICBitmapFrameDecode> f0;
    if (FAILED(dec->GetFrame(0, &f0))) return false;
    UINT uw = 0, uh = 0;
    f0->GetSize(&uw, &uh);
    w_ = static_cast<int>(uw);
    h_ = static_cast<int>(uh);
    if (w_ <= 0 || h_ <= 0) return false;

    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(gfx.dc()->CreateBitmap(D2D1::SizeU(uw, uh), nullptr, 0, &bp, &gpu_bmp_))) {
        return false;
    }

    // Read ANMF frame duration if available (default 16ms = 60Hz)
    frame_duration_ = 0.016;
    HANDLE hf = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        uint8_t hdr[4096]{};
        DWORD rd = 0;
        if (ReadFile(hf, hdr, sizeof(hdr), &rd, nullptr) && rd > 32) {
            for (DWORD i = 0; i + 24 < rd; ++i) {
                if (hdr[i] == 'A' && hdr[i + 1] == 'N' && hdr[i + 2] == 'M' && hdr[i + 3] == 'F') {
                    uint32_t ms = static_cast<uint32_t>(hdr[i + 20]) |
                                  (static_cast<uint32_t>(hdr[i + 21]) << 8) |
                                  (static_cast<uint32_t>(hdr[i + 22]) << 16);
                    if (ms >= 8 && ms <= 500) frame_duration_ = ms / 1000.0;
                    break;
                }
            }
        }
        CloseHandle(hf);
    }

    valid_ = true;
    stop_flag_.store(false);
    paused_.store(false);
    worker_ = std::thread([this]() { worker_loop(); });
    return true;
}

void WebpAnimator::stop() {
    stop_flag_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lk(mtx_);
    queue_.clear();
    gpu_bmp_.Reset();
    valid_ = false;
}

void WebpAnimator::set_paused(bool paused) {
    paused_.store(paused);
    if (!paused) cv_.notify_all();
}

void WebpAnimator::worker_loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        CoUninitialize();
        return;
    }
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path_.c_str(), nullptr, GENERIC_READ,
                                              WICDecodeMetadataCacheOnDemand, &dec))) {
        CoUninitialize();
        return;
    }

    uint32_t next_idx = 0;
    const size_t stride = static_cast<size_t>(w_) * 4;
    const size_t bytes = stride * static_cast<size_t>(h_);
    const size_t max_queue = (bytes > 16u * 1024u * 1024u) ? 3u : 8u;

    while (!stop_flag_.load()) {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(16), [&]() {
                return stop_flag_.load() || (!paused_.load() && queue_.size() < max_queue);
            });
            if (stop_flag_.load()) break;
            if (paused_.load() || queue_.size() >= max_queue) continue;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        if (SUCCEEDED(dec->GetFrame(next_idx, &frame))) {
            ComPtr<IWICFormatConverter> conv;
            if (SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
                SUCCEEDED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
                DecodedFrame df;
                df.index = next_idx;
                df.bgra.resize(bytes);
                if (SUCCEEDED(conv->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(bytes), df.bgra.data()))) {
                    std::lock_guard<std::mutex> lk(mtx_);
                    queue_.push_back(std::move(df));
                }
            }
        }
        next_idx = (next_idx + 1) % std::max<uint32_t>(1, frame_count_);
    }

    CoUninitialize();
}

ID2D1Bitmap1* WebpAnimator::current_bitmap(Gfx& gfx, double dt) {
    if (!valid_ || !gpu_bmp_) return nullptr;
    accum_ = std::min(accum_ + dt, frame_duration_ * 2.5);
    int steps = 0;
    while (accum_ >= frame_duration_) {
        accum_ -= frame_duration_;
        ++steps;
    }
    if (steps > 0 || uploaded_idx_ == UINT32_MAX) {
        DecodedFrame chosen;
        bool got = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            int pop_cnt = (uploaded_idx_ == UINT32_MAX) ? 1 : std::min(steps, static_cast<int>(queue_.size()));
            for (int i = 0; i < pop_cnt; ++i) {
                if (!queue_.empty()) {
                    chosen = std::move(queue_.front());
                    queue_.pop_front();
                    got = true;
                }
            }
        }
        if (got) {
            D2D1_RECT_U rc = D2D1::RectU(0, 0, static_cast<UINT32>(w_), static_cast<UINT32>(h_));
            gpu_bmp_->CopyFromMemory(&rc, chosen.bgra.data(), static_cast<UINT32>(w_ * 4));
            uploaded_idx_ = chosen.index;
            cv_.notify_one();
        }
    }
    return (uploaded_idx_ != UINT32_MAX) ? gpu_bmp_.Get() : nullptr;
}

// -----------------------------------------------------------------------------
// LRU Cache
// -----------------------------------------------------------------------------
ID2D1Bitmap1* Assets::LruCache::get(const std::string& key) {
    auto it = map.find(key);
    if (it == map.end()) return nullptr;
    order.splice(order.begin(), order, it->second.second);
    return it->second.first.Get();
}

void Assets::LruCache::put(const std::string& key, ComPtr<ID2D1Bitmap1> bmp) {
    auto it = map.find(key);
    if (it != map.end()) {
        it->second.first = std::move(bmp);
        order.splice(order.begin(), order, it->second.second);
        return;
    }
    order.push_front(key);
    map[key] = {std::move(bmp), order.begin()};
    while (map.size() > max_items && !order.empty()) {
        std::string tail = order.back();
        order.pop_back();
        map.erase(tail);
    }
}

// -----------------------------------------------------------------------------
// Assets WIC -> D2D1 Bitmap Loader
// -----------------------------------------------------------------------------
ComPtr<ID2D1Bitmap1> Assets::load_bitmap(const std::wstring& path, int target_w, int target_h,
                                         const std::string& mode, int max_dim) {
    if (!paths::file_exists(path)) return nullptr;
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(gfx_.wic()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &dec))) {
        return nullptr;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return nullptr;

    UINT ow = 0, oh = 0;
    if (FAILED(frame->GetSize(&ow, &oh)) || ow == 0 || oh == 0) return nullptr;

    IWICBitmapSource* src = frame.Get();
    ComPtr<IWICBitmapScaler> scaler;

    UINT dw = ow, dh = oh;
    if (max_dim > 0 && static_cast<int>(std::max(ow, oh)) > max_dim) {
        double s = static_cast<double>(max_dim) / static_cast<double>(std::max(ow, oh));
        dw = std::max<UINT>(1, static_cast<UINT>(std::lround(ow * s)));
        dh = std::max<UINT>(1, static_cast<UINT>(std::lround(oh * s)));
    } else if (target_w > 0 && target_h > 0 && mode != "none") {
        if (mode == "stretch") {
            dw = static_cast<UINT>(target_w);
            dh = static_cast<UINT>(target_h);
        } else {
            double sx = static_cast<double>(target_w) / ow;
            double sy = static_cast<double>(target_h) / oh;
            double s = (mode == "cover") ? std::max(sx, sy) : std::min(sx, sy);
            dw = std::max<UINT>(1, static_cast<UINT>(std::lround(ow * s)));
            dh = std::max<UINT>(1, static_cast<UINT>(std::lround(oh * s)));
        }
    }

    if (dw != ow || dh != oh) {
        if (SUCCEEDED(gfx_.wic()->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(src, dw, dh, WICBitmapInterpolationModeHighQualityCubic))) {
            src = scaler.Get();
        }
    }

    ComPtr<IWICFormatConverter> conv;
    if (FAILED(gfx_.wic()->CreateFormatConverter(&conv))) return nullptr;
    if (FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return nullptr;
    }

    // Center-crop for exact cover dimensions if requested
    IWICBitmapSource* final_src = conv.Get();
    ComPtr<IWICBitmapClipper> clipper;
    UINT fw = dw, fh = dh;
    if (target_w > 0 && target_h > 0 && mode == "cover" &&
        (static_cast<int>(dw) > target_w || static_cast<int>(dh) > target_h)) {
        UINT cx = (dw > static_cast<UINT>(target_w)) ? (dw - static_cast<UINT>(target_w)) / 2 : 0;
        UINT cy = (dh > static_cast<UINT>(target_h)) ? (dh - static_cast<UINT>(target_h)) / 2 : 0;
        UINT cw = std::min(dw, static_cast<UINT>(target_w));
        UINT ch = std::min(dh, static_cast<UINT>(target_h));
        WICRect rc{static_cast<INT>(cx), static_cast<INT>(cy), static_cast<INT>(cw), static_cast<INT>(ch)};
        if (SUCCEEDED(gfx_.wic()->CreateBitmapClipper(&clipper)) && SUCCEEDED(clipper->Initialize(conv.Get(), &rc))) {
            final_src = clipper.Get();
            fw = cw;
            fh = ch;
        }
    }

    const UINT stride = fw * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * fh);
    if (FAILED(final_src->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()))) {
        return nullptr;
    }

    ComPtr<ID2D1Bitmap1> bmp;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    gfx_.dc()->CreateBitmap(D2D1::SizeU(fw, fh), pixels.data(), stride, &bp, &bmp);
    return bmp;
}

ID2D1Bitmap1* Assets::ui(const std::string& name, int w, int h, const std::string& mode) {
    std::string key = name + ":" + std::to_string(w) + "x" + std::to_string(h) + ":" + mode;
    auto it = ui_cache_.find(key);
    if (it != ui_cache_.end()) return it->second.Get();
    ComPtr<ID2D1Bitmap1> bmp = load_bitmap(paths::data_path_utf8("ui/" + name), w, h, mode);
    ui_cache_[key] = bmp;
    return bmp.Get();
}

ID2D1Bitmap1* Assets::logo(int w) {
    return ui("PZImgNIENTUM_logo.png", w, static_cast<int>(w * 0.62f), "contain");
}

ID2D1Bitmap1* Assets::cover(const Track* track, int size) {
    if (!track) return nullptr;
    std::string key = track->id + ":" + std::to_string(size);
    if (auto* hit = cover_cache_.get(key)) return hit;
    ComPtr<ID2D1Bitmap1> bmp = load_bitmap(track->cover_path(), size, size, "cover");
    cover_cache_.put(key, bmp);
    return bmp.Get();
}

ID2D1Bitmap1* Assets::thumb(const GalleryItem* item, int w, int h) {
    if (!item) return nullptr;
    std::string key = item->id + ":" + std::to_string(w) + "x" + std::to_string(h);
    if (auto* hit = thumb_cache_.get(key)) return hit;
    ComPtr<ID2D1Bitmap1> bmp = load_bitmap(item->thumb_path(), w, h, (h <= 400) ? "cover" : "contain");
    thumb_cache_.put(key, bmp);
    return bmp.Get();
}

ID2D1Bitmap1* Assets::full(const std::string& relpath, int max_size) {
    std::wstring p = paths::media_path_utf8(relpath);
    std::string key = to_utf8(p) + ":" + std::to_string(max_size);
    if (auto* hit = full_cache_.get(key)) return hit;
    ComPtr<ID2D1Bitmap1> bmp = load_bitmap(p, 0, 0, "none", max_size);
    full_cache_.put(key, bmp);
    return bmp.Get();
}

ID2D1Bitmap1* Assets::char_static(const std::string& name) {
    auto it = char_cache_.find(name);
    if (it != char_cache_.end()) return it->second.Get();
    ComPtr<ID2D1Bitmap1> bmp = load_bitmap(paths::data_path_utf8("characters/" + name + "_0.png"), 0, 0, "none");
    char_cache_[name] = bmp;
    return bmp.Get();
}

ID2D1Bitmap1* Assets::blur_for(const Track* track) {
    if (!track) return nullptr;
    std::string key = track->act + "_" + track->act_zh;
    auto it = blur_cache_.find(key);
    if (it != blur_cache_.end()) return it->second.Get();
    ComPtr<ID2D1Bitmap1> bmp = load_bitmap(paths::data_path_utf8("cache/blur/" + key + ".jpg"), 1920, 1080, "cover");
    blur_cache_[key] = bmp;
    return bmp.Get();
}

ID2D1Bitmap1* Assets::act_cg(const Track* track, int max_w) {
    if (!track) return nullptr;
    std::string key = "cg:" + track->act + ":" + std::to_string(max_w);
    if (auto* hit = full_cache_.get(key)) return hit;
    ComPtr<ID2D1Bitmap1> bmp = load_bitmap(paths::media_path_utf8(track->cg_rel()), 0, 0, "none", max_w);
    full_cache_.put(key, bmp);
    return bmp.Get();
}

} // namespace np
