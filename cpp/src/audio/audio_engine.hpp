#pragma once

#include "../core/models.hpp"
#include "../core/zip_npy.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace np {

struct AudioDeviceInfo {
    std::wstring id;
    std::string name;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init(int device_index = -1);
    void shutdown();

    void update(double now_sec);

    void play(const Track* track, int start_ms = 0, bool preview = false,
              int preview_len_ms = 20000, double fade_in = 0.0);
    void play_preview(const Track* track, int start_ms = 0, int preview_len_ms = 20000,
                      double fade_over = 0.55);
    void stop_preview(bool fade = true, bool resume = true);
    bool cancel_handover(bool restore = true);
    void suspend_full();
    void drop_suspended();
    void set_preview_remaining(int ms);

    void pause();
    void toggle();
    void stop();
    void seek(int ms);

    void set_volume(float v);
    float volume() const { return base_volume_; }
    void set_muted(bool m);
    bool muted() const { return muted_.load(); }

    void play_ui(const std::string& kind);

    void refresh_devices();
    std::vector<AudioDeviceInfo> output_devices() const;
    void set_output_device(int index);
    int current_device_index() const { return device_index_; }

    const Track* track() const { return track_; }
    void set_track_ptr_only(const Track* t) { track_ = t; }
    bool playing() const { return playing_.load(); }
    bool previewing() const { return previewing_; }
    bool preview_active() const { return previewing_ || preview_pending_; }
    bool full_mode() const { return full_mode_; }
    int position_ms() const;
    int duration_ms() const;

    void spectrum_at(int ms, float out_bars[48], float& out_bass, float& out_energy) const;

    std::function<void()> on_finished;
    std::function<void()> on_preview_ended;

private:
    struct PcmClip {
        uint32_t sr = 48000;
        uint32_t ch = 2;
        std::vector<float> samples;
    };

    struct Handover {
        const Track* track = nullptr;
        int start_ms = 0;
        int preview_len_ms = 20000;
    };

    struct Suspended {
        const Track* track = nullptr;
        int pos_ms = 0;
        bool was_playing = false;
    };

    const Track* track_ = nullptr;
    bool previewing_ = false;
    bool full_mode_ = false;
    int preview_stop_ms_ = 0;
    bool preview_pending_ = false;
    bool resume_on_fade_ = true;
    double preview_fade_in_ = 1.0;
    double preview_fade_out_ = 1.2;

    bool has_handover_ = false;
    Handover handover_{};
    bool has_suspended_ = false;
    Suspended suspended_{};

    float base_volume_ = 0.78f;
    std::atomic<bool> muted_{false};

    // Smooth fade state (evaluated on main loop + smoothed per sample in audio thread)
    double fade_val_ = 1.0;
    double fade_from_ = 1.0;
    double fade_to_ = 1.0;
    double fade_t0_ = 0.0;
    double fade_dur_ = 1.0;
    bool fade_active_ = false;
    std::function<void()> fade_done_;

    std::atomic<float> target_gain_{0.78f};
    std::atomic<bool> playing_{false};

    // Spectrum
    mutable std::mutex spec_mtx_;
    std::string spec_id_;
    SpectrumData spec_data_;
    bool spec_valid_ = false;
    void load_spectrum(const Track* t);

    // UI SFX
    PcmClip sfx_move_;
    PcmClip sfx_select_;
    struct ActiveSfx {
        const PcmClip* clip = nullptr;
        double frame_pos = 0.0;
        float gain = 0.32f;
    };
    std::mutex sfx_mtx_;
    std::vector<ActiveSfx> active_sfx_;
    void load_wav(const std::wstring& path, PcmClip& out);

    // WASAPI + MF Decoder
    std::vector<AudioDeviceInfo> devices_;
    int device_index_ = -1;

    struct StreamState;
    std::unique_ptr<StreamState> stream_;

    bool start_wasapi_thread();
    void stop_wasapi_thread();
    void wasapi_loop();
    void decoder_loop(uint64_t generation, std::wstring path, int start_ms);

    void apply_volume();
    void start_fade(double target, double duration, std::function<void()> done = nullptr);
    void emit_preview_end();
    void finish_preview_handover();
    void resume_suspended();
    void restore_paused(const Track* t, int pos_ms);
    void open_source(const Track* t, int start_ms);
};

} // namespace np
