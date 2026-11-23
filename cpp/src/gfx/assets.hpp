#pragma once

#include "gfx.hpp"
#include "../core/models.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace np {

class WebpAnimator {
public:
    WebpAnimator() = default;
    ~WebpAnimator();

    bool start(const std::wstring& path, Gfx& gfx);
    void stop();
    void set_paused(bool paused);
    ID2D1Bitmap1* current_bitmap(Gfx& gfx, double dt);
    int width() const { return w_; }
    int height() const { return h_; }
    bool valid() const { return valid_; }

private:
    struct DecodedFrame {
        uint32_t index = 0;
        std::vector<uint8_t> bgra;
    };

    std::wstring path_;
    bool valid_ = false;
    int w_ = 0;
    int h_ = 0;
    uint32_t frame_count_ = 0;
    double frame_duration_ = 1.0 / 60.0;
    double accum_ = 0.0;
    uint32_t current_idx_ = 0;
    uint32_t uploaded_idx_ = UINT32_MAX;

    std::atomic<bool> stop_flag_{false};
    std::atomic<bool> paused_{false};
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<DecodedFrame> queue_;
    std::thread worker_;
    ComPtr<ID2D1Bitmap1> gpu_bmp_;

    void worker_loop();
};

class Assets {
public:
    explicit Assets(Gfx& gfx) : gfx_(gfx) {}
    ~Assets() = default;

    ID2D1Bitmap1* ui(const std::string& name, int w = 0, int h = 0, const std::string& mode = "contain");
    ID2D1Bitmap1* logo(int w = 540);
    ID2D1Bitmap1* cover(const Track* track, int size = 472);
    ID2D1Bitmap1* thumb(const GalleryItem* item, int w = 640, int h = 360);
    ID2D1Bitmap1* full(const std::string& relpath, int max_size = 3840);
    ID2D1Bitmap1* char_static(const std::string& name);
    ID2D1Bitmap1* blur_for(const Track* track);
    ID2D1Bitmap1* act_cg(const Track* track, int max_w = 1920);

    void preload_async(const std::wstring& path, int max_size);

private:
    Gfx& gfx_;

    struct LruCache {
        size_t max_items = 16;
        std::list<std::string> order;
        std::unordered_map<std::string, std::pair<ComPtr<ID2D1Bitmap1>, std::list<std::string>::iterator>> map;

        ID2D1Bitmap1* get(const std::string& key);
        void put(const std::string& key, ComPtr<ID2D1Bitmap1> bmp);
    };

    std::unordered_map<std::string, ComPtr<ID2D1Bitmap1>> ui_cache_;
    std::unordered_map<std::string, ComPtr<ID2D1Bitmap1>> char_cache_;
    std::unordered_map<std::string, ComPtr<ID2D1Bitmap1>> blur_cache_;
    LruCache cover_cache_{24};
    LruCache thumb_cache_{64};
    LruCache full_cache_{8};

    ComPtr<ID2D1Bitmap1> load_bitmap(const std::wstring& path, int target_w = 0, int target_h = 0,
                                     const std::string& mode = "none", int max_dim = 0);
};

} // namespace np
