#include "zip_npy.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstring>

namespace np {

namespace {

inline uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

struct BitReader {
    const uint8_t* data;
    size_t size;
    size_t byte_pos = 0;
    uint32_t bit_buf = 0;
    int bit_cnt = 0;

    void fill(int n) {
        while (bit_cnt < n && byte_pos < size) {
            bit_buf |= static_cast<uint32_t>(data[byte_pos++]) << bit_cnt;
            bit_cnt += 8;
        }
    }

    bool read_bits(int n, uint32_t& out) {
        if (n == 0) { out = 0; return true; }
        fill(n);
        if (bit_cnt < n) return false;
        out = bit_buf & ((1u << n) - 1u);
        bit_buf >>= n;
        bit_cnt -= n;
        return true;
    }

    void align_byte() {
        int drop = bit_cnt & 7;
        bit_buf >>= drop;
        bit_cnt -= drop;
    }

    bool read_bytes(uint8_t* dst, size_t n) {
        align_byte();
        while (n > 0 && bit_cnt >= 8) {
            *dst++ = static_cast<uint8_t>(bit_buf & 0xFF);
            bit_buf >>= 8;
            bit_cnt -= 8;
            --n;
        }
        if (byte_pos + n > size) return false;
        if (n > 0) {
            std::memcpy(dst, data + byte_pos, n);
            byte_pos += n;
        }
        return true;
    }
};

struct HuffTable {
    static constexpr int MAX_BITS = 15;
    int count[MAX_BITS + 1]{};
    int offset[MAX_BITS + 1]{};
    std::vector<uint16_t> symbols;

    bool build(const int* lengths, int num_syms) {
        std::memset(count, 0, sizeof(count));
        for (int i = 0; i < num_syms; ++i) {
            if (lengths[i] > MAX_BITS) return false;
            if (lengths[i] > 0) count[lengths[i]]++;
        }
        int total = 0;
        for (int b = 1; b <= MAX_BITS; ++b) {
            offset[b] = total;
            total += count[b];
        }
        symbols.resize(total);
        int cur[MAX_BITS + 1];
        std::memcpy(cur, offset, sizeof(cur));
        for (int i = 0; i < num_syms; ++i) {
            if (lengths[i] > 0) {
                symbols[cur[lengths[i]]++] = static_cast<uint16_t>(i);
            }
        }
        return true;
    }

    bool decode(BitReader& br, uint16_t& sym) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= MAX_BITS; ++len) {
            uint32_t bit = 0;
            if (!br.read_bits(1, bit)) return false;
            code |= static_cast<int>(bit);
            int c = count[len];
            if (code - c < first) {
                sym = symbols[index + (code - first)];
                return true;
            }
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return false;
    }
};

bool inflate_raw(const uint8_t* src, size_t src_len, std::vector<uint8_t>& out, size_t expected_size) {
    out.clear();
    if (expected_size > 0 && expected_size < 64u * 1024u * 1024u) {
        out.reserve(expected_size);
    }
    BitReader br{src, src_len};

    static const int len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const int len_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const int dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const int dist_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static const int cl_order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    uint32_t bfinal = 0;
    do {
        uint32_t btype = 0;
        if (!br.read_bits(1, bfinal) || !br.read_bits(2, btype)) return false;
        if (btype == 0) {
            uint8_t hdr[4];
            if (!br.read_bytes(hdr, 4)) return false;
            uint16_t len = rd16(hdr);
            uint16_t nlen = rd16(hdr + 2);
            if (static_cast<uint16_t>(~len) != nlen) return false;
            size_t old_sz = out.size();
            out.resize(old_sz + len);
            if (!br.read_bytes(out.data() + old_sz, len)) return false;
        } else if (btype == 1 || btype == 2) {
            HuffTable lit_tab, dist_tab;
            if (btype == 1) {
                int ll[288], dl[32];
                for (int i = 0; i <= 143; ++i) ll[i] = 8;
                for (int i = 144; i <= 255; ++i) ll[i] = 9;
                for (int i = 256; i <= 279; ++i) ll[i] = 7;
                for (int i = 280; i <= 287; ++i) ll[i] = 8;
                for (int i = 0; i < 32; ++i) dl[i] = 5;
                lit_tab.build(ll, 288);
                dist_tab.build(dl, 32);
            } else {
                uint32_t hlit = 0, hdist = 0, hclen = 0;
                if (!br.read_bits(5, hlit) || !br.read_bits(5, hdist) || !br.read_bits(4, hclen)) return false;
                hlit += 257;
                hdist += 1;
                hclen += 4;
                if (hlit > 286 || hdist > 30) return false;
                int cl_lens[19]{};
                for (uint32_t i = 0; i < hclen; ++i) {
                    uint32_t v = 0;
                    if (!br.read_bits(3, v)) return false;
                    cl_lens[cl_order[i]] = static_cast<int>(v);
                }
                HuffTable cl_tab;
                if (!cl_tab.build(cl_lens, 19)) return false;
                int lens[320]{};
                uint32_t total = hlit + hdist;
                uint32_t idx = 0;
                while (idx < total) {
                    uint16_t sym = 0;
                    if (!cl_tab.decode(br, sym)) return false;
                    if (sym < 16) {
                        lens[idx++] = sym;
                    } else {
                        uint32_t rep = 0, val = 0;
                        if (sym == 16) {
                            if (idx == 0 || !br.read_bits(2, rep)) return false;
                            rep += 3;
                            val = static_cast<uint32_t>(lens[idx - 1]);
                        } else if (sym == 17) {
                            if (!br.read_bits(3, rep)) return false;
                            rep += 3;
                        } else {
                            if (!br.read_bits(7, rep)) return false;
                            rep += 11;
                        }
                        if (idx + rep > total) return false;
                        while (rep--) lens[idx++] = static_cast<int>(val);
                    }
                }
                if (!lit_tab.build(lens, static_cast<int>(hlit)) ||
                    !dist_tab.build(lens + hlit, static_cast<int>(hdist))) {
                    return false;
                }
            }

            while (true) {
                uint16_t sym = 0;
                if (!lit_tab.decode(br, sym)) return false;
                if (sym < 256) {
                    out.push_back(static_cast<uint8_t>(sym));
                } else if (sym == 256) {
                    break;
                } else {
                    int li = sym - 257;
                    if (li < 0 || li >= 29) return false;
                    uint32_t le = 0;
                    if (!br.read_bits(len_extra[li], le)) return false;
                    int length = len_base[li] + static_cast<int>(le);

                    uint16_t dsym = 0;
                    if (!dist_tab.decode(br, dsym) || dsym >= 30) return false;
                    uint32_t de = 0;
                    if (!br.read_bits(dist_extra[dsym], de)) return false;
                    size_t dist = static_cast<size_t>(dist_base[dsym] + de);
                    if (dist == 0 || dist > out.size()) return false;
                    for (int k = 0; k < length; ++k) {
                        out.push_back(out[out.size() - dist]);
                    }
                }
            }
        } else {
            return false;
        }
    } while (!bfinal);
    return true;
}

struct NpyArray {
    std::string descr;
    std::vector<size_t> shape;
    const uint8_t* payload = nullptr;
    size_t payload_bytes = 0;
};

bool parse_npy(const std::vector<uint8_t>& raw, NpyArray& arr) {
    if (raw.size() < 10) return false;
    if (std::memcmp(raw.data(), "\x93NUMPY", 6) != 0) return false;
    uint8_t major = raw[6];
    size_t hdr_len = 0;
    size_t hdr_off = 0;
    if (major == 1) {
        hdr_len = rd16(raw.data() + 8);
        hdr_off = 10;
    } else if (major == 2 || major == 3) {
        if (raw.size() < 12) return false;
        hdr_len = rd32(raw.data() + 8);
        hdr_off = 12;
    } else {
        return false;
    }
    if (hdr_off + hdr_len > raw.size()) return false;
    std::string hdr(reinterpret_cast<const char*>(raw.data() + hdr_off), hdr_len);

    auto find_val = [&](const char* key) -> std::string {
        auto pos = hdr.find(key);
        if (pos == std::string::npos) return {};
        auto col = hdr.find(':', pos);
        if (col == std::string::npos) return {};
        return hdr.substr(col + 1);
    };

    auto dpos = hdr.find("'descr'");
    if (dpos == std::string::npos) dpos = hdr.find("\"descr\"");
    if (dpos != std::string::npos) {
        auto q1 = hdr.find_first_of("'\"", hdr.find(':', dpos));
        if (q1 != std::string::npos) {
            auto q2 = hdr.find(hdr[q1], q1 + 1);
            if (q2 != std::string::npos) arr.descr = hdr.substr(q1 + 1, q2 - q1 - 1);
        }
    }

    auto spos = hdr.find("'shape'");
    if (spos == std::string::npos) spos = hdr.find("\"shape\"");
    if (spos != std::string::npos) {
        auto p1 = hdr.find('(', spos);
        auto p2 = hdr.find(')', p1);
        if (p1 != std::string::npos && p2 != std::string::npos) {
            std::string inner = hdr.substr(p1 + 1, p2 - p1 - 1);
            size_t cur = 0;
            while (cur < inner.size()) {
                while (cur < inner.size() && (inner[cur] == ' ' || inner[cur] == ',')) ++cur;
                if (cur >= inner.size()) break;
                size_t end = cur;
                while (end < inner.size() && inner[end] >= '0' && inner[end] <= '9') ++end;
                if (end > cur) {
                    arr.shape.push_back(std::stoull(inner.substr(cur, end - cur)));
                    cur = end;
                } else {
                    ++cur;
                }
            }
        }
    }

    arr.payload = raw.data() + hdr_off + hdr_len;
    arr.payload_bytes = raw.size() - (hdr_off + hdr_len);
    return true;
}

} // namespace

bool load_npz_spectrum(const std::wstring& path, SpectrumData& out) {
    out = {};
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 22 || sz.QuadPart > 64LL * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    std::vector<uint8_t> zip(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(h, zip.data(), static_cast<DWORD>(zip.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != zip.size()) return false;

    size_t pos = 0;
    while (pos + 30 <= zip.size()) {
        uint32_t sig = rd32(zip.data() + pos);
        if (sig != 0x04034B50) break; // local file header
        uint16_t flags = rd16(zip.data() + pos + 6);
        uint16_t method = rd16(zip.data() + pos + 8);
        uint32_t csize = rd32(zip.data() + pos + 18);
        uint32_t usize = rd32(zip.data() + pos + 22);
        uint16_t nlen = rd16(zip.data() + pos + 26);
        uint16_t elen = rd16(zip.data() + pos + 28);
        if (pos + 30 + nlen + elen > zip.size()) break;
        std::string name(reinterpret_cast<const char*>(zip.data() + pos + 30), nlen);
        size_t data_off = pos + 30 + nlen + elen;
        if (data_off + csize > zip.size()) break;

        if (!(flags & 0x08) && (name == "spec.npy" || name == "bass.npy" || name == "energy.npy" || name == "fps.npy")) {
            std::vector<uint8_t> raw;
            bool dec_ok = false;
            if (method == 0) {
                raw.assign(zip.begin() + data_off, zip.begin() + data_off + csize);
                dec_ok = true;
            } else if (method == 8) {
                dec_ok = inflate_raw(zip.data() + data_off, csize, raw, usize);
            }
            if (dec_ok) {
                NpyArray arr;
                if (parse_npy(raw, arr)) {
                    if (name == "spec.npy" && arr.shape.size() == 2 && arr.shape[1] == 48 && arr.descr == "|u1") {
                        size_t need = arr.shape[0] * 48;
                        if (arr.payload_bytes >= need) {
                            out.frames = static_cast<int>(arr.shape[0]);
                            out.spec.assign(arr.payload, arr.payload + need);
                        }
                    } else if (name == "bass.npy" && arr.descr == "|u1") {
                        out.bass.assign(arr.payload, arr.payload + arr.payload_bytes);
                    } else if (name == "energy.npy" && arr.descr == "|u1") {
                        out.energy.assign(arr.payload, arr.payload + arr.payload_bytes);
                    } else if (name == "fps.npy") {
                        if (arr.descr.find("i4") != std::string::npos && arr.payload_bytes >= 4) {
                            int32_t v = 30; std::memcpy(&v, arr.payload, 4);
                            out.fps = std::max(1, static_cast<int>(v));
                        } else if (arr.descr.find("i8") != std::string::npos && arr.payload_bytes >= 8) {
                            int64_t v = 30; std::memcpy(&v, arr.payload, 8);
                            out.fps = std::max(1, static_cast<int>(v));
                        }
                    }
                }
            }
        }
        pos = data_off + csize;
    }
    return out.valid();
}

} // namespace np
