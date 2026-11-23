#include "audio_engine.hpp"
#include "../core/paths.hpp"
#include "../core/types.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <avrt.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <propsys.h>

namespace {
const PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14
};
}

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>

#pragma comment(lib, "avrt.lib")

namespace np {

struct AudioEngine::StreamState {
    std::atomic<bool> audio_stop{false};
    std::thread audio_thread;
    std::atomic<bool> reinit_device{false};

    std::mutex dec_mtx;
    std::condition_variable dec_cv;
    std::thread dec_thread;
    std::atomic<uint64_t> generation{0};
    std::atomic<bool> dec_cancel{false};

    // Ring / chunk queue of float stereo PCM at source sample rate
    std::mutex pcm_mtx;
    struct Chunk {
        std::vector<float> samples; // interleaved stereo
        size_t frames = 0;
    };
    std::deque<Chunk> chunks;
    size_t chunk_frame_offset = 0;
    double resamp_pos = 0.0;
    float last_l = 0.0f, last_r = 0.0f;
    bool have_prev = false;

    std::atomic<uint32_t> src_sr{48000};
    std::atomic<uint32_t> src_ch{2};
    std::atomic<uint64_t> played_src_frames{0};
    std::atomic<uint64_t> base_src_frames{0};
    std::atomic<bool> decode_eof{false};
    std::atomic<bool> playback_finished{false};
    std::atomic<bool> error_occurred{false};
    std::atomic<int> duration_ms{0};
    std::atomic<uint64_t> queued_frames{0};

    std::atomic<uint32_t> dev_sr{48000};
    std::atomic<uint32_t> dev_ch{2};
    float current_gain = 0.78f;

    void clear_pcm(int start_ms, uint32_t sr) {
        std::lock_guard<std::mutex> lk(pcm_mtx);
        chunks.clear();
        chunk_frame_offset = 0;
        resamp_pos = 0.0;
        have_prev = false;
        last_l = last_r = 0.0f;
        queued_frames.store(0);
        uint64_t bf = static_cast<uint64_t>(std::max(0, start_ms)) * sr / 1000ULL;
        base_src_frames.store(bf);
        played_src_frames.store(bf);
        decode_eof.store(false);
        playback_finished.store(false);
        error_occurred.store(false);
    }
};

AudioEngine::AudioEngine() : stream_(std::make_unique<StreamState>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init(int device_index) {
    MFStartup(MF_VERSION);
    refresh_devices();
    if (device_index >= 0 && device_index < static_cast<int>(devices_.size())) {
        device_index_ = device_index;
    } else {
        device_index_ = -1;
    }
    load_wav(paths::data_path(L"ui\\menu_move.wav"), sfx_move_);
    load_wav(paths::data_path(L"ui\\menu_select.wav"), sfx_select_);
    apply_volume();
    return start_wasapi_thread();
}

void AudioEngine::shutdown() {
    stop();
    stop_wasapi_thread();
    MFShutdown();
}

void AudioEngine::load_wav(const std::wstring& path, PcmClip& out) {
    out = {};
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 44 || sz.QuadPart > 32LL * 1024 * 1024) {
        CloseHandle(h);
        return;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(sz.QuadPart));
    DWORD rd = 0;
    BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &rd, nullptr);
    CloseHandle(h);
    if (!ok || rd < 44 || std::memcmp(buf.data(), "RIFF", 4) != 0) return;

    uint16_t fmt_tag = 1, ch = 2, bits = 16;
    uint32_t sr = 44100;
    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        uint32_t csz = 0;
        std::memcpy(&csz, buf.data() + pos + 4, 4);
        if (pos + 8 + csz > buf.size()) break;
        if (std::memcmp(buf.data() + pos, "fmt ", 4) == 0 && csz >= 16) {
            std::memcpy(&fmt_tag, buf.data() + pos + 8, 2);
            std::memcpy(&ch, buf.data() + pos + 10, 2);
            std::memcpy(&sr, buf.data() + pos + 12, 4);
            std::memcpy(&bits, buf.data() + pos + 22, 2);
        } else if (std::memcmp(buf.data() + pos, "data", 4) == 0) {
            const uint8_t* d = buf.data() + pos + 8;
            out.sr = sr;
            out.ch = std::max<uint16_t>(1, ch);
            if (fmt_tag == 1 && bits == 16) {
                size_t n = csz / 2;
                out.samples.resize(n);
                const int16_t* s16 = reinterpret_cast<const int16_t*>(d);
                for (size_t i = 0; i < n; ++i) out.samples[i] = s16[i] / 32768.0f;
            } else if (fmt_tag == 3 && bits == 32) {
                size_t n = csz / 4;
                out.samples.resize(n);
                std::memcpy(out.samples.data(), d, n * 4);
            }
            break;
        }
        pos += 8 + csz + (csz & 1);
    }
}

void AudioEngine::refresh_devices() {
    devices_.clear();
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return;
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
        return;
    UINT count = 0;
    col->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(col->Item(i, &dev))) continue;
        LPWSTR id_str = nullptr;
        if (FAILED(dev->GetId(&id_str)) || !id_str) continue;
        AudioDeviceInfo info;
        info.id = id_str;
        CoTaskMemFree(id_str);

        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(kPkeyDeviceFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                info.name = to_utf8(pv.pwszVal);
            }
            PropVariantClear(&pv);
        }
        if (info.name.empty()) info.name = "Audio Output";
        devices_.push_back(std::move(info));
    }
}

std::vector<AudioDeviceInfo> AudioEngine::output_devices() const {
    return devices_;
}

void AudioEngine::set_output_device(int index) {
    if (index >= 0 && index < static_cast<int>(devices_.size())) {
        device_index_ = index;
    } else {
        device_index_ = -1;
    }
    stream_->reinit_device.store(true);
}

bool AudioEngine::start_wasapi_thread() {
    stream_->audio_stop.store(false);
    stream_->audio_thread = std::thread([this]() { wasapi_loop(); });
    return true;
}

void AudioEngine::stop_wasapi_thread() {
    if (stream_->dec_thread.joinable()) {
        stream_->dec_cancel.store(true);
        stream_->dec_cv.notify_all();
        stream_->dec_thread.join();
    }
    stream_->audio_stop.store(true);
    if (stream_->audio_thread.joinable()) {
        stream_->audio_thread.join();
    }
}

void AudioEngine::wasapi_loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task_idx = 0;
    HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_idx);

    while (!stream_->audio_stop.load()) {
        stream_->reinit_device.store(false);
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
            Sleep(100);
            continue;
        }
        ComPtr<IMMDevice> device;
        HRESULT hr = E_FAIL;
        if (device_index_ >= 0 && device_index_ < static_cast<int>(devices_.size())) {
            hr = enumerator->GetDevice(devices_[device_index_].id.c_str(), &device);
        }
        if (FAILED(hr) || !device) {
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        }
        if (FAILED(hr) || !device) {
            Sleep(200);
            continue;
        }

        ComPtr<IAudioClient> client;
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())))) {
            Sleep(200);
            continue;
        }
        WAVEFORMATEX* pwfx = nullptr;
        if (FAILED(client->GetMixFormat(&pwfx)) || !pwfx) {
            Sleep(200);
            continue;
        }

        uint32_t dev_sr = pwfx->nSamplesPerSec;
        uint32_t dev_ch = pwfx->nChannels;
        bool is_float = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
        bool is_16 = (pwfx->wFormatTag == WAVE_FORMAT_PCM && pwfx->wBitsPerSample == 16);
        if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE && pwfx->cbSize >= 22) {
            auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
            if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) is_float = true;
            else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) && pwfx->wBitsPerSample == 16) is_16 = true;
        }
        stream_->dev_sr.store(dev_sr);
        stream_->dev_ch.store(dev_ch);

        REFERENCE_TIME hns_buf = 400000; // 40ms
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, hns_buf, 0, pwfx, nullptr);
        bool event_mode = true;
        if (FAILED(hr)) {
            event_mode = false;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hns_buf, 0, pwfx, nullptr);
        }
        CoTaskMemFree(pwfx);
        if (FAILED(hr)) {
            Sleep(200);
            continue;
        }

        HANDLE ev = nullptr;
        if (event_mode) {
            ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            client->SetEventHandle(ev);
        }
        UINT32 buf_frames = 0;
        client->GetBufferSize(&buf_frames);
        ComPtr<IAudioRenderClient> render;
        client->GetService(IID_PPV_ARGS(&render));
        client->Start();

        std::vector<float> mix_l;
        std::vector<float> mix_r;

        while (!stream_->audio_stop.load() && !stream_->reinit_device.load()) {
            if (ev) WaitForSingleObject(ev, 25);
            else Sleep(8);

            UINT32 pad = 0;
            if (FAILED(client->GetCurrentPadding(&pad))) break;
            UINT32 avail = (buf_frames > pad) ? (buf_frames - pad) : 0;
            if (avail == 0) continue;

            BYTE* dst_ptr = nullptr;
            if (FAILED(render->GetBuffer(avail, &dst_ptr)) || !dst_ptr) break;

            mix_l.assign(avail, 0.0f);
            mix_r.assign(avail, 0.0f);

            const float tg = muted_.load() ? 0.0f : target_gain_.load();
            const bool is_play = playing_.load();

            if (is_play) {
                std::lock_guard<std::mutex> lk(stream_->pcm_mtx);
                const uint32_t s_sr = std::max<uint32_t>(1, stream_->src_sr.load());
                const double step = static_cast<double>(s_sr) / static_cast<double>(dev_sr);
                uint64_t consumed_src = 0;

                auto peek_sample = [&](size_t idx, float& l, float& r) -> bool {
                    size_t rem = idx + stream_->chunk_frame_offset;
                    for (const auto& c : stream_->chunks) {
                        if (rem < c.frames) {
                            l = c.samples[rem * 2];
                            r = c.samples[rem * 2 + 1];
                            return true;
                        }
                        rem -= c.frames;
                    }
                    return false;
                };

                auto pop_frames = [&](size_t cnt) {
                    size_t left = cnt;
                    while (left > 0 && !stream_->chunks.empty()) {
                        auto& front = stream_->chunks.front();
                        size_t avail_in_chunk = front.frames - stream_->chunk_frame_offset;
                        if (left < avail_in_chunk) {
                            stream_->chunk_frame_offset += left;
                            stream_->queued_frames.fetch_sub(left);
                            left = 0;
                        } else {
                            left -= avail_in_chunk;
                            stream_->queued_frames.fetch_sub(avail_in_chunk);
                            stream_->chunks.pop_front();
                            stream_->chunk_frame_offset = 0;
                        }
                    }
                };

                for (UINT32 i = 0; i < avail; ++i) {
                    // smooth gain per sample to eliminate clicks
                    stream_->current_gain += (tg - stream_->current_gain) * 0.004f;
                    size_t i0 = static_cast<size_t>(stream_->resamp_pos);
                    float frac = static_cast<float>(stream_->resamp_pos - static_cast<double>(i0));
                    float l0 = 0, r0 = 0, l1 = 0, r1 = 0;
                    if (!peek_sample(i0, l0, r0)) {
                        if (stream_->decode_eof.load()) {
                            stream_->playback_finished.store(true);
                        }
                        break;
                    }
                    if (!peek_sample(i0 + 1, l1, r1)) {
                        l1 = l0;
                        r1 = r0;
                    }
                    mix_l[i] = (l0 + (l1 - l0) * frac) * stream_->current_gain;
                    mix_r[i] = (r0 + (r1 - r0) * frac) * stream_->current_gain;
                    stream_->resamp_pos += step;
                }

                size_t drop = static_cast<size_t>(stream_->resamp_pos);
                if (drop > 0) {
                    size_t actual_avail = static_cast<size_t>(stream_->queued_frames.load());
                    size_t actual_drop = std::min(drop, actual_avail);
                    pop_frames(actual_drop);
                    stream_->resamp_pos -= static_cast<double>(actual_drop);
                    consumed_src = actual_drop;
                }
                if (consumed_src > 0) {
                    stream_->played_src_frames.fetch_add(consumed_src);
                    stream_->dec_cv.notify_one();
                }
                if (stream_->decode_eof.load() && stream_->chunks.empty()) {
                    stream_->playback_finished.store(true);
                }
            } else {
                stream_->current_gain += (tg - stream_->current_gain) * 0.01f;
            }

            // Mix UI sound effects cleanly
            {
                std::lock_guard<std::mutex> slk(sfx_mtx_);
                for (size_t s = 0; s < active_sfx_.size();) {
                    auto& fx = active_sfx_[s];
                    if (!fx.clip || fx.clip->samples.empty()) {
                        active_sfx_.erase(active_sfx_.begin() + s);
                        continue;
                    }
                    size_t total_f = fx.clip->samples.size() / fx.clip->ch;
                    double step = static_cast<double>(fx.clip->sr) / static_cast<double>(dev_sr);
                    bool done = false;
                    for (UINT32 i = 0; i < avail; ++i) {
                        size_t fi = static_cast<size_t>(fx.frame_pos);
                        if (fi >= total_f) { done = true; break; }
                        float l = fx.clip->samples[fi * fx.clip->ch];
                        float r = (fx.clip->ch > 1) ? fx.clip->samples[fi * fx.clip->ch + 1] : l;
                        mix_l[i] += l * fx.gain;
                        mix_r[i] += r * fx.gain;
                        fx.frame_pos += step;
                    }
                    if (done || static_cast<size_t>(fx.frame_pos) >= total_f) {
                        active_sfx_.erase(active_sfx_.begin() + s);
                    } else {
                        ++s;
                    }
                }
            }

            if (is_float) {
                float* out = reinterpret_cast<float*>(dst_ptr);
                for (UINT32 i = 0; i < avail; ++i) {
                    float l = std::clamp(mix_l[i], -1.0f, 1.0f);
                    float r = std::clamp(mix_r[i], -1.0f, 1.0f);
                    if (dev_ch == 1) {
                        out[i] = 0.5f * (l + r);
                    } else {
                        out[i * dev_ch + 0] = l;
                        out[i * dev_ch + 1] = r;
                        for (UINT32 c = 2; c < dev_ch; ++c) out[i * dev_ch + c] = 0.0f;
                    }
                }
            } else if (is_16) {
                int16_t* out = reinterpret_cast<int16_t*>(dst_ptr);
                for (UINT32 i = 0; i < avail; ++i) {
                    float l = std::clamp(mix_l[i], -1.0f, 1.0f);
                    float r = std::clamp(mix_r[i], -1.0f, 1.0f);
                    if (dev_ch == 1) {
                        out[i] = static_cast<int16_t>(0.5f * (l + r) * 32767.0f);
                    } else {
                        out[i * dev_ch + 0] = static_cast<int16_t>(l * 32767.0f);
                        out[i * dev_ch + 1] = static_cast<int16_t>(r * 32767.0f);
                        for (UINT32 c = 2; c < dev_ch; ++c) out[i * dev_ch + c] = 0;
                    }
                }
            } else {
                std::memset(dst_ptr, 0, avail * dev_ch * 4);
            }

            render->ReleaseBuffer(avail, 0);
        }

        client->Stop();
        if (ev) CloseHandle(ev);
    }

    if (avrt) AvRevertMmThreadCharacteristics(avrt);
    CoUninitialize();
}

void AudioEngine::decoder_loop(uint64_t generation, std::wstring path, int start_ms) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (FAILED(hr) || !reader) {
        if (stream_->generation.load() == generation) {
            stream_->error_occurred.store(true);
            stream_->decode_eof.store(true);
        }
        CoUninitialize();
        return;
    }

    ComPtr<IMFMediaType> out_type;
    MFCreateMediaType(&out_type);
    out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    out_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, out_type.Get());
    if (FAILED(hr)) {
        if (stream_->generation.load() == generation) {
            stream_->error_occurred.store(true);
            stream_->decode_eof.store(true);
        }
        CoUninitialize();
        return;
    }

    uint32_t sr = 48000, ch = 2;
    ComPtr<IMFMediaType> cur_type;
    if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &cur_type))) {
        cur_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
        cur_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
    }
    int64_t dur_ms = 0;
    {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv))) {
            if (pv.vt == VT_UI8) dur_ms = static_cast<int64_t>(pv.uhVal.QuadPart / 10000ULL);
            else if (pv.vt == VT_I8) dur_ms = static_cast<int64_t>(pv.hVal.QuadPart / 10000LL);
        }
        PropVariantClear(&pv);
    }

    if (stream_->generation.load() != generation) { CoUninitialize(); return; }
    stream_->src_sr.store(sr);
    stream_->src_ch.store(ch);
    if (dur_ms > 0) stream_->duration_ms.store(static_cast<int>(dur_ms));
    stream_->clear_pcm(start_ms, sr);

    // Read 1 sample before SetCurrentPosition so MF FLAC byte-stream seeks properly
    bool need_seek = (start_ms > 0);
    bool first_read = true;
    int64_t target_hns = static_cast<int64_t>(start_ms) * 10000LL;
    const size_t max_queue_frames = static_cast<size_t>(sr) * 2; // 2 seconds buffer

    while (!stream_->dec_cancel.load() && stream_->generation.load() == generation) {
        {
            std::unique_lock<std::mutex> lk(stream_->dec_mtx);
            stream_->dec_cv.wait_for(lk, std::chrono::milliseconds(15), [&]() {
                return stream_->dec_cancel.load() ||
                       stream_->generation.load() != generation ||
                       stream_->queued_frames.load() < max_queue_frames;
            });
        }
        if (stream_->dec_cancel.load() || stream_->generation.load() != generation) break;

        DWORD flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &ts, &sample);

        if (need_seek && first_read) {
            first_read = false;
            PROPVARIANT pos;
            PropVariantInit(&pos);
            pos.vt = VT_I8;
            pos.hVal.QuadPart = target_hns;
            reader->SetCurrentPosition(GUID_NULL, pos);
            need_seek = false;
            continue;
        }
        first_read = false;

        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
            stream_->decode_eof.store(true);
            break;
        }
        if (!sample) continue;

        // If seek landed slightly early, discard or align timestamp
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(&buf)) || !buf) continue;
        BYTE* ptr = nullptr;
        DWORD len = 0;
        if (FAILED(buf->Lock(&ptr, nullptr, &len)) || !ptr || len == 0) continue;

        size_t num_samples = len / sizeof(float);
        size_t frames = num_samples / std::max<uint32_t>(1, ch);
        const float* fp = reinterpret_cast<const float*>(ptr);

        StreamState::Chunk chunk;
        chunk.frames = frames;
        chunk.samples.resize(frames * 2);
        if (ch == 1) {
            for (size_t i = 0; i < frames; ++i) {
                chunk.samples[i * 2] = fp[i];
                chunk.samples[i * 2 + 1] = fp[i];
            }
        } else {
            for (size_t i = 0; i < frames; ++i) {
                chunk.samples[i * 2] = fp[i * ch + 0];
                chunk.samples[i * 2 + 1] = fp[i * ch + 1];
            }
        }
        buf->Unlock();

        if (frames > 0) {
            std::lock_guard<std::mutex> lk(stream_->pcm_mtx);
            stream_->chunks.push_back(std::move(chunk));
            stream_->queued_frames.fetch_add(frames);
        }
    }

    CoUninitialize();
}

void AudioEngine::open_source(const Track* t, int start_ms) {
    if (!t) return;
    uint64_t gen = stream_->generation.fetch_add(1) + 1;
    stream_->dec_cancel.store(true);
    stream_->dec_cv.notify_all();
    if (stream_->dec_thread.joinable()) stream_->dec_thread.join();
    stream_->dec_cancel.store(false);

    stream_->duration_ms.store(static_cast<int>(t->duration * 1000.0));
    stream_->clear_pcm(start_ms, stream_->src_sr.load());
    std::wstring path = t->abs_path();
    stream_->dec_thread = std::thread([this, gen, path, start_ms]() {
        decoder_loop(gen, path, start_ms);
    });
}

void AudioEngine::play_ui(const std::string& kind) {
    const PcmClip* clip = (kind == "select") ? &sfx_select_ : &sfx_move_;
    if (clip->samples.empty()) return;
    std::lock_guard<std::mutex> lk(sfx_mtx_);
    if (active_sfx_.size() > 8) active_sfx_.erase(active_sfx_.begin());
    active_sfx_.push_back(ActiveSfx{clip, 0.0, 0.32f});
}

void AudioEngine::load_spectrum(const Track* t) {
    if (!t) return;
    std::lock_guard<std::mutex> lk(spec_mtx_);
    if (spec_id_ == t->id) return;
    spec_id_ = t->id;
    std::wstring p = paths::data_path_utf8("cache/spectrum/" + t->id + ".npz");
    SpectrumData sd;
    if (load_npz_spectrum(p, sd)) {
        spec_data_ = std::move(sd);
        spec_valid_ = true;
    } else {
        spec_data_ = {};
        spec_valid_ = false;
    }
}

void AudioEngine::spectrum_at(int ms, float out_bars[48], float& out_bass, float& out_energy) const {
    std::lock_guard<std::mutex> lk(spec_mtx_);
    if (!spec_valid_) {
        double t = now_sec();
        double base = 0.28 + 0.18 * std::sin(t * 1.7);
        for (int i = 0; i < 48; ++i) {
            double v = base + 0.22 * std::sin(t * 2.3 + i * 0.38) + 0.12 * std::sin(t * 5.1 + i * 0.91);
            out_bars[i] = clampf(static_cast<float>(v));
        }
        out_bass = static_cast<float>(0.35 + 0.12 * std::sin(t * 2.1));
        out_energy = 0.45f;
        return;
    }
    double f = std::max(0.0, ms / 1000.0 * spec_data_.fps);
    int max_i = std::max(0, spec_data_.frames - 1);
    int i = std::min(static_cast<int>(f), max_i);
    int j = std::min(i + 1, max_i);
    float a = static_cast<float>(f - i);
    const uint8_t* si = spec_data_.spec.data() + static_cast<size_t>(i) * 48;
    const uint8_t* sj = spec_data_.spec.data() + static_cast<size_t>(j) * 48;
    for (int k = 0; k < 48; ++k) {
        out_bars[k] = (si[k] * (1.0f - a) + sj[k] * a) / 255.0f;
    }
    float bi = i < static_cast<int>(spec_data_.bass.size()) ? spec_data_.bass[i] / 255.0f : 0.0f;
    float bj = j < static_cast<int>(spec_data_.bass.size()) ? spec_data_.bass[j] / 255.0f : bi;
    out_bass = bi * (1.0f - a) + bj * a;
    float ei = i < static_cast<int>(spec_data_.energy.size()) ? spec_data_.energy[i] / 255.0f : 0.0f;
    float ej = j < static_cast<int>(spec_data_.energy.size()) ? spec_data_.energy[j] / 255.0f : ei;
    out_energy = ei * (1.0f - a) + ej * a;
}

int AudioEngine::position_ms() const {
    uint32_t sr = std::max<uint32_t>(1, stream_->src_sr.load());
    uint64_t frames = stream_->played_src_frames.load();
    return static_cast<int>(frames * 1000ULL / sr);
}

int AudioEngine::duration_ms() const {
    int d = stream_->duration_ms.load();
    if (d <= 0 && track_) d = static_cast<int>(track_->duration * 1000.0);
    return d;
}

void AudioEngine::apply_volume() {
    target_gain_.store(clampf(static_cast<float>(base_volume_ * fade_val_)));
}

void AudioEngine::start_fade(double target, double duration, std::function<void()> done) {
    fade_from_ = fade_val_;
    fade_to_ = clampd(target);
    fade_t0_ = now_sec();
    fade_dur_ = std::max(0.02, duration);
    fade_done_ = std::move(done);
    fade_active_ = true;
}

void AudioEngine::update(double now) {
    if (fade_active_) {
        double k = std::min(1.0, (now - fade_t0_) / fade_dur_);
        double e = k * k * (3.0 - 2.0 * k);
        fade_val_ = fade_from_ + (fade_to_ - fade_from_) * e;
        apply_volume();
        if (k >= 1.0) {
            fade_active_ = false;
            auto cb = std::move(fade_done_);
            fade_done_ = nullptr;
            if (cb) cb();
        }
    }

    if (previewing_ && preview_stop_ms_ > 0 && playing_.load()) {
        if (position_ms() >= preview_stop_ms_) {
            previewing_ = false;
            preview_pending_ = true;
            start_fade(0.0, preview_fade_out_, [this]() {
                playing_.store(false);
                emit_preview_end();
            });
        }
    }

    if (playing_.load() && stream_->playback_finished.exchange(false)) {
        playing_.store(false);
        if (previewing_ || preview_pending_) {
            emit_preview_end();
        } else {
            full_mode_ = false;
            if (on_finished) on_finished();
        }
    }
}

void AudioEngine::emit_preview_end() {
    fade_active_ = false;
    fade_done_ = nullptr;
    previewing_ = false;
    preview_pending_ = false;
    preview_stop_ms_ = 0;
    bool res = resume_on_fade_;
    resume_on_fade_ = true;
    if (on_preview_ended) on_preview_ended();
    if (res) resume_suspended();
}

void AudioEngine::suspend_full() {
    if (has_suspended_) return;
    if (full_mode_ && track_ != nullptr) {
        has_suspended_ = true;
        suspended_ = {track_, position_ms(), playing_.load()};
        playing_.store(false);
    }
}

void AudioEngine::play_preview(const Track* t, int start_ms, int preview_len_ms, double fade_over) {
    if (!t) return;
    if (full_mode_ && track_ != nullptr) {
        if (playing_.load() && fade_val_ > 0.01 && fade_over > 0.0) {
            has_handover_ = true;
            handover_ = {t, start_ms, preview_len_ms};
            start_fade(0.0, fade_over, [this]() { finish_preview_handover(); });
            return;
        }
        suspend_full();
    }
    play(t, start_ms, true, preview_len_ms, 0.0);
}

void AudioEngine::finish_preview_handover() {
    if (!has_handover_) return;
    auto req = handover_;
    has_handover_ = false;
    suspend_full();
    play(req.track, req.start_ms, true, req.preview_len_ms, 0.0);
}

bool AudioEngine::cancel_handover(bool restore) {
    if (!has_handover_) return false;
    has_handover_ = false;
    if (restore && full_mode_ && track_ != nullptr && playing_.load()) {
        start_fade(1.0, 0.35);
    }
    return true;
}

void AudioEngine::drop_suspended() {
    has_suspended_ = false;
}

void AudioEngine::resume_suspended() {
    if (!has_suspended_) return;
    auto saved = suspended_;
    has_suspended_ = false;
    if (!saved.track) return;
    if (saved.was_playing) {
        play(saved.track, saved.pos_ms, false, 20000, 0.75);
    } else {
        restore_paused(saved.track, saved.pos_ms);
    }
}

void AudioEngine::restore_paused(const Track* t, int pos_ms) {
    fade_active_ = false;
    fade_done_ = nullptr;
    fade_val_ = 1.0;
    apply_volume();
    playing_.store(false);
    track_ = t;
    load_spectrum(t);
    open_source(t, pos_ms);
    previewing_ = false;
    full_mode_ = true;
    preview_pending_ = false;
    preview_stop_ms_ = 0;
}

void AudioEngine::play(const Track* t, int start_ms, bool preview, int preview_len_ms, double fade_in) {
    if (!t) return;
    bool changed = (track_ == nullptr || track_->id != t->id);
    has_handover_ = false;
    if (!preview) has_suspended_ = false;
    resume_on_fade_ = true;
    fade_active_ = false;
    fade_done_ = nullptr;

    if (changed) {
        playing_.store(false);
        track_ = t;
        paths::log("audio play: " + t->id + (preview ? " (preview)" : " (full)"));
        load_spectrum(t);
        open_source(t, start_ms);
    } else if (start_ms > 0 || preview) {
        open_source(t, start_ms);
    }

    previewing_ = preview;
    full_mode_ = !preview;
    preview_pending_ = false;
    preview_stop_ms_ = preview ? (start_ms + preview_len_ms) : 0;

    if (preview) {
        double secs = std::max(1.0, preview_len_ms / 1000.0);
        preview_fade_in_ = std::min(2.2, std::max(1.2, 0.9 + secs * 0.04));
        preview_fade_out_ = std::min(1.9, std::max(1.0, 0.8 + secs * 0.035));
        fade_val_ = 0.0;
        apply_volume();
        start_fade(1.0, preview_fade_in_);
    } else {
        fade_val_ = (fade_in > 0.0) ? 0.0 : 1.0;
        apply_volume();
        if (fade_in > 0.0) start_fade(1.0, fade_in);
    }

    playing_.store(true);
}

void AudioEngine::stop() {
    has_handover_ = false;
    fade_active_ = false;
    fade_done_ = nullptr;
    fade_val_ = 1.0;
    apply_volume();
    playing_.store(false);

    stream_->generation.fetch_add(1);
    stream_->dec_cancel.store(true);
    stream_->dec_cv.notify_all();
    if (stream_->dec_thread.joinable()) stream_->dec_thread.join();
    stream_->dec_cancel.store(false);
    stream_->clear_pcm(0, stream_->src_sr.load());

    previewing_ = false;
    full_mode_ = false;
    preview_pending_ = false;
    preview_stop_ms_ = 0;
    has_suspended_ = false;
    resume_on_fade_ = true;
}

void AudioEngine::stop_preview(bool fade, bool resume) {
    if (has_handover_) {
        cancel_handover(true);
        return;
    }
    if (!previewing_ && !preview_pending_) return;
    preview_stop_ms_ = 0;
    previewing_ = false;
    resume_on_fade_ = resume;
    if (fade) {
        preview_pending_ = true;
        start_fade(0.0, preview_fade_out_, [this]() {
            playing_.store(false);
            emit_preview_end();
        });
    } else {
        fade_val_ = 0.0;
        apply_volume();
        playing_.store(false);
        emit_preview_end();
    }
}

void AudioEngine::set_preview_remaining(int ms) {
    if (previewing_) {
        preview_stop_ms_ = position_ms() + std::max(0, ms);
    }
}

void AudioEngine::pause() {
    playing_.store(false);
}

void AudioEngine::toggle() {
    if (playing_.load()) {
        playing_.store(false);
    } else {
        if (fade_val_ < 0.99 && !previewing_) {
            fade_val_ = 1.0;
            apply_volume();
        }
        playing_.store(true);
    }
}

void AudioEngine::seek(int ms) {
    if (!track_) return;
    ms = std::max(0, std::min(duration_ms(), ms));
    open_source(track_, ms);
}

void AudioEngine::set_volume(float v) {
    base_volume_ = clampf(v);
    apply_volume();
}

void AudioEngine::set_muted(bool m) {
    muted_.store(m);
}

} // namespace np
