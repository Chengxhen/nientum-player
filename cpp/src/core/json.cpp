#include "json.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace np {

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {
        if (s_.size() >= 3 && (unsigned char)s_[0] == 0xEF && (unsigned char)s_[1] == 0xBB && (unsigned char)s_[2] == 0xBF) {
            i_ = 3;
        }
    }

    bool parse(JsonValue& out) {
        skip_ws();
        if (!parse_val(out)) return false;
        skip_ws();
        return true;
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void skip_ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n'))
            ++i_;
    }

    bool parse_val(JsonValue& out) {
        skip_ws();
        if (i_ >= s_.size()) return false;
        char c = s_[i_];
        if (c == 'n') {
            if (s_.compare(i_, 4, "null") == 0) { i_ += 4; out = nullptr; return true; }
            return false;
        }
        if (c == 't') {
            if (s_.compare(i_, 4, "true") == 0) { i_ += 4; out = true; return true; }
            return false;
        }
        if (c == 'f') {
            if (s_.compare(i_, 5, "false") == 0) { i_ += 5; out = false; return true; }
            return false;
        }
        if (c == '"') {
            std::string str;
            if (!parse_str(str)) return false;
            out = std::move(str);
            return true;
        }
        if (c == '[') return parse_arr(out);
        if (c == '{') return parse_obj(out);
        if (c == '-' || std::isdigit((unsigned char)c)) return parse_num(out);
        return false;
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back((char)cp);
        } else if (cp <= 0x7FF) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    bool parse_hex4(uint32_t& v) {
        if (i_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parse_str(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        out.clear();
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i_ >= s_.size()) return false;
                char e = s_[i_++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!parse_hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 2 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        i_ += 2;
                        uint32_t low = 0;
                        if (!parse_hex4(low)) return false;
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parse_num(JsonValue& out) {
        const char* start = s_.c_str() + i_;
        char* end = nullptr;
        double d = std::strtod(start, &end);
        if (end == start) return false;
        i_ = static_cast<size_t>(end - s_.c_str());
        out = d;
        return true;
    }

    bool parse_arr(JsonValue& out) {
        ++i_; // '['
        JsonArray arr;
        skip_ws();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; out = std::move(arr); return true; }
        while (i_ < s_.size()) {
            JsonValue v;
            if (!parse_val(v)) return false;
            arr.push_back(std::move(v));
            skip_ws();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; out = std::move(arr); return true; }
            return false;
        }
        return false;
    }

    bool parse_obj(JsonValue& out) {
        ++i_; // '{'
        JsonObject obj;
        skip_ws();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; out = std::move(obj); return true; }
        while (i_ < s_.size()) {
            skip_ws();
            std::string key;
            if (!parse_str(key)) return false;
            skip_ws();
            if (i_ >= s_.size() || s_[i_] != ':') return false;
            ++i_;
            JsonValue v;
            if (!parse_val(v)) return false;
            obj.insert_or_assign(std::move(key), std::move(v));
            skip_ws();
            if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; out = std::move(obj); return true; }
            return false;
        }
        return false;
    }
};

void escape_str(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back((char)c);
            }
            break;
        }
    }
    out.push_back('"');
}

void dump_impl(std::string& out, const JsonValue& v, int indent, int depth) {
    if (v.is_null()) { out += "null"; return; }
    if (v.is_bool()) { out += (v.as_bool() ? "true" : "false"); return; }
    if (v.is_number()) {
        double d = v.as_double();
        if (d == static_cast<long long>(d) && std::abs(d) < 1e15) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
            out += buf;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.6g", d);
            out += buf;
        }
        return;
    }
    if (v.is_string()) { escape_str(out, v.as_string()); return; }
    if (v.is_array()) {
        const auto& a = v.as_array();
        if (a.empty()) { out += "[]"; return; }
        out.push_back('[');
        for (size_t i = 0; i < a.size(); ++i) {
            if (i > 0) out.push_back(',');
            if (indent >= 0) {
                out.push_back('\n');
                out.append((depth + 1) * indent, ' ');
            }
            dump_impl(out, a[i], indent, depth + 1);
        }
        if (indent >= 0) {
            out.push_back('\n');
            out.append(depth * indent, ' ');
        }
        out.push_back(']');
        return;
    }
    if (v.is_object()) {
        const auto& o = v.as_object();
        if (o.empty()) { out += "{}"; return; }
        out.push_back('{');
        bool first = true;
        for (const auto& [k, val] : o) {
            if (!first) out.push_back(',');
            first = false;
            if (indent >= 0) {
                out.push_back('\n');
                out.append((depth + 1) * indent, ' ');
            }
            escape_str(out, k);
            out += (indent >= 0 ? ": " : ":");
            dump_impl(out, val, indent, depth + 1);
        }
        if (indent >= 0) {
            out.push_back('\n');
            out.append(depth * indent, ' ');
        }
        out.push_back('}');
    }
}

} // namespace

bool JsonValue::parse(const std::string& text, JsonValue& out) {
    Parser p(text);
    return p.parse(out);
}

bool JsonValue::parse_file(const std::wstring& path, JsonValue& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 64LL * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    std::string buf(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf.data(), static_cast<DWORD>(sz.QuadPart), &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    buf.resize(read);
    return parse(buf, out);
}

std::string JsonValue::dump(int indent) const {
    std::string out;
    dump_impl(out, *this, indent, 0);
    return out;
}

bool JsonValue::save_file(const std::wstring& path, int indent) const {
    std::string s = dump(indent);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
    CloseHandle(h);
    return ok && written == s.size();
}

} // namespace np
