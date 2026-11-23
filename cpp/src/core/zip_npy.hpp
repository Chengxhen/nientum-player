#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace np {

struct SpectrumData {
    int fps = 30;
    int frames = 0;
    std::vector<uint8_t> spec;   // frames * 48
    std::vector<uint8_t> bass;   // frames
    std::vector<uint8_t> energy; // frames

    bool valid() const { return frames > 0 && spec.size() >= static_cast<size_t>(frames) * 48; }
};

bool load_npz_spectrum(const std::wstring& path, SpectrumData& out);

} // namespace np
